#include "shared.h"
#include "gs_car_state.h"

#include "ai/ai_pool.h"
#include "ai/ai_road_shape.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"

#include "gs_car_display.h"

#include "render/camera/gs_camera.h"
#include "render/mng_shadow.h"
#include "world/mng_world.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/sector_manager.h"
#include "world/streaming_manager.h"
#include "game/map/mng_map_viewer.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/local_player.h"
#include "game/scenaric/entity/entity_driver.h"
#include "physics/vehicle/physics_vehicle.h"
#include "physics/gs_physics.h" // TDUSurface
#include "physics/physics_material.h"
#include "physics/physics_config.h"
#include "car_state.h"
#include "vehicle_base.h"
#include "render/hiearchy_node.h"
#include "audio/gs_audio.h"
#include "audio/vehicle/gs_vehicle_sound.h"
#include "render/camera/camera_game.h"
#include "render/camera/camera_vehicle.h"
#include "game/mng_hud.h"
#include "game/gm_base.h"
#include "flash/mng_fade.h"
#include "tdu_instance.h"
#include "flash/implementation/flash_player.h"

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
    policeStateJobs.fill( nullptr );

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

    void publishBodyAudioTelemetry( CarBodyState &body, float torque ) {
        const float omega = body.Omega;

        const float rpm = omega * kOmegaToRPM;
        const float powerPS = torque * omega * kWattsToPS;
        const float speedKmh = body.SpeedInKmh * kMetersToKmh;

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

    constexpr float kNoHeightmapSentinel = -100000.0f; // DAT_00f42100
    constexpr float kHeightmapOutOfBounds = 100000.0f; // DAT_00f418e0
    constexpr float kBridgeTunnelBand = 1.0f;          // DAT_00fab544

    float sampleHeightmapAt( const Eigen::Vector4f &position ) {
        // FUN_00843dd0 (height only)
        if ( gpWorld == nullptr ) {
            return kNoHeightmapSentinel;
        }
        StreamingManager *pStreaming = gpWorld->getStreamingManager();
        if ( pStreaming == nullptr ) {
            return kNoHeightmapSentinel;
        }

        Sector *pSector =
            pStreaming->sectorManager.getSectorAtPosition( position );
        if ( pSector == nullptr || pSector->state != 1 ||
             pSector->geometry.pHeightmapInstance == nullptr ) {
            return kNoHeightmapSentinel;
        }

        const float height = SampleGroundHeight(
            pSector->geometry.pHeightmapInstance, pSector->heightmap,
            position.x(), position.z() );
        return ( kHeightmapOutOfBounds <= height ) ? kNoHeightmapSentinel
                                                   : height;
    }

    bool isOnTunnelRoad( const CarState &carState ) {
        // FUN_008afa40
        const ObjectWorldLocation &location = carState.getWorldLocation();
        if ( location.LocationType != ObjectWorldLocation::OWL_Road ) {
            return false;
        }

        AiRoadMap::Road *pRoad =
            gpAI->getRoadMap().getRoad( location.LocationRoad.RoadID );
        if ( pRoad == nullptr ) {
            return false;
        }
        AiRoadShape *pShape = pRoad->getShape();
        if ( pShape == nullptr ) {
            return false;
        }

        AiRoadIt it;
        pShape->computeIt( location.LocationRoad.FractionMin, it );
        if ( it.pRoad == nullptr ) {
            return false;
        }
        return it.getEntity()->pProfile->isTunnel();
    }

    void publishWheelAudioTelemetry( WheelState &wheel ) {
        // FUN_0083bbd0
        const float rollSpeed = std::fabs( wheel.Omega * wheel.WheelRadius );
        const float longSpeed = std::fabs( wheel.LongitudinalSpeed );
        const float groundSpeedKmh = Max( rollSpeed, longSpeed ) * kMetersToKmh;
        const float latSpeedKmh =
            std::fabs( wheel.LateralSpeed ) * kMetersToKmh;

        const bool bMoving = longSpeed >= kSlipGateSpeed ||
                             std::fabs( wheel.Omega ) >= kSlipGateSpeed;
        const float slipLong = bMoving ? wheel.SlipRatio : 0.0f;
        const float slipLat = bMoving ? wheel.SlipAngle : 0.0f;

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

static bool GetBodyShiftedPose( const JPH::Body *pBody,
                                Matrix4x4f_t &outPose ) {
    bool bActive = false;
    if ( !gpPhysics->getBodyPose( const_cast<JPH::Body *>( pBody ), outPose,
                                  bActive ) ) {
        return false;
    }
    outPose.row( 3 ).head<3>() -= GSPhysics::GetShiftOrigin().head<3>();
    return true;
}

static void RefreshVehiclePartsDisplay( const PhysicsVehicle &vehicle,
                                        const Matrix4x4f_t &bodyMatrix,
                                        HiearchyNode *pRoot ) {
    // FUN_004560c0
    if ( pRoot == nullptr ) {
        return;
    }
    HiearchyNode *pBodyNode = pRoot->findChild( vehicle.getBodyRenderKey() );
    if ( pBodyNode == nullptr ) {
        return;
    }
    const Matrix4x4f_t invChassis = vehicle.getChassisShiftedMatrix().inverse();
    pBodyNode->Flags |= 1;
    pBodyNode->TransformMatrix = bodyMatrix;

    for ( const PhysicsVehicle::AttachedPart &part :
          vehicle.getAttachedParts() ) {
        HiearchyNode *pNode = pRoot->findChild( part.RenderKey );
        if ( pNode == nullptr ) {
            continue;
        }
        const GSPhysics::PropMemberAttachment attachment =
            gpPhysics->getPropMemberAttachment( part.pBody );
        const bool bLoose =
            ( attachment.NumUnbreakable == 0 && attachment.NumBroken != 0 ) ||
            attachment.NumBreakable == 0;
        const bool bHanging =
            attachment.NumBroken != 0 && attachment.NumUnbreakable != 0;
        if ( !bLoose && !bHanging ) {
            continue;
        }
        Matrix4x4f_t pose;
        if ( !GetBodyShiftedPose( part.pBody, pose ) ) {
            continue;
        }
        pNode->Flags |= 1;
        pNode->TransformMatrix =
            bLoose ? pose : Matrix4x4f_t( pose * invChassis * bodyMatrix );
    }
}

static void TickReplayFadeSequencer() {
    // FUN_0081c330
    static constexpr float kFadeDuration = 0.5f; // DAT_00f8a660
    static bool sbStepStarted = false;           // DAT_010e7c1c

    if ( gpTestDriveInstance->getActiveGameMode() == GM_DriveIn ) {
        return;
    }
    if ( gpMngFade == nullptr ||
         ( gpMngFade->getTransitionFlags() & 1 ) != 0 ) {
        return;
    }

    eFadeRequest request = GSPhysics::peekFadeRequest();
    if ( sbStepStarted ) {
        if ( request == eFadeRequest::FR_FadeOut ) {
            gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() |
                                           8 );
        }
        request = GSPhysics::popFadeRequest();
        sbStepStarted = false;
    }

    switch ( request ) {
    case eFadeRequest::FR_ReplayPlayback:
        OTDU_UNIMPLEMENTED_ONCE; // TODO: Replay
        break;
    case eFadeRequest::FR_FadeIn:
        gpMngFade->restartTransition(
            ( gpMngFade->getTransitionFlags() & ~8u ) | 5, kFadeDuration );
        gpMngFade->setRenderLogo( false );
        sbStepStarted = true;
        break;
    case eFadeRequest::FR_FadeOut:
        gpMngFade->restartTransition(
            ( gpMngFade->getTransitionFlags() & ~0xcu ) | 1, kFadeDuration );
        gpMngFade->setRenderLogo( true );
        sbStepStarted = true;
        break;
    default:
        break;
    }

    OTDU_UNIMPLEMENTED; // TODO: replay manager
}

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
        PhysicsVehicle *pLocalVehicle =
            ( pLocalCar != nullptr ) ? pLocalCar->getPhysicsVehicle() : nullptr;
        if ( pLocalVehicle == nullptr ||
             gpMngWorld->getDisableStreamingOnCar() ) {
            if ( !gpMngWorld->getDisableStreamingOnCamera() &&
                 gpCamera != nullptr ) {
                const Eigen::Vector3f c =
                    gpCamera->getActiveCamera()->getWorldPosition();
                Eigen::Vector4f center( c.x(), c.y(), c.z(), 1.0f );
                gpWorld->setStreamingPosition( &center );
            }
        } else if ( pLocalVehicle->isActive() ) {
            Eigen::Vector4f carWorldPos = Eigen::Vector4f::Zero();
            {
                PhysicsLock physLock;
                carWorldPos =
                    pLocalVehicle->getPhysicsMatrix().row( 3 ).transpose();
            }
            if ( gpWorldStreaming != nullptr &&
                 gpWorldStreaming->isEnabled() ) {
                carWorldPos += gpWorldStreaming->getPosition();
            }
            gpWorld->setStreamingPosition( &carWorldPos );
        }
    }

    if ( gpMngMapViewer != nullptr &&
         gpMngMapViewer->getState() != MngMapViewer::MS_Idle ) {
        Eigen::Vector4f mapStreamPosition = gpMngMapViewer->getStreamFocus();
        gpWorld->setStreamingPosition( &mapStreamPosition );
    }

    Eigen::Vector4f streamShift = Eigen::Vector4f::Zero();
    if ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() ) {
        streamShift = gpWorldStreaming->getPosition();
    }

    struct AudioSampleScratch {
        float SmoothedTorque = 0.0f;
        uint32_t NumWheels = 0;
        bool bSampled = false;
    };
    std::array<AudioSampleScratch, kMaxNumVehicles> audioScratch = {};

    {
        PhysicsLock physLock;

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
            const uint32_t modelNumWheels =
                Min( pPhysicsVehicle->getNumWheels(), CarState::kMaxNumWheels );
            if ( modelNumWheels > 0 ) {
                const float bodyHalfHeight =
                    carState.body.BodyDimensions.y() * 0.5f;
                float wheelRestSum = 0.0f;
                for ( uint32_t w = 0; w < modelNumWheels; w++ ) {
                    wheelRestSum +=
                        carState.wheels[w].Position + bodyHalfHeight;
                }
                const float modelWheelRestY = wheelRestSum / modelNumWheels;
                pPhysicsVehicle->calibrateDisplayYOffset( modelWheelRestY );
                float authoredWheelY[CarState::kMaxNumWheels] = {};
                for ( uint32_t w = 0; w < modelNumWheels; w++ ) {
                    authoredWheelY[w] =
                        carState.wheels[w].Position + bodyHalfHeight;
                }
                pPhysicsVehicle->setAuthoredWheelHeights( authoredWheelY,
                                                          modelNumWheels );
            }

            const Matrix4x4f_t &physicsMatrix =
                pPhysicsVehicle->getPhysicsMatrix();
            body.PhysicsWorldMatrix = physicsMatrix;

            const Matrix4x4f_t shiftedMatrix =
                pPhysicsVehicle->computeDisplayMatrix( physicsMatrix );
            body.OffsetedWorldMatrix = shiftedMatrix;

            body.PreviousWorldMatrix = body.WorldMatrix;

            Matrix4x4f_t worldMatrix = shiftedMatrix;
            worldMatrix( 3, 0 ) += streamShift.x();
            worldMatrix( 3, 1 ) += streamShift.y();
            worldMatrix( 3, 2 ) += streamShift.z();
            body.WorldMatrix = worldMatrix;
            body.bBackOnTracked = pPhysicsVehicle->consumeBackOnTrackPlaced();
            if ( body.bBackOnTracked ) {
                body.PreviousWorldMatrix = body.WorldMatrix;
            }

            body.bIsInTunnel =
                carState.getSimulationType() ==
                    CarState::eSimulationType::CSST_Local &&
                carState.getOwner() == CarState::eOwner::CSO_LocalPlayer &&
                isOnTunnelRoad( carState );

            // FUN_0083bbd0 (inlined)
            if ( !pPhysicsVehicle->getAttachedParts().empty() &&
                 carState.getVehicleInstance() != nullptr ) {
                const Matrix4x4f_t bodyMatrix =
                    pPhysicsVehicle->getChassisShiftedMatrix() *
                    pPhysicsVehicle->getInvChassisMatrix() * physicsMatrix;
                RefreshVehiclePartsDisplay(
                    *pPhysicsVehicle, bodyMatrix,
                    carState.getVehicleInstance()->getNodeByIndex( 0 ) );
            }
            body.bHasBrokenPart = pPhysicsVehicle->hasBrokenPart();

            body.SpeedInKmh = pPhysicsVehicle->getReportedSpeed();

            const float publishedSteer = pPhysicsVehicle->getFilteredSteer();
            body.SteeringRaw = publishedSteer;

            const float leanScale = std::min(
                std::max( 1.0f - std::fabs( body.BikeLeanLateral ), 0.0f ),
                1.0f );
            body.Steering = leanScale * publishedSteer;
            body.Omega = pPhysicsVehicle->getEngineAngularVelocity();
            body.Gears = pPhysicsVehicle->getCurrentGear();
            body.GearState = pPhysicsVehicle->getCurrentGear() + 1;
            body.OdometerKms = pPhysicsVehicle->getOdometerKms();
            body.OdometerTenMeters = pPhysicsVehicle->getOdometerTenMeters();
            body.Distance = pPhysicsVehicle->getOdometerDistance();
            body.Torque = pPhysicsVehicle->getEngineTorqueOut();
            body.MaxOmega = pPhysicsVehicle->getEngineOmegaMax();

            const PhysicsVehicle::DrivingGearbox gearbox =
                pPhysicsVehicle->getDrivingGearbox();
            const bool bStopLampsArmed =
                !gearbox.bAutomatic ||
                ( pPhysicsVehicle->getAutoReverseState() != 0 &&
                  pPhysicsVehicle->getDrivingMode() ==
                      PhysicsVehicle::DM_Player );
            body.bIsBraking =
                bStopLampsArmed && pPhysicsVehicle->getInputBrake() > 0.1f;
            body.bIsReversing = pPhysicsVehicle->getCurrentGear() < 0;

            const uint32_t physNumWheels =
                Min( pPhysicsVehicle->getNumWheels(), CarState::kMaxNumWheels );

            int32_t numGrounded = 0;
            int32_t numOnRoad = 0;
            for ( uint32_t w = 0; w < physNumWheels; w++ ) {
                const PhysicsVehicle::WheelReadout ro =
                    pPhysicsVehicle->getWheelReadout( w );
                WheelState &wheel = carState.wheels[w];
                wheel.bInitialized = true;
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
                    Eigen::Vector4f( ro.ContactPoint.x(), ro.ContactPoint.y(),
                                     ro.ContactPoint.z(), 1.0f );

                const Eigen::Vector4f contactWorld =
                    ro.ContactPoint + GSPhysics::GetShiftOrigin();
                const float heightmapY = sampleHeightmapAt( contactWorld );
                wheel.bOnBridge = false;
                wheel.bInsideTunnel = false;
                if ( heightmapY <= kNoHeightmapSentinel ) {
                    wheel.bInsideTunnel = true;
                } else {
                    if ( heightmapY + kBridgeTunnelBand < contactWorld.y() ) {
                        wheel.bOnBridge = true;
                    }
                    if ( contactWorld.y() < heightmapY - kBridgeTunnelBand ) {
                        wheel.bInsideTunnel = true;
                    }
                }
                wheel.DistanceFromGround = contactWorld.y() - heightmapY;

                wheel.bOnRoad = ro.bGrounded &&
                                ( TDUSurface::isOnRoad( ro.ContactSurface ) ||
                                  gbGrassAsTarmac );
                if ( ro.bGrounded ) {
                    numGrounded++;
                    if ( wheel.bOnRoad ) {
                        numOnRoad++;
                    }
                }

                if ( ro.ContactMaterial != TDUMaterialValue::kDefault ) {
                    wheel.ContactMaterial =
                        static_cast<eMaterialType>( ro.ContactMaterial );
                    continue;
                }
                switch ( ro.ContactSurface ) {
                case TDUSurface::RoadMesh:
                case TDUSurface::RoadGround:
                    wheel.ContactMaterial = eMaterialType::MT_Asphalt;
                    break;
                case TDUSurface::Paved:
                case TDUSurface::Generic:
                    wheel.ContactMaterial = eMaterialType::MT_Concrete;
                    break;
                case TDUSurface::Terrain:
                default:
                    wheel.ContactMaterial = ( gbGrassAsTarmac )
                                                ? eMaterialType::MT_Asphalt
                                                : eMaterialType::MT_Grass;
                    break;
                }
            }
            body.NumWheelsGrounded = numGrounded;
            body.NumWheelsOnRoad = numOnRoad;

            if ( gpScenaric != nullptr &&
                 &carState == gpScenaric->getLocalPlayer().getCarState() &&
                 carState.getVehicleInstance() != nullptr ) {
                // FUN_0083bbd0 (inlined)
                const CameraVehicle *pCamera =
                    gpCamera->getActiveVehicleCamera();
                const uint32_t cameraFlags =
                    ( pCamera != nullptr ) ? pCamera->getFlags() : 0u;
                const bool bForceOpaque =
                    ( cameraFlags >> 0x12 & 1 ) != 0 ||
                    ( ( cameraFlags >> 0x13 & 1 ) != 0 &&
                      carState.getVehicleInstance()->getType() ==
                          VehicleBase::eType::VBT_Bike );
                float alpha = 1.0f;
                if ( pPhysicsVehicle->updatePhantomAlpha(
                         deltaTime, bForceOpaque, alpha ) ) {
                    carState.getVehicleInstance()->setAlpha( alpha );
                }
            }

            pPhysicsVehicle->flushCollisionEvents( body.CollisionEvents );
            for ( CollisionEvent &event : body.CollisionEvents ) {
                event.Location = carState.getWorldLocation();
            }

            carState.bInstanceAlive = true;

            audioScratch[i].SmoothedTorque =
                pPhysicsVehicle->getEngineSmoothedTorque();
            audioScratch[i].NumWheels = physNumWheels;
            audioScratch[i].bSampled = true;
        }
    }

    for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
        if ( !audioScratch[i].bSampled ) {
            continue;
        }
        CarState &carState = carStates[i];

        publishBodyAudioTelemetry( carState.body,
                                   audioScratch[i].SmoothedTorque );
        for ( uint32_t w = 0; w < audioScratch[i].NumWheels; w++ ) {
            publishWheelAudioTelemetry( carState.wheels[w] );
        }

        if ( gpVehicleSound != nullptr &&
             carState.getVehicleSound() == nullptr ) {
            carState.setVehicleSound(
                gpVehicleSound->createInstance( &carState ) );
        }
    }

    {
        PhysicsLock physLock;

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

    {
        PhysicsLock physLock;

        for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
            if ( carStates[i].getResource() != nullptr ) {
                carStates[i].resolveCollisionEvents();
            }
        }

        if ( gpPhysics != nullptr ) {
            RoadPhantomManager &phantoms = gpPhysics->getRoadPhantomManager();
            phantoms.dispatchEvents();
            phantoms.clearEvents();
        }
        TickReplayFadeSequencer();

        OTDU_UNIMPLEMENTED; // TODO: Rest of FUN_0081c330 (camera target
                            // position, GM actor 0x2ad flag, replay camera
                            // restore branch)
    }

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

    bool bLocalPlayerArrested = false;
    if ( gpScenaric != nullptr ) {
        CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
        if ( pLocalCar != nullptr ) {
            const int32_t instanceID = pLocalCar->carInstanceID;
            if ( instanceID >= 0 && policeStateJobs[instanceID] != nullptr ) {
                policeStateJobs[instanceID]->check( &carStates[instanceID],
                                                    deltaTime );
            }
            bLocalPlayerArrested = pLocalCar->getPoliceState().isArrested();
        }
    }

    if ( gpMngHud != nullptr ) {
        FlashPlayer *pHudPlayer = gpMngHud->getHudPlayer();
        if ( pHudPlayer != nullptr ) {
            const bool bMapAvailable =
                pHudPlayer->getVariableAsInt( "/:IsMapAvailable" ) != 0;
            if ( bMapAvailable == bLocalPlayerArrested ) {
                pHudPlayer->setVariableValue(
                    "/:IsMapAvailable",
                    static_cast<int32_t>( !bLocalPlayerArrested ) );
            }
        }
    }

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

CarState &GSCarState::grabCarState( const uint32_t slot ) {
    // FUN_0091d0f0 (inlined)
    OTDU_ASSERT( slot < kMaxNumVehicles );
    bUsedSlot |= ( 1u << slot );
    if ( slot + 1 > numCarStates ) {
        numCarStates = slot + 1;
    }
    return carStates[slot];
}

CarState *GSCarState::findCarState( const PhysicsVehicle *pVehicle ) {
    // FUN_00841dc0
    for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
        if ( isCarStateUsed( i ) &&
             carStates[i].getPhysicsVehicle() == pVehicle ) {
            return &carStates[i];
        }
    }
    return nullptr;
}

void GSCarState::registerCollidedObject( void *pObject ) {
    // FUN_008aff60 (inlined)
    for ( void *pEntry : pointerSet.Content ) {
        if ( pEntry == pObject ) {
            return;
        }
    }
    for ( void *&pEntry : pointerSet.Content ) {
        if ( pEntry == nullptr ) {
            pEntry = pObject;
            return;
        }
    }
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

void GSCarState::unloadVehicle( CarState *pCarState,
                                const bool bDeferred,
                                const bool bReleaseDriver ) {
    // FUN_008ad230
    if ( bDeferred ) {
        carToDeinstanciate.push( pCarState );
        return;
    }

    VehicleBaseResources *pResource = pCarState->getResource();
    releaseCarState( pCarState, bReleaseDriver );
    if ( pResource != nullptr ) {
        pResource->unload();
        releaseResourceEntry( pResource );
    }
}

void GSCarState::releaseCarState( CarState *param_2, bool param_3 ) {
    // FUN_008ad980
    VehicleBaseResources *pVVar2 = param_2->pResource;
    param_2->pResource = nullptr;
    EntityDriver *pDriver = param_2->getDriver();
    if ( pDriver != nullptr ) {
        param_2->unloadDriver();

        if ( param_3 ) {
            pDriver->removeInstanceReference();
            pDriver->setEnvMap( false, nullptr );
            if ( gpScenaric != nullptr ) {
                gpScenaric->getAvatarManager().releaseAvatar( pDriver );
            }
        }
    }

    if ( param_2->field_0x39cc != nullptr ) {
        gpAI->getAIPool()->removeVehicle(
            static_cast<AiVehicle *>( param_2->field_0x39cc ),
            "CarState uninstanciation", false );
        param_2->field_0x39cc = nullptr;
    }

    if ( param_2->pPhysicsVehicle != nullptr ) {
        gpPhysics->destroyVehicle( param_2->pPhysicsVehicle );
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
