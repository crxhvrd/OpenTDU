#include "shared.h"
#include "ai_veh_nav_trainer.h"

#include "ai/ai_debug_draw.h"
#include "ai/ai_vehicle.h"
#include "core/maths_helper.h"
#include "debug/ed_debug_text.h"
#include "game/vehicle/car_state.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/draw_list.h"

static constexpr float kSpeedTolerance = 1.0f;          // DAT_00f4165c
static constexpr float kBrakeStoppedSpeed = 0.1f;       // DAT_00faa7d8
static constexpr float kBrakeReapplySpeed = 2.0f;       // DAT_00f416b0
static constexpr float kBrakeStableTime = 0.1f;         // DAT_00f41668
static constexpr float kSpeedGradTolerance = 0.01f;     // DAT_00f416d4
static constexpr float kTargetSpeedMargin = 1.1f;       // DAT_00f4174c
static constexpr float kTurnWarmUpTime = 10.0f;         // DAT_00f4166c
static constexpr float kTurnMinSpeed = 4.0f;            // DAT_00faa7d4
static constexpr float kTurnStallAccel = 0.05f;         // DAT_00faa7cc
static constexpr float kTurnStallTimeout = 5.0f;        // DAT_00f41750
static constexpr float kTurnGripLossTime = 0.25f;       // DAT_00f416dc
static constexpr float kSettleTimeout = 20.0f;          // DAT_00f03ee0
static constexpr float kSettleSpeedError = 0.01f;       // DAT_00faa7c8
static constexpr float kSettleTime = 5.0f;              // DAT_00faa7c0
static constexpr float kSettleMaxRadiusError = 100.0f;  // DAT_00faa7c4
static constexpr float kCircleRadiusTolerance = 0.2f;   // DAT_00faa7bc
static constexpr float kCircleSpeedUpDelay = 1.0f;      // DAT_00f4165c
static constexpr float kAccelThrottle = 0.5f;           // DAT_00faa7b8
static constexpr float kAccelMovingSpeed = 1.0f;        // DAT_00f4165c
static constexpr float kAccelSampleStart = 10.0f;       // DAT_00f4166c
static constexpr float kAccelMaxDistance = 1000.0f;     // DAT_00faa7b4
static constexpr float kStopDistMinTime = 0.1f;         // DAT_00f41668
static constexpr float kPlacementDelay = 0.2f;          // DAT_00f41730
static constexpr float kSlipThreshold = 1.0f;           // DAT_00f4165c
static constexpr float kSmoothAccelCoef = 0.98f;        // DAT_00faa7e4
static constexpr float kSmoothSpeedCoef = 0.5f;         // DAT_00faa7e0
static constexpr float kCircleLeadTime = 0.25f;         // DAT_00faa7dc
static constexpr float kSteerCircleStartRatio = 0.5f;   // DAT_00f41674
static constexpr float kTurnTestSpeed = 0.5f;           // DAT_00f41674
static constexpr float kSustainStableTime = 1.0f;       // DAT_00f4165c
static constexpr float kBrakeStartThrottle = 1.0f;      // DAT_00f4165c
static constexpr float kInvHalfTurnDeg = 1.0f / 180.0f; // DAT_00f4173c
static constexpr float kInitialMinSpeed = 1000000.0f;   // DAT_00f41814
static constexpr float kDebugShapeHeight = 0.1f;        // DAT_00f41668
static constexpr float kCircleDrawStep = kPI / 16.0f;   // DAT_00f41f64
static constexpr float kTrackMarkerSize = 0.1f;         // DAT_00faa7e8
static constexpr float kTrackMarkerLength = 2.0f;       // DAT_00f416b0

static bool gbTrainerDetectFullSlide = true;  // DAT_00faa7d0
static bool gbTrainerDetectOverSteer = true;  // DAT_00faa7d1
static bool gbTrainerDetectUnderSteer = true; // DAT_00faa7d2

static const char *kSequenceNames[] = {
    "SEQ_NOTHING",
    "SEQ_SUSTAIN_1",
    "SEQ_SUSTAIN_2",
    "SEQ_BRAKE_H1",
    "SEQ_BRAKE_H2",
    "SEQ_SPEED_GRAD1",
    "SEQ_SPEED_GRAD2",
    "SEQ_TURNING_SPEED",
    "SEQ_TURNING_SPEED1",
    "SEQ_STEER_ALT_1",
    "SEQ_STEER_ALT_2",
    "SEQ_STEER_CIRCLE",
    "SEQ_ACCEL",
    "SEQ_CMP_LOCO_PHYSIC",
    "SEQ_CMP_LOCO_MANUAL",
    "SEQ_STOP_DIST",
    "SEQ_VEH_PLACEMENT",
    "SEQ_CMP_LOCO2_PHYSIC",
    "SEQ_CMP_LOCO2_MANUAL",
    "SEQ_ERROR",
};

static inline Eigen::Vector4f GetCarPosition( CarState *pCarState ) {
    return pCarState->getWorldMatrix().row( 3 ).transpose();
}

static inline Eigen::Vector4f GetCarForward( CarState *pCarState ) {
    return pCarState->getWorldMatrix().row( 2 ).transpose();
}

void AiVehNavTrainer::SampleTrack::clear() {
    // FUN_0095b7f0
    Samples.clear();
    Samples.shrink_to_fit();
    LastTime = -1.0f;
}

void AiVehNavTrainer::SampleTrack::record( CarState *pCarState,
                                           const float time ) {
    // FUN_0095c390
    if ( LastTime >= 0.0f && time - LastTime < Interval ) {
        return;
    }
    LastTime = time;

    Sample sample;
    sample.Position = GetCarPosition( pCarState );
    sample.Velocity = pCarState->getLinearVelocity();
    sample.Time = time;
    Samples.push_back( sample );
}

AiVehNavTrainer::AiVehNavTrainer()
    : AiVehNavigation(),
      AiFsm(),
      pOwner( nullptr ),
      bShowDebug( false ),
      sequenceAfterPlacement( SEQ_Nothing ),
      paramA( 0.0f ),
      paramB( 0.0f ),
      paramC( 0.0f ),
      paramD( 0.0f ),
      paramCount( 0 ),
      startMatrix( Matrix4x4f_t::Identity() ),
      startTime( -1.0f ),
      simTime( 0.0f ),
      slidingTime( 0.0f ),
      overSteerTime( 0.0f ),
      underSteerTime( 0.0f ),
      stallStartTime( -1.0f ),
      stallTime( 0.0f ),
      minSpeed( kInitialMinSpeed ),
      maxSpeed( 0.0f ),
      meanSpeed( 0.0f ),
      instSpeed( 0.0f ),
      smoothSpeed( 0.0f ),
      instAccel( 0.0f ),
      smoothAccel( 0.0f ),
      stableStartTime( -1.0f ),
      stableTime( 0.0f ),
      stepStartTime( 0.0f ),
      stepTime( 0.0f ),
      pointA( Eigen::Vector4f::Zero() ),
      pointB( Eigen::Vector4f::Zero() ),
      pointC( Eigen::Vector4f::Zero() ),
      distance( 0.0f ),
      result0( 0.0f ),
      result1( 0.0f ),
      stepCount( 0 ),
      tracks(),
      targetSpeed( 0.0f ),
      stop() {
    // FUN_0095c120
    goTo( SEQ_Nothing );
}

void AiVehNavTrainer::attach( AiVehicle *pVehicle ) {
    // FUN_0095c060
    pOwner = pVehicle;
    reset( false );
    paramA = 0.0f;
    tracks[0] = SampleTrack();
    tracks[1] = SampleTrack();
}

void AiVehNavTrainer::reset( const bool bFull ) {
    // FUN_0095ba90
    goTo( SEQ_Nothing );
    resetStats();
}

void AiVehNavTrainer::resetStats() {
    // FUN_0095b770
    startTime = -1.0f;
    simTime = 0.0f;
    slidingTime = 0.0f;
    overSteerTime = 0.0f;
    underSteerTime = 0.0f;
    minSpeed = kInitialMinSpeed;
    maxSpeed = 0.0f;
    meanSpeed = 0.0f;
    instSpeed = 0.0f;
    smoothSpeed = 0.0f;
    instAccel = 0.0f;
    smoothAccel = 0.0f;
}

void AiVehNavTrainer::tick( const float deltaTime, const float totalTime ) {
    // FUN_0095c440
    process( FE_Update );
    updateStats( deltaTime, totalTime );
}

void AiVehNavTrainer::detach() {
    // FUN_0095b700
    tracks[0] = SampleTrack();
    tracks[1] = SampleTrack();
}

bool AiVehNavTrainer::getTarget( Eigen::Vector4f &outDirection,
                                 float &outSpeed ) const {
    // FUN_0095bcc0
    switch ( getCurrentState() ) {
    case SEQ_Nothing:
    case SEQ_BrakeH2:
    case SEQ_Error:
        outDirection = GetCarForward( pOwner->getCarState() );
        outSpeed = 0.0f;
        return true;
    case SEQ_Sustain1:
    case SEQ_Sustain2:
    case SEQ_BrakeH1:
    case SEQ_CmpLoco2Physic:
    case SEQ_CmpLoco2Manual:
        outDirection = GetCarForward( pOwner->getCarState() );
        outSpeed = paramA;
        return true;
    case SEQ_SpeedGrad1:
    case SEQ_SpeedGrad2:
    case SEQ_CmpLocoPhysic:
    case SEQ_CmpLocoManual:
    case SEQ_StopDist:
        outDirection = GetCarForward( pOwner->getCarState() );
        outSpeed = targetSpeed;
        return true;
    case SEQ_TurningSpeed:
    case SEQ_TurningSpeed1:
    case SEQ_SteerCircle:
        outDirection = computeCircleDirection( pointA, paramA );
        outSpeed = targetSpeed;
        return true;
    case SEQ_SteerAlt1:
        outDirection = pointA;
        outSpeed = paramA;
        return true;
    case SEQ_SteerAlt2:
        outDirection = pointB;
        outSpeed = paramA;
        return true;
    case SEQ_Accel:
        outDirection = GetCarForward( pOwner->getCarState() );
        outSpeed = paramB;
        return true;
    default:
        return false;
    }
}

void AiVehNavTrainer::getHeightAndTwist( float &outHeight,
                                         float &outTwist ) const {
    // FUN_00923420
    outTwist = 0.0f;
    PhysicsVehicle *pPhysics = pOwner->getCarState()->getPhysicsVehicle();
    if ( pPhysics == nullptr ) {
        outHeight = pOwner->getInfos().GroundMatrix( 3, 1 );
        return;
    }
    const Matrix4x4f_t chassis = pPhysics->getChassisWorldMatrix();
    outHeight = chassis( 3, 1 ) -
                chassis( 1, 1 ) * pPhysics->computeGroundToOriginOffset();
}

void AiVehNavTrainer::getAccelResults( float &outMaxSpeed,
                                       float &outMeanAccel ) const {
    // FUN_0095b6a0
    outMaxSpeed = result0;
    outMeanAccel = result1;
}

void AiVehNavTrainer::getTurnResult( float &outMaxSpeed ) const {
    // FUN_0095b6c0
    outMaxSpeed = result0;
}

void AiVehNavTrainer::getBrakeResults( float &outTime,
                                       float &outDistance ) const {
    // FUN_0095b6d0
    outTime = stepTime;
    outDistance = distance;
}

void AiVehNavTrainer::drawDebugText() const {
    // FUN_0095b890
    const int32_t sequence = getCurrentState();
    if ( sequence >= SEQ_Nothing && sequence <= SEQ_Error ) {
        gEdDebugText.drawText( kSequenceNames[sequence] );
    }
    gEdDebugText.drawText( "Simu time: %.2f sec.", simTime );
    gEdDebugText.drawText( "Sliding time: %.2f sec.", slidingTime );
    gEdDebugText.drawText( "OverSteer time: %.2f sec.", overSteerTime );
    gEdDebugText.drawText( "UnderSteer time: %.2f sec.", underSteerTime );
    gEdDebugText.drawText( "Mean speed: %.2f m/s", meanSpeed );
    gEdDebugText.drawText( "Inst speed: %.2f m/s", instSpeed );
    gEdDebugText.drawText( "Smooth speed: %.2f m/s", smoothSpeed );
    gEdDebugText.drawText( "Inst accel: %.2f m/s", instAccel );
    gEdDebugText.drawText( "Smooth accel: %.2f m/s", smoothAccel );
}

static void DrawSampleTrack( const AiVehNavTrainer::SampleTrack &track,
                             const uint32_t color,
                             DrawList &drawList ) {
    // FUN_0095c1d0
    const int32_t numSamples = static_cast<int32_t>( track.Samples.size() );
    if ( numSamples <= 0 ) {
        return;
    }

    drawList.setDiffuse( 0, ToDiffuse( color ) );
    drawList.setActiveMaterial( nullptr );
    if ( !drawList.beginPrimitive( PT_Triangle, numSamples * 3 ) ) {
        return;
    }
    for ( const AiVehNavTrainer::Sample &sample : track.Samples ) {
        const Eigen::Vector4f position =
            sample.Position - GetShiftWorldPosition();
        const float tipZ = kTrackMarkerSize * kTrackMarkerLength + position.z();
        drawList.pushVertex( position.x(), position.y(), position.z() );
        drawList.pushVertex( position.x() - kTrackMarkerSize, position.y(),
                             tipZ );
        drawList.pushVertex( position.x() + kTrackMarkerSize, position.y(),
                             tipZ );
    }
    drawList.commitPrimitive();
}

void AiVehNavTrainer::debugDraw( DrawList *pDrawList ) {
    // FUN_0095c470
    const Eigen::Vector4f offset =
        -GetShiftWorldPosition() +
        Eigen::Vector4f( 0.0f, kDebugShapeHeight, 0.0f, 0.0f );

    switch ( getCurrentState() ) {
    case SEQ_TurningSpeed:
    case SEQ_TurningSpeed1:
    case SEQ_SteerCircle: {
        DrawDebugSphereMarker( pointA + offset, 1.0f, 0xff0000ff, *pDrawList );
        float angle = 0.0f;
        float nextAngle = 0.0f;
        do {
            nextAngle = angle + kCircleDrawStep;
            const Eigen::Vector4f start =
                pointA + Eigen::Vector4f( cosf( angle ) * paramA, 0.0f,
                                          sinf( angle ) * paramA, 0.0f );
            const Eigen::Vector4f end =
                pointA + Eigen::Vector4f( cosf( nextAngle ) * paramA, 0.0f,
                                          sinf( nextAngle ) * paramA, 0.0f );
            DrawDebugLine( start + offset, end + offset, 0xffff00ff,
                           *pDrawList );
            angle = nextAngle;
        } while ( nextAngle <= kTwoPI );
        break;
    }
    case SEQ_StopDist:
        DrawDebugLine( pointA + offset, pointC + offset, 0xff0000ff,
                       *pDrawList );
        break;
    default:
        break;
    }

    DrawSampleTrack( tracks[0], 0xff0000ff, *pDrawList );
    DrawSampleTrack( tracks[1], 0xffff0000, *pDrawList );
}

void AiVehNavTrainer::updateStats( const float deltaTime,
                                   const float totalTime ) {
    // FUN_0095be60
    if ( startTime < 0.0f ) {
        startTime = totalTime;
    }
    const float elapsed = totalTime - startTime;
    simTime = elapsed + deltaTime;

    float rearSlip = 0.0f;
    float frontSlip = 0.0f;
    float maxSlip = 0.0f;
    pOwner->getAxleSlipRatios( rearSlip, frontSlip, maxSlip );
    if ( ( rearSlip > kSlipThreshold && frontSlip > kSlipThreshold ) ||
         ( rearSlip < -kSlipThreshold && frontSlip < -kSlipThreshold ) ) {
        slidingTime += deltaTime;
    }
    if ( pOwner->isOverSteering() ) {
        overSteerTime += deltaTime;
    }
    if ( pOwner->isUnderSteering() ) {
        underSteerTime += deltaTime;
    }

    Eigen::Vector4f velocity = pOwner->getCarState()->getLinearVelocity();
    velocity.y() = 0.0f;
    const float speed = velocity.norm();
    if ( minSpeed > speed ) {
        minSpeed = speed;
    }
    if ( speed > maxSpeed ) {
        maxSpeed = speed;
    }

    instAccel = ( speed - instSpeed ) / deltaTime;
    smoothAccel = ( 1.0f - kSmoothAccelCoef ) * instAccel +
                  kSmoothAccelCoef * smoothAccel;
    smoothSpeed =
        ( 1.0f - kSmoothSpeedCoef ) * speed + kSmoothSpeedCoef * smoothSpeed;
    instSpeed = speed;
    meanSpeed = ( meanSpeed * elapsed + speed * deltaTime ) / simTime;
}

void AiVehNavTrainer::placeVehicle( const Matrix4x4f_t &matrix, float speed ) {
    // FUN_0095c7f0
    CarState *pCarState = pOwner->getCarState();
    if ( pCarState == nullptr ) {
        return;
    }
    PhysicsVehicle *pPhysics = pCarState->getPhysicsVehicle();
    if ( pPhysics == nullptr ) {
        return;
    }

    if ( speed < 0.0f ) {
        speed = paramA;
    }
    {
        PhysicsLock lock;
        pPhysics->resetToTransform( matrix, true );
        // sic: world Z axis, not the start heading
        pPhysics->setVelocity( Eigen::Vector3f( 0.0f, 0.0f, speed ), true, -1 );
    }
    pPhysics->setEngineState( eEngineState::ES_Running );
}

float AiVehNavTrainer::accumulateStableTime() {
    if ( stableStartTime < 0.0f ) {
        stableStartTime = simTime;
    }
    stableTime = simTime - stableStartTime;
    return stableTime;
}

Eigen::Vector4f
AiVehNavTrainer::computeCircleDirection( const Eigen::Vector4f &center,
                                         const float radius ) const {
    // FUN_0095bb60
    const Eigen::Vector4f carPosition = GetCarPosition( pOwner->getCarState() );
    Eigen::Vector4f radial = carPosition - center;
    radial.y() = 0.0f;
    radial /= radial.norm();

    const float angle = atan2f( -radial.z(), radial.x() ) +
                        kCircleLeadTime * smoothSpeed / radius;
    const Eigen::Vector4f target =
        Eigen::Vector4f( cosf( angle ) * radius, 0.0f, -sinf( angle ) * radius,
                         0.0f ) +
        center;
    const Eigen::Vector4f direction = target - carPosition;
    return direction / direction.norm();
}

float AiVehNavTrainer::computeFlatRadiusError() const {
    Eigen::Vector4f delta = pointA - GetCarPosition( pOwner->getCarState() );
    delta.y() = 0.0f;
    delta.w() = 0.0f;
    return fabsf( delta.norm() - paramA );
}

bool AiVehNavTrainer::handle( const int32_t event, const int32_t state ) {
    // FUN_0095c8d0
    CarState *pCarState = pOwner->getCarState();
    AiVehLocomotion *pLoco = pOwner->getLocomotion();
    AiVehicleInfo &infos = pOwner->getInfos();

    switch ( state ) {
    case SEQ_Nothing:
    case SEQ_Error:
        if ( event == FE_Enter ) {
            infos.bHandbrake = true;
            return true;
        }
        if ( event == FE_Exit ) {
            infos.bHandbrake = false;
            return true;
        }
        return false;

    case SEQ_Sustain1:
    case SEQ_BrakeH1:
        if ( event == FE_Enter ) {
            stableStartTime = -1.0f;
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        if ( fabsf( paramA - smoothSpeed ) < kSpeedTolerance ) {
            const float threshold = ( state == SEQ_Sustain1 ) ? paramD : paramC;
            if ( accumulateStableTime() > threshold ) {
                goTo( ( state == SEQ_Sustain1 ) ? SEQ_Sustain2 : SEQ_BrakeH2 );
            }
        } else {
            stableStartTime = -1.0f;
        }
        return true;

    case SEQ_Sustain2:
        if ( event == FE_Enter ) {
            stepStartTime = simTime;
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        stepTime = simTime - stepStartTime;
        if ( stepTime >= paramC ) {
            goTo( SEQ_Nothing );
        }
        return true;

    case SEQ_BrakeH2:
        if ( event == FE_Enter ) {
            stepStartTime = simTime;
            stableStartTime = -1.0f;
            pointA = GetCarPosition( pCarState );
            pointB = pointA;
            distance = 0.0f;
            pLoco->setDrivenByNavigation( false );
            pLoco->setManualInputs( 0.0f, 1.0f, 0.0f, 0.0f, 0.0f );
            return true;
        }
        if ( event == FE_Update ) {
            if ( kBrakeReapplySpeed > instSpeed ) {
                pLoco->setManualInputs( 0.0f, 1.0f, 0.0f, 0.0f, 0.0f );
            }
            stepTime = simTime - stepStartTime;
            pointB = GetCarPosition( pCarState );
            distance = ( pointB - pointA ).norm();
            if ( kBrakeStoppedSpeed > fabsf( instSpeed ) ) {
                if ( accumulateStableTime() > paramD ) {
                    stepTime -= stableTime;
                    goTo( SEQ_Nothing );
                }
            } else {
                stableStartTime = -1.0f;
            }
            return true;
        }
        if ( event == FE_Exit ) {
            pLoco->setDrivenByNavigation( true );
            return true;
        }
        return false;

    case SEQ_SpeedGrad1:
        if ( event == FE_Enter ) {
            stableStartTime = -1.0f;
            stepStartTime = simTime;
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        stepTime = simTime - stepStartTime;
        if ( targetSpeed * kSpeedGradTolerance >=
             fabsf( smoothSpeed - targetSpeed ) ) {
            if ( accumulateStableTime() > paramC ) {
                stepTime -= stableTime;
                goTo( SEQ_SpeedGrad2 );
            }
        } else {
            stableStartTime = -1.0f;
        }
        return true;

    case SEQ_SpeedGrad2:
        if ( event != FE_Enter ) {
            return false;
        }
        if ( paramB > 0.0f ) {
            if ( targetSpeed >= paramA ) {
                goTo( SEQ_Nothing );
                return true;
            }
            targetSpeed += paramB;
            if ( targetSpeed >= paramA ) {
                targetSpeed = paramA;
            }
        } else {
            if ( 0.0f >= targetSpeed ) {
                goTo( SEQ_Nothing );
                return true;
            }
            targetSpeed += paramB;
            if ( 0.0f >= targetSpeed ) {
                targetSpeed = 0.0f;
            }
        }
        goTo( SEQ_SpeedGrad1 );
        return true;

    case SEQ_TurningSpeed: {
        if ( event == FE_Enter ) {
            stableStartTime = -1.0f;
            stallStartTime = -1.0f;
            stallTime = -1.0f;
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        stepTime = simTime - stepStartTime;
        targetSpeed = smoothSpeed * kTargetSpeedMargin + 1.0f;
        distance = computeFlatRadiusError();

        if ( ( simTime > kTurnWarmUpTime || smoothSpeed > kTurnMinSpeed ) &&
             kTurnStallAccel > smoothAccel ) {
            if ( stallStartTime < 0.0f ) {
                stallStartTime = simTime;
            }
            stallTime = simTime - stallStartTime;
        } else {
            stallStartTime = -1.0f;
        }
        if ( stallTime > kTurnStallTimeout ) {
            goTo( SEQ_Error );
        }

        const bool bGripLost =
            ( gbTrainerDetectUnderSteer && pOwner->isUnderSteering() ) ||
            ( gbTrainerDetectOverSteer && pOwner->isOverSteering() ) ||
            ( gbTrainerDetectFullSlide && pOwner->isFullSliding() );
        if ( bGripLost && smoothSpeed > kTurnMinSpeed ) {
            if ( stableStartTime < 0.0f ) {
                result0 = smoothSpeed;
                stableStartTime = simTime;
            }
            stableTime = simTime - stableStartTime;
            if ( stableTime > kTurnGripLossTime ) {
                goTo( SEQ_Nothing );
            }
            return true;
        }
        stableStartTime = -1.0f;
        return true;
    }

    case SEQ_TurningSpeed1: {
        if ( event == FE_Enter ) {
            stepStartTime = -1.0f;
            stableStartTime = simTime;
            return true;
        }
        if ( event == FE_Exit ) {
            stepStartTime = simTime - stepTime;
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        stableTime = simTime - stableStartTime;
        if ( stableTime > kSettleTimeout ) {
            goTo( SEQ_Error );
        }

        const int32_t shiftState =
            pCarState->getPhysicsVehicle()->getDrivingGearbox().ShiftState;
        const bool bShifting = shiftState != PhysicsGearboxSim::SS_Settle &&
                               shiftState != PhysicsGearboxSim::SS_InGear;
        const float speedError = pOwner->getTrafficLocomotion()
                                     .getSpeedControl()
                                     .getErrorProportional();
        if ( !bShifting && kSettleSpeedError >= fabsf( speedError ) ) {
            if ( stepStartTime < 0.0f ) {
                stepStartTime = simTime;
            }
            if ( simTime - stepStartTime >= kSettleTime ) {
                goTo( SEQ_TurningSpeed );
            }
        } else {
            stepStartTime = -1.0f;
        }

        distance = computeFlatRadiusError();
        if ( distance >= kSettleMaxRadiusError ) {
            goTo( SEQ_Error );
        }
        return true;
    }

    case SEQ_SteerAlt1:
        if ( event == FE_Enter ) {
            stepStartTime = simTime;
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        if ( simTime - stepStartTime >= paramC ) {
            goTo( SEQ_SteerAlt2 );
        }
        return true;

    case SEQ_SteerAlt2:
        if ( event == FE_Enter ) {
            stepStartTime = simTime;
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        if ( simTime - stepStartTime >= paramC ) {
            stepCount++;
            goTo( ( stepCount >= paramCount ) ? SEQ_Nothing : SEQ_SteerAlt1 );
        }
        return true;

    case SEQ_SteerCircle: {
        if ( event == FE_Enter ) {
            stepStartTime = -1.0f;
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        pointB = GetCarPosition( pCarState );
        distance = ( pointB - pointA ).norm() - paramA;
        if ( kCircleRadiusTolerance < distance ) {
            stepStartTime = -1.0f;
            return true;
        }
        if ( stepStartTime < 0.0f ) {
            stepStartTime = simTime;
        }
        const float onCircleTime = simTime - stepStartTime;
        if ( onCircleTime >= kCircleSpeedUpDelay && paramB > targetSpeed ) {
            targetSpeed = smoothSpeed * kTargetSpeedMargin + 1.0f;
            if ( targetSpeed > paramB ) {
                targetSpeed = paramB;
            }
        }
        if ( onCircleTime >= paramC ) {
            goTo( SEQ_Nothing );
        }
        return true;
    }

    case SEQ_Accel: {
        if ( event == FE_Enter ) {
            pLoco->setDrivenByNavigation( false );
            pLoco->setManualInputs( kAccelThrottle, 0.0f, 0.0f, 0.0f, 0.0f );
            pointA = GetCarPosition( pCarState );
            result0 = kAccelSampleStart;
            result1 = 0.0f;
            return true;
        }
        if ( event == FE_Exit ) {
            pLoco->setDrivenByNavigation( true );
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        if ( kAccelMovingSpeed > pCarState->getBody()->SpeedInKmh ) {
            pointA = GetCarPosition( pCarState );
            return true;
        }

        stepTime = simTime - stepStartTime;
        pointB = GetCarPosition( pCarState );
        distance = ( pointB - pointA ).norm();

        static const float sMinFinalSpeed =
            ( kAccelSampleStart > paramA ) ? paramA : kAccelSampleStart;
        if ( distance > result0 ) {
            result0 += 1.0f;
            result1 +=
                pCarState->getPhysicsVehicle()->getLocalAcceleration().norm();
        }
        if ( instSpeed < paramB && distance <= kAccelMaxDistance ) {
            return true;
        }
        if ( instSpeed > sMinFinalSpeed ) {
            result1 /= result0 - kAccelSampleStart;
            result0 = instSpeed;
            goTo( SEQ_Nothing );
        } else {
            goTo( SEQ_Error );
        }
        return true;
    }

    case SEQ_CmpLocoPhysic:
    case SEQ_CmpLocoManual: {
        const bool bPhysic = state == SEQ_CmpLocoPhysic;
        if ( event == FE_Enter ) {
            if ( bPhysic ) {
                tracks[0].clear();
                tracks[1].clear();
                if ( pLoco != nullptr ) {
                    pLoco->setPhysicsActive( true, false );
                }
            }
            placeVehicle( startMatrix, -1.0f );
            targetSpeed = paramB;
            stepStartTime = simTime;
            return true;
        }
        if ( event == FE_Exit ) {
            if ( bPhysic ) {
                return false;
            }
            if ( pLoco != nullptr ) {
                pLoco->setPhysicsActive( true, false );
            }
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        tracks[bPhysic ? 0 : 1].record( pCarState, simTime - stepStartTime );
        if ( !bPhysic && pOwner->isPhysicsDriven() && pLoco != nullptr ) {
            pLoco->setPhysicsActive( false, false );
        }
        if ( targetSpeed == paramB && instSpeed >= paramB ) {
            targetSpeed = paramA;
            return true;
        }
        if ( targetSpeed == paramA && paramA >= instSpeed ) {
            goTo( bPhysic ? SEQ_CmpLocoManual : SEQ_Nothing );
        }
        return true;
    }

    case SEQ_StopDist:
        if ( event == FE_Enter ) {
            stop.State = 1;
            stop.InitialDistance = paramB;
            stop.RemainingDistance = paramB;
            pointA = GetCarPosition( pCarState );
            pointC = pointA + GetCarForward( pCarState ) * paramB;
            pointC.y() = pointA.y();
            targetSpeed = paramA;
            return true;
        }
        if ( event == FE_Update ) {
            pointB = GetCarPosition( pCarState );
            distance = ( pointA - pointB ).norm();
            stepTime = simTime - stepStartTime;
            stop.BrakingDistance =
                pOwner->computeBrakingDistance( infos.Speed, 0.0f, 0.0f );
            stop.RemainingDistance = paramB - distance;
            targetSpeed = paramA;
            ApplyStopRamp( stop, infos.bHandbrake, targetSpeed, false );
            if ( instSpeed == 0.0f && stepTime > kStopDistMinTime ) {
                goTo( SEQ_Nothing );
            }
            return true;
        }
        if ( event == FE_Exit ) {
            stop.State = 0;
            stop.InitialDistance = 0.0f;
            stop.RemainingDistance = 0.0f;
            infos.bHandbrake = false;
            return true;
        }
        return false;

    case SEQ_VehPlacement:
        if ( event == FE_Enter ) {
            stepStartTime = simTime;
            infos.bHandbrake = true;
            return true;
        }
        if ( event == FE_Update ) {
            if ( simTime - stepStartTime > kPlacementDelay ) {
                goTo( sequenceAfterPlacement );
            }
            return true;
        }
        if ( event == FE_Exit ) {
            infos.bHandbrake = false;
            return true;
        }
        return false;

    case SEQ_CmpLoco2Physic:
    case SEQ_CmpLoco2Manual: {
        const bool bPhysic = state == SEQ_CmpLoco2Physic;
        if ( event == FE_Enter ) {
            stepStartTime = simTime;
            if ( pLoco != nullptr ) {
                pLoco->setPhysicsActive( bPhysic, false );
            }
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        stepTime = simTime - stepStartTime;
        if ( simTime > paramC ) {
            goTo( SEQ_Nothing );
        } else if ( stepTime > paramD ) {
            goTo( bPhysic ? SEQ_CmpLoco2Manual : SEQ_CmpLoco2Physic );
        }
        return true;
    }

    default:
        return false;
    }
}

void AiVehNavTrainer::startSustainSpeed( const Matrix4x4f_t &start,
                                         const float speed,
                                         const float duration ) {
    // FUN_0095e0b0
    paramA = speed;
    paramC = duration;
    paramD = kSustainStableTime;
    placeVehicle( start, -1.0f );
    reset( true );
    goTo( SEQ_Sustain1 );
}

void AiVehNavTrainer::startBrakeTest( const Matrix4x4f_t &start,
                                      const float speed,
                                      const float stableTime ) {
    // FUN_0095e040
    paramA = speed;
    paramC = stableTime;
    paramD = kBrakeStableTime;
    placeVehicle( start, -1.0f );
    AiVehLocomotion *pLoco = pOwner->getLocomotion();
    pLoco->reset( true );
    pLoco->setPadThrottle( kBrakeStartThrottle );
    reset( true );
    goTo( SEQ_BrakeH1 );
}

void AiVehNavTrainer::startSpeedGradations( const Matrix4x4f_t &start,
                                            const float maxSpeed,
                                            const float step,
                                            const float stableTime ) {
    // FUN_0095dfe0
    paramC = stableTime;
    paramA = maxSpeed;
    paramB = step;
    targetSpeed = ( step > 0.0f ) ? 0.0f : maxSpeed;
    placeVehicle( start, targetSpeed );
    reset( true );
    goTo( SEQ_SpeedGrad1 );
}

void AiVehNavTrainer::startTurnTest( const Matrix4x4f_t &start,
                                     const float radius ) {
    // FUN_0095def0
    paramA = radius;
    paramB = kTurnTestSpeed;
    targetSpeed = 0.0f;
    placeVehicle( start, 0.0f );
    pointA = ( start.row( 3 ) + start.row( 0 ) * radius ).transpose();
    reset( true );
    stepStartTime = simTime;
    stepTime = 0.0f;
    goTo( SEQ_TurningSpeed );
}

void AiVehNavTrainer::startSteerAlternate( const Matrix4x4f_t &start,
                                           const float speed,
                                           const float angleDeg,
                                           const float duration,
                                           const int32_t count ) {
    // FUN_0095ddc0
    paramA = speed;
    paramB = angleDeg * kInvHalfTurnDeg * kPI;
    paramCount = count;
    paramC = duration;
    placeVehicle( start, -1.0f );

    const Eigen::Vector4f forward = start.row( 2 ).transpose();
    pointA = forward;

    // forward * RotateY(paramB) (FUN_005033a0 + FUN_00404a80)
    const float cosAngle = cosf( paramB );
    const float sinAngle = sinf( paramB );
    pointB = Eigen::Vector4f(
        forward.x() * cosAngle + forward.z() * sinAngle, forward.y(),
        forward.z() * cosAngle - forward.x() * sinAngle, forward.w() );

    reset( true );
    stepStartTime = simTime;
    stepTime = 0.0f;
    stepCount = 0;
    goTo( SEQ_SteerAlt1 );
}

void AiVehNavTrainer::startSteerCircle( const Matrix4x4f_t &start,
                                        const float offset,
                                        const float radius,
                                        const float maxSpeed,
                                        const float duration ) {
    // FUN_0095dcb0
    paramA = radius;
    paramB = maxSpeed;
    targetSpeed = maxSpeed * kSteerCircleStartRatio;
    paramC = duration;
    paramD = offset;
    placeVehicle( start, targetSpeed );
    pointA =
        ( start.row( 3 ) + start.row( 0 ) * ( offset + radius ) ).transpose();
    reset( true );
    stepStartTime = simTime;
    stepTime = 0.0f;
    goTo( SEQ_SteerCircle );
}

void AiVehNavTrainer::startAccelTest( const Matrix4x4f_t &start,
                                      const float startSpeed,
                                      const float maxSpeed ) {
    // FUN_0095dc60
    paramA = startSpeed;
    paramB = maxSpeed;
    placeVehicle( start, -1.0f );
    reset( true );
    stepStartTime = simTime;
    stepTime = 0.0f;
    goTo( SEQ_Accel );
}

void AiVehNavTrainer::startCompareLocos( const Matrix4x4f_t &start,
                                         const float lowSpeed,
                                         const float highSpeed ) {
    // FUN_0095b840
    paramA = lowSpeed;
    paramB = highSpeed;
    startMatrix = start;
    reset( true );
    stepStartTime = simTime;
    stepTime = 0.0f;
    goTo( SEQ_CmpLocoPhysic );
}

void AiVehNavTrainer::startStopDistance( const Matrix4x4f_t &start,
                                         const float speed,
                                         const float stopDistance ) {
    // FUN_0095db70
    paramA = speed;
    paramB = stopDistance;
    placeVehicle( start, -1.0f );
    reset( true );
    stepStartTime = simTime;
    stepTime = 0.0f;
    pointA = start.row( 3 ).transpose();
    pointC = pointA;
    distance = 0.0f;
    sequenceAfterPlacement = SEQ_StopDist;
    goTo( SEQ_VehPlacement );
}

void AiVehNavTrainer::startCompareLocos2( const Matrix4x4f_t &start,
                                          const float speed,
                                          const float totalTime,
                                          const float period ) {
    // FUN_0095dc10
    paramA = speed;
    paramC = totalTime;
    paramD = period;
    startMatrix = start;
    placeVehicle( start, -1.0f );
    reset( true );
    goTo( SEQ_CmpLoco2Physic );
}
