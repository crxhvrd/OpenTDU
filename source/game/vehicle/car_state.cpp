#include "shared.h"
#include "car_state.h"

#include "vehicle_base.h"
#include "core/maths_helper.h"

#include "gs_car_display.h"
#include "gs_car_state.h"
#include "game/game_object.h"
#include "physics/gs_physics.h"
#include "vehicle_car.h"
#include "database/gs_database.h"
#include "game/avatar/avatar_clothes.h"
#include "game/scenaric/entity/entity_driver.h"
#include "physics/vehicle/physics_vehicle.h"
#include "ai/ai_vehicle.h"
#include "ai/ai_veh_behaviour.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/game_intro_tutorial.h"
#include "game/gm_world.h"
#include "game/mng_gps.h"
#include "ai/gs_ai.h"
#include "ai/ai_road_shape.h"
#include "ai/ai_config.h"
#include "gs_timer.h"
#include "player_data/gs_playerdata.h"
#include "physics/gs_physicsfx.h"
#include "player_data/saved_road_discovery.h"
#include "render/camera/gs_camera.h"
#include "render/camera/camera_vehicle.h"
#include "system/ed_sys.h"
#include "tdu_instance.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/weather/gs_weather.h"

static float FLOAT_00fab4d0 = 0.5f; // DAT_00fab4d0

// Grass/dirt threshold ratio
static float DAT_00f4174c = 1.1f; // DAT_00f4174c

CarState::CarState()
    : GameObject(),
      name( "UNKNOWN" ),
      owner( CarState::eOwner::CSO_Bot ),
      carInstanceID( 0xffffffff ),
      numWheels( 0 ),
      carID( 0xffffffff ),
      tireID( 0xffffffff ),
      rimID( 0xffffffff ),
      colorID( 0xffffffff ),
      pVehicleBase( nullptr ),
      pVehicleSound( nullptr ),
      pDriver( nullptr ),
      pPassenger( nullptr ),
      pCargo( nullptr ),
      field_0x39cc( nullptr ),
      field_0x39d4( nullptr ),
      field_0x39d8( nullptr ),
      aiLockFlags( 0 ),
      collisionCallbacks{},
      field_0x39fc( nullptr ),
      bInstanceAlive( false ),
      bIsIndoor( false ),
      bUseShowcaseControl( false ),
      packHashcode( 0 ),
      pResource( nullptr ),
      pPhysicsVehicle( nullptr ) {
    // FUN_00414270
    body.reset();
    for ( WheelState &wheelState : wheels ) {
        wheelState.reset();
    }
    reset();
}

CarState::~CarState() {}

int32_t CarState::getType() {
    // FUN_0047a8a0
    return 0;
}

Matrix4x4f_t &CarState::getWorldMatrix() {
    // FUN_008ae5b0
    return body.WorldMatrix;
}

void CarState::setPhysicsVehicle( PhysicsVehicle *pVehicle ) {
    // FUN_0084a5d0 (inlined)
    pPhysicsVehicle = pVehicle;
    if ( pVehicle != nullptr ) {
        numWheels = pVehicle->getNumWheels();
        const uint32_t numFilledWheels = Min( numWheels, kMaxNumWheels );

        std::array<PhysicsVehicle::WheelReadout, kMaxNumWheels> readouts;
        for ( uint32_t i = 0; i < numFilledWheels; i++ ) {
            readouts[i] = pVehicle->getWheelReadout( i );
        }

        const Eigen::Vector3f &box = pVehicle->getBodyDimensions();
        body.BodyDimensions =
            Eigen::Vector4f( box.x(), pVehicle->getBodyHeight(), box.z(),
                             readouts[0].LocalPosition.z() );
        if ( numFilledWheels >= 4 ) {
            const uint32_t rearWheel = 1u << ( numFilledWheels / 2 - 1 );
            body.WheelBase = readouts[0].LocalPosition.z() -
                             readouts[rearWheel].LocalPosition.z();
            body.TrackWidth = std::fabs( readouts[1].LocalPosition.x() -
                                         readouts[0].LocalPosition.x() );
            body.TrackLength =
                std::fabs( readouts[rearWheel + 1].LocalPosition.x() -
                           readouts[rearWheel].LocalPosition.x() );
        }

        for ( uint32_t i = 0; i < numFilledWheels; i++ ) {
            const PhysicsVehicle::WheelReadout &readout = readouts[i];
            WheelState &wheel = wheels[i];
            wheel.WheelIndex = static_cast<int32_t>( i );
            wheel.bInitialized = true;
            wheel.WheelRadius = readout.Radius;
            wheel.Position = readout.ReleasedPosition;
            wheel.RelativeWorldPosition = Eigen::Vector4f(
                readout.LocalPosition.x(), readout.LocalPosition.y(),
                readout.LocalPosition.z(), 1.0f );
        }
        gGameObjectDico.registerObject( this );
        gpPhysics->setVehicleOwner( pVehicle,
                                    static_cast<GameObject *>( this ) );
    }
}

void CarState::resetBackOnTrack() {
    // FUN_008ae5c0
    body.resetBackOnTrack();
    if ( pPhysicsVehicle != nullptr ) {
        pPhysicsVehicle->setBackOnTrackForced( false );
        pPhysicsVehicle->setRespawnForced( false );
    }
}

void CarState::reset() {
    // FUN_008af040
    policeState.reset();
    collisionCallbacks[0].pCallback = nullptr;
    collisionCallbacks[1].pCallback = nullptr;
    body.reset();
    for ( WheelState &wheelState : wheels ) {
        wheelState.reset();
    }
    carID = -1;
    tireID = -1;
    rimID = -1;
    owner = eOwner::CSO_NetPlayer;
    simulationType = eSimulationType::CSST_Local;
    activeMode = eActiveMode::CSAM_Paused;
    numWheels = 0;
    bInstanceAlive = false;
    bUseShowcaseControl = false;

    pPhysicsVehicle = nullptr;
    pVehicleBase = nullptr;
    pVehicleSound = nullptr;
    field_0x39cc = nullptr;
    field_0x39fc = nullptr;
    field_0x39d4 = nullptr;
    aiLockFlags = 0;

    name = "UNKNOWN";
}

void CarState::setInstanceID( const int32_t index ) { carInstanceID = index; }

Eigen::Vector4f CarState::getWorldPosition() {
    // FUN_008af030
    return body.WorldMatrix.row( 3 );
}

void CarState::retrieveBoundingSphere( Eigen::Vector4f *pOutSpherePosition,
                                       float *pOutSphereRadius ) {
    // FUN_008af7d0
    *pOutSphereRadius = body.BodyDimensions.norm() * 0.5f;
    *pOutSpherePosition = body.WorldMatrix.row( 3 );
}

Eigen::Vector4f &CarState::getLinearVelocity() {
    // FUN_008ae5a0
    return body.LinearVelocity;
}

Eigen::Vector4f &CarState::getDimensions() {
    // FUN_008ae590
    return body.BodyDimensions;
}

void CarState::retrieveBoundingBox( Matrix4x4f_t &pOutBoundingBox ) {
    // FUN_008af950
    Eigen::Vector3f dimensions = body.BodyDimensions.head<3>();
    pOutBoundingBox = MakeScaleMatrix( dimensions );

    pOutBoundingBox.row( 3 ) +=
        Eigen::Vector4f( 0.0f, body.BodyDimensions.y() * 0.5f, 0.0f, 0.0f );

    pOutBoundingBox = pOutBoundingBox * body.WorldMatrix;
}

void CarState::retrieveAcceleration( Eigen::Vector4f &param_1 ) {
    // FUN_008afa00
    Eigen::Vector4f local_20 =
        TDUMat4x4MulVec( body.WorldMatrix, body.AccelDiff );
    param_1 = local_20 * 9.81f;
}

bool CarState::isIndoor() const { return bIsIndoor; }

bool CarState::isReversing() const { return body.bIsReversing; }

bool CarState::isBraking() const { return body.bIsBraking; }

bool CarState::isIndicatingLeft() const { return body.bIndicatingLeft; }

bool CarState::isIndicatingRight() const { return body.bIndicatingRight; }

bool CarState::headlightsOn() const { return body.bHeadlightsOn; }

bool CarState::hazardLightsOn() const { return body.bHazardLightsOn; }

static constexpr float kBackOnTrackCooldown = 5.0f;       // DAT_00f41750
static constexpr float kBackOnTrackMaxSpeed = 13.888889f; // DAT_00f42130
static constexpr float kZeroLength = 0.01f;               // DAT_00f416d4

static void PushWaterSplash( const PhysicsVehicle &vehicle ) {
    // FUN_007ca480
    if ( gpPhysicsFX == nullptr || !gpPhysicsFX->isReady() ) {
        return;
    }
    const Matrix4x4f_t world = vehicle.getChassisWorldMatrix();
    const Eigen::Vector4f up = world.row( 1 ).transpose();

    PhysicsFXEvent event;
    event.FXMaterialA = GSPhysicsFX::RemapMaterial(
        static_cast<int32_t>( eMaterialType::MT_Tires ) );
    event.FXMaterialB = GSPhysicsFX::RemapMaterial(
        static_cast<int32_t>( eMaterialType::MT_Water ) );
    event.pBodyA = vehicle.getChassisBody();
    event.MassA = vehicle.getVehicleMass();
    event.MassB = vehicle.getVehicleMass();
    event.VelocityA = Eigen::Vector4f::Zero();
    event.VelocityB = Eigen::Vector4f::Zero();
    event.Kind = PFXEK_Impact;
    event.Energy = vehicle.getChassisVelocity().norm(); // FUN_00829b20
    event.ContactPosition = world.row( 3 ).transpose() - up;
    event.ContactPosition.w() = 1.0f;
    event.Normal = up;
    gpPhysicsFX->getEventQueue().push( event );
}

static void PlaceAtBackOnTrackPose( CarBodyState &body,
                                    PhysicsVehicle &vehicle ) {
    // FUN_008b0b10
    vehicle.resetToTransform( body.BackOnTrackWorldMatrix, true );
    vehicle.getReplayPlayback().bTeleported = true;
    vehicle.requestRollLimitRestore();
    if ( body.BackOnTrackVelocity.norm() > 1.0f ) { // FLOAT_00f4165c
        vehicle.setVelocity( body.BackOnTrackVelocity.head<3>(), true, -1 );
        vehicle.setEngineState( eEngineState::ES_Running );
    }
    if ( vehicle.getPhantomMode() != 5 ) {
        vehicle.requestPhantomMode( 2, 1.0f );
    }
    body.BackOnTrackState = 0;
    body.BackOnTrackTimer = kBackOnTrackCooldown;
}

void CarState::computeBackOnTrackPose( const int32_t mode ) {
    // FUN_008b0230
    static constexpr float kRoadEndMargin = 10.0f; // FLOAT_00f4166c
    static constexpr float kLaneMargin = 2.5f;     // DAT_00f416c0
    static constexpr float kRespawnSpeed = 10.0f;

    // Secret Island respawn box and point
    static constexpr float kIslandMinX = 63360.0f;
    static constexpr float kIslandMaxX = 65280.0f;
    static constexpr float kIslandMinZ = 43776.0f;
    static constexpr float kIslandMaxZ = 45312.0f;
    static const Eigen::Vector4f kIslandRespawn( 64426.0f, 4.0f, 44637.0f,
                                                 1.0f );

    PhysicsVehicle *pVehicle = pPhysicsVehicle;
    if ( pVehicle == nullptr ) {
        return;
    }
    OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_00853340 (P2P sync reset)

    Matrix4x4f_t matrix = body.WorldMatrix;
    const Eigen::Vector4f worldPosition = body.WorldMatrix.row( 3 ).transpose();
    Eigen::Vector4f position = worldPosition;
    Eigen::Vector4f velocity = Eigen::Vector4f::Zero();

    const auto computeOnRoad = [&]() {
        uint32_t roadID = static_cast<uint32_t>( body.ActiveRoadID );
        if ( body.ActiveRoadID < 1 ||
             ( gpActiveGameMode->getCurrentGameMode() != GM_Mission &&
               !IsRoadAvailable( roadID ) ) ) {
            const AiRoadMap::Road *pNearest =
                gpAI->getRoadMap().findNearestRoad( position, false );
            roadID = ( pNearest != nullptr ) ? pNearest->ID : 0;
            body.PositionOnRoad = position;
            body.PrevPositionOnRoad = position;
            if ( roadID < 1 ) {
                matrix = Matrix4x4f_t::Identity();
                position = worldPosition;
                return;
            }
        }

        AiRoadMap::Road *pRoad = gpAI->getRoadMap().getRoad( roadID );
        AiRoadShape *pShape = pRoad->getShape();
        const float fractionMargin = kRoadEndMargin / pShape->TotalLength;

        Eigen::Vector4f travel = body.PositionOnRoad - body.PrevPositionOnRoad;
        if ( body.PrevPositionOnRoad.norm() < kZeroLength ) {
            travel = body.WorldMatrix.row( 2 ).transpose();
        }

        Matrix4x4f_t frame = Matrix4x4f_t::Identity();
        frame.row( 2 ) = travel.transpose();
        frame.row( 3 ) = body.PositionOnRoad.transpose();
        AiRoadIt it;
        ProjectOnRoadShape( frame, it, nullptr, false, pShape );
        const float fraction = Clamp( pShape->computeRoadFraction( it ),
                                      fractionMargin, 1.0f - fractionMargin );
        pShape->computeIt( fraction, it );

        float lateral =
            1.0f - kLaneMargin / it.getEntity()->pProfile->InnerWidth;
        const Eigen::Vector4f tangent = it.computeDirection();
        Eigen::Vector4f forward = tangent;
        bool bGpsDirection = false;
        const auto reverse = [&]() {
            lateral = -lateral;
            forward = tangent * -1.0f;
        };
        if ( mode == 0 ) {
            Eigen::Vector4f gpsDirection;
            bGpsDirection =
                gpMngGPS != nullptr && gpMngGPS->computeRouteDirectionOnRoad(
                                           roadID, position, gpsDirection );
            const Eigen::Vector4f &reference =
                bGpsDirection ? gpsDirection : travel;
            if ( tangent.dot( reference ) < 0.0f ) {
                reverse();
            }
        } else if ( mode == 2 ) {
            reverse();
        }
        position = it.computeWidthPosition( lateral );

        const Eigen::Vector4f up( 0.0f, 1.0f, 0.0f, 0.0f );
        const Eigen::Vector4f side = Vector4DCross( up, forward ).normalized();
        const Eigen::Vector4f newUp =
            Vector4DCross( forward, side ).normalized();
        forward = Vector4DCross( side, newUp ).normalized();
        if ( bGpsDirection || mode != 0 ) {
            velocity = forward * kRespawnSpeed;
        }
        matrix.row( 0 ) = side.transpose();
        matrix.row( 1 ) = newUp.transpose();
        matrix.row( 2 ) = forward.transpose();
    };

    CameraVehicle *pCamera = gpCamera->getActiveVehicleCamera();
    if ( !gEdSys.bWindowActivated ) {
    } else if ( pCamera != nullptr && pCamera->getType() == 1 &&
                !gpCamera->isInReplayMode() ) {
        // Debug mouse camera: drop the car 2 m along Z from the camera
        const Eigen::Vector3f cameraPosition = pCamera->getWorldPosition();
        matrix = Matrix4x4f_t::Identity();
        position = Eigen::Vector4f( cameraPosition.x(), cameraPosition.y(),
                                    cameraPosition.z() + 2.0f, 1.0f );
    } else if ( kIslandMinX < position.x() && position.x() < kIslandMaxX &&
                kIslandMinZ < position.z() && position.z() < kIslandMaxZ ) {
        matrix = Matrix4x4f_t::Identity();
        position = kIslandRespawn;
        body.PositionOnRoad = worldPosition;
        body.PrevPositionOnRoad = worldPosition;
    } else {
        computeOnRoad();
    }

    matrix.row( 3 ) = position.transpose();
    matrix( 3, 3 ) = 1.0f;
    if ( pVehicle->isRespawnForced() ) {
        velocity = Eigen::Vector4f::Zero();
    }
    pVehicle->setBackOnTrackForced( false );
    pVehicle->setRespawnForced( false );
    body.BackOnTrackState = 1;
    body.BackOnTrackWorldMatrix = matrix;
    body.BackOnTrackVelocity = velocity;
    pVehicle->setBackOnTrackPlaced( true );
    body.bBackOnTrackRequest = false;
}

void CarState::updateBackOnTrack( float deltaTime ) {
    // FUN_008b0b10
    PhysicsVehicle *pVehicle = pPhysicsVehicle;
    if ( pVehicle == nullptr || !pVehicle->isActive() ) {
        return;
    }

    if ( worldLocation.LocationType == ObjectWorldLocation::OWL_Road ) {
        static constexpr float kReanchorDistance = 20.0f; // DAT_00f03ee0
        const Eigen::Vector4f position = body.WorldMatrix.row( 3 ).transpose();
        if ( kReanchorDistance < ( position - body.PositionOnRoad ).norm() ||
             body.PositionOnRoad.norm() < kZeroLength ) {
            body.PrevPositionOnRoad =
                ( body.PositionOnRoad.norm() < kZeroLength )
                    ? position
                    : body.PositionOnRoad;
            body.PositionOnRoad = position;
            body.RoadX = ( worldLocation.LocationRoad.FractionMin +
                           worldLocation.LocationRoad.FractionMax ) *
                         0.5f;
            body.ActiveRoadID =
                static_cast<int32_t>( worldLocation.LocationRoad.RoadID );
            body.RoadZ = ( worldLocation.LocationRoad.LateralMin +
                           worldLocation.LocationRoad.LateralMax ) *
                         0.5f;
        }
    }

    const bool bLocal = FUN_00406ce0();
    switch ( body.BackOnTrackState ) {
    case 0: {
        bool bInWater = false;
        if ( bLocal && !pVehicle->isKeyframed() &&
             !pVehicle->isPhysicsPaused() && gbBackOnTrackFadeGuard ) {
            bInWater = pVehicle->isInWater();
            if ( GSPhysics::peekFadeRequest() != eFadeRequest::FR_Idle ) {
                GSPhysics::pushFadeRequest( eFadeRequest::FR_FadeIn );
            }
        }

        const uint8_t drivingMode = pVehicle->getDrivingMode();
        bool bAllowed = drivingMode != PhysicsVehicle::DM_Parked &&
                        drivingMode != PhysicsVehicle::DM_SlowDown &&
                        !pVehicle->isPhysicsPaused() &&
                        !pVehicle->isKeyframed() &&
                        !( gpActiveGM != nullptr && gpActiveGM->isPaused() );
        if ( pVehicle->isBackOnTrackForced() || pVehicle->isRespawnForced() ) {
            bAllowed = true;
        }
        if ( hasAILockFlags() ) {
            bAllowed = false;
        }
        if ( !gpCamera->isInReplayMode() && bLocal && body.bHasBikeAvatar ) {
            pVehicle->setBikeCrashRecovery( true );
        }
        if ( !bAllowed ) {
            return;
        }

        if ( 0.0f < body.BackOnTrackTimer ) {
            body.BackOnTrackTimer -= deltaTime;
        }
        const bool bRequested = ( body.bBackOnTrackRequest &&
                                  body.SpeedInKmh < kBackOnTrackMaxSpeed &&
                                  body.BackOnTrackTimer < 0.0f ) ||
                                bInWater || pVehicle->isBackOnTrackForced();
        if ( !bRequested && !pVehicle->isRespawnForced() &&
             !pVehicle->isBikeCrashRecoveryPending() ) {
            return;
        }

        body.BackOnTrackState = 0;
        computeBackOnTrackPose( 0 );
        body.bBackOnTrackRequest = false;
        if ( bInWater ) {
            PushWaterSplash( *pVehicle );
        }
    } break;
    case 1: {
        const Eigen::Vector4f target =
            body.BackOnTrackWorldMatrix.row( 3 ).transpose();
        const bool bPlaceNow =
            ( !bLocal || ( gpWorld->isStreamedAround( &target, true, false ) &&
                           !pVehicle->isInWater() ) ) &&
            !pVehicle->isBikeCrashRecoveryPending();
        if ( bPlaceNow ) {
            PlaceAtBackOnTrackPose( body, *pVehicle );
        } else {
            if ( gpActiveGM != nullptr ) {
                gpActiveGM->setBlockPauseByStart( true );
            }
            GSPhysics::pushFadeRequest( eFadeRequest::FR_FadeOut );
            body.BackOnTrackState = 2;
            if ( !pVehicle->isBikeCrashRecoveryPending() ) {
                pVehicle->setPostponedPause( 1 );
            }
        }
        CameraVehicle *pCamera = gpCamera->getActiveVehicleCamera();
        if ( pCamera != nullptr && ( pCamera->getFlags() >> 0x11 & 1 ) == 0 ) {
            pCamera->resetTransform();
        }
    } break;
    case 2:
        if ( GSPhysics::peekFadeRequest() == eFadeRequest::FR_Idle ) {
            body.BackOnTrackState = 3;
            pVehicle->resetToTransform( body.BackOnTrackWorldMatrix, false );
            pVehicle->requestRollLimitRestore();
        }
        break;
    case 3: {
        const Eigen::Vector4f target =
            body.BackOnTrackWorldMatrix.row( 3 ).transpose();
        if ( ( !pVehicle->isBikeCrashRecoveryPending() ||
               !body.bHasBikeAvatar ) &&
             gpWorld->isStreamedAround( &target, true, true ) ) {
            pVehicle->setBikeCrashRecovery( false );
            GSPhysics::pushFadeRequest( eFadeRequest::FR_FadeIn );
            pVehicle->setPostponedPause( 2 );
            PlaceAtBackOnTrackPose( body, *pVehicle );
            if ( gpActiveGM != nullptr ) {
                gpActiveGM->setPauseMenuLocked( false );
            }
        }
    } break;
    default:
        break;
    }
}

static constexpr float kMinBlameSpeed = 0.5f;        // FLOAT_00f41674
static constexpr float kTwoOverPi = 0.63661975f;     // DAT_00f41a30
static constexpr float kLaneBlameWeight = 0.33f;     // DAT_00f41a0c
static constexpr float kOverspeedBlameWeight = 0.1f; // FLOAT_00f41668
static constexpr float kSpeedLimitScale = 1.0f;      // DAT_00faa990
static constexpr float kContactBoundsScale = 1.5f;   // DAT_00f416b8
static constexpr float kAtFaultBlameRatio = 0.5f;    // FLOAT_00f41674

static void DispatchAICollision( const CollisionEvent &event,
                                 CarState *pCarState,
                                 CarState *pOther ) {
    // FUN_008af6f0
    AiVehicle *pAiVehicle =
        static_cast<AiVehicle *>( pCarState->getAIController() );
    if ( pAiVehicle == nullptr ) {
        return;
    }

    // FUN_00923b30
    AiVehicle *pOtherAiVehicle =
        static_cast<AiVehicle *>( pOther->getAIController() );
    if ( pAiVehicle->getBehaviour() != nullptr ) {
        pAiVehicle->getBehaviour()->onCollision( pCarState, pOther, event );
    }
    if ( pOtherAiVehicle != nullptr &&
         pOtherAiVehicle->getBehaviour() != nullptr ) {
        pOtherAiVehicle->getBehaviour()->onCollision( pCarState, pOther,
                                                      event );
    }
}

struct PostponedVelocityScale {
    std::vector<PhysicsVehicle *> pScaled;

    void request( PhysicsVehicle *pVehicle, const float scale ) {
        // FUN_00829bb0
        if ( pVehicle == nullptr || std::find( pScaled.begin(), pScaled.end(),
                                               pVehicle ) != pScaled.end() ) {
            return;
        }
        pScaled.push_back( pVehicle );
        const Eigen::Vector4f velocity = pVehicle->getChassisVelocity() * scale;
        pVehicle->setVelocity( velocity.head<3>(), false, -1 );
    }
};

static float ComputeImpactAngleFactor( const CollisionEvent &event ) {
    // FUN_008af840
    const float speed = std::sqrt( event.Velocity.dot( event.Velocity ) );
    if ( kMinBlameSpeed > speed ) {
        return 0.0f;
    }

    Eigen::Vector2f contact( event.ContactPosition.x(),
                             event.ContactPosition.z() );
    Eigen::Vector2f velocity( event.Velocity.x(), event.Velocity.z() );
    if ( contact.squaredNorm() == 0.0f || velocity.squaredNorm() == 0.0f ) {
        return 0.0f;
    }

    contact.normalize();
    velocity.normalize();

    const float cosAngle = std::clamp( velocity.dot( contact ), -1.0f, 1.0f );
    const float factor = 1.0f - std::acos( cosAngle ) * kTwoOverPi;
    return ( 0.0f > factor ) ? 0.0f : factor;
}

static void ComputeRoadBehaviourScore( const CollisionEvent &event,
                                       int32_t &outLaneScore,
                                       float &outOverspeed ) {
    // FUN_008afae0
    const ObjectLocationRoad &location = event.Location.LocationRoad;
    const float speed = std::sqrt( event.Velocity.dot( event.Velocity ) );
    const float lateral = ( location.LateralMax + location.LateralMin ) * 0.5f;
    const float fraction =
        ( location.FractionMax + location.FractionMin ) * 0.5f;

    const AiRoadMap &roadMap = gpAI->getRoadMap();
    AiRoadMap::Road *pRoad = roadMap.getRoad( location.RoadID );
    AiRoadShape *pShape = ( pRoad != nullptr ) ? pRoad->getShape() : nullptr;

    if ( pShape == nullptr ) {
        return;
    }
    AiRoadIt it;
    pShape->computeIt( fraction, it );
    if ( it.pRoad == nullptr ) {
        return;
    }

    const AiRoadMap::Entity *pEntity = it.getEntity();
    int32_t speedLimitIndex = pEntity->SpeedLimitIndice;
    if ( speedLimitIndex >= roadMap.getNumSpeedLimits() ) {
        speedLimitIndex = 0;
    }
    outOverspeed =
        speed - roadMap.getSpeedLimit( speedLimitIndex ) * kSpeedLimitScale;
    if ( 0.0f > outOverspeed ) {
        outOverspeed = 0.0f;
    }

    if ( kMinBlameSpeed > speed ) {
        outLaneScore = 0;
        return;
    }

    const float centerLine = pEntity->pProfile->computeCenterLineRatio();
    const float direction = event.Location.VelocityX;
    if ( direction > 0.0f ) {
        if ( centerLine > location.LateralMin ) {
            if ( centerLine <= lateral ) {
                outLaneScore = 1;
            } else {
                outLaneScore = ( centerLine > location.LateralMax ) ? 3 : 2;
            }
        }
    } else if ( 0.0f > direction ) {
        if ( location.LateralMax > centerLine ) {
            if ( lateral <= centerLine ) {
                outLaneScore = 1;
            } else {
                outLaneScore = ( location.LateralMin > centerLine ) ? 3 : 2;
            }
        }
    } else if ( centerLine > location.LateralMin &&
                location.LateralMax > centerLine ) {
        outLaneScore = 1;
    }
}

static bool ComputeLocationBlame( const CollisionEvent &event,
                                  const CarState *pCarState,
                                  int32_t &outLaneScore,
                                  float &outOverspeed,
                                  float &outJunction ) {
    // FUN_008afcb0 (inlined)
    switch ( event.Location.LocationType ) {
    case ObjectWorldLocation::OWL_Road:
        ComputeRoadBehaviourScore( event, outLaneScore, outOverspeed );
        return true;
    case ObjectWorldLocation::OWL_Junction:
        if ( uint32_t(
                 pCarState->getPoliceState().getJunctionNotRespected() ) ==
             event.Location.LocationOther.RoadID ) {
            outJunction = 1.0f;
        }
        return true;
    case ObjectWorldLocation::OWL_OffRoad:
        return true;
    default:
        return false;
    }
}

static void ComputeCollisionBlame( const CollisionEvent &event,
                                   const CarState *pCarState,
                                   const CarState *pOther,
                                   const CollisionEvent &otherEvent,
                                   float &outBlame,
                                   float &outOtherBlame ) {
    // FUN_008afcb0
    int32_t laneScore = 0;
    int32_t otherLaneScore = 0;
    float overspeed = 0.0f;
    float otherOverspeed = 0.0f;
    float junction = 0.0f;
    float otherJunction = 0.0f;
    const float angle = ComputeImpactAngleFactor( event );
    const float otherAngle = ComputeImpactAngleFactor( otherEvent );
    if ( !ComputeLocationBlame( event, pCarState, laneScore, overspeed,
                                junction ) ||
         !ComputeLocationBlame( otherEvent, pOther, otherLaneScore,
                                otherOverspeed, otherJunction ) ) {
        outBlame = 0.0f;
        outOtherBlame = 0.0f;
        return;
    }

    outBlame = float( laneScore ) * kLaneBlameWeight +
               overspeed * kOverspeedBlameWeight + junction;
    outOtherBlame = float( otherLaneScore ) * kLaneBlameWeight +
                    otherOverspeed * kOverspeedBlameWeight + otherJunction;

    bool bNormalize = false;
    if ( outBlame == outOtherBlame && outBlame == 0.0f ) {
        if ( angle > otherAngle ) {
            outBlame = 1.0f;
            bNormalize = true;
        } else if ( otherAngle > angle ) {
            outOtherBlame = 1.0f;
        } else {
            outBlame = angle;
            outOtherBlame = otherAngle;
        }
    } else {
        outBlame *= angle;
        outOtherBlame *= otherAngle;
    }

    if ( bNormalize || outBlame != 0.0f ) {
        outBlame = outBlame / ( outBlame + outOtherBlame );
        outOtherBlame = outOtherBlame / ( outBlame + outOtherBlame );
    } else if ( outOtherBlame != 0.0f ) {
        outOtherBlame = 1.0f;
    }

    const float now = gpTimer->GameTotalTime;
    if ( now - pCarState->getPoliceState().getWasHitLastTime() <
         gAIConfig.InfGracePeriod ) {
        outBlame = 0.0f;
    }
    if ( now - pOther->getPoliceState().getWasHitLastTime() <
         gAIConfig.InfGracePeriod ) {
        outOtherBlame = 0.0f;
    }
}

static bool IsContactOutsideBounds( const CollisionEvent &event,
                                    CarState *pCarState ) {
    // FUN_008af770
    const float distance =
        std::sqrt( event.ContactPosition.dot( event.ContactPosition ) );
    Eigen::Vector4f spherePosition;
    float sphereRadius = 0.0f;
    pCarState->retrieveBoundingSphere( &spherePosition, &sphereRadius );
    return distance > sphereRadius * kContactBoundsScale;
}

void CarState::resolveCollisionEvents() {
    // FUN_008aff60
    PostponedVelocityScale softCollision;
    const int32_t numEvents = int32_t( body.CollisionEvents.size() );
    for ( int32_t i = 0; i < numEvents; i++ ) {
        const CollisionEvent &event = body.CollisionEvents[i];
        void *pOtherObject = nullptr;
        if ( event.Type == eCollisionType::CT_Vehicle ) {
            if ( event.pOtherVehicle == nullptr ) {
                continue;
            }
        } else if ( event.Type == eCollisionType::CT_DynamicBody &&
                    event.pOtherObject != nullptr ) {
            pOtherObject = event.pOtherObject;
            gpCarState->registerCollidedObject( pOtherObject );
        } else {
            continue;
        }

        CarState *pOther = nullptr;
        if ( event.Type == eCollisionType::CT_Vehicle ) {
            pOther = gpCarState->findCarState( event.pOtherVehicle );
            if ( pOther != nullptr ) {
                DispatchAICollision( event, this, pOther );
            }
        }

        if ( gpScenaric == nullptr ||
             this != gpScenaric->getLocalPlayer().getCarState() ) {
            continue;
        }

        const AiVehicle *pOtherAiVehicle =
            ( pOther != nullptr )
                ? static_cast<const AiVehicle *>( pOther->getAIController() )
                : nullptr;
        if ( pOtherAiVehicle != nullptr &&
             pOtherAiVehicle->isSoftCollision() ) {
            softCollision.request( pPhysicsVehicle, 0.5f );
            softCollision.request( pOther->getPhysicsVehicle(), 0.5f );
        }

        if ( gAIConfig.InfGracePeriod >
             gpTimer->GameTotalTime - gpPlayerData->getLastArrestedTime() ) {
            continue;
        }

        float energy = event.Energy;
        bool bHandled = false;
        for ( int32_t j = 0; j < numEvents; j++ ) {
            const CollisionEvent &sibling = body.CollisionEvents[j];
            if ( j == i || sibling.pOtherObject != event.pOtherObject ) {
                continue;
            }
            if ( j < i ) {
                bHandled = true;
                break;
            }
            energy += sibling.Energy;
        }
        if ( bHandled || energy == 0.0f ) {
            continue;
        }

        bool bAtFault = true;
        if ( pOther != nullptr ) {
            if ( policeState.isRecentCarCollision( pOther ) ) {
                continue;
            }

            const CollisionEvent *pOtherEvent = nullptr;
            for ( const CollisionEvent &otherEvent :
                  pOther->body.CollisionEvents ) {
                if ( otherEvent.EventID == event.EventID ) {
                    pOtherEvent = &otherEvent;
                    break;
                }
            }
            if ( pOtherEvent == nullptr ||
                 IsContactOutsideBounds( *pOtherEvent, pOther ) ) {
                continue;
            }

            float blame = 0.0f;
            float otherBlame = 0.0f;
            ComputeCollisionBlame( event, this, pOther, *pOtherEvent, blame,
                                   otherBlame );
            bAtFault = blame > otherBlame * kAtFaultBlameRatio;
        }

        const bool bRecent =
            policeState.checkRecentCollision( pOther, pOtherObject );
        if ( !bAtFault || bRecent ) {
            continue;
        }

        for ( const CollisionCallback &callback : collisionCallbacks ) {
            if ( callback.pCallback != nullptr ) {
                callback.pCallback( uint8_t( event.Type ), pOther, pOtherObject,
                                    callback.pUserData );
            }
        }
    }
}

void CarState::addCollisionCallback( CollisionCallback_t pCallback,
                                     void *pUserData ) {
    // FUN_008ae530
    for ( CollisionCallback &slot : collisionCallbacks ) {
        if ( slot.pCallback == nullptr ) {
            slot.pCallback = pCallback;
            slot.pUserData = pUserData;
            return;
        }
    }
    for ( CollisionCallback &slot : collisionCallbacks ) {
        if ( slot.pCallback == pCallback ) {
            slot.pUserData = pUserData;
            return;
        }
    }
}

void CarState::removeCollisionCallback( CollisionCallback_t pCallback ) {
    // FUN_008ae4c0
    for ( CollisionCallback &slot : collisionCallbacks ) {
        if ( slot.pCallback == pCallback ) {
            slot.pCallback = nullptr;
            return;
        }
    }
}

bool CarState::hasCollisionCallback( CollisionCallback_t pCallback,
                                     void *pUserData ) const {
    // FUN_008ae4f0
    for ( const CollisionCallback &slot : collisionCallbacks ) {
        if ( slot.pCallback == pCallback ) {
            return pUserData == nullptr || slot.pUserData == pUserData;
        }
    }
    return false;
}

void CarState::updateGroundColor() {
    // FUN_008af2c0
    VehicleBase *pVehicle = pVehicleBase;
    if ( pVehicle == nullptr ) {
        return;
    }

    const bool bIsBike =
        ( pVehicle->getType() == VehicleBase::eType::VBT_Bike ||
          pVehicle->getType() == VehicleBase::eType::VBT_TrafficBike );
    int32_t hierarchyIndex = 4;

    // Per-wheel path when no AI vehicle, or AI vehicle's local-player virtual
    // returns true. Else path shares one body-center color across all wheels.
    // TODO: implement AI vtable dispatch (pAIVehicle+0x710, vtable slot 0x28)
    // when the AI system is in place; for now field_0x39cc is always null.
    if ( field_0x39cc == nullptr ) {
        // Per-wheel: each wheel gets its own computeGroundColor query
        const Eigen::Vector4f shiftOffset =
            gpWorldStreaming->isStreamingActive()
                ? gpWorldStreaming->getPosition()
                : Eigen::Vector4f::Zero();

        for ( uint32_t i = 0; i < numWheels; i++ ) {
            WheelState &wheel = wheels[i];

            if ( pVehicle->getInstanceByIndex( hierarchyIndex ) != nullptr ) {
                // OffsetedWorldMatrix * PositionInCar + shiftOffset = wheel
                // world pos
                const Eigen::Vector4f wheelWorldPos =
                    TDUMat4x4MulVec( body.OffsetedWorldMatrix,
                                     wheel.RelativeWorldPosition ) +
                    shiftOffset;

                if ( !bIsIndoor ) {
                    if ( !gpWorld->computeGroundColor(
                             &wheel.ShadowColor, &wheelWorldPos,
                             GCF_Shade | GCF_ShadeRGB ) ) {
                        wheel.ShadowColor =
                            Eigen::Vector4f( 1.f, 1.f, 1.f, 1.f );
                    }
                } else {
                    wheel.ShadowColor = Eigen::Vector4f( 1.f, 1.f, 1.f, 1.f );
                }

                if ( !gpWorld->computeGroundColor(
                         &wheel.GroundColor, &wheelWorldPos, GCF_Color ) ) {
                    wheel.GroundColor = Eigen::Vector4f( 1.f, 1.f, 1.f, 1.f );
                }

                if ( wheel.bGrounded && !wheel.bOnRoad ) {
                    wheel.ContactMaterial =
                        ( wheel.GroundColor.y() <=
                          wheel.GroundColor.x() * DAT_00f4174c )
                            ? eMaterialType::MT_Dirt
                            : eMaterialType::MT_Grass;
                }
            }

            hierarchyIndex = bIsBike ? 6 : ( hierarchyIndex + 1 );
        }
    } else {
        // Non-per-wheel (AI remote vehicle): one ground color for all wheels
        const Eigen::Vector4f *pShiftPos =
            gpWorldStreaming->isStreamingActive()
                ? &gpWorldStreaming->getPosition()
                : nullptr;

        Eigen::Vector4f bodyWorldPos = body.OffsetedWorldMatrix.row( 3 );
        if ( pShiftPos != nullptr ) {
            bodyWorldPos += *pShiftPos;
        }

        Eigen::Vector4f shadowColor( 1.f, 1.f, 1.f, 1.f );
        Eigen::Vector4f groundColor( 1.f, 1.f, 1.f, 1.f );

        if ( !bIsIndoor ) {
            gpWorld->computeGroundColor( &shadowColor, &bodyWorldPos,
                                         GCF_Shade | GCF_ShadeRGB );
        }
        gpWorld->computeGroundColor( &groundColor, &bodyWorldPos, GCF_Color );

        for ( uint32_t i = 0; i < numWheels; i++ ) {
            WheelState &wheel = wheels[i];

            if ( pVehicle->getInstanceByIndex( hierarchyIndex ) != nullptr ) {
                wheel.ShadowColor = shadowColor;
                wheel.GroundColor = groundColor;

                if ( wheel.bGrounded && !wheel.bOnRoad ) {
                    wheel.ContactMaterial =
                        ( wheel.GroundColor.y() <=
                          wheel.GroundColor.x() * DAT_00f4174c )
                            ? eMaterialType::MT_Dirt
                            : eMaterialType::MT_Grass;
                }
            }

            hierarchyIndex = bIsBike ? 6 : ( hierarchyIndex + 1 );
        }
    }
}

void CarState::openConvertibleWindows() {
    // FUN_008af100
    DBEntryCarPhysicsData *pCarData = gpDatabase->getCarDBEntry( carID );
    if ( pCarData != nullptr && pVehicleBase != nullptr &&
         pVehicleBase->getType() == VehicleBase::eType::VBT_Car ) {
        const uint64_t bodyHashcode =
            gpDatabase->getGameDB().Databases[1].getHashcodeForPointer(
                pCarData->CarBody.pAsRawPointer );
        if ( bodyHashcode == 0x56760d || bodyHashcode == 0x57fcad ) {
            VehicleCar *pCar = static_cast<VehicleCar *>( pVehicleBase );
            pCar->setWindowOpenRight( 1.0f );
            pCar->setWindowOpenLeft( 1.0f );
        }
    }

    if ( pPhysicsVehicle != nullptr ) {
        body.WindowState = 0;
    }
}

void CarState::retrieveLights( DirectionalLight *param_1,
                               DirectionalLight *param_2 ) {
    // FUN_008b4d00
    int32_t local_70 = 0;
    Eigen::Vector4f local_40 = Eigen::Vector4f::Zero();
    Eigen::Vector4f local_50 = Eigen::Vector4f::Zero();
    Eigen::Vector4f local_60 = Eigen::Vector4f::Zero();
    for ( WheelState &wheel : wheels ) {
        Eigen::Vector4f local_30 = wheel.getShadowColor();
        if ( 0.001f < local_30.x() ) {
            local_70++;
            local_40 += local_30;
        }
    }

    auto &activeWeatherCfg = gpWeather->getActiveWeatherConfig();

    if ( local_70 != 0 ) {
        float fVar9 = 1.0f / (float)local_70;

        Eigen::Vector4f auVar11 = local_40 * fVar9 * gSunGlobalFactor;
        local_40 = auVar11 / 100.0f;
        float local_74 = ( local_40.z() * 0.114f + local_40.y() * 0.587f +
                           local_40.x() * 0.299f );
        if ( local_74 < gSunLumMin ) {
            local_74 = gSunLumMin;
        }
        if ( gSunLumMax < local_74 ) {
            local_74 = gSunLumMax;
        }

        if ( gpWeather->isConfigDirty() ) {
            gpWeather->updateActiveConfig();
        }

        local_40.x() = activeWeatherCfg.getSunSpecularColor().x();

        Eigen::Vector4f local_30 =
            activeWeatherCfg.getSunAmbientColor() * local_74;

        local_50 = activeWeatherCfg.getSunDiffuseColor() * local_74;

        Eigen::Vector4f local_20 =
            activeWeatherCfg.getSunSpecularColor() * local_74;

        local_40 =
            Vector4DLerp( local_30, activeWeatherCfg.getSunAmbientColor(),
                          gSunAmbientFactor );

        local_60 = Vector4DLerp(
            local_50, activeWeatherCfg.getSunDiffuseColor(), gDiffuseFactor );

        local_50 = Vector4DLerp(
            local_20, activeWeatherCfg.getSunSpecularColor(), gSpecularFactor );

        if ( pVehicleBase != nullptr ) {
            pVehicleBase->setCurrentLuminosity( local_74 );
        }
    } else {
        if ( gpWeather->isConfigDirty() ) {
            gpWeather->updateActiveConfig();
        }
        local_40 = activeWeatherCfg.getSunAmbientColor();
        local_60 = activeWeatherCfg.getSunDiffuseColor();
        local_50 = activeWeatherCfg.getSunSpecularColor();
    }

    param_1->Ambient = local_40;
    param_1->Ambient.w() = 1.0f;

    param_1->Diffuse = local_60;
    param_1->Diffuse.w() = 1.0f;

    param_1->Specular = local_50;
    param_1->Specular.w() = 1.0f;

    if ( param_2 != nullptr ) {
        Eigen::Vector4f local_30 = local_60 * 0.5f;
        local_30.w() = 0.5f;

        local_60.x() =
            ( local_50.z() + local_50.y() + local_50.x() ) * 0.33333334f;
        local_60.w() = 1.0f;
        local_60.y() = local_60.x();
        local_60.z() = local_60.x();

        param_2->Ambient = local_40;
        param_2->Ambient.w() = 1.0f;
        param_2->Diffuse = local_30;
        param_2->Specular = Vector4DLerp( local_60, local_50, FLOAT_00fab4d0 );

        float fVar8 = param_1->Direction.y();
        float fVar1 = param_1->Direction.z();

        param_2->Direction =
            Eigen::Vector3f( param_1->Direction.x() * -1.0f,
                             abs( fVar8 * -1.0f ) * -1.0f, fVar1 * -1.0f );
    }
}

bool CarState::FUN_00406ce0() {
    // FUN_00406ce0
    return owner == eOwner::CSO_LocalPlayer &&
           simulationType == eSimulationType::CSST_Local;
}

bool CarState::unloadDriver() {
    // FUN_008057d0
    if ( pDriver == nullptr ) {
        return true;
    }
    if ( gpCarState == nullptr ) {
        return false;
    }

    PooledAvatarEntry *pRsc = pDriver->getPooledEntry();
    if ( pRsc != nullptr ) {
        pRsc->CarBelt.decreaseReferenceCount();
    }

    gpCarState->unregisterCallback( this, pDriver );

    EntityDriver *pSeatedDriver = pDriver;
    pDriver = nullptr;
    pSeatedDriver->setCarState( nullptr );
    return true;
}

void WheelState::reset() {
    // FUN_00413eb0
    WheelRadius = 0.35f;
    SuspensionTravelLength = 0.0f;
    SteerAngle = 0.0f;
    CamberAngle = 0.0f;
    CamberBreak = 0.0f;
    Omega = 0.0f;
    SlipRatio = 0.0f;
    SlipAngle = 0.0f;
    LongitudinalSpeed = 0.0f;
    LateralSpeed = 0.0f;
    Load = 0.0f;
    WheelAngle = 0.0f;
    bInitialized = false;
    WheelIndex = 0;
    AudioGroundSpeedKmh = 0.0f;
    AudioLatSpeedKmh = 0.0f;
    AudioSlipLong = 0.0f;
    AudioSlipLat = 0.0f;
    WorldPosition = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    RelativeWorldPosition = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    ProjectedPositionOnGround = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
}

Eigen::Vector4f WheelState::getShadowColor() const {
    // FUN_008af1d0
    if ( ( bGrounded == false ) || ( bOnRoad != false ) ) {
        if ( ContactMaterial != eMaterialType::MT_Asphalt ) {
            return { ShadowColor.z(), ShadowColor.z(), ShadowColor.z(), 1.0f };
        }

        if ( ( bOnBridge != false ) && ( bOnRoad != false ) ) {
            float local_34 = ( DistanceFromGround - 0.3f ) * 0.5f;
            if ( 1.0f < local_34 ) {
                local_34 = 1.0f;
            }
            Eigen::Vector4f vec1{ ShadowColor.y(), ShadowColor.y(),
                                  ShadowColor.y(), 1.0f };
            Eigen::Vector4f vec2{ ShadowColor.x(), ShadowColor.x(),
                                  ShadowColor.x(), 1.0f };

            return Vector4DLerp( vec1, vec2, local_34 );
        }
        if ( ( bInsideTunnel != false ) && ( bOnRoad != false ) ) {
            return { ShadowColor.y(), ShadowColor.y(), ShadowColor.y(), 1.0f };
        }
    }

    return { ShadowColor.x(), ShadowColor.x(), ShadowColor.x(), 1.0f };
}

void CarBodyState::reset() {
    // FUN_008ae940
    WorldMatrix = Matrix4x4f_t::Identity();
    PreviousWorldMatrix = Matrix4x4f_t::Identity();
    OffsetedWorldMatrix = Matrix4x4f_t::Identity();
    PhysicsWorldMatrix = Matrix4x4f_t::Identity();
    AngularVelocity = Eigen::Vector4f::Zero();
    LinearVelocity = Eigen::Vector4f::Zero();
    Accel = Eigen::Vector4f::Zero();
    AccelDiff = Eigen::Vector4f::Zero();
    SpeedInKmh = 0.0f;
    Omega = 10.0f;
    EngineRPM = 0.0f;
    EnginePowerPS = 0.0f;
    EngineTorqueNm = 0.0f;
    AudioSpeedKmh = 0.0f;
    Gears = 1;
    Handbrake = 0.0f;
    Clutch = 0.0f;
    Lean = 0.0f;
    GearRequest = 1;
    bGearUpEdge = false;
    bGearDownEdge = false;
    bIgnition = false;
    bIgnitionOnce = false;
    bBackOnTrackRequest = false;
    WindowState = 0;
    OdometerKms = 0;
    OdometerTenMeters = 0;
    bUseWheelMatrix = false;
    bInitialized = false;
    BodyDimensions = Eigen::Vector4f( 2.0f, 1.0f, 4.0f, 0.0f );
    WheelBase = 3.0f;
    TrackWidth = 1.8f;
    TrackLength = 1.8f;
    bIsBike = false;
}

void CarBodyState::resetBackOnTrack() {
    // FUN_008ae3e0
    PositionOnRoad = Eigen::Vector4f::Zero();
    PrevPositionOnRoad = Eigen::Vector4f::Zero();
    ActiveRoadID = 0;
    RoadX = 0.0f;
    RoadZ = 0.0f;
    BackOnTrackState = 0;
    BackOnTrackTimer = 5.0f;
    BackOnTrackWorldMatrix = Matrix4x4f_t::Identity();
    BackOnTrackVelocity = Eigen::Vector4f::Zero();
}
