#include "shared.h"
#include "login_world_scene.h"

#include "config/gs_config.h"
#include "core/arg_parser.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "game/login/cin_builder.h"
#include "game/gm_scene.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_display.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "flash/implementation/flash_transform.h"
#include "gs_timer.h"
#include "render/camera/gs_camera.h"
#include "render/gs_render.h"
#include "render/postfx/gs_postfx.h"
#include "render/postfx/postfx_node.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/weather/gs_weather.h"

#include <filesystem>

static bool gbStockLoginMenu = false;
static CmdLineArg CmdLineArgStockLogin( "vanilla_loginscreen",
                                        []( const char * ) {
                                            gbStockLoginMenu = true;
                                        } );

static constexpr const char *kLoginMovieName = "LOG_IN";

// Generated there by AssetExtractor::BuildGeneratedMovies
static constexpr const char *kGeneratedMovieRoot = "opentdu_data/flash/";
static constexpr const char *kBackdropClip = "/otdu_bg";

// Fallback view (if bank streaming fails/does not exist)
static const Eigen::Vector3f kCameraPosition = { 64285.047f, 37.758f,
                                                 44133.129f };
static const Eigen::Vector3f kCameraTarget = { 63519.45f, 60.0f, 46305.20f };
static const Eigen::Vector3f kCameraUp = { 0.0f, 1.0f, 0.0f };
static constexpr float kCameraFovDegrees = 55.0f;
static constexpr TimeOfDay kTimeOfDay = { 16.0f, 30.0f, 0.0f };

static constexpr const char *kAttractBankPath =
    CinBuilder::kLoginAttractBankPath;
static constexpr const char *kAttractCinematicSuffix =
    CinBuilder::kCinematicSuffix;
static constexpr uint32_t kCinMagic = 0x214e4943u; // "CIN!"

struct LoginOverlay {
    uint16_t Depth;
    int16_t Alpha;
};

static constexpr LoginOverlay kOverlays[] = {
    {4, 0x100}, // top letterbox bar
    {5, 0x100}, // bottom letterbox bar
    {6, 0    }, // stage-sized dimming fill (black, 40% alpha)
};

static void SetSceneCompositeEnabled( const bool bEnabled ) {
    if ( gpPostFX == nullptr || gpPostFX->isFXLocked() ) {
        return;
    }

    const bool bHDR = gpRender->isHDREnabled();
    PostFXStep *pDesaturate =
        gpPostFX->getEffect<ePostFXStep::PFXS_Desaturation>();
    PostFXStep *pHDRI = gpPostFX->getEffect<ePostFXStep::PFXS_HDRI>();
    for ( PostFXStep *pStep : { pDesaturate, pHDRI } ) {
        if ( pStep == nullptr || pStep->getInput() == nullptr ) {
            continue;
        }

        const bool bStepEnabled = bEnabled && ( bHDR == ( pStep == pHDRI ) );
        pStep->getInput()->setEnabled( bStepEnabled );

        if ( bStepEnabled ) {
            pStep->tick();
        }
    }
}

bool LoginWorldScene::SelectLoginMovie() {
    const std::string moviePath = std::string( kGeneratedMovieRoot ) +
                                  gpConfig->getAssetQualityFolder() + "/" +
                                  kLoginMovieName + ".bnk";
    std::error_code errorCode;
    const bool bEnabled =
        !gbStockLoginMenu && std::filesystem::exists( moviePath, errorCode );
    if ( !gbStockLoginMenu && !bEnabled ) {
        OTDU_LOG_WARN( "'%s' not found; using the retail login menu\n",
                       moviePath.c_str() );
    }

    gpFlash->setMovieRootPath( kLoginMovieName,
                               bEnabled ? kGeneratedMovieRoot : "" );
    return bEnabled;
}

LoginAttractCinematic::LoginAttractCinematic()
    : pBankData( nullptr ),
      bank(),
      worldAnchor( Eigen::Vector3f::Zero() ),
      currentCin( -1 ),
      time( 0.0f ) {}

LoginAttractCinematic::~LoginAttractCinematic() { release(); }

static bool HasSuffixI( const std::string &string, const char *pSuffix ) {
    const size_t suffixLength = strlen( pSuffix );
    return string.size() >= suffixLength &&
           StringCmpI( string.c_str() + string.size() - suffixLength,
                       pSuffix ) == 0;
}

bool LoginAttractCinematic::load( const char *pBankPath,
                                  const char *pCinematicSuffix ) {
    release();

    uint32_t bankSize = 0;
    const std::string bankPath = pBankPath;
    if ( !gpFile->loadBank( bankPath.c_str(), &pBankData, &bankSize, 0 ) ||
         bank.load( static_cast<const uint8_t *>( pBankData ), bankSize,
                    false ) != 0 ) {
        OTDU_LOG_WARN( "Login attract: cannot load '%s'\n", bankPath.c_str() );
        release();
        return false;
    }

    for ( int32_t i = 0; i < bank.getNumElements(); i++ ) {
        ParsedBankEntry entry;
        bank.getEntry( i, entry );
        if ( entry.pData != nullptr && entry.Size >= 4 &&
             *static_cast<const uint32_t *>( entry.pData ) == kCinMagic &&
             HasSuffixI( entry.Filename, pCinematicSuffix ) ) {
            cinEntries.push_back( i );
        }
    }

    const bool bMissingAnchor =
        !CinBuilder::LoadAnchor( pBankPath, worldAnchor );
    if ( cinEntries.empty() || bMissingAnchor ) {
        release();
        return false;
    }

    OTDU_LOG_DEBUG( "Login attract: %zu cinematic(s) in '%s'\n",
                    cinEntries.size(), bankPath.c_str() );
    return true;
}

void LoginAttractCinematic::release() {
    if ( pPlayer != nullptr ) {
        pPlayer->reset();
        pPlayer.reset();
    }
    playerBuffer.clear();
    cinEntries.clear();
    currentCin = -1;
    time = 0.0f;
    lastView = CinResCollection::CameraView();

    if ( pBankData != nullptr ) {
        TestDrive::Free( pBankData );
        pBankData = nullptr;
    }
}

Matrix4x4f_t LoginAttractCinematic::computeAnchorMatrix() const {
    const Eigen::Vector4f &origin = gpWorldStreaming->getPosition();
    Matrix4x4f_t matrix = Matrix4x4f_t::Identity();
    matrix.row( 3 ) << worldAnchor.x() - origin.x(),
        worldAnchor.y() - origin.y(), worldAnchor.z() - origin.z(), 1.0f;
    return matrix;
}

bool LoginAttractCinematic::start( const int32_t index ) {
    if ( pPlayer != nullptr ) {
        pPlayer->reset();
    }
    pPlayer = std::make_unique<CinResCollection>();

    currentCin = index;
    time = 0.0f;

    ParsedBankEntry entry;
    bank.getEntry( cinEntries[size_t( index )], entry );
    const uint32_t bufferSize =
        ExtractBufferSize( entry.pData, uint32_t( entry.Size ) );
    playerBuffer.assign( bufferSize, 0 );
    if ( bufferSize == 0 ||
         !pPlayer->createInterface( entry.pData, uint32_t( entry.Size ),
                                    playerBuffer.data(), &bank ) ) {
        OTDU_LOG_WARN( "Login attract: cannot play cinematic %d ('%s')\n",
                       index, entry.Filename.c_str() );
        pPlayer.reset();
        return false;
    }

    const CinematicHeader *pHeader = pPlayer->getHeader();
    if ( pHeader->NumActors != 0 || pHeader->NumSubAnimatedProps != 0 ) {
        pPlayer->reset();
        pPlayer.reset();
        return false;
    }

    return true;
}

bool LoginAttractCinematic::tick( const float deltaTime,
                                  CinResCollection::CameraView &outView ) {
    if ( !isLoaded() ) {
        return false;
    }

    if ( pPlayer == nullptr ) {
        const int32_t numCins = int32_t( cinEntries.size() );
        for ( int32_t i = 1; i <= numCins && pPlayer == nullptr; i++ ) {
            start( ( currentCin + i ) % numCins );
        }
        if ( pPlayer == nullptr ) {
            release();
            return false;
        }
    }

    pPlayer->setBaseMatrix( computeAnchorMatrix() );
    pPlayer->tick( time, deltaTime );
    if ( pPlayer->getCameraView().bValid ) {
        lastView = pPlayer->getCameraView();
    }

    time += deltaTime;
    if ( time >= pPlayer->getTotalTime() ) {
        pPlayer->reset();
        pPlayer.reset();
    }

    outView = lastView;
    return lastView.bValid;
}

LoginWorldScene::LoginWorldScene()
    : mngShadow(),
      mngWorld(),
      mngCloudsPlane(),
      mngBackground(),
      mngOcean(),
      mngSunFlare(),
      mngEnvMap(),
      mngImpostor(),
      mngImpostorSimple(),
      mngTallGrass(),
      pEnvMappedCar( nullptr ),
      pLoginPlayer( nullptr ),
      attractCameraPosition( Eigen::Vector4f::Zero() ),
      savedTime(),
      savedTimeScale( 0.0f ),
      bStreamed( false ),
      bActive( false ) {
    mngWorld.setStreamEnabled( false );
}

LoginWorldScene::~LoginWorldScene() {
    if ( !bActive ) {
        return;
    }

    attractCinematic.release();
    if ( pEnvMappedCar != nullptr ) {
        GSCarDisplay::SetRealTimeEnvMappedCar( nullptr );
        pEnvMappedCar = nullptr;
    }
    showcaseCar.destroy();
    gpRender->unregisterConfigListener( this );
    gpCamera->clearCinematicCameraOverride();
    SetSceneCompositeEnabled( false );
    gpWeather->setTimeScale( savedTimeScale );
    gpWeather->setTime( true, &savedTime );
}

std::array<Manager *, LoginWorldScene::kNumManagers>
LoginWorldScene::getManagers() {
    return { &mngShadow,         &mngWorld,    &mngCloudsPlane, &mngBackground,
             &mngOcean,          &mngSunFlare, &mngEnvMap,      &mngImpostor,
             &mngImpostorSimple, &mngTallGrass };
}

void LoginWorldScene::reset( FlashPlayer *pPlayer ) {
    pLoginPlayer = pPlayer;
    bStreamed = false;

    if ( !bActive ) {
        savedTime = gpWeather->getCurrentTime();
        savedTimeScale = gpWeather->getTimeScale();
        gpRender->registerConfigListener( this );
        bActive = true;
    }

    gpWeather->setTimeScale( 0.0f );
    gpWeather->setTime( false, &kTimeOfDay );
    gpWeather->setCurrentWeatherDesc(
        gpWeather->getWeatherDescName( GM_FreeRide ) );

    std::vector<CinCameraKey> keys;
    CinShowcaseCar car;
    if ( pEnvMappedCar != nullptr ) {
        GSCarDisplay::SetRealTimeEnvMappedCar( nullptr );
        pEnvMappedCar = nullptr;
    }
    showcaseCar.destroy();
    if ( CinBuilder::LoadKeys( CinBuilder::kLoginAttractKeysPath, keys,
                               car ) ) {
        showcaseCar.spawn( car );
    }

    std::error_code errorCode;
    if ( std::filesystem::exists( kAttractBankPath, errorCode ) &&
         attractCinematic.load( kAttractBankPath, kAttractCinematicSuffix ) ) {
        // First view, to stream around it
        CinResCollection::CameraView view;
        if ( attractCinematic.tick( 0.0f, view ) ) {
            setAttractCameraPosition( view );
        }
    }

    Eigen::Vector4f streamPosition = getStreamPosition();
    gpWorld->setStreamingPosition( &streamPosition );
    gpWorld->setDirtyStreaming( true );

    updateCamera();
}

void LoginWorldScene::tick() {
    if ( !bActive ) {
        return;
    }

    tickShowcaseCar();

    if ( gpTimer->GameSpeed == 0.0f ) {
        gpTimer->GameSpeed = ( gpTimer->PreviousGameSpeed > 0.0f )
                                 ? gpTimer->PreviousGameSpeed
                                 : 1.0f;
    }

    bool bHasView = true;
    if ( attractCinematic.isLoaded() ) {
        CinResCollection::CameraView view;
        bHasView = attractCinematic.tick( gpTimer->SystemDeltaTime, view );
        if ( bHasView ) {
            gpCamera->setCinematicCameraOverride(
                view.Position, view.Up, view.Target, view.Fov * kDegToRad );
            // Streamed around the camera
            setAttractCameraPosition( view );
            Eigen::Vector4f streamPosition = attractCameraPosition;
            gpWorld->setStreamingPosition( &streamPosition );
        } else if ( !attractCinematic.isLoaded() ) {
            // Gave up on the bank: back to the fixed view
            Eigen::Vector4f streamPosition = getStreamPosition();
            gpWorld->setStreamingPosition( &streamPosition );
            bStreamed = false;
            bHasView = true;
        }
    }
    if ( !attractCinematic.isLoaded() ) {
        updateCamera();
    }

    SetSceneCompositeEnabled( true );
    ApplyWeatherToPostFX( 1.0f );

    if ( !bStreamed ) {
        const Eigen::Vector4f position = getStreamPosition();
        bStreamed = gpWorld->isStreamedAround( &position, true, false );
        if ( bStreamed ) {
            OTDU_LOG_DEBUG( "Login world view streamed\n" );
        }
    }

    if ( bStreamed && bHasView && pLoginPlayer != nullptr ) {
        FlashClip backdrop;
        if ( pLoginPlayer->getClipPlayer( kBackdropClip, &backdrop ) &&
             backdrop.pSprite != nullptr ) {
            backdrop.setProperty( eFlashProperty::FP_Visible, false );

            for ( const LoginOverlay &overlay : kOverlays ) {
                CXForm transform;
                if ( pLoginPlayer->getObjectColorTransform( "/", overlay.Depth,
                                                            &transform ) &&
                     transform.MulTerm[3] != overlay.Alpha ) {
                    transform.MulTerm[3] = overlay.Alpha;
                    pLoginPlayer->setObjectColorTransform( "/", overlay.Depth,
                                                           transform );
                }
            }
        }
    }
}

void LoginWorldScene::tickShowcaseCar() {
    showcaseCar.tick();

    CarState *pCarState = showcaseCar.getCarState();
    VehicleBase *pVehicle = showcaseCar.getVehicle();
    if ( pVehicle == nullptr ) {
        return;
    }

    if ( pEnvMappedCar != pVehicle ) {
        GSCarDisplay::SetRealTimeEnvMappedCar( pVehicle );
        pEnvMappedCar = pVehicle;
    }

    if ( gpMngShadow != nullptr ) {
        const Eigen::Vector4f target =
            pCarState->getBody()->WorldMatrix.row( 3 ).transpose();
        gpMngShadow->setTarget( &target );
    }
}

bool LoginWorldScene::onConfigurationChanged( const int32_t ) {
    SetSceneCompositeEnabled( bActive );
    return true;
}

void LoginWorldScene::setAttractCameraPosition(
    const CinResCollection::CameraView &view ) {
    const Eigen::Vector3f position =
        view.Position + gpWorldStreaming->getPosition().head<3>();
    attractCameraPosition << position.x(), position.y(), position.z(), 1.0f;
}

Eigen::Vector4f LoginWorldScene::getStreamPosition() const {
    if ( attractCinematic.isLoaded() ) {
        return attractCameraPosition;
    }
    return Eigen::Vector4f( kCameraPosition.x(), kCameraPosition.y(),
                            kCameraPosition.z(), 1.0f );
}

void LoginWorldScene::updateCamera() {
    const Eigen::Vector3f origin = gpWorldStreaming->getPosition().head<3>();
    gpCamera->setCinematicCameraOverride( kCameraPosition - origin, kCameraUp,
                                          kCameraTarget - origin,
                                          kCameraFovDegrees * kDegToRad );
}
