#include "shared.h"
#include "gm_world.h"

#include <cmath>

#include "gs_timer.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "flash/gs_flash.h"
#include "flash/flash_mouse.h"
#include "world/weather/gs_weather.h"
#include "render/camera/gs_camera.h"
#include "player_data/gs_playerdata.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "database/gs_database.h"
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

float gFXPFadeInDelay = 0.5f;   // DAT_00f9c0f4
float gFXPFadeOutDelay = 0.5f;  // DAT_00f9c0f8
float gFXPLumThreshold = 0.99f; // DAT_00f9c0fc
float gFXPBlurSize = 1.0f;      // DAT_00f9c100
float gFXPModCoeff0 = 1.0f;     // DAT_00f9c104
float gFXPModCoeff1 = 0.125f;   // DAT_00f9c108

static constexpr int32_t kFXPFadeInBlurPassCount = 3; // DAT_00f9c10c

GameModeWorld *gpActiveGM = nullptr; // DAT_010e7c6c

// Height above the sampled terrain surface at which the freeride spawn seats
// the car body origin (approximates the body-centre to wheel-contact offset).
static constexpr float kSpawnGroundClearance = 0.5f;

enum eAutoGearboxState : int32_t {
    AGS_Stopped = 0,     // standing still, engine idling
    AGS_EngagingReverse, // brake held at a standstill -> select reverse
    AGS_Reverse,         // reverse engaged (pedals swapped)
    AGS_MovingOff,       // pulling away forwards (waiting for 1st)
    AGS_Forward,         // driving forwards, shift logic in charge
    AGS_LineLock,        // both pedals at a standstill (declutched, revving)
};
static constexpr float kAutoGbxPedal = 0.1f; // DAT_00fab720 (pedal pressed)
static constexpr float kAutoGbxInput = 0.1f; // FLOAT_00f41668
static constexpr float kAutoGbxPedalIdle = 0.01f;        // DAT_00f416d4
static constexpr float kAutoGbxHoldBrake = 0.5f;         // FLOAT_00f41674
static constexpr float kAutoGbxMovingSpeed = 0.2777778f; // DAT_00f41868, 1km/h
static constexpr float kAutoGbxMoveOffThrottle = 0.2f;   // DAT_00f41730
static constexpr float kAutoGbxReverseExitSpeed = 2.0f;  // DAT_00f416b0

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
    OTDU_UNIMPLEMENTED; // TODO: SCENARIC

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
      initializeTime( 0.0f ),
      pauseFadeTimer( 0.0f ),
      autoGearboxState( 0 ),
      autoGearboxThrottle( 0.0f ),
      autoGearboxBrake( 0.0f ) {
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
        OTDU_UNIMPLEMENTED; // TODO: GameLogic
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
    bPauseMenuLocked = false; // 0x1031fc
    gameMechType = 0;         // 0x1031f4

    if ( gpCamera != nullptr && gpPlayerData != nullptr ) {
        gpCamera->setSeatPosition( gpPlayerData->getSeatPositionHeight(),
                                   gpPlayerData->getSeatPositionPosition() );
    }

    if ( ( flags & 4 ) != 0 && gpScenaric != nullptr &&
         gpScenaric->getLocalPlayer().getCarState() == nullptr ) {
        installLocalPlayerCar();
    }

    if ( gpCamera != nullptr && gpScenaric != nullptr ) {
        CarState *pCar = gpScenaric->getLocalPlayer().getCarState();
        if ( pCar != nullptr ) {
            gpCamera->restoreCamera();
        }
    }

    OTDU_UNIMPLEMENTED; // TODO: SCENARIC (driver avatar, HUD bind)
}

constexpr int32_t kDefaultPlayerCarID = 1030;
int32_t gRequestedPlayerCarID = -1;

// Debug teleport request
bool gRequestedPlayerTeleport = false;
Eigen::Vector3f gRequestedPlayerTeleportPos = Eigen::Vector3f::Zero();

void GameModeWorld::installLocalPlayerCar() {
    installLocalPlayerCar( kDefaultPlayerCarID, nullptr );
}

void GameModeWorld::installLocalPlayerCar(
    int32_t carID, const Matrix4x4f_t *pSpawnOverride ) {
    // FUN_007bf280 (inlined) / FUN_0079da80.
    if ( gpCarState == nullptr || gpCarDisplay == nullptr ||
         gpScenaric == nullptr ) {
        return;
    }

    VehicleBaseResources *pRes = gpCarState->grabFreeResourceEntry();
    if ( pRes == nullptr ) {
        return;
    }
    if ( !pRes->load( carID, -1, 0, 4, -1, 0,
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

        // Centre streaming on the spawn so terrain loads around the car.
        if ( gpWorldStreaming != nullptr ) {
            const Eigen::Vector4f center( spawn( 3, 0 ), 0.0f, spawn( 3, 2 ),
                                          0.0f );
            gpWorldStreaming->setPosition( center );
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
        if ( pDB != nullptr ) {
            gpPhysics->destroyVehicle( state.pPhysicsVehicle );
            pPlayerPhysicsVehicle =
                gpPhysics->createVehicle( pFactory, spawn, *pDB, pTorqueBin );
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
    Matrix4x4f_t pose = ( pPlayerPhysicsVehicle != nullptr &&
                          pPlayerPhysicsVehicle->isActive() )
                            ? pPlayerPhysicsVehicle->getChassisWorldMatrix()
                            : pCar->getBody()->WorldMatrix;
    pose( 3, 0 ) = worldPos.x();
    pose( 3, 1 ) = worldPos.y();
    pose( 3, 2 ) = worldPos.z();

    if ( gpWorldStreaming != nullptr ) {
        const Eigen::Vector4f center( worldPos.x(), 0.0f, worldPos.z(), 0.0f );
        gpWorldStreaming->setPosition( center );
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
    CarState *pCar = ( gpScenaric != nullptr )
                         ? gpScenaric->getLocalPlayer().getCarState()
                         : nullptr;
    if ( pCar == nullptr ) {
        return;
    }

    PlayerConfiguration *pCfg =
        ( gpConfig != nullptr && gpConfig->pPlayerConfig != nullptr &&
          gpConfig->NbMaxPlayers > 0 )
            ? &gpConfig->pPlayerConfig[0]
            : nullptr;

    // Combined analog value of a bound device action (0 when unbound)
    auto readAxis = []( DeviceAction *pAction ) -> float {
        if ( pAction == nullptr || pAction->NumAddedChannels == 0 ) {
            return 0.0f;
        }
        return pAction->FUN_00434e40( 0.0f );
    };

    // Rescale a raw axis past its dead-zone into [0,1] (FLOAT_00f4165c == 1.0).
    auto applyDeadZone = []( float value, float deadZone ) -> float {
        return value >= deadZone ? ( value - deadZone ) / ( 1.0f - deadZone )
                                 : 0.0f;
    };

    const bool bBound =
        pCfg != nullptr && ( ( pCfg->pCarBrake != nullptr &&
                               pCfg->pCarBrake->NumAddedChannels != 0 ) ||
                             ( pCfg->pCarLeft != nullptr &&
                               pCfg->pCarLeft->NumAddedChannels != 0 ) ||
                             ( pCfg->pCarRight != nullptr &&
                               pCfg->pCarRight->NumAddedChannels != 0 ) ||
                             ( pCfg->pCarClutch != nullptr &&
                               pCfg->pCarClutch->NumAddedChannels != 0 ) );

    float throttle = 0.0f;
    float brake = 0.0f;
    float steer = 0.0f;

    if ( pCfg != nullptr && deltaTime > 0.0f ) {
        const float rawAccel = readAxis( pCfg->pCarBrake );  // CAR_ACCEL
        const float rawBrake = readAxis( pCfg->pCarLeft );   // CAR_BRAKE
        const float rawLeft = readAxis( pCfg->pCarRight );   // CAR_LEFT
        const float rawRight = readAxis( pCfg->pCarClutch ); // CAR_RIGHT

        throttle = applyDeadZone( rawAccel, gAccelDeadZone );
        brake = applyDeadZone( rawBrake, gBrakeDeadZone );

        steer = rawRight - rawLeft;
    }

    (void)bBound;

    CarBodyState *pBody = pCar->getBody();
    pBody->Throttle = throttle;
    pBody->Brake = brake;
    pBody->SteeringRaw = steer;
    pBody->Steering = steer;

    float simThrottle = throttle;
    float simBrake = brake;
    if ( isDrivingInputScripted() ) {
        pPlayerPhysicsVehicle->setClutchOverride( -1.0f );
        resetAutomaticGearbox();
    } else {
        arbitrateAutomaticGearbox( deltaTime, throttle, brake, simThrottle,
                                   simBrake );
    }
    pPlayerPhysicsVehicle->setInput( simThrottle, simBrake, steer );
    pPlayerPhysicsVehicle->setAutoReverseState( autoGearboxState );

    if ( pCfg != nullptr ) {
        pBody->bHeadlightsOn =
            readAxis( pCfg->pCarIgnition ) > 0.5f;           // CAR_LIGHT
        pBody->bHonking = readAxis( pCfg->pCarHorn ) > 0.5f; // CAR_HORN
        pBody->Handbrake = readAxis( pCfg->pCarCamera );     // CAR_HBRAKE
        pBody->Clutch = readAxis( pCfg->pBikeAccel );        // CAR_CLUTCH

        int32_t gearSelect = 1;
        if ( IsActionDown( pCfg->pCarGear1 ) )
            gearSelect = 0; // CAR_GREV -> reverse
        if ( IsActionDown( pCfg->pCarGear2 ) )
            gearSelect = 2; // CAR_G1ST
        if ( IsActionDown( pCfg->pCarGear3 ) )
            gearSelect = 3; // CAR_G2ND
        if ( IsActionDown( pCfg->pCarGear4 ) )
            gearSelect = 4; // CAR_G3RD
        if ( IsActionDown( pCfg->pCarGear5 ) )
            gearSelect = 5; // CAR_G4TH
        if ( IsActionDown( pCfg->pCarGear6 ) )
            gearSelect = 6; // CAR_G5TH
        if ( IsActionDown( pCfg->pCarGear7 ) )
            gearSelect = 7; // CAR_G6TH
        if ( IsActionDown( pCfg->pCarHandbrake ) )
            gearSelect = 8; // CAR_G7TH
        pBody->GearRequest = gearSelect;

        pBody->bGearUpEdge = IsActionPressed( pCfg->pCarGearDown ); // CAR_GUP
        pBody->bGearDownEdge =
            IsActionPressed( pCfg->pBikeGearUp ); // CAR_GDOWN

        const bool bChangeViewHeld =
            IsActionDown( pCfg->pCarBackview ); // CAR_CAMERA
        if ( gCamViewLockHoldTime < 0.0f ) {
            gCamViewLockHoldTime = bChangeViewHeld ? -1.0f : 0.0f;
        } else if ( bChangeViewHeld ) {
            gCamViewLockHoldTime += deltaTime;
        } else {
            gCamViewLockHoldTime = 0.0f;
        }
    }

    if ( gpMngInGameMenu != nullptr ) {
        pBody->WindowState = gpMngInGameMenu->getWindowsDirection();
    }
}

void GameModeWorld::resetAutomaticGearbox() {
    autoGearboxState = AGS_Stopped;
    autoGearboxThrottle = 0.0f;
    autoGearboxBrake = 0.0f;
}

void GameModeWorld::arbitrateAutomaticGearbox( float deltaTime,
                                               float accel,
                                               float brake,
                                               float &outThrottle,
                                               float &outBrake ) {
    // FUN_0041a210
    PhysicsVehicle *pVehicle = pPlayerPhysicsVehicle;
    outThrottle = autoGearboxThrottle;
    outBrake = autoGearboxBrake;

    const PhysicsVehicle::DrivingGearbox gearbox =
        pVehicle->getDrivingGearbox();
    if ( !gearbox.bAutomatic || deltaTime <= 0.0f || !gearbox.bEngineFiring ) {
        // Manual gearbox or a dead engine: the pedals pass straight through.
        pVehicle->setClutchOverride( -1.0f );
        autoGearboxThrottle = accel;
        autoGearboxBrake = brake;
        outThrottle = accel;
        outBrake = brake;
        return;
    }

    const float speed = pVehicle->getChassisSpeed();
    bool bDeclutch = false;
    switch ( autoGearboxState ) {
    case AGS_Stopped:
        if ( accel > kAutoGbxPedal ) {
            if ( brake > kAutoGbxPedal ) {
                autoGearboxState = AGS_LineLock;
                break; // leaves the outputs untouched for this frame
            }
            if ( brake < kAutoGbxPedal ) {
                autoGearboxState = AGS_MovingOff;
            }
        }
        if ( brake > kAutoGbxPedal && accel < kAutoGbxPedal ) {
            autoGearboxState = AGS_EngagingReverse;
        }
        outBrake = kAutoGbxHoldBrake; // auto-hold, keeps the car put
        outThrottle = accel;
        pVehicle->setCurrentGear( 2 ); // sits in 1st, declutched
        bDeclutch = true;
        break;

    case AGS_EngagingReverse:
        if ( accel > kAutoGbxPedal ) {
            autoGearboxState = AGS_LineLock;
        }
        if ( gearbox.bShiftIdle && gearbox.Gear > 0 ) {
            if ( gearbox.Gear > 1 ) {
                pVehicle->setCurrentGear( 1 ); // snap to neutral first
            }
            pVehicle->requestDownshift(); // neutral -> reverse
        }
        if ( gearbox.Gear == 0 ) {
            // Reverse selected: the pedals swap roles.
            outBrake = accel;
            outThrottle = brake;
        } else {
            outBrake = kAutoGbxHoldBrake;
            outThrottle = 0.0f;
        }
        if ( speed > kAutoGbxMovingSpeed && gearbox.Gear == 0 ) {
            autoGearboxState = AGS_Reverse;
        } else if ( brake < kAutoGbxPedal ) {
            autoGearboxState = AGS_Stopped;
        }
        break;

    case AGS_Reverse:
        pVehicle->setCurrentGear( 0 );
        outBrake = accel;
        outThrottle = brake;
        if ( std::fabs( gearbox.ForwardSpeed ) < kAutoGbxReverseExitSpeed ) {
            if ( accel >= kAutoGbxInput || speed >= kAutoGbxInput ) {
                if ( brake < kAutoGbxInput && accel > kAutoGbxInput ) {
                    autoGearboxState = AGS_MovingOff;
                }
            } else {
                autoGearboxState = AGS_Stopped;
            }
        }
        break;

    case AGS_MovingOff:
        if ( brake > kAutoGbxPedal ) {
            autoGearboxState = AGS_LineLock;
        }
        if ( gearbox.bShiftIdle ) {
            if ( gearbox.Gear < 2 ) {
                if ( gearbox.Gear == 0 ) {
                    pVehicle->setCurrentGear( 1 ); // out of reverse first
                }
                pVehicle->requestUpshift();
            } else if ( gearbox.Gear > 2 ) {
                pVehicle->requestDownshift();
            }
        }
        if ( gearbox.Gear == 2 ) {
            outBrake = ( brake < kAutoGbxPedal ) ? 0.0f : brake;
            outThrottle = accel;
        } else {
            outBrake = kAutoGbxHoldBrake;
            outThrottle = kAutoGbxMoveOffThrottle; // keep the revs up
        }
        if ( speed > kAutoGbxMovingSpeed && gearbox.Gear == 2 ) {
            autoGearboxState = AGS_Forward;
        } else if ( accel < kAutoGbxPedal ) {
            autoGearboxState = AGS_Stopped;
        }
        break;

    case AGS_Forward: {
        outBrake = brake;
        outThrottle = accel;
        bool bCheckStop = true;
        if ( speed < kAutoGbxMovingSpeed && accel > kAutoGbxPedal &&
             brake > kAutoGbxPedal ) {
            autoGearboxState = AGS_LineLock;
            bCheckStop = false;
        }
        if ( bCheckStop &&
             ( ( brake < kAutoGbxPedalIdle && speed < kAutoGbxMovingSpeed ) ||
               ( gearbox.Gear == 1 && accel > kAutoGbxInput ) ) ) {
            autoGearboxState = AGS_Stopped;
        }
        break;
    }

    case AGS_LineLock:
        if ( brake < kAutoGbxPedal ) {
            autoGearboxState = AGS_MovingOff;
        }
        outBrake = kAutoGbxHoldBrake;
        outThrottle = accel;
        pVehicle->setCurrentGear( 2 );
        bDeclutch = true;
        break;

    default:
        autoGearboxState = AGS_Stopped;
        break;
    }

    pVehicle->setClutchOverride( bDeclutch ? 0.0f : -1.0f );

    autoGearboxThrottle = outThrottle;
    autoGearboxBrake = outBrake;
}

void GameModeWorld::tick( float deltaTime, float totalTime ) {
    // FUN_007beb90
    GameModeScene::tick( deltaTime, totalTime );

#ifdef OTDU_DEVBUILD
    // Runtime vehicle switch
    if ( gRequestedPlayerCarID >= 0 ) {
        const int32_t carID = gRequestedPlayerCarID;
        gRequestedPlayerCarID = -1;
        DBEntryCarPhysicsData *pDB = ( gpDatabase != nullptr )
                                         ? gpDatabase->getCarDBEntry( carID )
                                         : nullptr;
        if ( pDB != nullptr && !gpDatabase->isBike( pDB ) ) {
            // Respawn upright at the current position (+ a small lift).
            Matrix4x4f_t pose = Matrix4x4f_t::Identity();
            CarState *pCar = ( gpScenaric != nullptr )
                                 ? gpScenaric->getLocalPlayer().getCarState()
                                 : nullptr;
            Matrix4x4f_t cur = Matrix4x4f_t::Identity();
            if ( pPlayerPhysicsVehicle != nullptr &&
                 pPlayerPhysicsVehicle->isActive() ) {
                cur = pPlayerPhysicsVehicle->getChassisWorldMatrix();
            } else if ( pCar != nullptr ) {
                cur = pCar->getBody()->WorldMatrix;
            }
            pose( 3, 0 ) = cur( 3, 0 );
            pose( 3, 1 ) = cur( 3, 1 ); // + 1.5f;
            pose( 3, 2 ) = cur( 3, 2 );

            uninstallLocalPlayerCar();
            installLocalPlayerCar( carID, &pose );

            if ( gpMngHud != nullptr && gpScenaric != nullptr ) {
                gpMngHud->FUN_006e6a00(
                    gpScenaric->getLocalPlayer().getCarState() );
            }

            OTDU_LOG_INFO( "Debug: switched player car to %d\n", carID );
        } else {
            OTDU_LOG_INFO(
                "Debug: car %d not switchable (missing DB or bike)\n", carID );
        }
    }

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
        // car settles onto the streamed terrain instead of clipping. (The
        // heightfield lives in absolute space, so query the absolute XZ.)
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
        OTDU_UNIMPLEMENTED; // TODO: GameLogic
    }

    OTDU_UNIMPLEMENTED; // TODO: FLASH

    updatePauseFade();

    OTDU_UNIMPLEMENTED; // TODO: SANDBOX/CINVIEWER
}

void GameModeWorld::draw( float /*deltaTime*/, float /*totalTime*/ ) {
    // FUN_007bdc10
    if ( pGameMech != nullptr ) {
        OTDU_UNIMPLEMENTED; // TODO: GameLogic
    }
}

void GameModeWorld::pause() {
    // FUN_007be820
    GameModeScene::pause();

    OTDU_UNIMPLEMENTED; // TODO: SCENARIC (driver unload) / PHYSICS
                        // (postpone-pause)

    if ( gpPostFX != nullptr ) {
        PostFXStepGlowPause *pGlow =
            (PostFXStepGlowPause *)
                gpPostFX->getEffect<ePostFXStep::PFXS_GlowPause>();
        if ( pGlow != nullptr && pGlow->pModulate != nullptr &&
             pGlow->pModulate->isEnabled() && !gpPostFX->isFXLocked() ) {
            pGlow->pModulate->setEnabled( false );
        }
    }
}

void GameModeWorld::destroy() {
    // FUN_007be670
    if ( gpActiveGM != nullptr && gpActiveGM->isLoaded() ) {
        OTDU_UNIMPLEMENTED; // TODO: SCENARIC
    }

    GameModeWorldBase::destroy();

    destroyGameMech();

    OTDU_UNIMPLEMENTED; // TODO: ONLINE
}

void GameModeWorld::onResourceStreamed() {
    // FUN_007be140
    GameModeScene::onResourceStreamed();

    OTDU_UNIMPLEMENTED; // TODO: FLASH
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

        OTDU_UNIMPLEMENTED; // TODO: AUDIO
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
    OTDU_UNIMPLEMENTED; // TODO: SCENARIC
    // TODO: PLAYERDATA
    // TODO: FLASH
}

void GameModeWorld::onMenuPauseExit( bool bRestoreState ) {
    // FUN_007be040
    if ( bRestoreState ) {
        if ( gpWeather != nullptr ) {
            gpWeather->setUpdateTimerEnabled( true );
            OTDU_UNIMPLEMENTED; // TODO: WEATHER
        }

        if ( !hasRequestedExit() ) {
            if ( gpTimer != nullptr ) {
                gpTimer->GameSpeed = gpTimer->PreviousGameSpeed;
            }
            OTDU_UNIMPLEMENTED; // TODO: AUDIO
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

    OTDU_UNIMPLEMENTED; // TODO: GameLogic
    pGameMech = nullptr;
    return true;
}

void GameModeWorld::destroyGameMech() {
    // FUN_007bdc30
    if ( pGameMech != nullptr ) {
        OTDU_UNIMPLEMENTED; // TODO: GameLogic
        pGameMech = nullptr;
    }
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
