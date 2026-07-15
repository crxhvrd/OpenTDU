#include "shared.h"
#include "gm_world.h"

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
#include "input/ed_dev.h"
#include "input/input_device.h"
#include "game/vehicle/bank_vehicle.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/gs_car_display.h"
#include "game/vehicle/vehicle_base.h"
#include "game/vehicle/vehicle_car.h"
#include "game/vehicle/car_state.h"

#include "render/mng_env_map.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/local_player.h"

float gFXPFadeInDelay = 0.5f;   // DAT_00f9c0f4
float gFXPFadeOutDelay = 0.5f;  // DAT_00f9c0f8
float gFXPLumThreshold = 0.99f; // DAT_00f9c0fc
float gFXPBlurSize = 1.0f;      // DAT_00f9c100
float gFXPModCoeff0 = 1.0f;     // DAT_00f9c104
float gFXPModCoeff1 = 0.125f;   // DAT_00f9c108

GameModeWorld *gpActiveGM = nullptr; // DAT_010e7c6c

// Height above the sampled terrain surface at which the freeride spawn seats
// the car body origin (approximates the body-centre to wheel-contact offset).
static constexpr float kSpawnGroundClearance = 0.5f;

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
            pPlayerPhysicsVehicle =
                gpPhysics->createVehicle( pFactory, spawn, *pDB, pTorqueBin );
            state.pPhysicsVehicle = pPlayerPhysicsVehicle;
        }
    }

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
            OTDU_LOG_INFO( "Debug: switched player car to %d\n", carID );
        } else {
            OTDU_LOG_INFO(
                "Debug: car %d not switchable (missing DB or bike)\n", carID );
        }
    }
#endif

    // Drive the player car body from the physics simulation.
    CarState *pCar = ( gpScenaric != nullptr )
                         ? gpScenaric->getLocalPlayer().getCarState()
                         : nullptr;
    if ( pPlayerPhysicsVehicle != nullptr &&
         pPlayerPhysicsVehicle->isActive() && pCar != nullptr ) {
        // HACK: REMOVE ME: TEST FOR VEHICLE PHYSICS
        Device *pKb = gEdDev.getDevice( eDeviceType::DT_Keyboard, 0 );
        if ( pKb != nullptr ) {
            const float throttle =
                pKb->getChannelValueBinary( "UP" ) ? 1.0f : 0.0f;
            const float brake =
                pKb->getChannelValueBinary( "DOWN" ) ? 1.0f : 0.0f;
            const float steer =
                ( pKb->getChannelValueBinary( "RIGHT" ) ? 1.0f : 0.0f ) -
                ( pKb->getChannelValueBinary( "LEFT" ) ? 1.0f : 0.0f );
            pPlayerPhysicsVehicle->setInput( throttle, brake, steer );
        }
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
    OTDU_UNIMPLEMENTED; // TODO: SCENARIC/PHYSICS/POSTFX
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

        OTDU_UNIMPLEMENTED; //  TODO: POSTFX
    }

    pauseMenuActivated();
}

void GameModeWorld::pauseMenuActivated() {
    // FUN_007bdbc0
    if ( gpMngHud != nullptr ) {
        OTDU_UNIMPLEMENTED; // TODO: FLASH
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

        OTDU_UNIMPLEMENTED; // TODO: POSTFX
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
    OTDU_UNIMPLEMENTED; // TODO: POSTFX
}
