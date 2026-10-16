#include "shared.h"
#include "gm_test_car.h"

#include "ai/ai_pool.h"
#include "ai/gs_ai.h"
#include "database/gs_database.h"
#include "flash/gs_flash.h"
#include "flash/entities/flash_entity.h"
#include "flash/implementation/flash_display_list.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "flash/mng_flash_localize.h"
#include "game/avatar/avatar_configuration.h"
#include "game/mng_hud.h"
#include "game/scenaric/avatar_manager.h"
#include "game/scenaric/entity/entity_avatar_passenger.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "gs_timer.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"
#include "player_data/saved_road_discovery.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/camera/gs_camera.h"
#include "tdu_instance.h"
#include "world/gs_world.h"
#include "world/mng_ocean.h"
#include "world/streaming_manager.h"

static constexpr uint64_t kPauseQuitTextHashcode = 0x39c477full;
static constexpr float kCentisecondsPerSecond = 100.0f; // DAT_00f41708

static int32_t gTestCarID = -1;                     // DAT_010e7c8c
static int32_t gTestCarPaintIndex = -1;             // DAT_00f91ca0
static int32_t gTestCarRimID = -1;                  // DAT_00f91ca4
static int32_t gTestCarInteriorID = -1;             // DAT_00f91ca8
static float gTestDuration = 20.0f;                 // DAT_00f91cac
static AvatarConfiguration gPassengerConfiguration; // DAT_016964b0

static void ReleaseLocalPlayerCar() {
    LocalPlayer &localPlayer = gpScenaric->getLocalPlayer();
    CarState *pLocalCar = localPlayer.getCarState();
    if ( pLocalCar == nullptr ) {
        return;
    }

    EntityDriver *pDriver = pLocalCar->getDriver();
    if ( pDriver != nullptr ) {
        pLocalCar->unloadDriver();
        gpScenaric->getAvatarManager().releaseAvatar( pDriver );
    }

    if ( pLocalCar->getPhysicsVehicle() != nullptr ) {
        pLocalCar->getPhysicsVehicle()->setSecondaryDrivingControl( nullptr );
    }

    gpCarState->unloadVehicle( pLocalCar, false, false );
    localPlayer.setCarState( nullptr );
    gpCamera->setBoundCarState( nullptr );
}

static bool ShowHudClip( FlashPlayer *pPlayer, const char *pClipPath ) {
    FlashClip clip;
    if ( !pPlayer->getClipPlayer( pClipPath, &clip ) ) {
        return false;
    }

    ScopedMutexLock lock = gpFlash->scopedLock();
    clip.pSprite->getDisplayList()->getFlagsRW() |= 1;
    return true;
}

void GMTestCar::SetTestCar( const int32_t carID,
                            const int32_t paintIndex,
                            const int32_t rimID,
                            const int32_t interiorID ) {
    // FUN_007b4680
    gTestCarID = carID;
    gTestCarPaintIndex = paintIndex;
    gTestCarRimID = rimID;
    gTestCarInteriorID = interiorID;
}

void GMTestCar::SetTestDuration( const float duration ) {
    // FUN_007b4670
    gTestDuration = duration;
}

AvatarConfiguration &GMTestCar::GetPassengerConfiguration() {
    return gPassengerConfiguration;
}

GMTestCar::GMTestCar()
    : GameModeWorld( 6 ),
      startTime( 0.0f ),
      endTime( 0.0f ),
      pPassenger( nullptr ) {
    // FUN_007b4880
}

GMTestCar::~GMTestCar() {
    // FUN_0042bc70
    if ( gpTimer != nullptr ) {
        gpTimer->GameSpeed = gpTimer->PreviousGameSpeed;
    }
}

void GMTestCar::initialize() {
    // FUN_007b4e20
    if ( gpMngOcean != nullptr ) {
        gbOceanEnabled = true;
    }

    gpPlayerData->getLoadedSave()->getGarage()->beginTestDrive(
        uint32_t( gTestCarID ), gTestCarPaintIndex, gTestCarRimID,
        gTestCarInteriorID );

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->setActiveRenderLayers( 1 );
    pStreaming->areaGrid.bNeedStreamUpdate = true;
    pStreaming->areaGrid.bDrawArea = false;
    pStreaming->sectorManager.bNeedStreamUpdate = true;
    pStreaming->sectorManager.bDrawSector = false;

    GameModeWorld::initialize();
}

void GMTestCar::reset() {
    // FUN_007b4d40
    GameModeWorld::reset();

    gpPhysics->setForceFeedbackAllowed( true );

    loadPassenger();
    mngActor.placePlayerVehicle( 6, true );
    startTime = -1.0f;

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    PhysicsVehicle *pPhysicsVehicle =
        ( pLocalCar != nullptr ) ? pLocalCar->getPhysicsVehicle() : nullptr;
    if ( pPhysicsVehicle != nullptr &&
         ( pPhysicsVehicle->isPhysicsPaused() ||
           pPhysicsVehicle->getPostponedPause() == 1 ) &&
         pPhysicsVehicle->getPostponedPause() != 2 ) {
        PhysicsLock lock;
        pPhysicsVehicle->setPostponedPause( 2 );
    }

    AiPool *pAIPool = gpAI->getAIPool();
    pAIPool->setPoliceScale( 1.0f );
    pAIPool->setForceDensity( false );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: GSAI (police manager +0x44/+0x45 = 1;
                             // FUN_00910cb0 is not ported)
    gpAI->setDisableCops( false );
}

void GMTestCar::tick( float deltaTime, float totalTime ) {
    // FUN_007b4b60
    GameModeWorld::tick( deltaTime, totalTime );

    FlashPlayer *pHudPlayer = GetHudFlashPlayer();
    if ( pHudPlayer == nullptr ) {
        return;
    }

    const bool bHasTimeClip = ShowHudClip( pHudPlayer, "/HUD/time" );
    ShowHudClip( pHudPlayer, "/test" );
    if ( !bHasTimeClip ) {
        return;
    }

    if ( startTime < 0.0f ) {
        startTime = totalTime;
        endTime = totalTime + gTestDuration;
    }

    const float timeLeft = Max( endTime - totalTime, 0.0f );
    const int32_t secondsLeft = int32_t( timeLeft );
    pHudPlayer->setVariableValueVariadic( "/:Min", "%02d", secondsLeft / 60 );
    pHudPlayer->setVariableValueVariadic( "/:Sec", "%02d", secondsLeft % 60 );
    pHudPlayer->setVariableValueVariadic(
        "/:Cent", "%02d", int32_t( timeLeft * kCentisecondsPerSecond ) % 100 );

    if ( isPaused() ) {
        endTime += deltaTime;
    } else if ( totalTime > endTime ) {
        endTestDrive();
    }

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( gpCarState != nullptr && pLocalCar != nullptr ) {
        DiscoverCarRoad( pLocalCar );
    }

    updateOdometerAutosave();
    updateOdometer();
}

void GMTestCar::draw( float deltaTime, float totalTime ) {
    // FUN_007b46a0
    if ( pGameMech != nullptr ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: GameLogic
    }
}

void GMTestCar::pause() {
    // FUN_007b49e0
    releasePassenger();

    gpAI->setDisableCops( true );
    gpAI->setCopsActive( false );
    gpAI->getAIPool()->removeCops();

    GameModeWorld::pause();
}

void GMTestCar::destroy() {
    // FUN_007b48c0
    GameModeWorld::destroy();

    gpPhysics->setForceFeedbackAllowed( false );

    gpPlayerData->getLoadedSave()->getGarage()->endRental();
    ReleaseLocalPlayerCar();
}

bool GMTestCar::onMessage( FlashMessage *pMessage ) {
    // FUN_007b4a40
    FlashPlayer *pHudPlayer = GetHudFlashPlayer();

    switch ( *pMessage ) {
    case FlashMessage( "paou" ): { // pause out
        char whereTo[32] = {};
        if ( pHudPlayer != nullptr &&
             pHudPlayer->getVariableAsString( "/:WhereTo", whereTo ) &&
             FlashMessage( whereTo ) == FlashMessage( "quit" ) ) {
            endTestDrive();
        }
        return GameModeWorld::onMessage( pMessage );
    }
    case FlashMessage( "quit" ):
        endTestDrive();
        return true;
    case FlashMessage( "sbdd" ): // pause menu labels
        if ( pHudPlayer != nullptr && gpMngFlashLocalize != nullptr ) {
            pHudPlayer->setVariableValue( "/:cur_gm", "test" );
            pHudPlayer->setVariableValue( "/:IsRestart", 0 );
            gpMngFlashLocalize->setVarLocalized( pHudPlayer, "/:PauseQuit",
                                                 kPauseQuitTextHashcode );
        }
        return false;
    default:
        return GameModeWorld::onMessage( pMessage );
    }
}

void GMTestCar::loadPassenger() {
    // FUN_007b4720
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr ) {
        return;
    }

    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    const int32_t carID =
        static_cast<int32_t>( pSave->getGarage()
                                  ->pCars[pSave->getDriver()->CurrentCar]
                                  .getCarConfigID() );
    if ( carID >= CarDesc::kFirstBikeConfigID &&
         carID < CarDesc::kFirstAfterBikeConfigID ) {
        return;
    }

    DBEntryCarPhysicsData *pCarData = gpDatabase->getCarDBEntry( carID );
    if ( pCarData == nullptr ) {
        return;
    }

    const char *pNumSeats = pCarData->NumSeats.pAsString;
    if ( pNumSeats == nullptr || pNumSeats[0] == '\0' || pNumSeats[0] == '1' ||
         pNumSeats[0] == '3' ) {
        return;
    }

    EntityAvatar *pAvatar =
        gpScenaric->getAvatarManager().allocateAvatar( ~0ull, 5 );
    if ( pAvatar != nullptr ) {
        pAvatar->installConfiguration( &gPassengerConfiguration, 0, false,
                                       false );
    }

    pPassenger = static_cast<EntityAvatarPassenger *>( pAvatar );
    gpScenaric->placePassenger( pLocalCar, pPassenger );
}

void GMTestCar::releasePassenger() {
    // FUN_007b49e0 (inlined)
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr || pPassenger == nullptr ) {
        return;
    }

    gpScenaric->unloadPassenger( pLocalCar, pPassenger );
    gpScenaric->getAvatarManager().releaseAvatar( pPassenger );
    pPassenger = nullptr;
}

void GMTestCar::endTestDrive() {
    // FUN_007b4960
    OTDU_UNIMPLEMENTED_ONCE; // TODO: GSAI

    SpotMap *pSpot = gpScenaric->getSpotList().getWorldSpotChecked(
        gpScenaric->getCurrentSpotIndex() );
    if ( pSpot != nullptr ) {
        LaunchSpotLevel( pSpot, true );
        return;
    }

    requestExit();
    gpTestDriveInstance->setNextGameMode( GM_Shop );
}
