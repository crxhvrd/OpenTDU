#include "shared.h"
#include "gs_car_state.h"

#include "gs_car_display.h"

#include "render/camera/gs_camera.h"
#include "render/mng_shadow.h"
#include "world/mng_world.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/local_player.h"
#include "physics/vehicle/physics_vehicle.h"
#include "physics/gs_physics.h" // TDUSurface
#include "physics/physics_config.h"
#include "car_state.h"
#include "audio/gs_audio.h"
#include "audio/vehicle/gs_vehicle_sound.h"
#include "render/camera/camera_game.h"

GSCarState *gpCarState = nullptr; // DAT_010e7518

GSCarState::GSCarState()
    : GameSystem(), pointerSet(), bUsedSlot( 0 ), numCarStates( 0 ) {
    // FUN_008ad0a0
    OTDU_ASSERT( gpCarState == nullptr );
    gpCarState = this;

    carStates.fill( CarState() );
    eventListeners.fill( nullptr );
    eventListenersData.fill( nullptr );
    eventNotifiables.fill( nullptr );
    eventNotifiablesData.fill( nullptr );
    eventNotifiablesProcessed.fill( nullptr );
    eventListenersDataProcessed.fill( nullptr );
    policeStateJobs.fill( PoliceStateJob() );

    for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
        carStates[i].setInstanceID( i );
    }
}

GSCarState::~GSCarState() {
    // FUN_008ad2c0
    gpCarState = nullptr;

    while ( vehicleResources.NumUsed > 0 ) {
        VehicleBaseResources *pVVar2 = vehicleResources.pUsed[0];
        if ( pVVar2 != nullptr ) {
            pVVar2->unload();
            releaseResourceEntry( pVVar2 );
        }
    }

    for ( CarState &state : carStates ) {
        state.reset();
        state.carInstanceID = -1;
    }
}

bool GSCarState::initialize( TestDriveGameInstance * ) {
    // FUN_008acec0
    // TODO: Original code allocates memory for freelist (not done here since
    // stl doesnt expose mem preallocate for queues)
    return true;
}

void GSCarState::terminate() {
    // FUN_008adc40
    for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
        if ( bUsedSlot & ( 1 << i ) ) {
            VehicleBaseResources *pVVar1 = carStates[i].pResource;
            releaseCarState( &carStates[i], false );

            if ( pVVar1 != nullptr ) {
                pVVar1->unload();
            }
        }
        carStates[i].carInstanceID = -1;
    }

    while ( !carToDeinstanciate.empty() ) {
        carToDeinstanciate.pop();
    }
}

namespace {
    constexpr float kOmegaToRPM = 9.5493f;      // DAT_00f41698, 60/2pi
    constexpr float kMetersToKmh = 3.6f;        // DAT_00f41768
    constexpr float kWattsToPS = 1.0f / 735.5f; // DAT_00f41694, metric hp
    constexpr float kSlipGateSpeed = 1.0f;      // FLOAT_00f4165c
    constexpr float kSmoothEngine =
        ( 1.0f - 0.5f ) * ( 10.0f / 9.0f );                            // 0.5556
    constexpr float kSmoothSpeed = ( 1.0f - 0.8f ) * ( 10.0f / 9.0f ); // 0.2222
    constexpr float kSmoothWheel = 1.0f / 9.0f; // DAT_00f420fc

    bool gCarStateSmoothTelemetry = false;

    inline void approach( float &current, float target, float rate ) {
        current += ( target - current ) * rate;
    }

    void publishBodyAudioTelemetry( CarBodyState &body,
                                    const PhysicsVehicle &vehicle ) {
        const float omega = vehicle.getEngineAngularVelocity();
        const float torque = vehicle.getEngineSmoothedTorque();

        const float rpm = omega * kOmegaToRPM;
        const float powerPS = torque * omega * kWattsToPS;
        const float speedKmh = vehicle.getChassisSpeed() * kMetersToKmh;

        if ( gCarStateSmoothTelemetry ) {
            approach( body.EngineRPM, rpm, kSmoothEngine );
            approach( body.EnginePowerPS, powerPS, kSmoothEngine );
            approach( body.EngineTorqueNm, torque, kSmoothEngine );
            approach( body.AudioSpeedKmh, speedKmh, kSmoothSpeed );
        } else {
            body.EngineRPM = rpm;
            body.EnginePowerPS = powerPS;
            body.EngineTorqueNm = torque;
            body.AudioSpeedKmh = speedKmh;
        }
    }

    void
    publishWheelAudioTelemetry( WheelState &wheel,
                                const PhysicsVehicle::WheelReadout &readout ) {
        // FUN_0083bbd0
        const float rollSpeed =
            std::fabs( readout.AngularVelocity * readout.Radius );
        const float longSpeed = std::fabs( readout.LongitudinalGroundSpeed );
        const float groundSpeedKmh = Max( rollSpeed, longSpeed ) * kMetersToKmh;
        const float latSpeedKmh =
            std::fabs( readout.LateralGroundSpeed ) * kMetersToKmh;

        const bool bMoving =
            longSpeed >= kSlipGateSpeed ||
            std::fabs( readout.AngularVelocity ) >= kSlipGateSpeed;
        const float slipLong = bMoving ? readout.LongitudinalSlip : 0.0f;
        const float slipLat = bMoving ? readout.LateralSlip : 0.0f;

        if ( gCarStateSmoothTelemetry ) {
            approach( wheel.AudioGroundSpeedKmh, groundSpeedKmh, kSmoothWheel );
            approach( wheel.AudioLatSpeedKmh, latSpeedKmh, kSmoothWheel );
            approach( wheel.AudioSlipLong, slipLong, kSmoothWheel );
            approach( wheel.AudioSlipLat, slipLat, kSmoothWheel );
        } else {
            wheel.AudioGroundSpeedKmh = groundSpeedKmh;
            wheel.AudioLatSpeedKmh = latSpeedKmh;
            wheel.AudioSlipLong = slipLong;
            wheel.AudioSlipLat = slipLat;
        }
    }
} // namespace

void GSCarState::tick( float totalTime, float deltaTime ) {
    // FUN_008adcd0
    while ( !carToDeinstanciate.empty() ) {
        CarState *pCVar12 = carToDeinstanciate.front();

        VehicleBaseResources *pVVar1 = pCVar12->getResource();
        releaseCarState( pCVar12, false );
        if ( pVVar1 != nullptr ) {
            pVVar1->unload();
            releaseResourceEntry( pVVar1 );
        }

        carToDeinstanciate.pop();
    }

    if ( gpMngWorld != nullptr && gpWorld != nullptr &&
         gpScenaric != nullptr ) {
        CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
        if ( pLocalCar == nullptr || pLocalCar->getResource() == nullptr ||
             gpMngWorld->getDisableStreamingOnCar() ) {
            if ( !gpMngWorld->getDisableStreamingOnCamera() &&
                 gpCamera != nullptr ) {
                const Eigen::Vector3f c =
                    gpCamera->getActiveCamera()->getWorldPosition();
                Eigen::Vector4f center( c.x(), c.y(), c.z(), 1.0f );
                gpWorld->setStreamingPosition( &center );
            }
        } else {
            Eigen::Vector4f carWorldPos =
                pLocalCar->getBody()->WorldMatrix.row( 3 );
            gpWorld->setStreamingPosition( &carWorldPos );
        }
    }

    Eigen::Vector4f streamShift = Eigen::Vector4f::Zero();
    if ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() ) {
        streamShift = gpWorldStreaming->getPosition();
    }
    for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
        if ( ( ( bUsedSlot >> i ) & 1u ) == 0 ) {
            continue;
        }
        CarState &carState = carStates[i];
        PhysicsVehicle *pPhysicsVehicle = carState.pPhysicsVehicle;
        if ( pPhysicsVehicle == nullptr || !pPhysicsVehicle->isActive() ) {
            continue;
        }

        CarBodyState &body = carState.body;
        Matrix4x4f_t worldMatrix = pPhysicsVehicle->getChassisWorldMatrix();
        float liftSum = 0.0f;
        const uint32_t liftNumWheels =
            Min( pPhysicsVehicle->getNumWheels(), CarState::kMaxNumWheels );
        const float liftBodyHalfH = carState.body.BodyDimensions.y() * 0.5f;
        for ( uint32_t w = 0; w < liftNumWheels; w++ ) {
            const float pWheelOffset =
                carState.wheels[w].Position + liftBodyHalfH;
            liftSum +=
                -pPhysicsVehicle->getWheelRestCenterDrop( w ) - pWheelOffset;
        }
        const float lift = liftNumWheels > 0 ? liftSum / liftNumWheels : 0.0f;
        worldMatrix( 3, 0 ) += lift * worldMatrix( 1, 0 );
        worldMatrix( 3, 1 ) += lift * worldMatrix( 1, 1 );
        worldMatrix( 3, 2 ) += lift * worldMatrix( 1, 2 );
        body.WorldMatrix = worldMatrix;
        Matrix4x4f_t offseted = worldMatrix;
        offseted( 3, 0 ) -= streamShift.x();
        offseted( 3, 1 ) -= streamShift.y();
        offseted( 3, 2 ) -= streamShift.z();
        body.OffsetedWorldMatrix = offseted;

        body.SpeedInKmh = pPhysicsVehicle->getChassisSpeed();
        body.Omega = pPhysicsVehicle->getEngineAngularVelocity();
        body.Gears = pPhysicsVehicle->getCurrentGear();
        body.GearState = pPhysicsVehicle->getCurrentGear() + 1;
        body.Torque = pPhysicsVehicle->getEngineTorqueOut();
        body.MaxOmega = pPhysicsVehicle->getEngineOmegaMax();

        publishBodyAudioTelemetry( body, *pPhysicsVehicle );

        if ( gpVehicleSound != nullptr &&
             carState.getVehicleSound() == nullptr ) {
            carState.setVehicleSound(
                gpVehicleSound->createInstance( &carState ) );
        }

        const PhysicsVehicle::DrivingGearbox gearbox =
            pPhysicsVehicle->getDrivingGearbox();
        const bool bStopLampsArmed =
            !gearbox.bAutomatic || pPhysicsVehicle->getAutoReverseState() != 0;
        body.bIsBraking =
            bStopLampsArmed && pPhysicsVehicle->getInputBrake() > 0.1f;
        body.bIsReversing = pPhysicsVehicle->getCurrentGear() < 0;

        const uint32_t physNumWheels =
            Min( pPhysicsVehicle->getNumWheels(), CarState::kMaxNumWheels );

        int32_t numGrounded = 0;
        for ( uint32_t w = 0; w < physNumWheels; w++ ) {
            const PhysicsVehicle::WheelReadout ro =
                pPhysicsVehicle->getWheelReadout( w );
            WheelState &wheel = carState.wheels[w];
            wheel.bInitialized = true;
            publishWheelAudioTelemetry( wheel, ro );
            wheel.bGrounded = ro.bGrounded;
            wheel.Omega = ro.AngularVelocity;
            wheel.SteerAngle = ro.SteerAngle;
            wheel.SlipRatio = ro.LongitudinalSlip;
            wheel.SlipAngle = ro.LateralSlip;
            wheel.LongitudinalSpeed = ro.LongitudinalGroundSpeed;
            wheel.LateralSpeed = ro.LateralGroundSpeed;
            wheel.Load = ro.SuspensionLoad;

            wheel.SuspensionTravelLength = ro.SuspensionDeflection;
            wheel.WheelRadius = ro.Radius;
            wheel.RelativeWorldPosition =
                Eigen::Vector4f( ro.LocalPosition.x(), ro.LocalPosition.y(),
                                 ro.LocalPosition.z(), 1.0f );
            wheel.ProjectedPositionOnGround =
                Eigen::Vector4f( ro.ContactPoint.x() - streamShift.x(),
                                 ro.ContactPoint.y() - streamShift.y(),
                                 ro.ContactPoint.z() - streamShift.z(), 1.0f );

            // FUN_0083bbd0
            switch ( ro.ContactSurface ) {
            case TDUSurface::Road:
                wheel.ContactMaterial = eMaterialType::MT_Asphalt;
                wheel.bOnRoad = true;
                break;
            case TDUSurface::Object:
                wheel.ContactMaterial = eMaterialType::MT_Concrete;
                wheel.bOnRoad = true;
                break;
            case TDUSurface::Terrain:
            default:
                wheel.ContactMaterial = ( gbGrassAsTarmac )
                                            ? eMaterialType::MT_Asphalt
                                            : eMaterialType::MT_Grass;
                wheel.bOnRoad = false;
                break;
            }
            if ( ro.bGrounded ) {
                numGrounded++;
            }
        }
        body.NumWheelsGrounded = numGrounded;
    }

    for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
        if ( ( bUsedSlot >> i ) & 1u ) {
            carStates[i].updateBackOnTrack( deltaTime );
        }
    }

    notifyListeners( deltaTime );
    if ( !gpCamera->isLocked() ) {
        gpCamera->tickActiveCamera( deltaTime );
        gpCamera->setActiveCameraAsDefault();
        gpCamera->bindToRender();
    }

    if ( gpAudio != nullptr ) {
        gpAudio->syncListenerToActiveCamera();
    }

    if ( gpVehicleSound != nullptr ) {
        Eigen::Vector3f listenerPosition = Eigen::Vector3f::Zero();
        if ( gpCamera != nullptr && gpCamera->getActiveCamera() != nullptr ) {
            listenerPosition = gpCamera->getActiveCamera()->getWorldPosition();
        }
        gpVehicleSound->tickInstances( listenerPosition );
    }

    for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
        if ( carStates[i].getResource() != nullptr ) {
            carStates[i].resolveCollisionEvents();
        }
    }

    OTDU_UNIMPLEMENTED; // TODO: Physics

    for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
        CarState *local_6c = &carStates[i];
        for ( int32_t iVar10 = 0; iVar10 < 4; iVar10++ ) {
            auto *callback = eventNotifiables[i * 4 + iVar10];
            if ( callback != nullptr ) {
                callback->onEvent( local_6c, deltaTime,
                                   eventNotifiablesData[i * 4 + iVar10] );
            }
        }
    }

    for ( int32_t iVar10 = 0; iVar10 < 2; iVar10++ ) {
        CarStateEventListener *callback = eventNotifiablesProcessed[iVar10];
        if ( callback != nullptr ) {
            callback->onEvent( &carStates[0], deltaTime,
                               eventListenersDataProcessed[iVar10] );
        }
    }

    OTDU_UNIMPLEMENTED; // TODO: Scenarics
                        // TODO: MngHUD

    if ( gpMngShadow != nullptr ) {
        for ( uint32_t iVar3 = 0; iVar3 < kMaxNumVehicles; iVar3++ ) {
            if ( bUsedSlot & ( 1 << iVar3 ) ) {
                CarState &carState = carStates[iVar3];
                PhysicsVehicle *pPhysicsVehicle = carState.pPhysicsVehicle;
                if ( pPhysicsVehicle != nullptr &&
                     pPhysicsVehicle->isActive() ) {
                    gpMngShadow->addShadowCaster( &carState );
                }
            }
        }
    }
}

void GSCarState::draw( float deltaTime ) {
    // FUN_008acea0
    // NOP
}

void GSCarState::pause() {
    // FUN_008adb20
    while ( !carToDeinstanciate.empty() ) {
        CarState *pCVar12 = carToDeinstanciate.front();

        VehicleBaseResources *pVVar1 = pCVar12->getResource();
        releaseCarState( pCVar12, false );
        if ( pVVar1 != nullptr ) {
            pVVar1->unload();
            releaseResourceEntry( pVVar1 );
        }

        carToDeinstanciate.pop();
    }

    for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
        if ( bUsedSlot & ( 1 << i ) ) {
            VehicleBaseResources *pVVar1 = carStates[i].pResource;
            releaseCarState( &carStates[i], false );

            if ( pVVar1 != nullptr ) {
                pVVar1->unload();
            }
        }
    }

    for ( CarState &state : carStates ) {
        state.reset();
    }
}

VehicleBaseResources *GSCarState::grabFreeResourceEntry() {
    // FUN_00414160
    if ( vehicleResources.NumUsed >= kMaxNumVehicles ) {
        OTDU_LOG_ERROR(
            "Failed to get free car state (pool ran out of entries!)\n" );
        return nullptr;
    }

    uint32_t freeTop = kMaxNumVehicles - 1 - vehicleResources.NumUsed;
    VehicleBaseResources *pEntry = vehicleResources.pFree[freeTop];
    vehicleResources.pFree[freeTop] = nullptr;
    vehicleResources.pUsed[vehicleResources.NumUsed] = pEntry;
    uint32_t elementIndex =
        static_cast<uint32_t>( pEntry - vehicleResources.Elements.data() );
    vehicleResources.pbUsed[elementIndex] = true;
    vehicleResources.NumUsed++;

    return pEntry;
}

CarState &GSCarState::grabNextFreeCarState() {
    for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
        if ( !( ( bUsedSlot >> i ) & 1u ) ) {
            bUsedSlot |= ( 1u << i );
            if ( i + 1 > numCarStates ) {
                numCarStates = i + 1;
            }
            return carStates[i];
        }
    }
    OTDU_ASSERT( false );
    return carStates[0];
}

void GSCarState::registerCallback( CarState *param_2,
                                   CarStateEventListener *pListener,
                                   void *pData ) {
    // FUN_008acf80
    for ( int32_t iVar4 = 0; iVar4 < 4; iVar4++ ) {
        CarStateEventListener *ppCVar1 =
            eventNotifiables[param_2->carInstanceID * 4 + iVar4];
        if ( ppCVar1 == nullptr ) {
            if ( -1 < iVar4 ) {
                eventNotifiables[param_2->carInstanceID * 4 + iVar4] =
                    pListener;
                eventNotifiablesData[param_2->carInstanceID * 4 + iVar4] =
                    pData;
            }
            return;
        }
    }
}

void GSCarState::unregisterCallback( CarState *param_2,
                                     CarStateEventListener *pListener ) {
    // FUN_008acf10
    if ( pListener != nullptr ) {
        int32_t iVar2 = 0;
        for ( iVar2 = 0; iVar2 < 4; iVar2++ ) {
            if ( eventNotifiables[param_2->carInstanceID * 4 + iVar2] ==
                 pListener ) {
                break;
            }
        }

        if ( 3 < iVar2 ) {
            OTDU_LOG_WARN( "Could not find callback %p in GSCarState!\n",
                           pListener );
            return;
        }

        eventNotifiables[param_2->carInstanceID * 4 + iVar2] = nullptr;
        eventNotifiablesData[param_2->carInstanceID * 4 + iVar2] = nullptr;
    }
}

void GSCarState::releaseResourceEntry( VehicleBaseResources *pResource ) {
    // FUN_004141c0
    uint32_t elementIndex =
        static_cast<uint32_t>( pResource - vehicleResources.Elements.data() );
    if ( elementIndex >= kMaxNumVehicles || vehicleResources.NumUsed == 0 ) {
        return;
    }
    uint32_t i = 0;
    while ( vehicleResources.pUsed[i] != pResource ) {
        if ( ++i >= vehicleResources.NumUsed ) {
            return;
        }
    }
    vehicleResources.pFree[kMaxNumVehicles - vehicleResources.NumUsed] =
        pResource;
    while ( i < vehicleResources.NumUsed - 1 ) {
        vehicleResources.pUsed[i] = vehicleResources.pUsed[i + 1];
        i++;
    }
    vehicleResources.pUsed[vehicleResources.NumUsed - 1] = nullptr;
    vehicleResources.pbUsed[elementIndex] = false;
    vehicleResources.NumUsed--;
}

void GSCarState::releaseCarState( CarState *param_2, bool param_3 ) {
    // FUN_008ad980
    VehicleBaseResources *pVVar2 = param_2->pResource;
    param_2->pResource = nullptr;
    if ( param_2->field_0x39c0 != nullptr ) {
        OTDU_UNIMPLEMENTED; // TODO: AVATAR
    }

    if ( param_2->field_0x39cc != nullptr ) {
        OTDU_UNIMPLEMENTED; // TODO: AI
        param_2->field_0x39cc = nullptr;
    }

    if ( param_2->pPhysicsVehicle != nullptr ) {
        param_2->pPhysicsVehicle->destroy();
        param_2->pPhysicsVehicle = nullptr;
    }

    if ( param_2->pVehicleBase != nullptr ) {
        gpCarDisplay->releaseVehicle( param_2 );
    }

    if ( param_2->pVehicleSound != nullptr && gpVehicleSound != nullptr ) {
        gpVehicleSound->destroyInstance( param_2 );
    }

    if ( param_2->field_0x39d4 != nullptr ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (P2P)
    }
    param_2->carID = -1;
    param_2->tireID = -1;
    param_2->rimID = -1;
    param_2->pPhysicsVehicle = nullptr;
    param_2->pVehicleBase = nullptr;
    param_2->pVehicleSound = nullptr;
    param_2->field_0x39cc = nullptr;

    if ( pVVar2 != nullptr ) {
        pVVar2->RefCount--;
    }
    bUsedSlot &= ~( 1 << param_2->carInstanceID );
    param_2->reset();
}

void GSCarState::notifyListeners( float param_1 ) {
    // FUN_008ad1a0
    for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
        CarState *pCVar2 = &carStates[i];
        if ( pCVar2->getResource() != nullptr ) {
            if ( eventListeners[i] != nullptr ) {
                eventListeners[i]->onEvent( pCVar2, param_1,
                                            eventListenersData[i] );

                pCVar2->bInstanceAlive = true;
            }
            pCVar2->updateGroundColor();
        }
    }
}

PointerSet::PointerSet() {
    // FUN_008acc70
    Content.fill( nullptr );
}
