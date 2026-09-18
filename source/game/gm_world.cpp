#include "shared.h"
#include "gm_world.h"

#include "tdu_instance.h"

#include <cmath>
#include <iterator>

#include "gs_timer.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/avatar/gs_avatar.h"
#include "game/scenaric/gs_scenaric.h"
#include "flash/gs_flash.h"
#include "flash/flash_mouse.h"
#include "flash/implementation/flash_sprite.h"
#include "world/weather/gs_weather.h"
#include "render/camera/gs_camera.h"
#include "player_data/gs_playerdata.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "database/gs_database.h"
#include "database/db_aftermarket_pack.h"
#include "config/gs_config.h"
#include "input/ed_dev.h"
#include "input/input_device.h"
#include "input/gs_device.h"
#include "game/vehicle/bank_vehicle.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/gs_car_display.h"
#include "game/vehicle/vehicle_base.h"
#include "game/vehicle/vehicle_car.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/car_desc.h"
#include "game/game_mechanism_player.h"

#include "render/mng_env_map.h"
#include "render/gs_render.h"
#include "render/postfx/gs_postfx.h"
#include "render/postfx/effects/postfx_glow_pause.h"
#include "render/postfx/effects/postfx_luminance_filter.h"
#include "render/postfx/effects/postfx_blur.h"
#include "render/postfx/effects/postfx_modulate.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/local_player.h"
#include "game/mng_ingame_menu.h"
#include "audio/gs_audio.h"
#include "core/time.h"

float gFXPFadeInDelay = 0.5f;   // DAT_00f9c0f4
float gFXPFadeOutDelay = 0.5f;  // DAT_00f9c0f8
float gFXPLumThreshold = 0.99f; // DAT_00f9c0fc
float gFXPBlurSize = 1.0f;      // DAT_00f9c100
float gFXPModCoeff0 = 1.0f;     // DAT_00f9c104
float gFXPModCoeff1 = 0.125f;   // DAT_00f9c108

static constexpr int32_t kFXPFadeInBlurPassCount = 3; // DAT_00f9c10c
// DAT_00f416e0
static constexpr TimeOfDay kPauseExitWeatherTimer = { 0.0f, 30.0f, 0.0f };

GameModeWorld *gpActiveGM = nullptr; // DAT_010e7c6c

// Height above the sampled terrain surface at which the freeride spawn seats
// the car body origin (approximates the body-centre to wheel-contact offset).
static constexpr float kSpawnGroundClearance = 0.5f;

static constexpr float kCarHotReloadCooldown = 2.5f; // DAT_00f416c0
static constexpr int32_t kCarHotReloadDrawDelay = 0x3d;

// DAT_016a9780
static Matrix4x4f_t gCarHotReloadPose = Matrix4x4f_t::Identity();

static int32_t gPlayerCarConfigID = -1;           // DAT_00f91c6c
static float gCarHotReloadRequestTime = 0.0f;     // DAT_016a6a3c
static int32_t gCarHotReloadDelay = 0;            // DAT_016a6a44
static bool gbCarHotReloadPending = false;        // DAT_016a6a41
static bool gbCarHotReloadRestoreDraw = false;    // DAT_016a6a40
static bool gbCarHotReloadCooldownElapsed = true; // DAT_00fab8dc

static constexpr float kIgnitionAccelThreshold = 0.1f;
static constexpr float kManualClutchExponent = 500.0f; // DAT_00fab728
static constexpr float kManualClutchEngaged = 0.5f;    // DAT_00fab724

static void PlaceLocalPlayerDriver( CarState *pCarState, const bool bAsync ) {
    VehicleBaseResources *pResource = pCarState->getResource();
    if ( pResource == nullptr || !pResource->bDriveResource ) {
        return;
    }

    int32_t clotheSet = 0;
    if ( gpDatabase != nullptr ) {
        DBEntryCarPhysicsData *pCarData =
            gpDatabase->getCarDBEntry( pResource->CarID );
        if ( pCarData != nullptr && gpDatabase->isBike( pCarData ) ) {
            clotheSet = 1;
        }
    }

    AvatarManager &avatarManager = gpScenaric->getAvatarManager();
    EntityAvatar *pAvatar = avatarManager.createAvatar(
        nullptr, clotheSet, bAsync, ~0ull, false, 1 );
    if ( pAvatar == nullptr ) {
        return;
    }

    if ( gpScenaric->placeDriver( pCarState, pAvatar->getDriver() ) ) {
        pAvatar->addInstanceReference();
        pAvatar->setEnvMap( true, nullptr );
    } else {
        avatarManager.releaseAvatar( pAvatar );
    }
}

static void ReleaseLocalPlayerDriver( CarState *pCarState ) {
    EntityDriver *pDriver = pCarState->getDriver();
    if ( pDriver == nullptr ) {
        return;
    }

    pDriver->removeInstanceReference();
    pDriver->setEnvMap( false, nullptr );
    pCarState->unloadDriver();
    gpScenaric->getAvatarManager().releaseAvatar( pDriver );
}

GameModeWorldBase::GameModeWorldBase()
    : GameModeScene(),
      mngWorld(),
      mngCloudsPlane(),
      mngBackground(),
      mngOcean(),
      mngCarTireSkid(),
      mngSunFlare(),
      mngEnvMap(),
      mngImpostor(),
      mngImpostorSimple(),
      mngTallGrass(),
      mngElectricalNetwork(),
      mngCarAlpha(),
      mngSectorName() {
    // FUN_009d8dd0
    registerManager( &mngWorld );
    registerManager( &mngCloudsPlane );
    registerManager( &mngBackground );
    registerManager( &mngOcean );
    registerManager( &mngCarTireSkid );
    registerManager( &mngSunFlare );
    registerManager( &mngEnvMap );
    registerManager( &mngImpostor );
    registerManager( &mngImpostorSimple );
    registerManager( &mngTallGrass );
    registerManager( &mngElectricalNetwork );
    registerManager( &mngCarAlpha );
    registerManager( &mngSectorName );

    bWithWorld = true;
}

GameModeWorldBase::~GameModeWorldBase() {
    // FUN_0042b520
}

void GameModeWorldBase::initialize() {
    // FUN_009d9130
    GameModeScene::initialize();

    OTDU_UNIMPLEMENTED; // TODO: AI

    cinematicTimer = 0.0f; // field48_0xfdc8
}

void GameModeWorldBase::destroy() {
    // FUN_009d90f0
    if ( gpAvatar != nullptr ) {
        gpAvatar->setEnvMap( false, nullptr );
    }

    GameModeScene::destroy();
}

GameModeWorld::GameModeWorld( uint32_t gameModeFlags )
    : GameModeWorldBase(),
      mngHudChallenge(),
      pGameMech( nullptr ),
      gameMechType( -1 ),
      gameMechID( 0xc0c3 ),
      flags( gameModeFlags ),
      bPaused( false ),
      bPauseMenuActive( false ),
      bPauseMenuLocked( false ),
      bCarPhysicsFlag( false ),
      carColorIndex( 0 ),
      carRimIndex( 0 ),
      carTireIndex( 0 ),
      lastOdometerKms( 0 ),
      lastDistance( 0.0f ),
      initializeTime( 0.0f ),
      pauseFadeTimer( 0.0f ) {
    // FUN_007be500
    registerManager( &mngHudChallenge );

    gpActiveGM = this;
}

GameModeWorld::~GameModeWorld() {
    // FUN_007be5d0
    gpActiveGM = nullptr;

    destroyGameMech();

    gpMngHud = nullptr;
}

void GameModeWorld::initialize() {
    // FUN_007bf6e0
    GameModeWorldBase::initialize();

    createGameMech( gameMechID, gameMechType );

    if ( pGameMech != nullptr && ( flags & 1 ) == 0 ) {
        mngActor.setGameMech( pGameMech );
        mngHudChallenge.setGameMech( pGameMech );
    }

    VehicleBase::bApplyWheelAttenuation = true;

    initializeTime = ( gpTimer != nullptr ) ? gpTimer->SystemTotalTime : 0.0f;
}

void GameModeWorld::reset() {
    // FUN_007bf280
    GameModeScene::reset();

    if ( gpFlash != nullptr && gpFlash->getMouse() != nullptr ) {
        gpFlash->getMouse()->field12_0x30 = true;
    }
    bPaused = false;
    bPauseMenuLocked = false;
    lastDistance = 0.0f;

    if ( gpCamera != nullptr && gpPlayerData != nullptr ) {
        gpCamera->setSeatPosition( gpPlayerData->getSeatPositionHeight(),
                                   gpPlayerData->getSeatPositionPosition() );
    }

    if ( ( flags & 4 ) != 0 && gpScenaric != nullptr &&
         gpScenaric->getLocalPlayer().getCarState() == nullptr ) {
        installLocalPlayerCar();
    } else if ( gpScenaric != nullptr &&
                gpScenaric->getLocalPlayer().getCarState() != nullptr ) {
        CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();

        if ( pCarState->getPhysicsVehicle() != nullptr &&
             gpTestDriveInstance->getActiveGameMode() != GM_Mission ) {
            PhysicsLock lock;
            OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS (postponed pause = 2)
        }

        VehicleBase *pVehicleBase = pCarState->getVehicleInstance();
        if ( pVehicleBase != nullptr &&
             ( pVehicleBase->getType() == VehicleBase::eType::VBT_Car ||
               pVehicleBase->getType() == VehicleBase::eType::VBT_Bike ) ) {
            GSCarDisplay::SetRealTimeEnvMappedCar( pVehicleBase );
        }
    }

    if ( gpScenaric != nullptr ) {
        CarState *pExistingCar = gpScenaric->getLocalPlayer().getCarState();
        if ( pExistingCar != nullptr ) {
            pPlayerPhysicsVehicle = pExistingCar->getPhysicsVehicle();
        }
    }

    if ( gpCamera != nullptr && gpScenaric != nullptr ) {
        CarState *pCar = gpScenaric->getLocalPlayer().getCarState();
        if ( pCar != nullptr ) {
            gpCamera->restoreCamera();
        }
    }

    CarState *pLocalCar = ( gpScenaric != nullptr )
                              ? gpScenaric->getLocalPlayer().getCarState()
                              : nullptr;
    if ( pLocalCar != nullptr ) {
        lastDistance = pLocalCar->getBody()->Distance;
        PlaceLocalPlayerDriver( pLocalCar, false );
    }

    const bool bSpawnFromGarage = ( flags & 4 ) != 0;
    if ( bSpawnFromGarage && gpPlayerData != nullptr ) {
        CarDesc *pCurrentCar = gpPlayerData->getCurrentCar();
        if ( pCurrentCar != nullptr ) {
            gPlayerCarConfigID = int32_t( pCurrentCar->getCarConfigID() );
            carColorIndex = pCurrentCar->paintIndex;
            if ( carColorIndex == -1 &&
                 gpDatabase->getNumColors( gPlayerCarConfigID ) > 0 ) {
                carColorIndex = 0;
            }
            carRimIndex = pCurrentCar->rimID;
            carTireIndex = pCurrentCar->interiorID;

            if ( pLocalCar != nullptr &&
                 pLocalCar->getVehicleInstance() != nullptr ) {
                pLocalCar->getVehicleInstance()->applyGaragePaint(
                    *pCurrentCar );
            }
        }
    }

    if ( gpMngHud != nullptr ) {
        gpMngHud->FUN_006e6a00( pLocalCar );
    }

    if ( bSpawnFromGarage ) {
        gpPlayerData->applyDrivingOptions();

        if ( pLocalCar != nullptr ) {
            pLocalCar->openConvertibleWindows();
        }
    }

    if ( pLocalCar != nullptr ) {
        lastOdometerKms = pLocalCar->getBody()->OdometerKms;
    }
}

constexpr int32_t kDefaultPlayerCarID = 1030;
int32_t gRequestedPlayerCarID = kDefaultPlayerCarID;
int32_t gRequestedPlayerCarColorID = -1;
int32_t gRequestedPlayerCarRimID = -1;
int32_t gRequestedPlayerCarInteriorID = 0;
bool gRequestedCarChange = false;

// Debug teleport request
bool gRequestedPlayerTeleport = false;
Eigen::Vector3f gRequestedPlayerTeleportPos = Eigen::Vector3f::Zero();

void GameModeWorld::installLocalPlayerCar() {
    installLocalPlayerCar( kDefaultPlayerCarID, nullptr );
}

void GameModeWorld::installLocalPlayerCar( int32_t carID,
                                           const Matrix4x4f_t *pSpawnOverride,
                                           int32_t rimID,
                                           int32_t paintIndex,
                                           int32_t interiorIndex ) {
    // FUN_007bf280 (inlined) / FUN_0079da80.
    if ( gpCarState == nullptr || gpCarDisplay == nullptr ||
         gpScenaric == nullptr ) {
        return;
    }

    VehicleBaseResources *pRes = gpCarState->grabFreeResourceEntry();
    if ( pRes == nullptr ) {
        return;
    }

    uint64_t packHashcode = kNoPackHashcode;
    if ( gpPlayerData != nullptr ) {
        const CarDesc *pCurrentCar = gpPlayerData->getCurrentCar();
        if ( pCurrentCar != nullptr &&
             int32_t( pCurrentCar->getCarConfigID() ) == carID ) {
            packHashcode = pCurrentCar->packHashcode;
        }
    }

    if ( !pRes->load( carID, rimID, 0, 4, packHashcode, 0,
                      eVehicleResourceType::VRT_Exterior ) ) {
        OTDU_LOG_ERROR(
            "Failed to load local-player vehicle resources (car %d)\n", carID );
        gpCarState->releaseResourceEntry( pRes );
        return;
    }

    CarState &state = gpCarState->grabNextFreeCarState();
    state.reset();
    state.owner = CarState::eOwner::CSO_LocalPlayer;
    state.simulationType = CarState::eSimulationType::CSST_Local;
    state.activeMode = CarState::eActiveMode::CSAM_Freeride;
    state.bUseShowcaseControl = false;
    state.carID = pRes->CarID;
    state.tireID = pRes->TireID;
    state.rimID = pRes->RimID;
    state.body.BackOnTrackState = 0;
    state.body.resetBackOnTrack();

    VehicleCar *pController =
        gpCarDisplay->instantiateCar( &state, &pRes->Exterior, &pRes->Interior,
                                      &pRes->FrontRims, &pRes->RearRims );
    if ( pController == nullptr ) {
        OTDU_LOG_ERROR( "instantiateCar failed for local-player car %d\n",
                        carID );
        gpCarState->releaseResourceEntry( pRes );
        return;
    }

    state.pVehicleBase = pController;
    state.pResource = pRes;
    pRes->RefCount++;

    gpCarState->registerCallback( &state, pController, nullptr );

    if ( paintIndex >= 0 ) {
        pController->applyPaint( (uint32_t)carID, ePaintType::PT_Default,
                                 paintIndex, rimID, interiorIndex, nullptr,
                                 nullptr, nullptr, nullptr );
    }

    if ( gpMngEnvMap != nullptr ) {
        GSCarDisplay::SetRealTimeEnvMappedCar( pController );
    }

    Matrix4x4f_t spawn;
    if ( pSpawnOverride != nullptr ) {
        // In-place respawn (debug switch): reuse the transform and leave the
        // streaming centre where it is.
        spawn = *pSpawnOverride;
    } else {
        if ( gpWorld != nullptr ) {
            const Matrix4x4f_t &prev = gpWorld->getPreviousTransform();
            const float distSq = prev( 3, 0 ) * prev( 3, 0 ) +
                                 prev( 3, 1 ) * prev( 3, 1 ) +
                                 prev( 3, 2 ) * prev( 3, 2 );
            if ( distSq < 1.0f ) {
                Eigen::Vector4f startPos =
                    gpScenaric->getDefaultStartPosition();
                gpWorld->setPlayerPosition( &startPos );
            }
        }
        spawn = ( gpWorld != nullptr ) ? gpWorld->getPreviousTransform()
                                       : Matrix4x4f_t::Identity();
        // HACK: Shift from default spawn pos to prevent spawn inside the hmap
        // (seems ok in Havok; not with Jolt).
        spawn( 3, 0 ) -= 15.0f;
        spawn( 3, 2 ) -= 15.0f;

        if ( gpWorld != nullptr ) {
            Eigen::Vector4f center( spawn( 3, 0 ), 0.0f, spawn( 3, 2 ), 0.0f );
            gpWorld->setStreamingPosition( &center );
        }
    }
    state.body.WorldMatrix = spawn;

    Matrix4x4f_t offseted = spawn;
    if ( gpWorldStreaming != nullptr ) {
        const Eigen::Vector4f &shift = gpWorldStreaming->getPosition();
        offseted( 3, 0 ) -= shift.x();
        offseted( 3, 1 ) -= shift.y();
        offseted( 3, 2 ) -= shift.z();
    }
    state.body.OffsetedWorldMatrix = offseted;

    if ( gpPhysics != nullptr && gpDatabase != nullptr ) {
        PhysFactory *pFactory =
            ( pRes->Exterior.pBankInstance != nullptr )
                ? pRes->Exterior.pBankInstance->getPhysFactory()
                : nullptr;
        const void *pTorqueBin =
            ( pRes->Exterior.pBankInstance != nullptr )
                ? pRes->Exterior.pBankInstance->getTorqueBinary()
                : nullptr;
        DBEntryCarPhysicsData *pDB = gpDatabase->getCarDBEntry( state.carID );
        const DBEntryAftermarketPack *pPack =
            gpDatabase->getAftermarketPack( pRes->PackHashcode );
        if ( pDB != nullptr ) {
            gpPhysics->destroyVehicle( state.pPhysicsVehicle );
            pPlayerPhysicsVehicle = gpPhysics->createVehicle(
                pFactory, spawn, *pDB, pTorqueBin, pPack );
            state.pPhysicsVehicle = pPlayerPhysicsVehicle;
        }
    }

    resetAutomaticGearbox();

    // Link the local player; the camera binds to slot 0.
    gpScenaric->getLocalPlayer().setCarState( &state );

    const Eigen::Vector4f pos = spawn.row( 3 );
    OTDU_LOG_INFO( "Installed local-player car %d at (%.1f, %.1f, %.1f)\n",
                   carID, pos.x(), pos.y(), pos.z() );
}

void GameModeWorld::uninstallLocalPlayerCar() {
    // Mirror of GMCarShowcase::unloadCar (FUN_007b5880)
    if ( gpCarState == nullptr || gpScenaric == nullptr ) {
        return;
    }
    CarState *pCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pCar == nullptr ) {
        return;
    }
    VehicleBaseResources *pRes = pCar->getResource();
    gpCarState->releaseCarState( pCar, false );
    if ( pRes != nullptr ) {
        pRes->unload(); // frees the streamed banks once RefCount < 1
        gpCarState->releaseResourceEntry( pRes );
    }
    pPlayerPhysicsVehicle = nullptr; // was destroyed by releaseCarState
    gpScenaric->getLocalPlayer().setCarState( nullptr );
    resetAutomaticGearbox();
}

void GameModeWorld::teleportLocalPlayerCar( const Eigen::Vector3f &worldPos ) {
    if ( gpScenaric == nullptr ) {
        return;
    }
    CarState *pCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pCar == nullptr ) {
        return;
    }

    // Keep the car's current orientation; only relocate its origin.
    Matrix4x4f_t pose = pCar->getBody()->WorldMatrix;
    if ( pPlayerPhysicsVehicle != nullptr &&
         pPlayerPhysicsVehicle->isActive() ) {
        PhysicsLock physLock;
        pose = pPlayerPhysicsVehicle->getChassisWorldMatrix();
    }
    pose( 3, 0 ) = worldPos.x();
    pose( 3, 1 ) = worldPos.y();
    pose( 3, 2 ) = worldPos.z();

    if ( gpWorld != nullptr ) {
        Eigen::Vector4f center( worldPos.x(), 0.0f, worldPos.z(), 0.0f );
        gpWorld->setStreamingPosition( &center );
    }

    if ( pPlayerPhysicsVehicle != nullptr &&
         pPlayerPhysicsVehicle->isActive() && gpPhysics != nullptr ) {
        PhysicsLock lock;
        pPlayerPhysicsVehicle->resetToTransform( pose );
    }

    CarBodyState *pBody = pCar->getBody();
    pBody->WorldMatrix = pose;
    Matrix4x4f_t offseted = pose;
    if ( gpWorldStreaming != nullptr ) {
        const Eigen::Vector4f &shift = gpWorldStreaming->getPosition();
        offseted( 3, 0 ) -= shift.x();
        offseted( 3, 1 ) -= shift.y();
        offseted( 3, 2 ) -= shift.z();
    }
    pBody->OffsetedWorldMatrix = offseted;

    bCarGrounded = false;
    resetAutomaticGearbox();

    OTDU_LOG_INFO( "Debug: teleported local-player car to (%.1f, %.1f, %.1f)\n",
                   worldPos.x(), worldPos.y(), worldPos.z() );
}

void GameModeWorld::updateLocalPlayerDrivingInput( float deltaTime ) {
    // FUN_00849380
    if ( pPlayerPhysicsVehicle == nullptr ||
         !pPlayerPhysicsVehicle->isActive() ) {
        return;
    }
    if ( pPlayerPhysicsVehicle->isInCinematicMode() ) {
        return;
    }
    CarState *pCar = ( gpScenaric != nullptr )
                         ? gpScenaric->getLocalPlayer().getCarState()
                         : nullptr;
    if ( pCar == nullptr ) {
        return;
    }

    CarBodyState *pBody = pCar->getBody();
    PlayerConfiguration *pCfg =
        ( gpConfig != nullptr && gpConfig->pPlayerConfig != nullptr &&
          gpConfig->NbMaxPlayers > 0 )
            ? &gpConfig->pPlayerConfig[0]
            : nullptr;

    if ( pCfg == nullptr || deltaTime <= 0.0f ) {
        // No device configuration (or a 0 dt)
        pBody->Throttle = 0.0f;
        pBody->Brake = 0.0f;
        pBody->SteeringRaw = 0.0f;
        pBody->Steering = 0.0f;
        PhysicsLock physLock;
        pPlayerPhysicsVehicle->setInput( 0.0f, 0.0f, 0.0f );
        return;
    }

    const bool bIsBike = pBody->bIsBike;

    DeviceAction *pAccelAction = bIsBike ? pCfg->pBikeAccel : pCfg->pCarAccel;
    DeviceAction *pBrakeAction = bIsBike ? pCfg->pBikeBrake : pCfg->pCarBrake;
    DeviceAction *pLeftAction = bIsBike ? pCfg->pBikeLeft : pCfg->pCarLeft;
    DeviceAction *pRightAction = bIsBike ? pCfg->pBikeRight : pCfg->pCarRight;

    const float rawAccel = ReadActionAxis( pAccelAction );
    const float rawBrake = ReadActionAxis( pBrakeAction );

    const float throttle = ApplyInputDeadZone( rawAccel, gAccelDeadZone );
    const float brake = ApplyInputDeadZone( rawBrake, gBrakeDeadZone );

    const float steer =
        ApplyInputDeadZone( ReadActionAxis( pRightAction ),
                            gSteeringDeadZone ) -
        ApplyInputDeadZone( ReadActionAxis( pLeftAction ), gSteeringDeadZone );

    const PhysicsVehicle::DrivingGearbox gearbox =
        pPlayerPhysicsVehicle->getDrivingGearbox();
    DeviceAction *pIgnitionAction = pCfg->pCarIgnition;
    const bool bIgnitionBound =
        pIgnitionAction != nullptr && pIgnitionAction->NumAddedChannels != 0;
    const bool bIgnitionHeld = bIgnitionBound
                                   ? IsActionDown( pIgnitionAction )
                                   : ( rawAccel > kIgnitionAccelThreshold );
    const bool bIgnitionEdge = bIgnitionBound
                                   ? IsActionPressed( pIgnitionAction )
                                   : ( rawAccel > kIgnitionAccelThreshold );

    pBody->bIgnition = !gearbox.bEngineRunning && bIgnitionHeld;
    pBody->bIgnitionOnce =
        bIgnitionBound && gearbox.bEngineRunning && bIgnitionEdge;

    DeviceAction *pGearSelectActions[] = {
        pCfg->pCarGearReverse, // -> 0 (reverse)
        pCfg->pCarGear1,       // -> 2
        pCfg->pCarGear2,       // -> 3
        pCfg->pCarGear3,       // -> 4
        pCfg->pCarGear4,       // -> 5
        pCfg->pCarGear5,       // -> 6
        pCfg->pCarGear6,       // -> 7
        pCfg->pCarGear7,       // -> 8
    };
    static constexpr int32_t kGearSelectValues[] = { 0, 2, 3, 4, 5, 6, 7, 8 };
    int32_t gearSelect = 1;
    bool bHasGearSelector = false;
    for ( size_t i = 0; i < std::size( pGearSelectActions ); ++i ) {
        DeviceAction *pAction = pGearSelectActions[i];
        if ( pAction == nullptr || pAction->NumAddedChannels == 0 ) {
            continue;
        }
        bHasGearSelector = true;
        if ( ( pAction->Flags & 1 ) != 0 ) {
            gearSelect = kGearSelectValues[i];
        }
    }
    pBody->GearRequest = gearSelect;

    DeviceAction *pClutchAction = pCfg->pCarClutch;
    if ( pClutchAction == nullptr || pClutchAction->NumAddedChannels == 0 ) {
        pBody->Clutch = 0.0f;
    } else {
        pBody->Clutch =
            ReadActionAxis( bIsBike ? pCfg->pBikeClutch : pClutchAction );
    }

    if ( pCfg->pBikeLeanForward == nullptr ||
         pCfg->pBikeLeanBackward == nullptr ) {
        pBody->Lean = 0.0f;
    } else {
        pBody->Lean = ReadActionAxis( pCfg->pBikeLeanForward ) -
                      ReadActionAxis( pCfg->pBikeLeanBackward );
    }

    pBody->Handbrake = ReadActionAxis( pCfg->pCarHandbrake );

    DeviceAction *pGearUpAction =
        bIsBike ? pCfg->pBikeGearUp : pCfg->pCarGearUp;
    DeviceAction *pGearDownAction =
        bIsBike ? pCfg->pBikeGearDown : pCfg->pCarGearDown;
    pBody->bGearUpEdge = ( gearbox.Gear < gearbox.NumGears - 1 ) &&
                         IsActionPressed( pGearUpAction );
    pBody->bGearDownEdge =
        ( gearbox.Gear > 0 ) && IsActionPressed( pGearDownAction );

    pCfg->bCarReset = false;
    pBody->bBackOnTrackRequest = IsActionPressed( pCfg->pBotPad );
    pBody->bHeadlightsOn = IsActionDown( pCfg->pCarLight );
    pBody->bHonking = IsActionDown( pCfg->pCarHorn );

    const bool bChangeViewHeld = IsActionDown( pCfg->pCarCamera );
    if ( gCamViewLockHoldTime < 0.0f ) { // Camera lock (freeride)
        gCamViewLockHoldTime = bChangeViewHeld ? -1.0f : 0.0f;
    } else if ( bChangeViewHeld ) {
        gCamViewLockHoldTime += deltaTime;
    } else {
        gCamViewLockHoldTime = 0.0f;
    }

    if ( gpMngInGameMenu != nullptr ) {
        pBody->WindowState = gpMngInGameMenu->getWindowsDirection();
    }

    pBody->Throttle = throttle;
    pBody->Brake = brake;
    pBody->SteeringRaw = steer;
    pBody->Steering = steer;

    const bool bAutomatic = pCfg->bUseAutomatic;
    pPlayerPhysicsVehicle->setAutomaticGearbox( bAutomatic );

    {
        PhysicsLock physLock;
        pPlayerPhysicsVehicle->setDrivingInputScripted(
            isDrivingInputScripted() );
        pPlayerPhysicsVehicle->setInput( throttle, brake, steer );
        pPlayerPhysicsVehicle->setHandbrake( pBody->Handbrake );
        pPlayerPhysicsVehicle->setIgnition( pBody->bIgnition,
                                            pBody->bIgnitionOnce );

        if ( !bAutomatic ) {
            const float clutchEngagement =
                std::pow( 1.0f - pBody->Clutch, kManualClutchExponent );
            pPlayerPhysicsVehicle->setClutchOverride( clutchEngagement );

            const bool bClutchOut = clutchEngagement >= kManualClutchEngaged;
            if ( pBody->bGearUpEdge && !bClutchOut ) {
                pPlayerPhysicsVehicle->requestUpshift();
            }
            if ( pBody->bGearDownEdge && !bClutchOut ) {
                pPlayerPhysicsVehicle->requestDownshift();
            }
            if ( bHasGearSelector ) {
                if ( bGearSelectBlocked ) {
                    if ( pBody->GearRequest == 1 ) {
                        bGearSelectBlocked = false;
                    }
                } else if ( pBody->GearRequest != gearbox.Gear ) {
                    if ( bClutchOut && pBody->GearRequest != 1 ) {
                        bGearSelectBlocked = true;
                    } else {
                        pPlayerPhysicsVehicle->setCurrentGear(
                            pBody->GearRequest );
                    }
                }
            }
        }
    }

    pBody->bGearUpEdge = false;
    pBody->bGearDownEdge = false;

    if ( pBody->bBackOnTrackRequest ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: back on track (BOT_PAD)
    }
}

void GameModeWorld::resetAutomaticGearbox() {
    if ( pPlayerPhysicsVehicle != nullptr ) {
        pPlayerPhysicsVehicle->resetAutomaticGearbox();
    }
}

void GameModeWorld::tick( float deltaTime, float totalTime ) {
    // FUN_007beb90
    GameModeScene::tick( deltaTime, totalTime );

#ifdef OTDU_DEVBUILD
    // Process teleport request
    if ( gRequestedPlayerTeleport ) {
        gRequestedPlayerTeleport = false;
        teleportLocalPlayerCar( gRequestedPlayerTeleportPos );
    }
#endif

    // Drive the player car body from the physics simulation.
    CarState *pCar = ( gpScenaric != nullptr )
                         ? gpScenaric->getLocalPlayer().getCarState()
                         : nullptr;
    if ( pPlayerPhysicsVehicle != nullptr &&
         pPlayerPhysicsVehicle->isActive() && pCar != nullptr ) {
        updateLocalPlayerDrivingInput( deltaTime );
    } else if ( !bCarGrounded && pCar != nullptr && gpPhysics != nullptr ) {
        // Fallback (no physics vehicle): one-shot cast-to-ground so the static
        // car settles onto the streamed terrain instead of clipping.
        // getGroundHeight takes absolute coordinates and rebases internally.
        CarBodyState *pBody = pCar->getBody();
        const float groundY = gpPhysics->getGroundHeight(
            pBody->WorldMatrix( 3, 0 ), pBody->WorldMatrix( 3, 2 ) );
        if ( groundY > -1.0e30f ) {
            const float targetY = groundY + kSpawnGroundClearance;
            pBody->WorldMatrix( 3, 1 ) = targetY;
            pBody->OffsetedWorldMatrix( 3, 1 ) = targetY;
            bCarGrounded = true;
        }
    }

    if ( pGameMech != nullptr ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: GameLogic
    }

    updatePauseMenuTrigger();

    updatePauseFade();

    updateCarHotReload();
}

void GameModeWorld::updateCarHotReload() {
    // FUN_007beb90 (inlined)
    if ( gCarHotReloadDelay != 0 ) {
        gCarHotReloadDelay--;
    }

    if ( gbCarHotReloadPending && gCarHotReloadDelay == 0 ) {
        gbCarHotReloadPending = false;
        loadHotReloadedCar();
        gbCarHotReloadRestoreDraw = true;
        gCarHotReloadDelay = kCarHotReloadDrawDelay;
    } else if ( gbCarHotReloadRestoreDraw && gCarHotReloadDelay == 0 ) {
        bDraw = true;
        gpWorld->setAreaStreamingEnabled( true );
        gCarHotReloadDelay = 1;
        gbCarHotReloadRestoreDraw = false;
    }

    if ( kCarHotReloadCooldown < GetTimeSeconds() - gCarHotReloadRequestTime ) {
        gbCarHotReloadCooldownElapsed = true;
    }

    if ( gCarHotReloadDelay != 0 || !gbCarHotReloadCooldownElapsed ||
         !gRequestedCarChange ) {
        return;
    }
    gRequestedCarChange = false;

    DBEntryCarPhysicsData *pNewCarData =
        gpDatabase->getCarDBEntry( gRequestedPlayerCarID );
    if ( pNewCarData == nullptr ) {
        return;
    }
    if ( gpDatabase->isBike( pNewCarData ) ) {
        OTDU_UNIMPLEMENTED; // TODO: PHYSICS (bike dynamics)
        return;
    }

    OTDU_UNIMPLEMENTED; // TODO: ONLINE (restart the cooldown when the P2P
                        // session has players)

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar != nullptr && pLocalCar->getPhysicsVehicle() != nullptr &&
         pLocalCar->getPhysicsVehicle()->isActive() ) {
        PhysicsLock lock;
        gCarHotReloadPose =
            pLocalCar->getPhysicsVehicle()->getChassisWorldMatrix();
    }

    const int32_t newCarID = gRequestedPlayerCarID;
    const int32_t currentCarID =
        ( pLocalCar != nullptr ) ? pLocalCar->getCarID() : -1;
    if ( newCarID != currentCarID ) {
        if ( pLocalCar != nullptr ) {
            PhysicsVehicle *pPhysicsVehicle = pLocalCar->getPhysicsVehicle();
            if ( pPhysicsVehicle != nullptr ) {
                PhysicsLock lock;
                bCarPhysicsFlag = pPhysicsVehicle->isInCinematicMode();
            }

            ReleaseLocalPlayerDriver( pLocalCar );

            gpCarState->unloadVehicle( pLocalCar, true, false );
            gpScenaric->getLocalPlayer().setCarState( nullptr );
            pPlayerPhysicsVehicle = nullptr;
        }

        gPlayerCarConfigID = newCarID;

        OTDU_UNIMPLEMENTED; // TODO: ONLINE (broadcast car/paint/rim/interior)

        gbCarHotReloadPending = true;
        gCarHotReloadDelay = 1;
    }

    bDraw = false;
    gpWorld->setAreaStreamingEnabled( false );
}

void GameModeWorld::loadHotReloadedCar() {
    // FUN_007beb90 (inlined)
    const int32_t carID = gPlayerCarConfigID;
    installLocalPlayerCar( carID, &gCarHotReloadPose );

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar != nullptr ) {
        if ( pLocalCar->getPhysicsVehicle() != nullptr ) {
            PhysicsLock lock;
            pLocalCar->getPhysicsVehicle()->setCinematicMode( bCarPhysicsFlag );
        }

        PlaceLocalPlayerDriver( pLocalCar, true );
    }

    mngHudChallenge.FUN_006e6a00( pLocalCar );

    gpPlayerData->applyDrivingOptions();

    carColorIndex = 0;
    if ( gpDatabase->getNumColors( carID ) > 0 ) {
        carTireIndex = 0;
        VehicleBase *pVehicle = ( pLocalCar != nullptr )
                                    ? pLocalCar->getVehicleInstance()
                                    : nullptr;
        if ( pVehicle != nullptr ) {
            pVehicle->applyPaint( uint32_t( carID ), ePaintType::PT_Default, 0,
                                  carRimIndex, 0, nullptr, nullptr, nullptr,
                                  nullptr );
        }
    }

    FlashPlayer *pHudPlayer = mngHudChallenge.getHudPlayer();
    if ( pHudPlayer != nullptr ) {
        pHudPlayer->setVariableValue( "/:PaintName", "" );
    }

    if ( pLocalCar != nullptr ) {
        pLocalCar->openConvertibleWindows();
    }

    if ( gpPlayerData != nullptr ) {
        CarDesc *pCurrentCar = gpPlayerData->getCurrentCar();
        if ( pCurrentCar != nullptr ) {
            pCurrentCar->setCarConfigID( uint32_t( carID ) );
        }
    }
}

void GameModeWorld::draw( float /*deltaTime*/, float /*totalTime*/ ) {
    // FUN_007bdc10
    if ( pGameMech != nullptr ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: GameLogic
    }
}

void GameModeWorld::pause() {
    // FUN_007be820
    GameModeScene::pause();

    CarState *pLocalCar = ( gpScenaric != nullptr )
                              ? gpScenaric->getLocalPlayer().getCarState()
                              : nullptr;
    if ( pLocalCar != nullptr ) {
        ReleaseLocalPlayerDriver( pLocalCar );

        if ( !gpTestDriveInstance->hasRequestedExit() &&
             gpTestDriveInstance->getActiveGameMode() != GM_OutroDemo ) {
            if ( pLocalCar->getPhysicsVehicle() != nullptr ) {
                PhysicsLock lock;
                OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS (postponed pause = 1)
            }
        } else {
            if ( pLocalCar->getPhysicsVehicle() != nullptr ) {
                OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS
            }
            uninstallLocalPlayerCar();
        }

        GSCarDisplay::SetRealTimeEnvMappedCar( nullptr );
    }

    if ( bPaused ) {
        bPaused = false;
        onMenuPauseExit( true );
    }

    if ( gpPostFX != nullptr ) {
        PostFXStepGlowPause *pGlow =
            (PostFXStepGlowPause *)
                gpPostFX->getEffect<ePostFXStep::PFXS_GlowPause>();
        if ( pGlow != nullptr && pGlow->pModulate != nullptr &&
             pGlow->pModulate->isEnabled() && !gpPostFX->isFXLocked() ) {
            pGlow->pModulate->setEnabled( false );
        }
    }

    OTDU_UNIMPLEMENTED; // TODO: AI
}

void GameModeWorld::destroy() {
    // FUN_007be670
    if ( gpActiveGM != nullptr &&
         gpActiveGM->getMissionDescription() != nullptr ) {
        OTDU_UNIMPLEMENTED; // TODO: SCENARIC
    }

    GameModeWorldBase::destroy();

    destroyGameMech();

    OTDU_UNIMPLEMENTED; // TODO: ONLINE
}

void GameModeWorld::onResourceStreamed() {
    // FUN_007be140
    GameModeScene::onResourceStreamed();

    MoviePlayer *pHud = gpFlash->getMoviePlayer( 1, 0 );
    if ( pHud != nullptr && pHud->MovieBank.isLoaded() &&
         pHud->MovieBank.getFlashPlayer() != nullptr ) {
        const bool bChallengeState =
            currentGameMode == GM_Mission && pGameMech != nullptr &&
            !pGameMech->getUnknown75C() && !gpScenaric->isFromDriveIn();
        pHud->MovieBank.getFlashPlayer()->setVariableValue(
            "/:state_Cha", bChallengeState ? 1 : 0 );
    }

    mngHudChallenge.initializeFlash();
}

bool GameModeWorld::onMessage( FlashMessage *pMessage ) {
    // FUN_007be470
    switch ( *pMessage ) {
    case FlashMessage( "paou" ): // pause out
        bPaused = false;
        bPauseMenuActive = false;
        onMenuPauseExit( true );
        return true;
    case FlashMessage( "afok" ): // pause fade animation over
        onMenuPauseAnimOver();
        return true;
    case FlashMessage( "pain" ): // pause in
        onMenuPauseEnter();
        return true;
    case FlashMessage( "skip" ): // skip the running spot cinematic
        if ( gpMngCinematics != nullptr &&
             gpMngCinematics->getActiveCinematic() != nullptr ) {
            gpMngCinematics->getActiveCinematic()->stop( true );
        }
        return true;
    default:
        return false;
    }
}

EventParameters *GameModeWorld::getMissionDescription() {
    // FUN_0042b4d0
    OTDU_UNIMPLEMENTED; // TODO: SCENARIC
    return nullptr;
}

void GameModeWorld::onPauseMenuRequested( bool bOpened ) {
    // FUN_007be2e0
    if ( bOpened ) {
        if ( gpWeather != nullptr ) {
            gpWeather->setUpdateTimerEnabled( false );
        }

        if ( gpAudio != nullptr ) {
            gpAudio->changeMixState( gpAudio->getMixFlags( "MixMenu" ) );
        }

        if ( gpTimer != nullptr ) {
            if ( gpTimer->PreviousGameSpeed != gpTimer->GameSpeed &&
                 gpTimer->GameSpeed != 0.0f ) {
                gpTimer->PreviousGameSpeed = gpTimer->GameSpeed;
            }
            gpTimer->GameSpeed = 0.0f;
        }

        if ( pauseFadeTimer == 0.0f && gpPostFX != nullptr &&
             !gpPostFX->isFXLocked() ) {
            PostFXStepGlowPause *pGlow =
                (PostFXStepGlowPause *)
                    gpPostFX->getEffect<ePostFXStep::PFXS_GlowPause>();
            if ( pGlow != nullptr && pGlow->pModulate != nullptr ) {
                const bool bEnable =
                    ( gpRender == nullptr ) || !gpRender->isHDREnabled();
                pGlow->pModulate->setEnabled( bEnable );
            }
        }
        pauseFadeTimer = gFXPFadeInDelay - pauseFadeTimer;
    }

    pauseMenuActivated();
}

void GameModeWorld::pauseMenuActivated() {
    // FUN_007bdbc0
    if ( gpMngHud != nullptr ) {
        FlashPlayer *pHudPlayer = gpMngHud->getHudPlayer();
        if ( pHudPlayer != nullptr ) {
            pHudPlayer->showFrame( "gps", "/", true );
        }
    }

    if ( gpFlash != nullptr && gpFlash->getMouse() != nullptr ) {
        gpFlash->getMouse()->field12_0x30 = false;
    }
    bPauseMenuActive = true;
}

void GameModeWorld::onMenuPauseAnimOver() {
    // FUN_007be3b0
    if ( gpTestDriveInstance->getActiveGameMode() == GM_Mission ) {
        OTDU_UNIMPLEMENTED; // TODO: SCENARIC (no save for 0xc0c1/0xc0c2
                            // missions outside training levels)
    }

    gpPlayerData->requestSave( 1 );
}

void GameModeWorld::onMenuPauseExit( bool bRestoreState ) {
    // FUN_007be040
    if ( bRestoreState ) {
        if ( gpWeather != nullptr ) {
            gpWeather->setUpdateTimer( true, kPauseExitWeatherTimer );
        }

        if ( !hasRequestedExit() ) {
            if ( gpTimer != nullptr ) {
                gpTimer->GameSpeed = gpTimer->PreviousGameSpeed;
            }
            if ( gpAudio != nullptr ) {
                gpAudio->requestMixStateResync();
            }
        }

        pauseFadeTimer = gFXPFadeOutDelay - pauseFadeTimer;
    }

    if ( gpFlash != nullptr && gpFlash->getMouse() != nullptr ) {
        gpFlash->getMouse()->field12_0x30 = true;
    }
}

bool GameModeWorld::createGameMech( uint32_t mechID, int32_t mechType ) {
    // FUN_007bdc30
    destroyGameMech();

    gameMechID = mechID;
    gameMechType = mechType;

    if ( mechType == 5 ) {
        pGameMech = nullptr;
        return true;
    }

    if ( mechType == 4 ) {
        pGameMech = new PlayerGameMech( mechType, mechID );
    } else {
        OTDU_UNIMPLEMENTED; // TODO: GameLogic
        pGameMech = nullptr;
        return true;
    }

    pGameMech->setOwner( this );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: GameLogic
    return true;
}

void GameModeWorld::destroyGameMech() {
    // FUN_007bdc30 (inlined)
    if ( pGameMech != nullptr ) {
        delete pGameMech;
        pGameMech = nullptr;
    }

    mngActor.setGameMech( nullptr );
}

bool GameModeWorld::canEnterSpot() const {
    // FUN_00792d90
    bool bVehicleBusy = false;

    CarState *pLocalCar = ( gpScenaric != nullptr )
                              ? gpScenaric->getLocalPlayer().getCarState()
                              : nullptr;
    if ( pLocalCar != nullptr && pLocalCar->getPhysicsVehicle() != nullptr ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS
    }

    return pGameMech != nullptr && pGameMech->hasCurrentActivity() &&
           !bVehicleBusy;
}

void GameModeWorld::updatePauseMenuTrigger() {
    // FUN_007beb90
    PlayerConfiguration *pPlayerCfg =
        ( gpConfig != nullptr ) ? gpConfig->pPlayerConfig : nullptr;

    // Is any of the in-game (level 1) movies still fully active?
    const bool bMovieActive =
        ( gpFlash != nullptr ) && gpFlash->FUN_0099b1e0( 0, 1 );

    FlashPlayer *pHudPlayer =
        ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;

    const bool bPauseMenuAllowed = ( ( flags >> 1 ) & 1 ) != 0;
    const bool bTriggered = ( pPlayerCfg != nullptr ) &&
                            ( IsActionPressed( pPlayerCfg->pMenuPause ) ||
                              !gEdSys.isWindowActivated() );

    if ( !bPauseMenuAllowed || gpConfig == nullptr ||
         gpConfig->NbMaxPlayers <= 0 || bPaused || !bTriggered ||
         bPauseMenuLocked || gpActiveGameMode == nullptr ||
         gpActiveGameMode->isMessageBoxVisible() || pHudPlayer == nullptr ||
         gpActiveGameMode->hasRequestedExit() || !bMovieActive ||
         gpMngFade == nullptr ||
         ( gpMngFade->getTransitionFlags() & 1 ) != 0 ) {
        return;
    }

    FlashClip hudClip;
    if ( !pHudPlayer->getClipPlayer( "/HUD", &hudClip ) ||
         hudClip.pSprite == nullptr ) {
        return;
    }

    if ( !hudClip.getProperty( eFlashProperty::FP_Visible ) ) {
        return;
    }

    bPaused = true;

    DeviceAction *pMenuPause = pPlayerCfg->pMenuPause;
    if ( pMenuPause != nullptr ) {
        const uint8_t actionFlags = pMenuPause->Flags;
        pMenuPause->Flags =
            uint8_t( ( ( actionFlags & 1 ) << 1 ) | ( actionFlags & 0xe1 ) );
    }

    onPauseMenuRequested( true );
}

void GameModeWorld::updatePauseFade() {
    // FUN_007bde50
    if ( gpPostFX == nullptr ) {
        return;
    }
    PostFXStepGlowPause *pGlow =
        (PostFXStepGlowPause *)
            gpPostFX->getEffect<ePostFXStep::PFXS_GlowPause>();

    if ( pauseFadeTimer == 0.0f ) {
        if ( !isPaused() ) {
            if ( !gpPostFX->isFXLocked() && pGlow != nullptr &&
                 pGlow->pModulate != nullptr ) {
                pGlow->pModulate->setEnabled( false );
            }
            if ( pGlow != nullptr ) {
                pGlow->field_0x3c = false;
            }
        }
        return;
    }

    if ( pGlow == nullptr || pGlow->pLuminanceFilter == nullptr ||
         pGlow->pBlur == nullptr || pGlow->pModulate == nullptr ) {
        return;
    }

    float lumStart, lumTarget;
    float blurTarget;
    int32_t numPass;
    float coeff0Start, coeff0Target;
    float coeff1Start, coeff1Target;
    float delay;

    if ( !isPaused() ) {
        // Fade out
        lumStart = 0.0f;
        lumTarget = pGlow->pLuminanceFilter->getLuminanceThreshold();
        blurTarget = pGlow->pBlur->getSampleSize();
        numPass = pGlow->pBlur->getNumPass();
        coeff0Start = 1.0f; // FLOAT_00f4165c
        coeff0Target = pGlow->pModulate->getCoefficient0();
        coeff1Start = 0.0f;
        coeff1Target = pGlow->pModulate->getCoefficient1();
        delay = gFXPFadeOutDelay;
    } else {
        // Fade in
        lumStart = gFXPLumThreshold;
        lumTarget = 0.0f;
        blurTarget = gFXPBlurSize;
        numPass = kFXPFadeInBlurPassCount;
        coeff0Start = gFXPModCoeff0;
        coeff0Target = 1.0f; // FLOAT_00f4165c
        coeff1Start = gFXPModCoeff0;
        coeff1Target = 0.0f;
        delay = gFXPFadeInDelay;
    }

    const float t = ( delay != 0.0f ) ? ( pauseFadeTimer / delay ) : 0.0f;

    float lum = ( lumTarget - lumStart ) * t + lumStart;
    const float blur = ( blurTarget - gFXPBlurSize ) * t + gFXPBlurSize;
    lum = Clamp( lum, 0.0f, 1.0f );

    pGlow->pLuminanceFilter->setLuminanceThreshold( lum );

    if ( numPass == 0 ) {
        numPass = 1;
    }
    pGlow->pBlur->setNumPass( numPass );
    if ( blur > 0.0f ) {
        pGlow->pBlur->setSampleSize( blur );
    }

    pGlow->pModulate->setCoefficient0( ( coeff0Target - coeff0Start ) * t +
                                       coeff0Start );
    pGlow->pModulate->setCoefficient1( ( coeff1Target - coeff1Start ) * t +
                                       coeff1Start );

    const float dt = ( gpTimer != nullptr ) ? gpTimer->SystemDeltaTime : 0.0f;
    pauseFadeTimer =
        ( pauseFadeTimer - dt > 0.0f ) ? ( pauseFadeTimer - dt ) : 0.0f;

    if ( !isPaused() && pauseFadeTimer == 0.0f ) {
        pGlow->field_0x3c = true;
    }
}
