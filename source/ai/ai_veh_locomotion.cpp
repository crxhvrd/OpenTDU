#include "shared.h"
#include "ai_veh_locomotion.h"

#include "ai/ai_config.h"
#include "ai/ai_debug_draw.h"
#include "ai/ai_vehicle.h"
#include "core/maths_helper.h"
#include "game/game_object.h"
#include "game/scenaric/entity/entity_car_player.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "physics/gs_physics.h"
#include "gs_timer.h"

#include <Jolt/Physics/Body/Body.h>

static constexpr float kBlockedSpeedThreshold = 0.1f;       // DAT_00f41668
static constexpr float kBlockedTargetSpeedThreshold = 0.2f; // DAT_00f41730
static constexpr float kDefaultSmoothSteerCoef = 0.2f;      // DAT_00f41730
static constexpr float kMaxPhysicsDistanceSq = 63999900.0f; // DAT_00f41cac
static constexpr float kKinematicCrossHalfSize = 0.2f;      // DAT_00faa95c

static constexpr float kTwoOverPi = 0.63661975f;         // DAT_00f41a30
static constexpr float kSpeedNormalization = 0.025f;     // DAT_00f41734
static constexpr bool kOverspeedBrake = true;            // DAT_00faa980
static constexpr float kOverspeedThreshold = 0.1f;       // DAT_00faa97c
static constexpr bool kNoThrottleInTurn = true;          // DAT_00faa978
static constexpr float kNoThrottleSteerThreshold = 0.5f; // DAT_00faa974
static constexpr float kThrottleRate = 30.0f;            // DAT_00faa970

// Index = AiVehLocoTraffic speed / steer PID type (0 car steer, 1 car speed,
// 2 bike steer, 3 bike speed)
static constexpr float kPidCoefP[4] = { -4.0f, -1.0f, -5.0f,
                                        -1.0f }; // DAT_00f7c358
static constexpr float kPidCoefI[4] = { 0.0f, 0.0f, 0.0f,
                                        0.0f }; // DAT_010e73f8
static constexpr float kPidCoefD[4] = { -0.4f, -1.0f, -0.4f,
                                        -1.0f }; // DAT_00f7c368

static constexpr int32_t kPidMaxValues = 100;
static constexpr float kPidIntegralTime = 1.0f;

static Matrix4x4f_t ComputePhysicsMatrix( const AiVehicle &vehicle ) {
    return vehicle.getInfos().GroundMatrix;
}

static PhysicsVehicle *GetPhysicsVehicle( AiVehicle *pVehicle ) {
    CarState *pCarState =
        ( pVehicle != nullptr ) ? pVehicle->getCarState() : nullptr;
    return ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;
}

static bool IsDrivingDisabled( const AiVehicle &vehicle,
                               const float totalTime ) {
    const AiVehicleInfo &infos = vehicle.getInfos();
    return infos.bHandbrake || !infos.bValid ||
           ( vehicle.isRemovalPending() &&
             totalTime > vehicle.getRemovalTime() );
}

AiVehLocomotion::AiVehLocomotion()
    : pVehicle( nullptr ),
      targetSpeed( 0.0f ),
      padThrottle( 0.0f ),
      padHandbrake( 0.0f ),
      padSteering( 0.0f ),
      bResetPhysicsOnAttach( true ),
      bPhysicsPaused( true ),
      bPhysicsControl( false ),
      targetDirection( kUnitXVector ),
      bSmoothSteering( true ),
      smoothSteerCoef( kDefaultSmoothSteerCoef ),
      bDrivenByNavigation( true ),
      manualAccel( 0.0f ),
      manualBrake( 0.0f ),
      manualHandbrake( 0.0f ),
      manualSteer( 0.0f ),
      manualLean( 0.0f ),
      blockedStartTime( 0.0f ),
      blockedEndTime( 0.0f ),
      groupPosNew( Eigen::Vector4f::Zero() ),
      initialSpeed( 0.0f ),
      virtualDistanceAccum( 0.0f ),
      timeAccum( 0.0f ),
      numAccum( 0 ) {
    // FUN_009386e0
}

void AiVehLocomotion::attach( AiVehicle *pOwner ) {
    // FUN_00938510
    pVehicle = pOwner;
    reset( false );

    CarState *pCarState = pVehicle->getCarState();
    PhysicsVehicle *pPhysics =
        ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;
    initialSpeed = pVehicle->getInfos().Speed;

    if ( pPhysics != nullptr && bResetPhysicsOnAttach ) {
        PhysicsLock lock;
        pPhysics->resetSimulationState();
        if ( pVehicle->getInfos().bValid ) {
            const Matrix4x4f_t matrix = ComputePhysicsMatrix( *pVehicle );
            const Eigen::Vector4f velocity =
                matrix.row( 2 ) * pVehicle->getInfos().Speed;
            pPhysics->resetToTransform( matrix, true );
            pPhysics->setVelocity( velocity.head<3>(), true, -1 );
            pPhysics->setVirtualSpeed( pVehicle->getInfos().Speed );
            OTDU_UNIMPLEMENTED_ONCE; // TODO: CarState body LinearVelocity
        }
    }

    if ( bResetPhysicsOnAttach ||
         ( pPhysics != nullptr && pPhysics->isPhysicsPaused() ) ) {
        bPhysicsControl = true;
        setPhysicsActive( false, false );
    } else {
        padThrottle = pCarState->getBody()->Throttle;
        padSteering = pCarState->getBody()->Steering;
        bPhysicsControl = true;
        bPhysicsPaused = false;
    }

    if ( pPhysics != nullptr ) {
        PhysicsLock lock;
        pPhysics->setDrivingControl( this );
    }
}

void AiVehLocomotion::reset( const bool bFull ) {
    // FUN_00937af0
    (void)bFull;
    targetDirection = kUnitXVector;
    targetSpeed = 0.0f;
    padThrottle = 0.0f;
    padHandbrake = 0.0f;
    padSteering = 0.0f;
    bSmoothSteering = true;
    smoothSteerCoef = kDefaultSmoothSteerCoef;
    bDrivenByNavigation = true;
    blockedStartTime = 0.0f;
    blockedEndTime = 0.0f;
    virtualDistanceAccum = 0.0f;
    timeAccum = 0.0f;
    numAccum = 0;
    bResetPhysicsOnAttach = true;

    PhysicsVehicle *pPhysics = GetPhysicsVehicle( pVehicle );
    if ( pPhysics != nullptr ) {
        const bool bPaused = pPhysics->isPhysicsPaused();
        bPhysicsControl = !bPaused;
        bPhysicsPaused = bPaused;
    }
}

void AiVehLocomotion::tick( const float deltaTime, const float totalTime ) {
    (void)deltaTime;
    (void)totalTime;
}

void AiVehLocomotion::detach() {
    // FUN_00937840
    PhysicsVehicle *pPhysics = GetPhysicsVehicle( pVehicle );
    if ( pPhysics != nullptr ) {
        PhysicsLock lock;
        pPhysics->setDrivingControl( nullptr );
    }
}

void AiVehLocomotion::debugDraw( DrawList *pDrawList ) {
    // FUN_00937ff0
    static constexpr float kDirectionHeight = 2.0f;
    static constexpr uint32_t kKinematicCrossColor = 0xff0000ff; // DAT_016a7fcc

    if ( isPhysicsDriven() ) {
        const AiVehicleInfo &infos = pVehicle->getInfos();
        const Eigen::Vector4f base =
            infos.GroundMatrix.row( 3 ).transpose() +
            Eigen::Vector4f( 0.0f, kDirectionHeight, 0.0f, 0.0f ) -
            GetShiftWorldPosition();
        const Eigen::Vector4f forward =
            base + infos.GroundMatrix.row( 2 ).transpose();
        const Eigen::Vector4f target = base + targetDirection;
        DrawDebugLine( base, forward, 0xff0000ff, *pDrawList );
        DrawDebugLine( base, target, 0xff00ff00, *pDrawList );
        return;
    }

    const Eigen::Vector4f center = groupPosNew - GetShiftWorldPosition();
    const Eigen::Vector4f axisX( kKinematicCrossHalfSize, 0.0f, 0.0f, 0.0f );
    const Eigen::Vector4f axisZ( 0.0f, 0.0f, kKinematicCrossHalfSize, 0.0f );
    DrawDebugLine( center - axisX, center + axisX, kKinematicCrossColor,
                   *pDrawList );
    // sic: X axis drawn twice (Y was likely intended)
    DrawDebugLine( center - axisX, center + axisX, kKinematicCrossColor,
                   *pDrawList );
    DrawDebugLine( center - axisZ, center + axisZ, kKinematicCrossColor,
                   *pDrawList );
}

void AiVehLocomotion::setPhysicsActive( const bool bActive,
                                        const bool bForce ) {
    // FUN_00938370
    if ( bPhysicsControl == bActive && !bForce ) {
        return;
    }
    bPhysicsControl = bActive;

    PhysicsVehicle *pPhysics = GetPhysicsVehicle( pVehicle );
    if ( pPhysics == nullptr ) {
        return;
    }

    PhysicsLock lock;
    if ( bActive ) {
        const Matrix4x4f_t matrix = ComputePhysicsMatrix( *pVehicle );
        const Eigen::Vector4f velocity =
            matrix.row( 2 ) * pVehicle->getInfos().Speed;
        pPhysics->setVirtual( false );
        pPhysics->resetToTransform( matrix, true );
        pPhysics->setVelocity( velocity.head<3>(), true, -1 );
        OTDU_UNIMPLEMENTED_ONCE; // TODO: gearbox state 2
        bPhysicsPaused = false;
    } else {
        pPhysics->setVirtual( true );
        bPhysicsPaused = true;
    }
}

void AiVehLocomotion::setDrivenByNavigation( const bool bDriven ) {
    // FUN_00937620
    if ( bDrivenByNavigation == bDriven ) {
        return;
    }
    bDrivenByNavigation = bDriven;
    manualAccel = 0.0f;
    manualBrake = 0.0f;
    manualHandbrake = 0.0f;
    manualSteer = 0.0f;
    manualLean = 0.0f;
}

void AiVehLocomotion::setManualInputs( const float accel,
                                       const float brake,
                                       const float handbrake,
                                       const float steer,
                                       const float lean ) {
    // FUN_009375e0
    manualAccel = accel;
    manualBrake = brake;
    manualHandbrake = handbrake;
    manualSteer = steer;
    manualLean = lean;
}

void AiVehLocomotion::updateBlockedTime( const float totalTime ) {
    // FUN_00937ab0
    blockedEndTime = totalTime;
    if ( pVehicle->getInfos().Speed >= kBlockedSpeedThreshold ||
         targetSpeed < kBlockedTargetSpeedThreshold ) {
        blockedStartTime = totalTime;
    }
    pVehicle->getInfos().BlockedTime = totalTime - blockedStartTime;
}

void AiVehLocomotion::checkPhysicsDistance() {
    // FUN_009382d0
    CarState *pCarState = pVehicle->getCarState();
    if ( pCarState == nullptr ) {
        return;
    }
    const Eigen::Vector4f delta =
        pCarState->getWorldPosition() - GSPhysics::GetShiftOrigin();
    if ( delta.head<3>().squaredNorm() > kMaxPhysicsDistanceSq ) {
        setPhysicsActive( false, false );
    }
}

void AiVehLocomotion::moveKinematic() {
    // FUN_00938760
    static constexpr float kMaxSlerpRatio = 0.2f;           // DAT_00faa964
    static constexpr float kSlerpSpeedDivisor = 10.0f;      // DAT_00faa960
    static constexpr float kSmoothedSpeedDecay = 0.8f;      // DAT_00f416d0
    static constexpr float kSmoothedSpeedEpsilon = 1.0e-4f; // DAT_00f41760

    AiVehicleInfo &infos = pVehicle->getInfos();
    CarState *pCarState = pVehicle->getCarState();
    PhysicsVehicle *pPhysics = GetPhysicsVehicle( pVehicle );
    if ( pPhysics == nullptr ) {
        return;
    }

    const Matrix4x4f_t chassisWorld = pPhysics->getChassisWorldMatrix();
    const float groundOffset = pPhysics->computeGroundToOriginOffset();
    Eigen::Vector4f groundPos =
        chassisWorld.row( 3 ).transpose() -
        chassisWorld.row( 1 ).transpose() * groundOffset;

    if ( infos.bHandbrake && infos.Speed <= 0.0f ) {
        return;
    }

    float twist = 0.0f;
    if ( pVehicle->getNavigation() != nullptr ) {
        float height = 0.0f;
        pVehicle->getNavigation()->getHeightAndTwist( height, twist );
        groundPos.y() = height;
    }

    targetDirection.w() = 0.0f;
    targetDirection /= targetDirection.norm();
    if ( targetDirection.x() == 0.0f && targetDirection.z() == 0.0f ) {
        return;
    }
    if ( std::isnan( targetDirection.x() ) ) {
        targetDirection = infos.GroundMatrix.row( 2 ).transpose();
    }

    const float accumTime = ( timeAccum > 0.0f ) ? timeAccum : 1.0f;
    const float speed = virtualDistanceAccum / accumTime;
    groupPosNew = groundPos + targetDirection * virtualDistanceAccum;

    const Eigen::Vector3f direction = targetDirection.head<3>();
    Eigen::Vector3f side = Eigen::Vector3f::UnitY().cross( direction );
    side.y() = -twist;
    side.normalize();
    const Eigen::Vector3f up = direction.cross( side ).normalized();

    Eigen::Matrix3f targetRows;
    targetRows.row( 0 ) = side;
    targetRows.row( 1 ) = up;
    targetRows.row( 2 ) = direction;
    const Eigen::Quaternionf targetRotation(
        Eigen::Matrix3f( targetRows.transpose() ) );
    const Eigen::Quaternionf currentRotation(
        Eigen::Matrix3f( chassisWorld.block<3, 3>( 0, 0 ).transpose() ) );
    const float ratio = std::min(
        kMaxSlerpRatio, ( kMaxSlerpRatio / kSlerpSpeedDivisor ) * speed );
    const Eigen::Matrix3f rotationRows =
        currentRotation.normalized()
            .slerp( ratio, targetRotation.normalized() )
            .toRotationMatrix()
            .transpose();

    Matrix4x4f_t center = Matrix4x4f_t::Identity();
    const Eigen::Vector3f row1 = rotationRows.row( 1 ).transpose();
    const Eigen::Vector3f row2 = rotationRows.row( 2 ).transpose();
    const Eigen::Vector3f row0 = row1.cross( row2 ).normalized();
    const Eigen::Vector3f row1Ortho = row2.cross( row0 ).normalized();
    center.block<1, 3>( 0, 0 ) = row0.transpose();
    center.block<1, 3>( 1, 0 ) = row1Ortho.transpose();
    center.block<1, 3>( 2, 0 ) = row2.normalized().transpose();

    const float halfHeight = pCarState->getBody()->BodyDimensions.y() * 0.5f;
    const float centerHeight = halfHeight + groundOffset;
    center.row( 3 ) = groupPosNew.transpose() + center.row( 1 ) * centerHeight;
    center( 3, 3 ) = 1.0f;

    Matrix4x4f_t chassis = center;
    chassis.row( 3 ) -= center.row( 1 ) * halfHeight;
    chassis( 3, 3 ) = 1.0f;
    {
        PhysicsLock lock;
        pPhysics->setPostponedMatrix( chassis );
    }

    infos.Speed = speed;
    infos.bValid = true;
    float smoothedSpeed = infos.SmoothedSpeed * kSmoothedSpeedDecay +
                          speed * ( 1.0f - kSmoothedSpeedDecay );
    if ( smoothedSpeed >= -kSmoothedSpeedEpsilon &&
         smoothedSpeed <= kSmoothedSpeedEpsilon ) {
        smoothedSpeed = 0.0f;
    }
    infos.SmoothedSpeed = smoothedSpeed;

    Matrix4x4f_t chassisShifted = chassis;
    chassisShifted.row( 3 ).head<3>() -=
        GSPhysics::GetShiftOrigin().head<3>().transpose();
    infos.GroundMatrix = pPhysics->computeDisplayMatrix( chassisShifted );
    infos.GroundMatrix.row( 3 ).head<3>() +=
        GSPhysics::GetShiftOrigin().head<3>().transpose();
}

void AiVehLocomotion::writeInputs( PhysicsVehicle &vehicle,
                                   const float deltaTime ) {
    // FUN_009378d0
    if ( vehicle.areWheelsActive() ) {
        virtualDistanceAccum += vehicle.getVirtualSpeed() * deltaTime;
    }
    timeAccum += deltaTime;
    numAccum++;

    if ( !bDrivenByNavigation ) {
        vehicle.setInput( manualAccel, manualBrake, manualSteer );
        vehicle.setHandbrake( manualHandbrake );
        return;
    }

    const eAiVehicleCategory category = pVehicle->getCategory();
    if ( category != eAiVehicleCategory::AVC_Traffic &&
         category != eAiVehicleCategory::AVC_Cop ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: input headlights flag
    }

    float throttle;
    float brake;
    float handbrake;
    float steer;
    if ( pVehicle->getInfos().bHandbrake ) {
        throttle = 0.0f;
        brake = 0.5f;
        handbrake = 1.0f;
        steer = 0.0f;
    } else if ( padThrottle > 0.0f ) {
        throttle = padThrottle;
        brake = 0.0f;
        handbrake = padHandbrake;
        steer = padSteering;
    } else {
        throttle = 0.0f;
        brake = -padThrottle;
        handbrake = padHandbrake;
        steer = padSteering;
    }

    vehicle.setInput( std::clamp( throttle, 0.0f, 1.0f ),
                      std::clamp( brake, 0.0f, 1.0f ),
                      std::clamp( steer, -1.0f, 1.0f ) );
    vehicle.setHandbrake( std::clamp( handbrake, 0.0f, 1.0f ) );
}

AiVehLocoTraffic::AiVehLocoTraffic()
    : AiVehLocomotion(),
      targetAngle( 0.0f ),
      correctThrottle( 0.0f ),
      speedControl(),
      speedPIDType( 1 ),
      currentAngle( 0.0f ),
      steerControl(),
      steerPIDType( 0 ),
      lastSteering( 0.0f ) {
    // FUN_00937030
}

void AiVehLocoTraffic::refreshPIDCoefs() {
    speedControl.setCoefs( kPidCoefP[speedPIDType], kPidCoefI[speedPIDType],
                           kPidCoefD[speedPIDType] );
    steerControl.setCoefs( kPidCoefP[steerPIDType], kPidCoefI[steerPIDType],
                           kPidCoefD[steerPIDType] );
}

void AiVehLocoTraffic::attach( AiVehicle *pOwner ) {
    // FUN_00936d30
    speedPIDType = 1;
    steerPIDType = 0;
    CarState *pCarState = pOwner->getCarState();
    if ( pCarState != nullptr && pCarState->getPhysicsVehicle() != nullptr &&
         pCarState->getBody()->bIsBike ) {
        speedPIDType = 3;
        steerPIDType = 2;
    }

    speedControl.initialize( kMinusOne, kOne, kPidIntegralTime, false,
                             kPidMaxValues );
    steerControl.initialize( kMinusOne, kOne, kPidIntegralTime, false,
                             kPidMaxValues );
    refreshPIDCoefs();
    speedControl.resetWithType( PIDCtrl::PT_MeasureDerivative );
    steerControl.resetWithType( PIDCtrl::PT_ErrorDerivative );

    reset( false );
    AiVehLocomotion::attach( pOwner );
}

void AiVehLocoTraffic::reset( const bool bFull ) {
    // FUN_00936b00
    if ( bFull ) {
        AiVehLocomotion::reset( bFull );
    }
    targetDirection = kUnitXVector;
    targetAngle = 0.0f;
    correctThrottle = 0.0f;
    padSteering = 0.0f;
    currentAngle = 0.0f;
    blockedStartTime = 0.0f;
    blockedEndTime = 0.0f;
    lastSteering = 0.0f;
    if ( bFull ) {
        targetSpeed = 0.0f;
        padThrottle = 0.0f;
        speedControl.reset( true );
        steerControl.reset( true );
    }
    setDrivenByNavigation( true );
}

void AiVehLocoTraffic::tick( const float deltaTime, const float totalTime ) {
    // FUN_00937090
    updateBlockedTime( totalTime );

    AiVehicleInfo &infos = pVehicle->getInfos();
    if ( IsDrivingDisabled( *pVehicle, totalTime ) ) {
        targetSpeed = 0.0f;
    } else {
        if ( !bDrivenByNavigation ) {
            targetDirection = infos.GroundMatrix.row( 2 );
            if ( manualAccel > 0.0f ) {
                targetSpeed = infos.MaxSpeed * manualAccel;
            } else if ( manualHandbrake > 0.0f ) {
                targetSpeed = 0.0f;
            } else {
                targetSpeed = -manualBrake * infos.MaxSpeed;
            }
        } else if ( pVehicle->getNavigation() != nullptr ) {
            pVehicle->getNavigation()->getTarget( targetDirection,
                                                  targetSpeed );
            targetSpeed = std::min( targetSpeed, infos.MaxSpeed );
        } else {
            targetDirection = infos.GroundMatrix.row( 2 );
            targetSpeed = 0.0f;
        }
        refreshPIDCoefs();
    }

    checkPhysicsDistance();
    PhysicsLock lock;
    if ( bPhysicsPaused ) {
        if ( numAccum == 0 ) {
            return;
        }
        moveKinematic();
    }
    virtualDistanceAccum = 0.0f;
    timeAccum = 0.0f;
    numAccum = 0;
}

void AiVehLocoTraffic::detach() {
    // FUN_009367c0
    AiVehLocomotion::detach();
    speedControl.release();
    steerControl.release();
}

void AiVehLocoTraffic::forwardInput( PhysicsVehicle &vehicle,
                                     float deltaTime ) {
    // FUN_00936cd0
    if ( bDrivenByNavigation && deltaTime > 0.0f ) {
        computeControls( vehicle, deltaTime );
    }
    writeInputs( vehicle, deltaTime );
}

void AiVehLocoTraffic::computeControls( PhysicsVehicle &vehicle,
                                        const float deltaTime ) {
    // FUN_00936830
    if ( IsDrivingDisabled( *pVehicle, gpTimer->GameTotalTime ) ) {
        padThrottle = 0.0f;
        padSteering = 0.0f;
        return;
    }

    const JPH::Body *pChassis = vehicle.getChassisBody();
    if ( pChassis == nullptr ) {
        return;
    }

    const JPH::Vec3 forward = pChassis->GetWorldTransform().GetColumn3( 2 );
    currentAngle = atan2f( forward.GetX(), forward.GetZ() );

    Eigen::Vector4f direction = targetDirection;
    direction.normalize();
    targetAngle = atan2f( direction.x(), direction.z() );

    const float delta = targetAngle - currentAngle;
    if ( delta > kPI ) {
        targetAngle -= kTwoPI;
    } else if ( delta <= kNegPI ) {
        targetAngle += kTwoPI;
    }

    const float clampedDelta = targetAngle - currentAngle;
    if ( clampedDelta > kHalfPI ) {
        targetAngle = currentAngle + kHalfPI;
    } else if ( clampedDelta < kNegHalfPi ) {
        targetAngle = currentAngle - kHalfPI;
    }

    steerControl.update( currentAngle * kTwoOverPi, targetAngle * kTwoOverPi,
                         deltaTime );
    padSteering = steerControl.getOutput();

    const float speed = pChassis->GetLinearVelocity().Length();
    speedControl.update( speed * kSpeedNormalization,
                         targetSpeed * kSpeedNormalization, deltaTime );

    if ( kOverspeedBrake &&
         speedControl.getErrorProportional() > kOverspeedThreshold ) {
        padThrottle = kMinusOne;
    } else {
        correctThrottle = speedControl.getOutput();
        if ( kNoThrottleInTurn &&
             vehicle.getEngineState() != eEngineState::ES_Running &&
             padThrottle >= kHalf && correctThrottle > 0.0f &&
             fabsf( padSteering ) > kNoThrottleSteerThreshold ) {
            correctThrottle = 0.0f;
        }
        padThrottle = std::clamp( padThrottle + kThrottleRate * deltaTime *
                                                    correctThrottle,
                                  kMinusOne, kOne );
    }

    padHandbrake = 0.0f;
    if ( bSmoothSteering ) {
        const float coef = std::clamp( smoothSteerCoef, 0.0f, 1.0f );
        padSteering = ( 1.0f - coef ) * lastSteering + coef * padSteering;
        lastSteering = padSteering;
    }
}

// AiVehLocoOpponent PID gains (Steer, Accel, Brake)
static constexpr float kOpponentSteerP = -4.0f; // DAT_00f7c32c
static constexpr float kOpponentSteerI = 0.0f;  // DAT_00f7c330
static constexpr float kOpponentSteerD = -0.4f; // DAT_00f7c334
static constexpr float kOpponentAccelP = -1.0f; // DAT_00f7c338
static constexpr float kOpponentAccelI = 0.0f;  // DAT_00f7c33c
static constexpr float kOpponentAccelD = -1.0f; // DAT_00f7c340
static constexpr float kOpponentBrakeP = 8.0f;  // DAT_00f7c344
static constexpr float kOpponentBrakeI = 0.0f;  // DAT_00f7c348
static constexpr float kOpponentBrakeD = 0.0f;  // DAT_00f7c34c

static constexpr float kOpponentThrottleRate = 30.0f; // DAT_00faa968
static constexpr bool kOpponentSharpTurnClamp =
    false; // DAT_016a2c3a (never written by retail)
static constexpr int32_t kOpponentMaxAccelMinGear = 3; // 3rd and up

static bool IsOpponentDrivingDisabled( const AiVehicle &vehicle ) {
    const AiVehicleInfo &infos = vehicle.getInfos();
    return infos.bHandbrake || !infos.bValid || vehicle.isRemovalPending();
}

AiVehLocoOpponent::AiVehLocoOpponent()
    : AiVehLocomotion(),
      steerControl(),
      accelControl(),
      brakeControl(),
      targetAngle( 0.0f ),
      throttleDelta( 0.0f ),
      currentAngle( 0.0f ),
      lastSteering( 0.0f ) {
    // FUN_009d7da0
}

void AiVehLocoOpponent::attach( AiVehicle *pOwner ) {
    // FUN_009d7b60
    reset( false );
    steerControl.initialize( kMinusOne, kOne, kPidIntegralTime, false,
                             kPidMaxValues );
    accelControl.initialize( kMinusOne, kOne, kPidIntegralTime, false,
                             kPidMaxValues );
    brakeControl.initialize( kMinusOne, kOne, kPidIntegralTime, false,
                             kPidMaxValues );
    AiVehLocomotion::attach( pOwner );
}

void AiVehLocoOpponent::reset( const bool bFull ) {
    // FUN_009d7a00
    targetAngle = 0.0f;
    throttleDelta = 0.0f;
    currentAngle = 0.0f;
    lastSteering = 0.0f;
    if ( !bFull ) {
        return;
    }

    AiVehLocomotion::reset( bFull );
    steerControl.reset( true );
    accelControl.reset( true );
    brakeControl.reset( true );
}

void AiVehLocoOpponent::tick( const float deltaTime, const float totalTime ) {
    // FUN_009d7e00
    (void)deltaTime;
    updateBlockedTime( totalTime );

    AiVehicleInfo &infos = pVehicle->getInfos();
    if ( IsOpponentDrivingDisabled( *pVehicle ) ) {
        targetSpeed = 0.0f;
    } else {
        if ( pVehicle->getNavigation() == nullptr ||
             !pVehicle->getNavigation()->getTarget( targetDirection,
                                                    targetSpeed ) ) {
            targetDirection = infos.GroundMatrix.row( 2 );
            targetSpeed = infos.Speed;
        }
        steerControl.setCoefs( kOpponentSteerP, kOpponentSteerI,
                               kOpponentSteerD );
        accelControl.setCoefs( kOpponentAccelP, kOpponentAccelI,
                               kOpponentAccelD );
        brakeControl.setCoefs( kOpponentBrakeP, kOpponentBrakeI,
                               kOpponentBrakeD );
    }

    checkPhysicsDistance();
    PhysicsLock lock;
    if ( bPhysicsPaused ) {
        if ( numAccum == 0 ) {
            return;
        }
        moveKinematic();
    }
    virtualDistanceAccum = 0.0f;
    timeAccum = 0.0f;
    numAccum = 0;
}

void AiVehLocoOpponent::detach() {
    // FUN_009d7970
    AiVehLocomotion::detach();
    steerControl.release();
    accelControl.release();
    brakeControl.release();
}

void AiVehLocoOpponent::forwardInput( PhysicsVehicle &vehicle,
                                      float deltaTime ) {
    // FUN_009d7920
    if ( bDrivenByNavigation && deltaTime > 0.0f ) {
        computeControls( vehicle, deltaTime );
    }
    writeInputs( vehicle, deltaTime );
}

void AiVehLocoOpponent::computeControls( PhysicsVehicle &vehicle,
                                         const float deltaTime ) {
    // FUN_009d75c0
    if ( IsOpponentDrivingDisabled( *pVehicle ) ) {
        padThrottle = 0.0f;
        padSteering = 0.0f;
        return;
    }

    const JPH::Body *pChassis = vehicle.getChassisBody();
    if ( pChassis == nullptr ) {
        return;
    }

    const JPH::Vec3 forward = pChassis->GetWorldTransform().GetColumn3( 2 );
    currentAngle = atan2f( forward.GetX(), forward.GetZ() );

    Eigen::Vector4f direction = targetDirection;
    direction.normalize();
    targetAngle = atan2f( direction.x(), direction.z() );

    const float delta = targetAngle - currentAngle;
    if ( delta > kPI ) {
        targetAngle -= kTwoPI;
    } else if ( delta <= kNegPI ) {
        targetAngle += kTwoPI;
    }

    const float clampedDelta = targetAngle - currentAngle;
    if ( clampedDelta > kHalfPI ) {
        targetAngle = currentAngle + kHalfPI;
    } else if ( clampedDelta < kNegHalfPi ) {
        targetAngle = currentAngle - kHalfPI;
    }

    steerControl.update( currentAngle * kTwoOverPi, targetAngle * kTwoOverPi,
                         deltaTime );
    padSteering = steerControl.getOutput();

    const float speed = pChassis->GetLinearVelocity().Length();
    const float invMaxSpeed = 1.0f / pVehicle->getInfos().MaxSpeed;
    accelControl.update( speed * invMaxSpeed, targetSpeed * invMaxSpeed,
                         deltaTime );
    brakeControl.update( speed * invMaxSpeed, targetSpeed * invMaxSpeed,
                         deltaTime );

    static const float sSharpTurnLimit = gAIConfig.OpponentSharpTurnLimit;
    throttleDelta = accelControl.getOutput() - brakeControl.getOutput();
    const bool bSharpTurn = kOpponentSharpTurnClamp &&
                            gAIConfig.OpponentSharpTurnMaxSpeed >= speed &&
                            fabsf( padSteering ) > sSharpTurnLimit;
    padThrottle = std::clamp( padThrottle + kOpponentThrottleRate * deltaTime *
                                                throttleDelta,
                              kMinusOne, kOne );
    if ( bSharpTurn && padThrottle > gAIConfig.OpponentSharpTurnClampValue ) {
        padThrottle = gAIConfig.OpponentSharpTurnClampValue;
    }

    const AiVehIndDPC &dpc = pVehicle->getDPC();
    if ( padThrottle > 0.0f &&
         vehicle.getCurrentGear() >= kOpponentMaxAccelMinGear ) {
        padThrottle = std::clamp( padThrottle, 0.0f, dpc.DPCPattern.MaxAccel );
    }

    padSteering = dpc.applySteerDeadZone( padSteering );
    if ( bSmoothSteering ) {
        const float coef = std::clamp( smoothSteerCoef, 0.0f, 1.0f );
        padSteering = ( 1.0f - coef ) * lastSteering + coef * padSteering;
        lastSteering = padSteering;
    }
}

static constexpr float kParkedHandbrake = 1.0f;                // DAT_00faa96c
static constexpr float kParkedActivateDistanceSq = 10000.0f;   // DAT_00f7c350
static constexpr float kParkedDeactivateDistanceSq = 40000.0f; // DAT_00f7c354
static constexpr float kNoPlayerDistanceSq = 1e10f;            // DAT_00f41b80

AiVehLocoParked::AiVehLocoParked()
    : AiVehLocomotion(), AiFsm(), pOwnerEntity( nullptr ) {
    // FUN_00937470
}

void AiVehLocoParked::attach( AiVehicle *pOwner ) {
    // FUN_00937420
    AiVehLocomotion::attach( pOwner );
    reset( false );
    currentState = PS_Inactive;
    bTransitionPending = false;
    nextState = PS_Inactive;
    stackSize = 0;
    process( FE_Enter );
    pVehicle->getInfos().bHandbrake = true;
}

void AiVehLocoParked::reset( const bool bFull ) {
    // FUN_009373e0
    if ( bFull ) {
        AiVehLocomotion::reset( bFull );
        currentState = PS_Inactive;
        bTransitionPending = false;
        nextState = PS_Inactive;
        stackSize = 0;
    }
    pOwnerEntity = nullptr;
}

void AiVehLocoParked::tick( const float deltaTime, const float totalTime ) {
    // FUN_009374c0
    (void)deltaTime;
    (void)totalTime;
    process( FE_Update );
    checkPhysicsDistance();
    PhysicsLock lock;
    if ( bPhysicsPaused ) {
        moveKinematic();
    }
    virtualDistanceAccum = 0.0f;
    timeAccum = 0.0f;
    numAccum = 0;
}

void AiVehLocoParked::detach() {
    // FUN_009373b0
    if ( pOwnerEntity != nullptr ) {
        if ( pOwnerEntity->getParkedVehicle() == pVehicle ) {
            pOwnerEntity->setParkedVehicle( nullptr );
        }
        pOwnerEntity = nullptr;
    }
    AiVehLocomotion::detach();
}

void AiVehLocoParked::debugDraw( DrawList *pDrawList ) {
    // FUN_009372b0
    (void)pDrawList;
}

void AiVehLocoParked::forwardInput( PhysicsVehicle &vehicle, float deltaTime ) {
    // FUN_00937280
    (void)deltaTime;
    vehicle.setInput( 0.0f, 0.0f, 0.0f );
    vehicle.setHandbrake( kParkedHandbrake );
}

float AiVehLocoParked::computePlayerDistanceSq() const {
    // FUN_00937310
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr ) {
        return kNoPlayerDistanceSq;
    }
    const Eigen::Vector4f delta =
        pPlayerCar->getWorldPosition() -
        pVehicle->getInfos().GroundMatrix.row( 3 ).transpose();
    return delta.squaredNorm();
}

bool AiVehLocoParked::handle( const int32_t event, const int32_t state ) {
    // FUN_00937510
    switch ( state ) {
    case PS_Inactive:
        if ( event != FE_Update ) {
            return false;
        }
        if ( kParkedActivateDistanceSq > computePlayerDistanceSq() ) {
            goTo( PS_Active );
        }
        return true;
    case PS_Active:
        if ( event != FE_Update ) {
            return false;
        }
        if ( computePlayerDistanceSq() > kParkedDeactivateDistanceSq ) {
            goTo( PS_Inactive );
        }
        return true;
    case PS_Parked:
        if ( event != FE_Update ) {
            return false;
        }
        gGameObjectDico.locate( pVehicle->getCarState(),
                                pVehicle->getInfos().LastRoadPos.pRoad, false );
        return true;
    default:
        return false;
    }
}
