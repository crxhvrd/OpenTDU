#include "shared.h"
#include "gm_cin_viewer.h"

#include <cctype>
#include <cstring>
#include <filesystem>

#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "flash/entities/flash_entity.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "gs_timer.h"
#include "physics/gs_physics.h"
#include "game/cinematic/mng_cinematics.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/scenaric_bank.h"
#include "game/scenaric/entity/base_entity.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/spots.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/streaming_manager.h"

static constexpr float kFlashSeekBarWidth = 600.0f; // DAT_00f41690
static constexpr int32_t kMaxSpotSectorWaitTicks = 600;

static inline const char *GetBankFolder( const char *pBankName ) {
    const bool bSpotBank =
        pBankName != nullptr &&
        ::toupper( static_cast<unsigned char>( pBankName[0] ) ) == 'E';
    return bSpotBank ? "Level/Hawai/Spots" : "Interior";
}

static bool IsExteriorSpotBank( const char *pBankName ) {
    return pBankName != nullptr &&
           ( pBankName[0] == 'E' || pBankName[0] == 'e' );
}

static void BankStemToHash8( const char *pBankName, char out[8] ) {
    memset( out, 0, 8 );
    if ( pBankName == nullptr ) {
        return;
    }
    for ( int32_t i = 0; i < 8 && pBankName[i] != '\0' && pBankName[i] != '.';
          i++ ) {
        const char c = pBankName[i];
        out[i] = ( c >= 'a' && c <= 'z' ) ? static_cast<char>( c - 32 ) : c;
    }
}

static eGameMode GetInteriorPlacementMode( const char *pBankName ) {
    char name8[8] = {};
    BankStemToHash8( pBankName, name8 ); // uppercases

    if ( name8[0] == 'G' ) {
        return GM_Garage;
    }
    if ( std::memcmp( name8, "IDR", 3 ) == 0 ) {
        return GM_Dressing;
    }
    return GM_Invalid;
}

GMCinViewer::GMCinViewer()
    : GameModeWorld( 6 ),
      flashListBank( &contentBanks ),
      flashListCine( &contentCine ) {
    // FUN_007b3720
    freeCineFrame = Matrix4x4f_t::Identity();
    freeCineFrame.row( 3 ) << 9200.0f, 0.01f, 9300.0f, 1.0f;
}

GMCinViewer::~GMCinViewer() {
    stopCine();
    leaveInteriorPreview();
}

void GMCinViewer::initialize() {
    // FUN_007b4440
    bPlayRequested = false;
    bPlayPending = false;

    contentBanks.clear();
    scanContentBanks( "Level/Hawai/Spots/" );
    scanContentBanks( "Interior/" );

    if ( gpScenaric != nullptr ) {
        const CinematicViewerConfig &cfg =
            gpScenaric->getCinematicViewerDefault();
        if ( !cfg.BankName.empty() ) {
            loadBankCinematics( nullptr, cfg.BankName.c_str() );
        }
    }

    GameModeWorld::initialize();

    if ( gpScenaric != nullptr ) {
        const CinematicViewerConfig &cfg =
            gpScenaric->getCinematicViewerDefault();
        if ( !cfg.BankName.empty() && !cfg.CinematicName.empty() ) {
            const size_t slash = cfg.BankName.find_last_of( "\\/" );
            selectedBankName = ( slash != std::string::npos )
                                   ? cfg.BankName.substr( slash + 1 )
                                   : cfg.BankName;
            selectedBankName = selectedBankName.substr( 0, 8 );
            selectedCineName = cfg.CinematicName;
        }
    }
}

void GMCinViewer::scanContentBanks( const char *pSubDir ) {
    std::string dir = gpConfig->getResRootPath();
    dir += pSubDir;

    std::error_code ec;
    for ( std::filesystem::directory_iterator it( dir, ec ), end;
          !ec && it != end; it.increment( ec ) ) {
        const std::filesystem::path &path = it->path();
        if ( path.extension() == ".bnk" ) {
            contentBanks.push_back( path.stem().string() );
        }
    }
}

void GMCinViewer::reset() {
    // FUN_007b4340
    GameModeWorld::reset();

    positionAtSpot( selectedBankName.c_str() );

    gpCamera->setCameraMode( 10, 0, 0.0f );
    gpPhysics->setPlayerSlowDownForced( true );

    FlashPlayer *pViewer = gpFlash->getFlashPlayer( "VIEWER" );
    if ( pViewer != nullptr ) {
        // NOTE: Not part of the original pcode (this is tentatively trying
        // to restore the flash logic for this GM)
        mngFlash.addWidget( "bnkl", &flashListBank );
        flashListBank.setPlayer( pViewer, "/array_bnk" );

        mngFlash.addWidget( "cinl", &flashListCine );
        flashListCine.setPlayer( pViewer, "/array_cine" );

        gpFlash->bindPlayerEntity( &mngFlash, "VIEWER" );
    }
}

void GMCinViewer::tick( float deltaTime, float totalTime ) {
    // FUN_007b4120
    GameModeWorld::tick( deltaTime, totalTime );

    if ( pInteriorScene != nullptr ) {
        pInteriorScene->tick();
    }

    if ( bPlayRequested ) {
        bPlayPending = true;
    }
    if ( bPlayPending && isSpotFrameReady() ) {
        playCine( selectedCineName.c_str() );
        bPlayPending = false;
        bPlayRequested = false;
    }

    if ( pCurrentCine != nullptr ) {
        const CinResCollection::CameraView &view =
            pCurrentCine->getCameraView();

        if ( view.bValid && gpCamera != nullptr ) {
            gpCamera->setCinematicCameraOverride( view.Position, view.Up,
                                                  view.Target,
                                                  view.Fov * 0.01745329252f );
        }

        const float curTime = pCurrentCine->getTime();
        const float totalTime = pCurrentCine->getDuration();

        FlashPlayer *pViewer = gpFlash->getFlashPlayer( "VIEWER" );
        if ( pViewer != nullptr ) {
            FlashClip seekBar;
            if ( totalTime > 0.0f &&
                 pViewer->getClipPlayer( "barre/cur", &seekBar ) ) {
                const float x = static_cast<float>( static_cast<int32_t>(
                                    curTime * kFlashSeekBarWidth ) ) /
                                totalTime;
                seekBar.setProperty( eFlashProperty::FP_X, x );
            }
            pViewer->setVariableValue( "/:cur_time", curTime );
        }
    }
}

void GMCinViewer::pause() {
    // FUN_007b39b0
    gpPhysics->setPhysicsThreadPaused( false );
    GameModeWorld::pause();
    mngFlash.removeWidget( &flashListBank );
    mngFlash.removeWidget( &flashListCine );
}

void GMCinViewer::destroy() {
    // FUN_007b3980
    stopCine();
    leaveInteriorPreview();
    GameModeWorld::destroy();
}

void GMCinViewer::positionAtSpot( const char *pBankName ) {
    // FUN_007b3450
    bPlayRequested = true;
    spotSectorWaitTicks = 0;
    pCurrentSpot = nullptr;

    if ( pBankName == nullptr || gpScenaric == nullptr ) {
        return;
    }

    int32_t spotIndex = -1;
    pCurrentSpot = findSpotForBank( pBankName, &spotIndex );

    if ( pCurrentSpot == nullptr ) {
        // Dressing/ETrade edge cases
        const int32_t currentIndex = gpScenaric->getCurrentSpotIndex();
        pCurrentSpot =
            gpScenaric->getSpotList().getWorldSpotChecked( currentIndex );
        spotIndex = ( pCurrentSpot != nullptr ) ? currentIndex : -1;

        if ( pCurrentSpot == nullptr ) {
            pCurrentSpot = findSpotForBank( "IAS_A", &spotIndex );
        }
        if ( pCurrentSpot == nullptr ) {
            return;
        }
    }

    Eigen::Vector4f pos( pCurrentSpot->X, pCurrentSpot->Y, pCurrentSpot->Z,
                         1.0f );

    teleportLocalPlayerCar( pos.head<3>() );

    if ( gpScenaric != nullptr && spotIndex >= 0 ) {
        gpScenaric->setCurrentSpotIndex( spotIndex );
    }

    if ( gpWorld != nullptr ) {
        gpWorld->setStreamingPosition( &pos );
        gpWorld->setPlayerPosition( &pos );
    }

    if ( gpCamera != nullptr ) {
        CameraGame *pCam = gpCamera->findCamera( gpCamera->getBoundCarState() );
        if ( pCam != nullptr ) {
            pCam->setPosition( pos );
        }
    }
    if ( gpActiveGMScene != nullptr ) {
        gpActiveGMScene->setVerifyGameFreeze( false );
    }
    if ( gpWorld != nullptr ) {
        Eigen::Vector4f streamPosition = pos;
        gpWorld->setPlayerPosition( &streamPosition );
        gpWorld->setStreamingPosition( &streamPosition );
        gpWorld->setDirtyStreaming( true );
    }
}

SpotMap *GMCinViewer::findSpotForBank( const char *pBankName,
                                       int32_t *pOutIndex ) {
    // FUN_007b3450 (the spot lookup half)
    if ( pOutIndex != nullptr ) {
        *pOutIndex = -1;
    }
    if ( pBankName == nullptr || gpScenaric == nullptr ) {
        return nullptr;
    }

    char name8[8] = {};
    BankStemToHash8( pBankName, name8 );

    name8[0] = 'E';

    uint64_t bankHash = 0;
    std::memcpy( &bankHash, name8, sizeof( bankHash ) );

    Spots &spots = gpScenaric->getSpotList();
    const uint32_t count = spots.getNumWorldSpots();
    for ( uint32_t i = 0; i < count; i++ ) {
        SpotMap *pSpot = spots.getWorldSpot( static_cast<int32_t>( i ) );
        if ( pSpot != nullptr && pSpot->SourceHashcode == bankHash ) {
            if ( pOutIndex != nullptr ) {
                *pOutIndex = static_cast<int32_t>( i );
            }
            return pSpot;
        }
    }
    return nullptr;
}

bool GMCinViewer::isSpotFrameReady() {
    const bool bSpotSectorReady =
        pCurrentSpot == nullptr || gpScenaric == nullptr ||
        gpScenaric->getSpotList().getSpotSector( pCurrentSpot->Hashcode ) !=
            nullptr;
    const bool bInteriorReady =
        pInteriorScene == nullptr ||
        pInteriorScene->getState() == SceneInterior::SIS_Live;

    if ( bSpotSectorReady && bInteriorReady ) {
        return true;
    }

    return ++spotSectorWaitTicks >= kMaxSpotSectorWaitTicks;
}

void GMCinViewer::enterInteriorPreview( const char *pBankName ) {
    leaveInteriorPreview();
    if ( pBankName == nullptr || pBankName[0] == '\0' || gpFile == nullptr ||
         gpConfig == nullptr ) {
        return;
    }

    char bankPath[512] = {};
    snprintf( bankPath, sizeof( bankPath ), "%sInterior/%s.bnk",
              gpConfig->getResRootPath(), pBankName );

    if ( !interiorBank.loadBank( gpFile, bankPath ) ) {
        OTDU_LOG_WARN( "Failed to load interior bank '%s'\n", bankPath );
        return;
    }
    interiorBank.loadContentAsync();

    pInteriorScene = std::make_unique<SceneInterior>();
    pInteriorScene->setPlacementGameMode(
        GetInteriorPlacementMode( pBankName ) );
    pInteriorScene->enter( &interiorBank, false );
}

void GMCinViewer::leaveInteriorPreview() {
    stopCine();

    const bool bWasEntered = pInteriorScene != nullptr;

    pInteriorScene.reset();
    if ( interiorBank.isLoaded() || interiorBank.getBankFile() != nullptr ) {
        interiorBank.unload();
    }

    if ( !bWasEntered || gpWorld == nullptr ) {
        return;
    }

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    if ( pStreaming != nullptr ) {
        pStreaming->sectorManager.bDrawSector = true;
        pStreaming->areaGrid.bDrawArea = true;
    }
}

void GMCinViewer::loadBankCinematics( const char *pBankFolder,
                                      const char *pBankName ) {
    // FUN_007b39e0
    contentCine.clear();
    if ( pBankName == nullptr || gpScenaric == nullptr ) {
        return;
    }

    ScenaricBank tempBank;
    GSScenaric::LoadScenaricBank( pBankFolder, pBankName, &tempBank );

    const int32_t count = tempBank.getEntityCount();
    for ( int32_t i = 0; i < count; i++ ) {
        Entity *pEntity = tempBank.getEntity( i );
        if ( pEntity != nullptr &&
             pEntity->getType() == eEntityType::ET_CINE ) {
            contentCine.push_back( pEntity->getInstanceName() );
        }
    }

    gpScenaric->unloadBank( &tempBank );
}

bool GMCinViewer::onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) {
    // FUN_007b3b00
    switch ( *pMessage ) {
    case FlashMessage( "bnch" ): { // a bank was picked in the bnkl list
        const char *pBankName = flashListBank.getSelectedName();
        selectedBankName = ( pBankName != nullptr ) ? pBankName : "";

        const size_t dot = selectedBankName.find_last_of( '.' );
        if ( dot != std::string::npos ) {
            selectedBankName.resize( dot );
        }
        loadBankCinematics( GetBankFolder( selectedBankName.c_str() ),
                            selectedBankName.c_str() );
        selectedCineName = contentCine.empty() ? std::string() : contentCine[0];

        positionAtSpot( selectedBankName.c_str() );

        if ( IsExteriorSpotBank( selectedBankName.c_str() ) ) {
            leaveInteriorPreview();
        } else {
            enterInteriorPreview( selectedBankName.c_str() );
        }
        return true;
    }
    case FlashMessage( "cich" ): { // a cinematic was picked in the cinl list
        const char *pCineName = flashListCine.getSelectedName();
        selectedCineName = ( pCineName != nullptr ) ? pCineName : "";
        if ( pCurrentCine != nullptr ) {
            FlashPlayer *pViewer = gpFlash->getFlashPlayer( "VIEWER" );
            if ( pViewer != nullptr ) {
                pViewer->setVariableValue( "/:IsPlay", 0 );
            }
            pCurrentCine->setPaused( false );
            if ( gpTimer != nullptr ) {
                gpTimer->GameSpeed = gpTimer->PreviousGameSpeed;
            }
            releaseCurrentCine();
        }

        positionAtSpot( selectedBankName.c_str() );
        return true;
    }
    case FlashMessage( "play" ): // toggle play / pause of the current cinematic
        togglePlayPause();
        return true;
    case FlashMessage( "frwd" ): // frame step / fast-forward
        stepFrame( 1.0f );
        return true;
    case FlashMessage( "rewd" ): // frame step back / rewind
        stepFrame( -1.0f );
        return true;
    case FlashMessage( "strt" ): // restart the current cinematic
        restartCine();
        return true;
    default:
        return GameModeWorld::onMessage( pMessage, pPlayer );
    }
}

bool GMCinViewer::executeAction( int32_t param_2,
                                 void *param_3,
                                 void * /*pData*/ ) {
    // FUN_007b3380
    FlashPlayer *pViewer = gpFlash->getFlashPlayer( "VIEWER" );

    if ( param_2 == CE_Started ) {
        EntityCinematic *pCine = static_cast<EntityCinematic *>( param_3 );
        if ( pViewer != nullptr && pCine != nullptr ) {
            pViewer->setVariableValue( "/:total_time", pCine->getDuration() );
        }
    } else if ( param_2 == CE_Finished ) {
        if ( pViewer != nullptr ) {
            pViewer->setVariableValue( "/:IsPlay", 1 );
        }
        if ( pCurrentCine != nullptr ) {
            pCurrentCine->setPaused( true );
        }
        if ( gpTimer != nullptr && gpTimer->GameSpeed != 0.0f ) {
            if ( gpTimer->PreviousGameSpeed != gpTimer->GameSpeed ) {
                gpTimer->PreviousGameSpeed = gpTimer->GameSpeed;
            }
            gpTimer->GameSpeed = 0.0f;
        }
    }

    return true;
}

std::string GMCinViewer::resolveCineResource( ScenaricBank &cineBank,
                                              const char *pCineName ) {
    std::string suffix = pCineName;
    if ( suffix.size() > 2 && suffix[0] == 'C' && suffix[1] == '_' ) {
        suffix = suffix.substr( 2 );
    }
    for ( char &c : suffix ) {
        c = static_cast<char>( ::toupper( static_cast<unsigned char>( c ) ) );
    }

    BankInstance &bi = cineBank.getBank();
    const int32_t n = bi.getNumElements();
    std::string firstCin;
    for ( int32_t i = 0; i < n; i++ ) {
        ParsedBankEntry e;
        bi.getEntry( i, e );
        if ( e.pData == nullptr || e.Size < 4 ||
             *reinterpret_cast<const uint32_t *>( e.pData ) !=
                 0x214e4943u /* "CIN!" */ ) {
            continue;
        }
        // basename = last path component, extension stripped
        std::string base = e.Filename;
        const size_t slash = base.find_last_of( "\\/" );
        if ( slash != std::string::npos ) {
            base = base.substr( slash + 1 );
        }
        const size_t dot = base.find_last_of( '.' );
        if ( dot != std::string::npos ) {
            base = base.substr( 0, dot );
        }
        if ( firstCin.empty() ) {
            firstCin = base;
        }

        std::string upper = base;
        for ( char &c : upper ) {
            c = static_cast<char>(
                ::toupper( static_cast<unsigned char>( c ) ) );
        }
        if ( upper.size() >= suffix.size() &&
             upper.compare( upper.size() - suffix.size(), suffix.size(),
                            suffix ) == 0 ) {
            return base;
        }
    }
    return firstCin;
}

void GMCinViewer::playCine( const char *pCineName ) {
    // FUN_007b3f80
    stopCine();
    if ( pCineName == nullptr || pCineName[0] == '\0' ||
         gpScenaric == nullptr || selectedBankName.empty() ) {
        return;
    }

    const Eigen::Vector4f streamOrigin =
        ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() )
            ? gpWorldStreaming->getPosition()
            : Eigen::Vector4f::Zero();

    SpotSector *pSpotSector =
        ( pCurrentSpot != nullptr )
            ? gpScenaric->getSpotList().getSpotSector( pCurrentSpot->Hashcode )
            : nullptr;

    ScenaricBank *pCineBank = nullptr;
    if ( pInteriorScene != nullptr && pInteriorScene->getBank().isLoaded() ) {
        pCineBank = &pInteriorScene->getBank();
    } else {
        Matrix4x4f_t bankTransform = freeCineFrame;
        if ( pCurrentSpot != nullptr ) {
            bankTransform = Matrix4x4f_t::Identity();
            if ( pSpotSector != nullptr ) {
                bankTransform = BuildSpotSectorRotation( pSpotSector );
            }
            bankTransform.row( 3 ) << pCurrentSpot->X, pCurrentSpot->Y,
                pCurrentSpot->Z, 1.0f;
        }
        playbackBank.setBankTransform( bankTransform );

        const char *pFolder = GetBankFolder( selectedBankName.c_str() );
        GSScenaric::LoadScenaricBank( pFolder, selectedBankName.c_str(),
                                      &playbackBank );
        bPlaybackBankActive = true;
        pCineBank = &playbackBank;
    }

    std::string cinName = resolveCineResource( *pCineBank, pCineName );

    pCurrentCine = new EntityCinematic();
    pCurrentCine->setGameModeInterface( this );
    pCurrentCine->setBank( pCineBank );
    pCurrentCine->setCutsceneName( cinName.c_str() );
    pCurrentCine->initialize();

    // FUN_007b4120
    gpPhysics->setPhysicsThreadPaused( false );

    const bool bUseInteriorFrame = pInteriorScene != nullptr &&
                                   pInteriorScene->getBank().isLoaded() &&
                                   pInteriorScene->getSourceSpot() != nullptr;

    if ( bUseInteriorFrame ) {
        Matrix4x4f_t frame = pInteriorScene->getBank().getBankTransform();
        frame.row( 3 ).head<3>() -= streamOrigin.head<3>();
        pCurrentCine->launchCineAtFrame( pCurrentSpot, frame );
    } else if ( pCurrentSpot != nullptr && pSpotSector != nullptr ) {
        pCurrentCine->launchSpotCine( pCurrentSpot, 0.0f );
    } else {
        Eigen::Vector3f anchor = -streamOrigin.head<3>();
        if ( pCurrentSpot == nullptr ) {
            freeCineFrame.row( 3 ).head<3>() = anchor;
        } else {
            anchor = Eigen::Vector3f( pCurrentSpot->X, pCurrentSpot->Y,
                                      pCurrentSpot->Z ) -
                     streamOrigin.head<3>();
        }
        pCurrentCine->setPlaybackAnchor( anchor );
        pCurrentCine->start( true );
    }

    if ( gpActiveGMScene != nullptr ) {
        gpActiveGMScene->setVerifyGameFreeze( true );
    }

    playbackSpeed = 1.0f;
}

void GMCinViewer::releaseCurrentCine() {
    if ( pCurrentCine != nullptr ) {
        pCurrentCine->stop( true );
        delete pCurrentCine;
        pCurrentCine = nullptr;
    }
    if ( bPlaybackBankActive ) {
        if ( gpScenaric != nullptr ) {
            gpScenaric->unloadBank( &playbackBank );
        }
        bPlaybackBankActive = false;
    }
}

void GMCinViewer::stopCine() {
    releaseCurrentCine();
    if ( gpCamera != nullptr ) {
        gpCamera->clearCinematicCameraOverride();
    }
}

void GMCinViewer::togglePlayPause() {
    // FUN_007b3b00
    if ( pCurrentCine == nullptr ) {
        return;
    }
    if ( !pCurrentCine->isPaused() ) {
        pCurrentCine->setPaused( true );
        if ( gpTimer != nullptr ) {
            if ( gpTimer->PreviousGameSpeed != gpTimer->GameSpeed &&
                 gpTimer->GameSpeed != 0.0f ) {
                gpTimer->PreviousGameSpeed = gpTimer->GameSpeed;
            }
            gpTimer->GameSpeed = 0.0f;
        }
    } else {
        pCurrentCine->setPaused( false );
        if ( gpTimer != nullptr ) {
            gpTimer->GameSpeed = gpTimer->PreviousGameSpeed;
        }
        playbackSpeed = 1.0f;
        pCurrentCine->setPlaybackSpeed( 1.0f );
    }
}

void GMCinViewer::stepFrame( float dir ) {
    // FUN_007b3b00
    if ( pCurrentCine == nullptr ) {
        return;
    }
    if ( pCurrentCine->isPaused() ) {
        pCurrentCine->seek( pCurrentCine->getTime() + dir * ( 1.0f / 60.0f ) );
    } else {
        playbackSpeed +=
            dir; // orig frameTime += 1.0 (fast-forward) / -1.0 (rewind)
        pCurrentCine->setPlaybackSpeed( playbackSpeed );
    }
}

void GMCinViewer::restartCine() {
    // FUN_007b3b00
    if ( pCurrentCine == nullptr ) {
        return;
    }
    playbackSpeed = 1.0f;
    pCurrentCine->setPlaybackSpeed( 1.0f );
    if ( gpTimer != nullptr ) {
        gpTimer->GameSpeed = gpTimer->PreviousGameSpeed;
    }
    pCurrentCine->setPaused( false );
    pCurrentCine->seek( 0.0f );

    FlashPlayer *pViewer = gpFlash->getFlashPlayer( "VIEWER" );
    if ( pViewer != nullptr ) {
        pViewer->setVariableValue( "/:IsPlay", 0 );
    }
}
