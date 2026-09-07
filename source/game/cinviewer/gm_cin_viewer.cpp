#include "shared.h"
#include "gm_cin_viewer.h"

#include <cctype>
#include <cstring>
#include <filesystem>

#include "config/gs_config.h"
#include "flash/entities/flash_entity.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "gs_timer.h"
#include "game/cinematic/mng_cinematics.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/scenaric_bank.h"
#include "game/scenaric/entity/base_entity.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/spots.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "world/gs_world.h"

static constexpr float kFlashSeekBarWidth = 600.0f; // DAT_00f41690

static inline const char *GetBankFolder( const char *pBankName ) {
    const bool bSpotBank =
        pBankName != nullptr &&
        ::toupper( static_cast<unsigned char>( pBankName[0] ) ) == 'E';
    return bSpotBank ? "Level/Hawai/Spots" : "Interior";
}

GMCinViewer::GMCinViewer()
    : GameModeWorld( 6 ),
      flashListBank( &contentBanks ),
      flashListCine( &contentCine ) {
    // FUN_007b3720
    OTDU_UNIMPLEMENTED; // TODO: SCENARIC (BANK)
}

GMCinViewer::~GMCinViewer() { stopCine(); }

void GMCinViewer::initialize() {
    // FUN_007b4440
    bPlayRequested = false;
    bPlayPending = false;

    contentBanks.clear();
    scanContentBanks( "Level/Hawai/Spots/" );
    scanContentBanks( "Interior/" );

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

    OTDU_UNIMPLEMENTED; // TODO: SCENARIC
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

    gpCamera->setCameraMode( 10, nullptr, 0.0f );

    FlashPlayer *pViewer = gpFlash->getFlashPlayer( "VIEWER" );
    if ( pViewer != nullptr ) {
        // NOTE: Not part of the original pcode (this is tentatively trying to
        // restore the flash logic for this GM)
        mngFlash.addWidget( "bnkl", &flashListBank );
        flashListBank.setPlayer( pViewer, "/array_bnk" );

        mngFlash.addWidget( "cinl", &flashListCine );
        flashListCine.setPlayer( pViewer, "/array_cine" );

        gpFlash->bindPlayerEntity( &mngFlash, "VIEWER" );
    }

    OTDU_UNIMPLEMENTED; // TODO: SCENARIC
}

void GMCinViewer::tick( float deltaTime, float totalTime ) {
    // FUN_007b4120
    GameModeWorld::tick( deltaTime, totalTime );

    if ( bPlayRequested ) {
        bPlayPending = true;
    }
    if ( bPlayPending ) {
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

    OTDU_UNIMPLEMENTED; // TODO: SCENARIC
}

void GMCinViewer::pause() {
    // FUN_007b39b0
    GameModeWorld::pause();
    OTDU_UNIMPLEMENTED; // TODO: SCENARIC
}

void GMCinViewer::destroy() {
    // FUN_007b3980
    OTDU_UNIMPLEMENTED; // TODO: SCENARIC
    stopCine();
    GameModeWorld::destroy();
}

void GMCinViewer::positionAtSpot( const char *pBankName ) {
    // FUN_007b3450
    bPlayRequested = true;

    if ( pBankName == nullptr || gpScenaric == nullptr ) {
        return;
    }

    char name8[8] = { 0 };
    for ( int32_t i = 0; i < 8 && pBankName[i] != '\0' && pBankName[i] != '.';
          i++ ) {
        const char c = pBankName[i];
        name8[i] = ( c >= 'a' && c <= 'z' ) ? static_cast<char>( c - 32 ) : c;
    }
    uint64_t sourceHash = 0;
    std::memcpy( &sourceHash, name8, sizeof( sourceHash ) );

    Spots &spots = gpScenaric->getSpotList();
    pCurrentSpot = nullptr;
    const uint32_t count = spots.getNumWorldSpots();

    for ( uint32_t i = 0; i < count; i++ ) {
        SpotMap *pSpot = spots.getWorldSpot( static_cast<int32_t>( i ) );
        if ( pSpot != nullptr && pSpot->SourceHashcode == sourceHash ) {
            pCurrentSpot = pSpot;
            break;
        }
    }
    if ( pCurrentSpot == nullptr ) {
        return;
    }

    const Eigen::Vector4f pos( pCurrentSpot->X, pCurrentSpot->Y,
                               pCurrentSpot->Z, 1.0f );

    teleportLocalPlayerCar( pos.head<3>() );

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

void GMCinViewer::loadBankCinematics( const char *pBankName ) {
    // FUN_007b39e0
    contentCine.clear();
    if ( pBankName == nullptr || gpScenaric == nullptr ) {
        return;
    }

    const char *pFolder = GetBankFolder( pBankName );

    ScenaricBank tempBank;
    GSScenaric::LoadScenaricBank( pFolder, pBankName, &tempBank );

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

bool GMCinViewer::onMessage( FlashMessage *pMessage ) {
    // FUN_007b3b00
    switch ( *pMessage ) {
    case FlashMessage( "bnch" ): { // a bank was picked in the bnkl list
        const char *pBankName = flashListBank.getSelectedName();
        selectedBankName = ( pBankName != nullptr ) ? pBankName : "";
        const size_t dot = selectedBankName.find_last_of( '.' );
        if ( dot != std::string::npos ) {
            selectedBankName.resize( dot );
        }
        loadBankCinematics( selectedBankName.c_str() );
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
        return GameModeWorld::onMessage( pMessage );
    }
}

bool GMCinViewer::executeAction( int32_t param_2, void *param_3 ) {
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

std::string GMCinViewer::resolveCineResource( const char *pCineName ) {
    std::string suffix = pCineName;
    if ( suffix.size() > 2 && suffix[0] == 'C' && suffix[1] == '_' ) {
        suffix = suffix.substr( 2 );
    }
    for ( char &c : suffix ) {
        c = static_cast<char>( ::toupper( static_cast<unsigned char>( c ) ) );
    }

    BankInstance &bi = playbackBank.getBank();
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

    const char *pFolder = GetBankFolder( selectedBankName.c_str() );
    GSScenaric::LoadScenaricBank( pFolder, selectedBankName.c_str(),
                                  &playbackBank );
    bPlaybackBankActive = true;

    std::string cinName = resolveCineResource( pCineName );

    pCurrentCine = new EntityCinematic();
    pCurrentCine->setGameModeInterface( this );
    pCurrentCine->setBank( &playbackBank );
    pCurrentCine->setCutsceneName( cinName.c_str() );
    pCurrentCine->initialize();

    if ( pCurrentSpot != nullptr ) {
        pCurrentCine->launchSpotCine( pCurrentSpot, 0.0f );
    } else {
        pCurrentCine->start( true );
        if ( gpCamera != nullptr ) {
            pCurrentCine->setPlaybackAnchor(
                gpCamera->getRenderCamera()->getTarget() );
        }
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
