#include "shared.h"
#include "mng_rental.h"

#include "tdu_instance.h"

#include "flash/gs_flash.h"
#include "flash/movie_player.h"
#include "flash/implementation/ed_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "flash/implementation/flash_resource.h"

#include "game/scenaric/gs_scenaric.h"

#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"

#include "ai/ai_road_query.h"
#include "flash/mng_fade.h"
#include "game/cinematic/gs_cinematics.h"
#include "game/gm_world.h"
#include "game/scenaric/game_intro_tutorial.h"
#include "game/scenaric/spots.h"
#include "game/vehicle/car_state.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/mng_world.h"
#include "world/sector_manager.h"
#include "world/streaming_manager.h"

static constexpr float kSecondsPerHour = 3600.0f;           // DAT_00f41740
static constexpr float kSecondsPerMinute = 60.0f;           // DAT_00f41744
static constexpr float kInvSecondsPerHour = 1.0f / 3600.0f; // DAT_00f4199c
static constexpr float kInvSecondsPerMinute = 1.0f / 60.0f; // DAT_00f41998

MngRental *gpMngRental = nullptr; // DAT_010e7fe

static constexpr float kFadeDuration = 0.5f; // FLOAT_00f41674

static bool IsRentalAgency( const SpotMap *pSpot ) {
    return pSpot != nullptr && pSpot->Type == ST_Unknown8 &&
           ( pSpot->SubType == 2 || pSpot->SubType == 3 );
}

static void SetHUDVisible( const bool bVisible ) {
    FlashPlayer *pHud = gpFlash->getFlashPlayer( "HUD" );
    if ( pHud == nullptr ) {
        return;
    }

    FlashClip clip;
    if ( pHud->getClipPlayer( "/hud", &clip ) ) {
        clip.setProperty( eFlashProperty::FP_Visible, bVisible );
    }
}

static void SetLevelMoviesHidden( const bool bHidden ) {
    const int32_t numMovies = gpFlash->getNumMovies( 1 );
    for ( int32_t i = 0; i < numMovies; i++ ) {
        MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, i );
        if ( pMovie == nullptr ) {
            continue;
        }

        if ( bHidden ) {
            pMovie->field5_0xad0 |= 8;
        } else {
            pMovie->field5_0xad0 &= ~8;
        }
        pMovie->MovieBank.getFlashPlayer()
            ->getBoundFlashResource()
            ->setDrawMode( pMovie->field5_0xad0 ? 2 : 0 );
    }
}

MngRental::MngRental()
    : Manager(),
      pFlashPlayer( nullptr ),
      bEnabled( true ),
      Matrix( Eigen::Matrix4f::Identity() ),
      activeGameMode( GM_Invalid ),
      respawnState( 0 ) {
    // FUN_007bc3e0
    if ( gpMngRental == nullptr ) {
        gpMngRental = this;
        activeGameMode = gpTestDriveInstance->getActiveGameMode();
        respawnState = 0;
    }
}

MngRental::~MngRental() {
    // FUN_007bc120
    if ( gpMngRental == this ) {
        gpMngRental = nullptr;
    }
}

bool MngRental::initialize( TestDriveGameInstance * ) {
    // FUN_006cffe0
    MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, 0 );
    if ( pMovie != nullptr && pMovie->MovieBank.isLoaded() ) {
        pFlashPlayer = pMovie->MovieBank.getFlashPlayer();
    } else {
        pFlashPlayer = nullptr;
    }
    return true;
}

void MngRental::tick( float, float ) {
    // FUN_006d0810
    if ( pFlashPlayer == nullptr ) {
        // The flash player was not ready at init time — keep retrying.
        MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, 0 );
        if ( pMovie != nullptr && pMovie->MovieBank.isLoaded() ) {
            pFlashPlayer = pMovie->MovieBank.getFlashPlayer();
        } else {
            pFlashPlayer = nullptr;
        }
        return;
    }

    if ( !updateRentalTimer() ) {
        return;
    }

    if ( respawnState == 1 ) {
        returnToRentalSpot();
        return;
    }

    if ( gpScenaric->getTimeLeftForRental() <= 0.0f &&
         ( gpMngFade->getTransitionFlags() & 1 ) == 0 ) {
        startRentalExpiry();
    }
}

void MngRental::startRentalExpiry() {
    if ( gpCinematics->isCinematicPlaying( CT_Foreground, false, false ) ||
         activeGameMode != GM_FreeRide ) {
        return;
    }

    gpMngFade->startTransition(
        ( gpMngFade->getTransitionFlags() & ~0xcu ) | 3u, kFadeDuration,
        &MngRental::OnFadeDone, reinterpret_cast<void *>( 0 ), this );
    SetHUDVisible( false );

    if ( IsRentalAgency( gpScenaric->getRentalSpot() ) ) {
        gpMngFade->setRenderLogo( true );
    }
}

void MngRental::returnToRentalSpot() {
    SpotMap *pRentalSpot = gpScenaric->getRentalSpot();
    if ( !IsRentalAgency( pRentalSpot ) || gpActiveGM == nullptr ) {
        return;
    }

    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( true );
    }

    Eigen::Vector4f position( pRentalSpot->X, pRentalSpot->Y, pRentalSpot->Z,
                              0.0f );
    if ( gpWorld->isAreaStreamingEnabled() ) {
        gpWorld->setStreamingPositionPtr( nullptr );
        if ( gpWorldStreaming->isStreamingActive() ) {
            gpWorldStreaming->setStreamCenter( nullptr );
        }
    }
    gpWorld->setStreamingPosition( &position );
    if ( gpWorldStreaming->isStreamingActive() ) {
        gpWorldStreaming->setStreamCenter( nullptr );
        gpWorldStreaming->setStreamCenterPosition( position );
        gpWorldStreaming->updateStreaming();
    }

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bNeedStreamUpdate = true;
    pStreaming->sectorManager.bDrawSector = false;
    pStreaming->sectorManager.bStreamSector = true;
    gpWorldStreaming->updateStreaming();

    if ( !gpWorld->isStreamedAround( &position, true, true ) ) {
        return;
    }

    SnapToRoadFrame( position, Matrix, eRoadSnapMode::RSM_KeepAwayFromEnds,
                     true, true );

    GameIntroTutorial &tutorial = gpScenaric->getIntroTutorial();
    if ( tutorial.getStep() == kTutorialStateFirstDrive ) {
        CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
        PhysicsVehicle *pPhysicsVehicle =
            ( pLocalCar != nullptr ) ? pLocalCar->getPhysicsVehicle() : nullptr;
        if ( pPhysicsVehicle != nullptr ) {
            PhysicsLock lock;
            pPhysicsVehicle->resetToTransform( Matrix, true );
            pPhysicsVehicle->setPostponedPause( 2 );
        }
        gpScenaric->setTimeLeftForRental(
            float( tutorial.getCarRentalTime() ) );
    } else {
        gpPlayerData->getLoadedSave()->getGarage()->endRental();
        gpActiveGM->unloadLocalPlayerCar();
        gpActiveGM->respawnLocalPlayerCar( Matrix );
        gpScenaric->setRentalSpot( nullptr );
    }

    gpMngFade->startTransition( ( gpMngFade->getTransitionFlags() & ~8u ) | 7u,
                                kFadeDuration, &MngRental::OnFadeDone,
                                reinterpret_cast<void *>( 1 ), this );
    respawnState = 0;
    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( false );
    }
    gpMngFade->setRenderLogo( false );
}

void MngRental::pause() {
    // FUN_006cff20
    if ( pFlashPlayer == nullptr ) {
        return;
    }
    FlashClip clip;
    if ( pFlashPlayer->getClipPlayer( "carRent", &clip ) ) {
        ScopedMutexLock lock = gpFlash->scopedLock();
        gpPreviousActiveFlashResource = gpActiveFlashResource;
        clip.pSprite->getDisplayList()->getFlagsRW() &= 0xfffffffe;
    }
}

void MngRental::OnFadeDone( void *pData, void *pSource ) {
    static_cast<MngRental *>( pData )->onFadeDone(
        int32_t( reinterpret_cast<intptr_t>( pSource ) ) );
}

void MngRental::onFadeDone( int32_t phase ) {
    // FUN_006d0620
    if ( phase == 0 ) {
        CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
        PhysicsVehicle *pPhysicsVehicle =
            ( pLocalCar != nullptr ) ? pLocalCar->getPhysicsVehicle() : nullptr;
        if ( pPhysicsVehicle != nullptr ) {
            PhysicsLock lock;
            Matrix = pPhysicsVehicle->getChassisWorldMatrix();
        }

        if ( !IsRentalAgency( gpScenaric->getRentalSpot() ) ) {
            gpPlayerData->getLoadedSave()->getGarage()->endRental();
            if ( gpActiveGM != nullptr ) {
                gpActiveGM->unloadLocalPlayerCar();
                gpActiveGM->respawnLocalPlayerCar( Matrix );
            }
        } else {
            if ( pPhysicsVehicle != nullptr ) {
                PhysicsLock lock;
                pPhysicsVehicle->setPostponedPause( 1 );
            }
            gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() |
                                           8u );
            respawnState = 1;
            if ( gpMngWorld != nullptr ) {
                gpMngWorld->setDisableStreamingOnCar( true );
            }
        }

        SetLevelMoviesHidden( true );
    } else if ( phase == 1 ) {
        SetHUDVisible( true );
        SetLevelMoviesHidden( false );
    }
}

bool MngRental::updateRentalTimer() {
    // FUN_00431cd0
    FlashClip clip;
    if ( !pFlashPlayer->getClipPlayer( "carRent", &clip ) ) {
        return false;
    }

    // Not renting: stop the clock clip from ticking (clear display-list bit 0).
    if ( !gpPlayerData->isRentingCar() ) {
        ScopedMutexLock lock = gpFlash->scopedLock();
        gpPreviousActiveFlashResource = gpActiveFlashResource;
        clip.pSprite->getDisplayList()->getFlagsRW() &= 0xfffffffe;
        return false;
    }

    // Show the clock only while free-riding / on a mission and outside a
    // foreground cinematic.
    bool bVisible = true;
    if ( ( activeGameMode != GM_FreeRide && activeGameMode != GM_Mission ) ||
         gpCinematics->isCinematicPlaying( CT_Foreground, false, false ) ) {
        bVisible = false;
    }
    clip.setProperty( eFlashProperty::FP_Visible, bVisible );

    // Break the remaining seconds into hh:mm:ss.
    const float timeLeft = gpScenaric->getTimeLeftForRental();
    const int32_t hours = (int32_t)( timeLeft * kInvSecondsPerHour );
    const float secondsInHour = timeLeft - (float)hours * kSecondsPerHour;
    const int32_t minutes = (int32_t)( secondsInHour * kInvSecondsPerMinute );
    const int32_t seconds =
        (int32_t)( secondsInHour - (float)minutes * kSecondsPerMinute );

    pFlashPlayer->setVariableValueVariadic( "/:hour_rent", "%02d", hours );
    pFlashPlayer->setVariableValueVariadic( "/:min_rent", "%02d", minutes );
    pFlashPlayer->setVariableValueVariadic( "/:sec_rent", "%02d", seconds );

    return true;
}
