#include "shared.h"
#include "camera_vehicle.h"

#include "config/gs_config.h"
#include "core/maths_helper.h"
#include "gs_camera.h"
#include "gs_timer.h"
#include "game/vehicle/car_state.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/gs_render.h"
#include "render/postfx/gs_postfx.h"

#include <algorithm>
#include <cmath>
#include <utility>

uint32_t gEditCameraFlags = 0; // DAT_010e7498

static constexpr float kBrakeGSmooth =
    1.11111f; // DAT_00f41ddc (frame-based lerp gain)
static constexpr float kSpeedSmoothKeep = 0.975f; // _DAT_00fab3b4
static constexpr float kSpeedSmoothRate = 100.0f; // DAT_00f41708
static constexpr float kUpSmoothKeep =
    0.95f; // DAT_00fab3d0 (ground-up low-pass)
static constexpr float kBikeInterp =
    0.95f; // DAT_00fab3b8 (bike snap-back damping)
static constexpr float kGroundMinHeight =
    0.8f; // DAT_00f416d0 (raycast valid floor)
static constexpr float kGroundContactMin = 0.75f;   // DAT_00fab3d4
static constexpr float kNoHitSentinel = -100000.0f; // DAT_00f418fc
static constexpr float kFixedFwdStep =
    20.0f; // DAT_00f03ee0 (in-car target forward step)
static constexpr float kLeanCross = 1.0f; // DAT_00fab3c8
static constexpr float kLeanFwd = 1.3f;   // DAT_00fab3cc
static constexpr float kSpringPos =
    0.004f; // DAT_00f41a20  (PosMin/Max byte -> m)
static constexpr float kSpringInt16 =
    0.001f; // FLOAT_00f4167c (int16 fixed-point read)
static constexpr float kSpringInt16W =
    1000.0f; // DAT_00f41718  (fixed-point write)
static constexpr float kSpringForce = 0.01f; // DAT_00f416d4  (Scale byte)
static constexpr float kSpringByte =
    0.1f; // FLOAT_00f41668 (Spring/Damper byte)
static constexpr float kSpringTime = 0.4f;   // DAT_00f4175c  (TimeScale byte)
static constexpr float kSpringClamp = 0.04f; // DAT_00f41980  (ForceClamp byte)
static constexpr float kLookAxisEpsilon = 0.0f; // FLOAT_00f41660
static constexpr float kLookSnapHi = 0.1f;      // FLOAT_00f41668
static constexpr float kLookSnapLo = -0.1f;     // FLOAT_00f4188c
static constexpr float kHeadTrackRange =
    0.75f; // DAT_00f416a4 (fraction of the camera's angle range reachable by
           // head tracking)
static constexpr float kLookSmoothGain = 0.25f; // DAT_00f416dc
static constexpr float kLookInertiaBase =
    0.6f; // DAT_00fab3e4 (AngleInertia==0 still keeps 60% per step)
static constexpr float kLookLeadKeep = 0.99f;    // _DAT_00fab3e0
static constexpr float kLookLeadClamp = 0.5f;    // DAT_00fab3dc (rad)
static constexpr float kLookLeadScale = 0.5f;    // DAT_00fab3d8
static constexpr float kLookLeadDeadZone = 0.0f; // DAT_016a2cbc (never written)
                                                 //
float gSmoothedSpeed = 0.0f;                     // DAT_016a2ca0
Eigen::Vector4f gSmoothUp( 0.0f, 1.0f, 0.0f, 0.0f ); // _DAT_016a8240
bool gSmoothUpInitialized = false;                   // DAT_016a8250 & 1
FollowParams gWorkFollow; // DAT_00fa9484 (blended follow workspace)
bool gCameraSnap = false; // DAT_010e74a4 (snap this frame)

bool gbFreeLookUnlocked = true;            // BOOL_00f7c808
bool gbFixedCamLookSnap = false;           // BOOL_010e7484
static bool gbFreeLookUnlockedPrev = true; // DAT_00f7c809 (edge detect)
static float gLookAngleBase = 0.0f;    // DAT_016a2cb8 (frozen yaw when locked)
static int32_t gLookLockCounter = 0;   // DAT_016a2cb4
static float gLookLeadSmoothed = 0.0f; // DAT_016a2cc0 (look-into-corner state)

// Set by the ground-clamp / collision-avoidance pass at the end of the master
// tick and consumed by the NEXT frame's free-look step, which then stops
// smoothing the axis the clamp is currently fighting.
static bool gbCamClampedLeft = false;  // DAT_010e74a0
static bool gbCamClampedRight = false; // DAT_010e74a1
static bool gbCamClampedUp = false;    // DAT_010e74a2
static bool gbCamClampedDown = false;  // DAT_010e74a3

// NaturalPoint inputs (unimplemented)
static float gHeadTrackYaw = 0.0f;
static float gHeadTrackPitch = 0.0f;

static float QueryLookLeadInput( PhysicsVehicle *pPhys ) {
    // FUN_008d4a60 (inlined)
    if ( pPhys == nullptr || !pPhys->isActive() ) {
        return 0.0f;
    }
    OTDU_UNIMPLEMENTED; // TODO: PHYSICS
    return 0.0f;
}

void ToggleFreeLookLock( CameraVehicle *pCamera ) {
    // FUN_008d1e70
    if ( pCamera == nullptr ) {
        return;
    }
    if ( ( pCamera->getVehicleFlags() & 1 ) != 0 ) {
        gbFixedCamLookSnap = !gbFixedCamLookSnap;
        return;
    }
    gbFreeLookUnlocked = !gbFreeLookUnlocked;
}

static void FollowParamsInit( FollowParams &params,
                              const Matrix4x4f_t &carMatrix ) {
    // FUN_008d1ea0
    params.LastAngleY = WrapAngle(
        std::atan2( carMatrix.row( 2 ).x(), carMatrix.row( 2 ).z() ) );
}

static inline void YawSmooth( Matrix4x4f_t &frame,
                              FollowParams &work,
                              float deltaTime,
                              uint32_t flags ) {
    // FUN_008d53b0
    const float heading = std::atan2( frame.row( 2 ).x(), frame.row( 2 ).z() );
    const float delta = WrapAngle( heading - work.LastAngleY );
    const float newAngle =
        work.LastAngleY + work.CameraSmoothAngle * delta * deltaTime;
    if ( flags & 2 ) {
        const float a = newAngle - heading;
        const float c = std::cos( a );
        const float s = std::sin( a );
        const Eigen::Vector4f r0 = frame.row( 0 ).transpose();
        const Eigen::Vector4f r2 = frame.row( 2 ).transpose();
        frame.row( 0 ) = ( c * r0 + s * r2 ).transpose();
        frame.row( 2 ) = ( -s * r0 + c * r2 ).transpose();
    }
    work.LastAngleY = newAngle;
}

inline float BlendClamp( float a, float b, float t ) {
    // FUN_008d1a40
    const float lerped = ( b - a ) * t + a;
    const float lo = std::min( a, b );
    const float hi = std::max( a, b );
    return std::min( hi, std::max( lo, lerped ) );
}

static float SpringIntegrate( SpringParam &sp, float deltaTime, float forceG ) {
    // FUN_008d1d10
    const float posMaxM = (float)sp.PosMax * kSpringPos;
    const float posMinM = (float)sp.PosMin * kSpringPos;
    float force = (float)sp.Scale * kSpringForce * forceG;
    const float timeScale = (float)sp.TimeScale * kSpringTime;

    const float clampMag = (float)sp.ForceClamp * kSpringClamp;
    if ( clampMag < std::fabs( force ) ) {
        force = ( force <= 0.0f ? kMinusOne : kOne ) * clampMag;
    }

    const float vel = (float)sp.Velocity * kSpringInt16;
    const float pos = (float)sp.Position * kSpringInt16;
    float newVel = ( ( force * timeScale -
                       timeScale * (float)sp.Damper * kSpringByte * vel ) -
                     timeScale * (float)sp.Spring * kSpringByte * pos ) *
                       deltaTime +
                   vel;
    float newPos = newVel * deltaTime + pos;

    if ( newPos < -posMinM ) {
        newVel = 0.0f;
        newPos = -posMinM;
    }
    if ( posMaxM < newPos ) {
        newVel = 0.0f;
        newPos = posMaxM;
    }

    sp.Position = (int16_t)(int32_t)( newPos * kSpringInt16W );
    sp.Velocity = (int16_t)(int32_t)( newVel * kSpringInt16W );
    return newPos;
}

static Eigen::Vector4f QueryGroundNormal( CarState *pCar ) {
    // FUN_00848cb0
    Eigen::Vector4f up( 0.0f, 1.0f, 0.0f, 0.0f );
    if ( pCar == nullptr ) {
        return up;
    }
    for ( uint32_t i = 0; i < pCar->getNumWheels(); i++ ) {
        if ( pCar->getWheel( i ).bGrounded ) {
            return up;
        }
    }
    OTDU_UNIMPLEMENTED; // TODO: PHYSICS
    return up;
}

static inline int8_t floatRoundToInt8( float value ) {
    return static_cast<int8_t>( std::lroundf( value ) );
}

static inline void adjustSpringField( uint8_t &field, float delta ) {
    uint8_t newVal = static_cast<uint8_t>( field + floatRoundToInt8( delta ) );
    field = ( newVal == 0 ) ? 1 : newVal;
}

static inline void editSpringParam( SpringParam *sp,
                                    const EditCamDevState *state ) {
    // FUN_008d06e0
    adjustSpringField( sp->PosMin, state->stick0.Y * -2.0f );
    adjustSpringField( sp->PosMax, state->stick0.X * 2.0f );
    adjustSpringField( sp->Spring, state->stick1.Y * -2.0f );
    adjustSpringField( sp->Damper, state->stick1.X * 2.0f );
    if ( state->bButton3 )
        sp->Scale++;
    if ( state->bButton0 )
        sp->Scale--;
    if ( state->bPovRight )
        sp->ForceClamp++;
    if ( state->bPovLeft )
        sp->ForceClamp--;
    if ( state->bButton1 )
        sp->TimeScale++;
    if ( state->bButton2 )
        sp->TimeScale--;
}

CameraVehicle::CameraVehicle( int32_t param_2 ) : CameraGame() {
    // FUN_008d29f0
    follow.PosZ = -5.1f;             // DAT_00f417d8
    follow.Trg = 5.0f;               // DAT_00f41750
    follow.PosHeight = 1.7f;         // DAT_00f417d4
    follow.TrgHeight = 2.0f;         // DAT_00f416b0
    follow.CameraSmoothAngle = 5.0f; // DAT_00f41750
    follow.LeanFactor = 0.4f;        // DAT_00f4175c
    followFast = follow;

    nearPlane = 0.5f;    // FLOAT_00f41674
    farPlane = 40000.0f; // DAT_00f41c78
    flags &= 0xfffdffff;
    carID = 0x270e;
    vehicleFlags = 2;
    vehicleFov = 68;

    fovLerp.MinInput = 50;
    fovLerp.MaxInput = 150;
    fovLerp.MinOutput = 63;
    fovLerp.MaxOutput = 75;

    lastAngleHorizontal = 0.0f;
    lastAngleVertical = 0.0f;
    brakeG = 0.0f;
}

CameraVehicle::~CameraVehicle() {
    // FUN_00412120
}

void CameraVehicle::initialize() {
    // FUN_008d1fb0
    Matrix4x4f_t carMatrix = Matrix4x4f_t::Identity();
    if ( gpCamera != nullptr && gpCamera->getBoundCarState() != nullptr ) {
        carMatrix = gpCamera->getBoundCarState()->getBody()->WorldMatrix;
    }
    FollowParamsInit( follow, carMatrix );

    pBumps[0].Time = 0.0f;
    pBumps[1].Time = 0.0f;
    pBumps[2].Time = 0.0f;

    OTDU_UNIMPLEMENTED;

    bInitialized = true;
}

bool CameraVehicle::tick( float deltaTime, bool param_1, bool param_2 ) {
    // FUN_008d5fd0
    CarState *pCar =
        ( gpCamera != nullptr ) ? gpCamera->getBoundCarState() : nullptr;

    PhysicsVehicle *pPhys =
        ( gpCamera != nullptr ) ? gpCamera->getTargetPhysEntity() : nullptr;
    if ( pPhys == nullptr && pCar != nullptr ) {
        pPhys = pCar->getPhysicsVehicle();
    }

    const float nearSave = nearPlane;
    const float farSave = farPlane;

    if ( pCar == nullptr || pPhys == nullptr || !pPhys->isActive() ) {
        setPosition( Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ) );
        setTarget( Eigen::Vector4f( 0.0f, 0.0f, 1.0f, 1.0f ) );
        upVector = Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f );
        updateMatrix( worldPosition.head<3>(), upVector.head<3>(),
                      worldTarget.head<3>() );
        return false;
    }

    CarBodyState *pBody = pCar->getBody();

    velocity = pPhys->getChassisVelocity();

    OTDU_UNIMPLEMENTED_ONCE;

    float speed = pPhys->getForwardSpeed();
    if ( !gCameraSnap ) {
        speed = ( speed - gSmoothedSpeed ) * ( kOne - kSpeedSmoothKeep ) *
                    deltaTime * kSpeedSmoothRate +
                gSmoothedSpeed;
    }
    gSmoothedSpeed = speed;

    float fovRad;
    if ( ( vehicleFlags & 0x10 ) == 0 ) {
        fovRad = (float)vehicleFov * kDegToRad;
    } else {
        const float loIn = (float)fovLerp.MinInput;
        const float hiIn = (float)fovLerp.MaxInput;
        float t = ( hiIn > loIn ) ? ( speed - loIn ) / ( hiIn - loIn ) : 0.0f;
        t = std::min( 1.0f, std::max( 0.0f, t ) );
        const float fovDeg =
            (float)fovLerp.MinOutput +
            ( (float)fovLerp.MaxOutput - (float)fovLerp.MinOutput ) * t;
        vehicleFov = (uint8_t)fovDeg;
        fovRad = fovDeg * kDegToRad;
    }

    const Matrix4x4f_t carMatrix = pBody->OffsetedWorldMatrix;

    Eigen::Vector4f outPos, outTarget, outUp;
    const bool bFixed = ( vehicleFlags & 1 ) != 0;
    if ( bFixed ) {
        tickFixed( pCar, pPhys, deltaTime, outPos, outTarget, outUp );
    } else {
        float t = 0.0f;
        if ( ( vehicleFlags & 0x80 ) != 0 && (int32_t)gEditCameraFlags >= 0 ) {
            const float loIn = (float)fovLerp.MinInput;
            const float hiIn = (float)fovLerp.MaxInput;
            t = ( hiIn > loIn ) ? ( speed - loIn ) / ( hiIn - loIn ) : 0.0f;
            t = std::min( 1.0f, std::max( 0.0f, t ) );
        }
        gWorkFollow.PosZ = BlendClamp( follow.PosZ, followFast.PosZ, t );
        gWorkFollow.PosHeight =
            BlendClamp( follow.PosHeight, followFast.PosHeight, t );
        gWorkFollow.Trg = BlendClamp( follow.Trg, followFast.Trg, t );
        gWorkFollow.TrgHeight =
            BlendClamp( follow.TrgHeight, followFast.TrgHeight, t );
        gWorkFollow.LeanFactor =
            BlendClamp( follow.LeanFactor, followFast.LeanFactor, t );
        gWorkFollow.CameraSmoothAngle = BlendClamp(
            follow.CameraSmoothAngle, followFast.CameraSmoothAngle, t );
        gWorkFollow.GroundHeight =
            BlendClamp( follow.GroundHeight, followFast.GroundHeight, t );
        if ( gCameraSnap ) {
            gWorkFollow.LastAngleY = follow.LastAngleY;
        }
        tickFollow( carMatrix, pCar, pPhys, deltaTime, gCameraSnap, outPos,
                    outTarget, outUp );
    }

    OTDU_UNIMPLEMENTED_ONCE;

    setPositionOffseted( outPos );
    setTargetOffseted( outTarget );
    upVector = Eigen::Vector4f( outUp.x(), outUp.y(), outUp.z(), 0.0f );

    setFOV( fovRad );

    if ( ( vehicleFlags & 0x20 ) != 0 ) {
        applyFreeLook( carMatrix, bFixed, deltaTime );
    }

    gbCamClampedLeft = false;  // DAT_010e74a0
    gbCamClampedRight = false; // DAT_010e74a1
    gbCamClampedUp = false;    // DAT_010e74a2
    gbCamClampedDown = false;  // DAT_010e74a3

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS (ground clamp + collision sweep)

    if ( ( vehicleFlags & 0x40 ) != 0 ) {
        applySpeedShake( deltaTime );
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: POSTFX

    updateMatrix( worldPosition.head<3>(), upVector.head<3>(),
                  worldTarget.head<3>() );

    if ( ( flags & 0xc0000 ) == 0 ) {
        nearPlane = nearSave;
        farPlane = farSave;
    } else {
        nearPlane = 0.8f; // DAT_00f416d0
        farPlane = farSave;
        interiorNearPlane = 0.05f; // DAT_00f7c820
        interiorFarPlane = 3.0f;   // DAT_00f416b4
    }

    if ( gpCamera != nullptr ) {
        gpCamera->setNearPlane( nearPlane );
        gpCamera->setFarPlane( farSave );
        gpCamera->setEnableDepthAware( true );
        gpCamera->setInteriorNearPlane( interiorNearPlane );
        gpCamera->setInteriorFarPlane( interiorFarPlane );
        gpCamera->setEnableDepthAwareInterior( true );
    }

    gCameraSnap = false; // DAT_010e74a4 = 0
    return true;
}

void CameraVehicle::destroy() {
    // FUN_008d54a0
    if ( gpPostFX == nullptr || gpPostFX->shouldUpdateEffects() ) {
        return;
    }

    PostFXStep *pRadialBlur =
        gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>();
    if ( pRadialBlur == nullptr ) {
        return;
    }
    pRadialBlur->getInput()->setEnabled( false );

    PostFXStep *pFakeDOF = gpPostFX->getEffect<ePostFXStep::PFXS_FakeDOF>();
    const bool bAnyBlurEnabled =
        pRadialBlur->getInput()->isEnabled() ||
        ( pFakeDOF != nullptr && pFakeDOF->getInput()->isEnabled() );

    // The sun/flare scene is culled  while no blur is running.
    RenderScene *pSunScene =
        gpRender->getRenderPass<eRenderPass::RP_Sun>().pScene;
    pSunScene->setFlags( bAnyBlurEnabled
                             ? ( pSunScene->getFlags() & 0xffffffdf )
                             : ( pSunScene->getFlags() | 0x20 ) );
}

void CameraVehicle::unknownFunc( float *param_1 ) {
    // FUN_008d0b60
    const EditCamDevState *state =
        reinterpret_cast<const EditCamDevState *>( param_1 );

    // Scale factor: trigger0 in [0,1] → fScale in [1,3]
    const float fScale = state->trigger0 * 2.0f + 1.0f;

    float savedNear = nearPlane;
    float savedFar = farPlane;

    // 0x40000000 — near/far plane
    if ( gEditCameraFlags & 0x40000000 ) {
        nearPlane += state->stick0.Y * fScale * 0.01f;
        farPlane += state->stick1.Y * fScale * 100.0f;
    }

    // 0x20 — angle range / neutral radius
    if ( gEditCameraFlags & 0x20 ) {
        angleRangeHorizontal += floatRoundToInt8( fScale * state->stick0.X );
        neutralRadius.MaxOutput += floatRoundToInt8( fScale * state->stick1.X );
        maxAngleDown += floatRoundToInt8( fScale * state->stick0.Y );
        maxAngleUp += floatRoundToInt8( fScale * state->stick1.Y );
    }

    // 0x40 — shake parameters
    if ( gEditCameraFlags & 0x40 ) {
        if ( !state->bTrigger1Digital ) {
            shakeSpeedSlow += floatRoundToInt8( fScale * state->stick0.X );
            shakeMaxSpeed += floatRoundToInt8( fScale * state->stick1.X );
        } else {
            shakeSpeedFast += floatRoundToInt8( fScale * state->stick0.X );
            shakeMaxSpeed2 += static_cast<uint8_t>(
                floatRoundToInt8( fScale * state->stick1.X ) );
        }
        shakeRPMPercent += floatRoundToInt8( fScale * state->stick0.Y );

        const float fShakeScale = 0.1f * fScale;
        if ( !state->bTrigger1Digital ) {
            shakeMagnitude.x() += (float)( (int)( (uint8_t)state->bButton1 -
                                                  (uint8_t)state->bButton2 ) ) *
                                  fShakeScale;
            shakeMagnitude.z() += (float)( (int)( (uint8_t)state->bButton3 -
                                                  (uint8_t)state->bButton0 ) ) *
                                  fShakeScale;
        } else {
            shakeMagnitude.y() += (float)( (int)( (uint8_t)state->bButton1 -
                                                  (uint8_t)state->bButton2 ) ) *
                                  fShakeScale;
            shakeMagnitude.w() += (float)( (int)( (uint8_t)state->bButton3 -
                                                  (uint8_t)state->bButton0 ) ) *
                                  fShakeScale;
        }
        const float fPeriodDelta =
            (float)( (int)( (uint8_t)state->bPovRight -
                            (uint8_t)state->bPovLeft ) ) *
            fShakeScale;
        const float fPeriodDeltaY =
            (float)( (int)( (uint8_t)state->bPovUp -
                            (uint8_t)state->bPovDown ) ) *
            fShakeScale;
        if ( !state->bTrigger1Digital ) {
            shakePeriod.y() += fPeriodDelta;  // ShakePeriodXFast
            shakePeriod.w() += fPeriodDeltaY; // ShakePeriodYFast
        } else {
            shakePeriod.x() += fPeriodDelta;  // ShakePeriodXSlow
            shakePeriod.z() += fPeriodDeltaY; // ShakePeriodYSlow
        }

        if ( shakeSpeedFast < shakeSpeedSlow )
            shakeSpeedFast = shakeSpeedSlow;
        if ( shakeMagnitude.x() < 0.0f )
            shakeMagnitude.x() = 0.0f;
        if ( shakeMagnitude.y() < 0.0f )
            shakeMagnitude.y() = 0.0f;
        if ( shakeMagnitude.z() < 0.0f )
            shakeMagnitude.z() = 0.0f;
        if ( shakeMagnitude.w() < 0.0f )
            shakeMagnitude.w() = 0.0f;
        if ( shakePeriod.x() < 0.0f )
            shakeMagnitude.x() = 0.0f; // ShakePeriodXSlow → MagnitudeXSlow
        if ( shakePeriod.y() < 0.0f )
            shakeMagnitude.y() = 0.0f; // ShakePeriodXFast → MagnitudeXFast
        if ( shakePeriod.z() < 0.0f )
            shakeMagnitude.z() = 0.0f; // ShakePeriodYSlow → MagnitudeYSlow
        if ( shakePeriod.w() < 0.0f )
            shakeMagnitude.w() = 0.0f; // ShakePeriodYFast → MagnitudeYFast
    }

    // 0x800000 — bump / slip angle
    if ( gEditCameraFlags & 0x800000 ) {
        const float fBumpScale = 0.1f * fScale;
        pBumps[0].Frequency += state->stick0.X * fBumpScale;
        pBumps[0].Magnitude += state->stick0.Y * fBumpScale;
        pBumps[1].Frequency += state->stick1.X * fBumpScale;
        pBumps[1].Magnitude += state->stick1.Y * fBumpScale;
        pBumps[2].Frequency += (float)( (int)( (uint8_t)state->bButton1 -
                                               (uint8_t)state->bButton2 ) ) *
                               fBumpScale;
        pBumps[2].Magnitude += (float)( (int)( (uint8_t)state->bButton3 -
                                               (uint8_t)state->bButton0 ) ) *
                               fBumpScale;
        minSlipAngle += (float)( (int)( (uint8_t)state->bPovRight -
                                        (uint8_t)state->bPovLeft ) ) *
                        fBumpScale;
        coefSlipAngle += (float)( (int)( (uint8_t)state->bPovUp -
                                         (uint8_t)state->bPovDown ) ) *
                         fBumpScale;
        freqSlipAngle += (float)( (int)( (uint8_t)state->bButton4 -
                                         (uint8_t)state->bButton5 ) ) *
                         fBumpScale;

        constexpr float kMaxBumpFreq = 100.0f;
        if ( kMaxBumpFreq < pBumps[0].Frequency )
            pBumps[0].Frequency = kMaxBumpFreq;
        if ( pBumps[0].Frequency < 0.0f )
            pBumps[0].Frequency = 0.0f;
        if ( kMaxBumpFreq < pBumps[1].Frequency )
            pBumps[1].Frequency = kMaxBumpFreq;
        if ( pBumps[1].Frequency < 0.0f )
            pBumps[1].Frequency = 0.0f;
        if ( kMaxBumpFreq < pBumps[2].Frequency )
            pBumps[2].Frequency = kMaxBumpFreq;
        if ( pBumps[2].Frequency < 0.0f )
            pBumps[2].Frequency = 0.0f;

        if ( 5.0f < minSlipAngle )
            minSlipAngle = 5.0f;
        if ( minSlipAngle < 0.0f )
            minSlipAngle = 0.0f;
        if ( kMaxBumpFreq < coefSlipAngle )
            minSlipAngle = kMaxBumpFreq; // matches original P-Code
        if ( coefSlipAngle < 0.0f )
            minSlipAngle = 0.0f; // matches original P-Code
        if ( kMaxBumpFreq < freqSlipAngle )
            freqSlipAngle = kMaxBumpFreq;
        if ( freqSlipAngle < 0.0f )
            freqSlipAngle = 0.0f;
    }

    // 0x10000 — neutral radius / intensity / angle max params
    if ( gEditCameraFlags & 0x10000 ) {
        neutralRadius.MinInput += floatRoundToInt8( fScale * state->stick0.X );
        const float fAngScale = 0.01f * fScale;
        if ( !state->bTrigger1Digital ) {
            intensity.MinInput += floatRoundToInt8( fScale * state->stick1.X );
            intensity.MinOutput += floatRoundToInt8( fScale * state->stick1.Y );
            horizontalAngleMaxMove +=
                (float)( (int)( (uint8_t)state->bButton1 -
                                (uint8_t)state->bButton2 ) ) *
                fAngScale;
        } else {
            intensity.MaxInput += floatRoundToInt8( fScale * state->stick1.X );
            intensity.MaxOutput += floatRoundToInt8( fScale * state->stick1.Y );
            horizontalAngleMaxMoveValue +=
                (float)( (int)( (uint8_t)state->bButton1 -
                                (uint8_t)state->bButton2 ) ) *
                fAngScale;
        }
        horizontalAngleMaxFade += (float)( (int)( (uint8_t)state->bButton0 -
                                                  (uint8_t)state->bButton3 ) ) *
                                  fAngScale;
        if ( !state->bTrigger1Digital ) {
            verticalAngleMaxMove +=
                (float)( (int)( (uint8_t)state->bPovRight -
                                (uint8_t)state->bPovLeft ) ) *
                fAngScale;
        } else {
            verticalAngleMaxMoveVal +=
                (float)( (int)( (uint8_t)state->bPovRight -
                                (uint8_t)state->bPovLeft ) ) *
                fAngScale;
        }
        verticalAngleMaxFade += (float)( (int)( (uint8_t)state->bPovUp -
                                                (uint8_t)state->bPovDown ) ) *
                                fAngScale;

        if ( intensity.MaxInput < intensity.MinInput )
            intensity.MaxInput = intensity.MinInput;
        if ( horizontalAngleMaxMove < 0.0f )
            horizontalAngleMaxMove = 0.0f;
        if ( horizontalAngleMaxMoveValue < 0.0f )
            horizontalAngleMaxMoveValue = 0.0f;
        if ( horizontalAngleMaxFade < horizontalAngleMaxMove &&
             horizontalAngleMaxMove != horizontalAngleMaxFade )
            horizontalAngleMaxFade = horizontalAngleMaxMove;
        if ( verticalAngleMaxMove < 0.0f )
            verticalAngleMaxMove = 0.0f;
        if ( verticalAngleMaxMoveVal < 0.0f )
            verticalAngleMaxMoveVal = 0.0f;
        if ( verticalAngleMaxFade < verticalAngleMaxMove &&
             verticalAngleMaxMove != verticalAngleMaxFade )
            verticalAngleMaxFade = verticalAngleMaxMove;
    }

    // 0x20000000 — FOV / FOV lerp
    if ( gEditCameraFlags & 0x20000000 ) {
        if ( ( vehicleFlags & 0x10 ) == 0 ) {
            vehicleFov += static_cast<uint8_t>(
                floatRoundToInt8( fScale * state->stick0.Y ) );
        } else {
            fovLerp.MinInput += floatRoundToInt8( fScale * state->stick0.X );
            fovLerp.MaxInput += floatRoundToInt8( fScale * state->stick1.X );
            fovLerp.MinOutput += floatRoundToInt8( fScale * state->stick0.Y );
            fovLerp.MaxOutput += floatRoundToInt8( fScale * state->stick1.Y );
            if ( fovLerp.MaxInput < fovLerp.MinInput )
                fovLerp.MaxInput = fovLerp.MinInput;
            if ( fovLerp.MaxOutput < fovLerp.MinOutput )
                fovLerp.MaxOutput = fovLerp.MinOutput;
        }
    }

    // 0x8000 (bit 15) — bike angle params
    if ( gEditCameraFlags & 0x8000 ) {
        bikeAngleMinInput += floatRoundToInt8( fScale * state->stick0.X );
        bikeAngleMaxInput += floatRoundToInt8( fScale * state->stick0.Y );
        bikeAngleOuput += floatRoundToInt8( fScale * state->stick1.X );
    }

    const float fSmoothScale = 0.05f;
    if ( ( vehicleFlags & 1 ) == 0 ) {
        // Follow camera mode
        FollowParams &cam =
            ( (int32_t)gEditCameraFlags < 0 ) ? followFast : follow;

        if ( gEditCameraFlags & 0x10000000 ) {
            const float posYDelta = state->stick0.Y * fScale * 0.1f;
            const float trgYDelta = state->stick1.Y * fScale * fSmoothScale;
            if ( !state->bTrigger1Digital ) {
                cam.PosZ += posYDelta;
                cam.PosHeight += trgYDelta;
            } else {
                cam.Trg += posYDelta;
                cam.TrgHeight += trgYDelta;
            }
        }
        if ( gEditCameraFlags & 0x8 ) {
            cam.LeanFactor += state->stick0.X * fScale * 0.025f;
        }
        if ( gEditCameraFlags & 0x2 ) {
            float newSmoothAngle =
                state->stick0.X * fScale * fSmoothScale + cam.CameraSmoothAngle;
            cam.CameraSmoothAngle = newSmoothAngle;
            if ( newSmoothAngle < 0.0f )
                cam.CameraSmoothAngle = 0.0f;
        }

        // 0x400000 — brake parameters (follow camera only)
        if ( gEditCameraFlags & 0x400000 ) {
            const float brakeDelta = state->stick0.Y * fScale * fSmoothScale;
            if ( !state->bTrigger1Digital ) {
                brakeYPosOffset += brakeDelta;
                brakeZPosOffset += state->stick1.Y * fScale * fSmoothScale;
            } else {
                brakeYTrgOffset += brakeDelta;
                brakeZTrgOffset += state->stick1.Y * fScale * fSmoothScale;
            }
            brakeGMax += (float)( (int)( (uint8_t)state->bButton3 -
                                         (uint8_t)state->bButton0 ) ) *
                         fSmoothScale;
            brakeGMin += (float)( (int)( (uint8_t)state->bButton1 -
                                         (uint8_t)state->bButton2 ) ) *
                         fSmoothScale;
            brakeGInertia = state->stick0.X * 0.001f + brakeGInertia;
            if ( 0.9999f < brakeGInertia )
                brakeYTrgOffset =
                    0.9999f; // matches original P-Code (likely bug)
        }
    } else {
        // Fixed camera mode
        if ( gEditCameraFlags & 0x10000000 ) {
            const float tf = fScale * 0.025f;
            if ( !state->bTrigger1Digital ) {
                params.TrgInCar.y() += state->stick1.Y * tf;
                params.PosIncar.y() += state->stick1.Y * tf;
                params.PosIncar.z() += state->stick0.Y * tf;
                params.TrgInCar.z() += state->stick0.Y * tf;
                params.PosIncar.x() += -( state->stick0.X * tf );
                params.TrgInCar.x() += -( state->stick0.X * tf );
            } else {
                params.TrgInCar.x() += -( state->stick0.X * tf );
                params.TrgInCar.z() += state->stick0.Y * tf;
                params.TrgInCar.y() += state->stick1.Y * tf;
            }
        }
        if ( gEditCameraFlags & 0x8000000 )
            editSpringParam( &params.MoveVertical, state );
        if ( gEditCameraFlags & 0x4000000 )
            editSpringParam( &params.MoveHorizontal, state );
        if ( gEditCameraFlags & 0x2000000 )
            editSpringParam( &params.LeanUpDown, state );
        if ( gEditCameraFlags & 0x1000000 )
            editSpringParam( &params.LeanLeftRight, state );
        if ( gEditCameraFlags & 0x8 ) {
            params.LeanFactor += state->stick0.X * fScale * 0.025f;
        }
    }

    nearPlane = savedNear;
    farPlane = savedFar;
}

inline uint8_t *ParseMatrix4x4( uint8_t *param_1, Matrix4x4f_t &matrix ) {
    // FUN_008d0ae0
    for ( int32_t i = 0; i < 4; i++ ) {
        for ( int32_t j = 0; j < 4; j++ ) {
            matrix.row( i )[j] =
                *(float *)( param_1 + ( i * 0x10 ) + ( j * 0x04 ) );
        }
    }

    return ( param_1 + 0x40 );
}

inline uint8_t *ParseVector4f( uint8_t *param_1, Eigen::Vector4f &vector ) {
    vector[0] = *(float *)( param_1 );
    vector[1] = *(float *)( param_1 + 0x4 );
    vector[2] = *(float *)( param_1 + 0x8 );
    vector[3] = *(float *)( param_1 + 0xc );
    return param_1 + 0x10;
}

inline uint8_t *ParseFollowParams( uint8_t *param_1, FollowParams &params ) {
    params.GroundHeight = 0.0f;
    params.PosZ = *(float *)( param_1 + 0 );
    params.PosHeight = *(float *)( param_1 + 4 );
    params.Trg = *(float *)( param_1 + 8 );
    params.TrgHeight = *(float *)( param_1 + 12 );
    params.LastAngleY = 0.0f;
    params.CameraSmoothAngle = *(float *)( param_1 + 20 );
    params.LeanFactor = *(float *)( param_1 + 24 );

    return param_1 + 0x1c;
}

inline uint8_t *ParseLerpParams( uint8_t *param_1, LerpParams &params ) {
    params.MinInput = param_1[0];
    params.MaxInput = param_1[1];
    params.MinOutput = param_1[2];
    params.MaxOutput = param_1[3];

    return param_1 + 0x04;
}

inline uint8_t *ParseSpring( uint8_t *param_1, SpringParam &params ) {
    params.Position = *(int16_t *)param_1;
    params.Velocity = *(int16_t *)( param_1 + 2 );
    params.Scale = *(int16_t *)( param_1 + 4 );
    params.PosMin = *(uint8_t *)( param_1 + 6 );
    params.PosMax = *(uint8_t *)( param_1 + 7 );
    params.Spring = *(uint8_t *)( param_1 + 8 );
    params.Damper = *(uint8_t *)( param_1 + 9 );
    params.ForceClamp = *(uint8_t *)( param_1 + 10 );
    params.TimeScale = *(uint8_t *)( param_1 + 11 );

    return param_1 + 12;
}

inline uint8_t *ParseCameraParams( uint8_t *param_1, FixedParams &params ) {
    // FUN_008d20f0
    param_1 = ParseVector4f( param_1, params.PosIncar );
    param_1 = ParseVector4f( param_1, params.TrgInCar );
    params.LeanFactor = *(float *)param_1;
    param_1 += 4;
    param_1 = ParseSpring( param_1, params.MoveVertical );
    param_1 = ParseSpring( param_1, params.MoveHorizontal );
    param_1 = ParseSpring( param_1, params.LeanUpDown );
    param_1 = ParseSpring( param_1, params.LeanLeftRight );
    return param_1;
}

uint8_t *CameraVehicle::load( uint8_t *param_1 ) {
    // FUN_008d2b60
    float fVar6 = *(float *)( param_1 + 4 ) * 0.5f;
    float fVar9 = 1.0f;
    fov = *(float *)( param_1 + 4 );
    float fVar7 = cosf( fVar6 );
    cosHalfFov = (float)fVar7;
    fVar6 = sinf( fVar6 );
    sinHalfFov = fVar6;
    tanHalfFov = (float)( fVar6 / fVar7 );
    float fVar8 = *(float *)( param_1 + 8 );
    aspectRatio = fVar8;
    invAspectRatio = 1.0f / fVar8;

    uint8_t *puVar1 = ParseMatrix4x4( param_1 + 0x18, worldToCam );
    puVar1 = ParseMatrix4x4( puVar1, invWorldToCam );

    float *pfVar2 = (float *)puVar1;
    focalInfos[0] = pfVar2[0];
    focalInfos[1] = pfVar2[1];
    focalInfos[2] = pfVar2[2];

    invAspectRatio = pfVar2[3];

    unknown[0] = pfVar2[4];
    unknown[1] = pfVar2[5];
    unknown[2] = pfVar2[6];
    unknown[3] = pfVar2[7];

    puVar1 += 0x20;

    cameraID = *(int32_t *)( puVar1 + 0 );
    carID = *(int32_t *)( puVar1 + 4 );
    cameraType = *(int32_t *)( puVar1 + 8 );

    puVar1 += 0x0c;

    char *pName = (char *)puVar1;
    name = pName; // TODO: Check if cam name is guaranteed to be null terminated
    puVar1 += 0x10;

    uint8_t *pv = puVar1;
    flags = *(uint32_t *)( pv + 0x00 );
    ParseVector4f( pv + 0x04, worldPosition );
    ParseVector4f( pv + 0x14, worldTarget );
    ParseVector4f( pv + 0x24, worldPositionWithUnknownOffset );
    ParseVector4f( pv + 0x34, worldTargetWithUnknownOffset );
    ParseVector4f( pv + 0x44, upVector );
    ParseVector4f( pv + 0x54, velocity );

    vehicleFlags = *(uint32_t *)( pv + 0x64 );
    bLastShowDriver = pv[0x68];
    angleRangeHorizontal = pv[0x69];
    maxAngleUp = pv[0x6a];
    maxAngleDown = pv[0x6b];
    angleInertia = pv[0x6c];
    vehicleFov = pv[0x6d];
    backviewType = pv[0x6e];
    wheelOffset = pv[0x6f];
    lastAngleHorizontal = *(float *)( pv + 0x70 );
    lastAngleVertical = *(float *)( pv + 0x74 );
    nearPlane = *(float *)( pv + 0x78 );
    farPlane = *(float *)( pv + 0x7c );

    ParseFollowParams( pv + 0x84, follow );
    ParseFollowParams( pv + 0xa4, followFast );

    uint8_t *puVar5 = ParseCameraParams( pv + 0xc0, params );

    ParseLerpParams( puVar5, fovLerp ); // 4 bytes

    shakeSpeedSlow = (float)puVar5[4];
    shakeSpeedFast = (float)puVar5[5];
    shakeRPMPercent = (float)puVar5[6];
    shakeMaxSpeed = (float)puVar5[7];

    ParseVector4f( puVar5 + 0x08, shakeMagnitude ); // XSlow,XFast,YSlow,YFast
    ParseVector4f( puVar5 + 0x18, shakePeriod );

    uint8_t *pfVar6 = puVar5 + 0x28;
    if ( cameraID < 0x186ab ) {
        pBumps[0].Frequency = 0.0f;
        pBumps[0].Magnitude = 0.0f;
        pBumps[1].Frequency = 0.0f;
        pBumps[1].Magnitude = 0.0f;
        pBumps[2].Frequency = 0.0f;
        pBumps[2].Magnitude = 0.0f;
        minSlipAngle = 0.0f;
        coefSlipAngle = 0.0f;
        freqSlipAngle = 1.0f;
    } else {
        pBumps[0].Frequency = *(float *)( puVar5 + 0x28 );
        pBumps[0].Magnitude = *(float *)( puVar5 + 0x2c );
        pBumps[1].Frequency = *(float *)( puVar5 + 0x30 );
        pBumps[1].Magnitude = *(float *)( puVar5 + 0x34 );
        pBumps[2].Frequency = *(float *)( puVar5 + 0x38 );
        pBumps[2].Magnitude = *(float *)( puVar5 + 0x3c );
        minSlipAngle = *(float *)( puVar5 + 0x40 );
        coefSlipAngle = *(float *)( puVar5 + 0x44 );
        freqSlipAngle = *(float *)( puVar5 + 0x48 );
        pfVar6 = puVar5 + 0x4c;
    }

    bikeAngleMinInput = pfVar6[0];
    bikeAngleMaxInput = pfVar6[1];
    bikeAngleOuput = pfVar6[2];
    shakeMaxSpeed2 = pfVar6[3];
    ParseLerpParams( pfVar6 + 0x04, neutralRadius );
    ParseLerpParams( pfVar6 + 0x08, intensity );

    horizontalAngleMaxMove = *(float *)( pfVar6 + 0x0c );
    horizontalAngleMaxMoveValue = *(float *)( pfVar6 + 0x10 );
    horizontalAngleMaxFade = *(float *)( pfVar6 + 0x14 );
    verticalAngleMaxMove = *(float *)( pfVar6 + 0x18 );
    verticalAngleMaxMoveVal = *(float *)( pfVar6 + 0x1c );
    verticalAngleMaxFade = *(float *)( pfVar6 + 0x20 );

    uint8_t *pfVar5 = pfVar6 + 0x24;
    if ( cameraID < 0x186ac ) {
        brakeGMax = 2.0f;
        brakeYPosOffset = 0.0f;
        brakeZPosOffset = 0.0f;
        brakeZTrgOffset = 0.0f;
        brakeGMin = 0.5f;
        brakeG = 0.0f;
        brakeYTrgOffset = 0.0f;
    } else {
        brakeYPosOffset = *(float *)( pfVar6 + 0x24 );
        brakeZPosOffset = *(float *)( pfVar6 + 0x28 );
        brakeYTrgOffset = *(float *)( pfVar6 + 0x2c );
        brakeZTrgOffset = *(float *)( pfVar6 + 0x30 );
        brakeGMax = *(float *)( pfVar6 + 0x34 );
        brakeGMin = *(float *)( pfVar6 + 0x38 );
        brakeG = *(float *)( pfVar6 + 0x3c );
        pfVar5 = pfVar6 + 0x40;
        if ( brakeYTrgOffset > 10.0f ) {
            brakeYTrgOffset = 10.0f; // FLOAT_00f4166c
        }
    }

    if ( cameraID < 0x186ad ) {
        brakeGInertia = 0.975f;
        cameraID = 0x186ad;
        return pfVar5;
    }
    brakeGInertia = *(float *)( pfVar5 + 0 );
    cameraID = 0x186ad;
    return pfVar5 + 4;
}

void CameraVehicle::resetTransform() {
    // FUN_008d1f60
    Matrix4x4f_t carMatrix = Matrix4x4f_t::Identity();
    if ( gpCamera != nullptr && gpCamera->getBoundCarState() != nullptr ) {
        carMatrix = gpCamera->getBoundCarState()->getBody()->WorldMatrix;
    }
    FollowParamsInit( follow, carMatrix );
    lastAngleHorizontal = 0.0f;
    gCameraSnap = true; // DAT_010e74a4 = 1
    lastAngleVertical = 0.0f;
    gWorkFollow.LastAngleY = follow.LastAngleY;
}

bool CameraVehicle::tickFollow( const Matrix4x4f_t &carMatrix,
                                CarState *pCar,
                                PhysicsVehicle *pPhys,
                                float deltaTime,
                                bool bSnap,
                                Eigen::Vector4f &outPos,
                                Eigen::Vector4f &outTarget,
                                Eigen::Vector4f &outUp ) {
    // FUN_008d5510
    FollowParams &work = gWorkFollow;

    float brakeDecel = ( pPhys != nullptr ) ? -pPhys->getLongitudinalG() : 0.0f;
    if ( brakeDecel < 0.0f ) {
        brakeDecel = 0.0f;
    }
    float bg =
        ( kOne - brakeGInertia ) * ( brakeDecel - brakeG ) * kBrakeGSmooth +
        brakeG;
    brakeG = bg;
    if ( gEditCameraFlags & 0x200000 ) {
        bg = brakeGMax;
    }
    const float span = brakeGMax - brakeGMin;
    const float t = ( span != 0.0f ) ? ( bg - brakeGMin ) / span : 0.0f;
    const float dPosH = Clamp( t * brakeYPosOffset, 0.0f, brakeYPosOffset );
    const float dPosZ = Clamp( t * brakeZPosOffset, 0.0f, brakeZPosOffset );
    const float dTrgH = Clamp( t * brakeYTrgOffset, 0.0f, brakeYTrgOffset );
    const float dTrg = Clamp( t * brakeZTrgOffset, 0.0f, brakeZTrgOffset );

    const Eigen::Vector4f localPos( 0.0f, work.PosHeight + dPosH,
                                    work.PosZ + dPosZ, 1.0f );
    const Eigen::Vector4f localTgt( 0.0f, work.TrgHeight + dTrgH,
                                    work.Trg + dTrg, 1.0f );

    const Eigen::Vector4f groundN = QueryGroundNormal( pCar );
    if ( !gSmoothUpInitialized ) {
        gSmoothUpInitialized = true;
        gSmoothUp = Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f );
    }
    if ( !bSnap ) {
        gSmoothUp =
            groundN * ( kOne - kUpSmoothKeep ) + gSmoothUp * kUpSmoothKeep;
    } else {
        gSmoothUp = groundN;
    }
    gSmoothUp.head<3>().normalize();

    const Eigen::Vector3f up3 = gSmoothUp.head<3>();
    const Eigen::Vector3f carFwd = carMatrix.row( 2 ).head<3>();
    Eigen::Vector3f right = up3.cross( carFwd ).normalized();
    const Eigen::Vector3f fwd = right.cross( up3 ).normalized();

    Matrix4x4f_t frame;
    frame.row( 0 ) =
        Eigen::RowVector4f( right.x(), right.y(), right.z(), 0.0f );
    frame.row( 1 ) = Eigen::RowVector4f( up3.x(), up3.y(), up3.z(), 0.0f );
    frame.row( 2 ) = Eigen::RowVector4f( fwd.x(), fwd.y(), fwd.z(), 0.0f );
    frame.row( 3 ) = carMatrix.row( 3 );

    OTDU_UNIMPLEMENTED_ONCE;

    YawSmooth( frame, work, deltaTime, vehicleFlags );

    outPos = TDUMat4x4MulVec( frame, localPos );
    outTarget = TDUMat4x4MulVec( frame, localTgt );
    outUp = kWorldUpVector;

    return true;
}

void CameraVehicle::tickFixed( CarState *pCar,
                               PhysicsVehicle *pPhys,
                               float deltaTime,
                               Eigen::Vector4f &outPos,
                               Eigen::Vector4f &outTarget,
                               Eigen::Vector4f &outUp ) {
    // FUN_008d4540
    const Matrix4x4f_t carMatrix = pCar->getBody()->OffsetedWorldMatrix;

    Eigen::Vector4f localPos = params.PosIncar;
    Eigen::Vector4f localTgt;
    {
        Eigen::Vector3f dir =
            ( params.TrgInCar.head<3>() - params.PosIncar.head<3>() );
        if ( dir.squaredNorm() > 1e-12f ) {
            dir.normalize();
        }
        localTgt.head<3>() = params.PosIncar.head<3>() + dir * kFixedFwdStep;
        localTgt.w() = 1.0f;
    }

    float rollLean = 0.0f;
    if ( ( vehicleFlags & 4 ) != 0 ) {
        Eigen::Vector4f accel = Eigen::Vector4f::Zero();
        pCar->retrieveAcceleration( accel );
        const float longG =
            ( pPhys != nullptr ) ? pPhys->getLongitudinalG() : 0.0f;
        if ( ( vehicleFlags & 0x100 ) != 0 ) {
            const float d =
                SpringIntegrate( params.MoveVertical, deltaTime, accel.y() );
            localPos.y() += d;
            localTgt.y() += d;
        }
        if ( ( vehicleFlags & 0x200 ) != 0 ) {
            const float d =
                SpringIntegrate( params.MoveHorizontal, deltaTime, longG );
            localPos.z() += d;
            localTgt.z() += d;
        }
        if ( ( vehicleFlags & 0x400 ) != 0 ) {
            const float d =
                SpringIntegrate( params.LeanUpDown, deltaTime, longG );
            localTgt.y() += d;
        }
        if ( ( vehicleFlags & 0x800 ) != 0 ) {
            rollLean =
                SpringIntegrate( params.LeanLeftRight, deltaTime, accel.x() );
        }
    }

    outUp = carMatrix.row( 1 ).transpose() +
            carMatrix.row( 0 ).transpose() * rollLean;

    if ( ( vehicleFlags & 8 ) != 0 ) {
        const float lean = pCar->getBody()->BikeLeanLateral * params.LeanFactor;
        outUp += carMatrix.row( 0 ).transpose() * lean;
    }
    outUp.head<3>().normalize();
    outUp.w() = 0.0f;

    outPos = TDUMat4x4MulVec( carMatrix, localPos );
    outTarget = TDUMat4x4MulVec( carMatrix, localTgt );
}
void CameraVehicle::applyFreeLook( const Matrix4x4f_t &carDisplayMatrix,
                                   bool bFixedCam,
                                   float deltaTime ) {
    // FUN_008d4a60
    const Matrix4x4f_t invCarMatrix = carDisplayMatrix.inverse();
    Eigen::Vector4f localPos =
        TDUMat4x4MulVec( invCarMatrix, worldPositionWithUnknownOffset );
    const Eigen::Vector4f localTgt =
        TDUMat4x4MulVec( invCarMatrix, worldTargetWithUnknownOffset );

    float angleH = 0.0f;
    float angleV = 0.0f;

    bool bSnapAngle = gbFixedCamLookSnap;
    if ( !bFixedCam || gpCamera->isViewUpdated() ) {
        bSnapAngle = false;
    }

    float inertia = (float)angleInertia * kSpringForce;
    float returnRate = ( kOne - kLookInertiaBase ) * inertia + kLookInertiaBase;

    if ( !gpCamera->isPaused() && gpTimer->GameSpeed > 0.0f ) {
        // Horizontal head look
        if ( ( vehicleFlags & 0x1000 ) != 0 ) {
            angleH = (float)angleRangeHorizontal * kDegToRad;
            if ( std::fabs( gpCamera->getLeftView() ) <= kLookAxisEpsilon ) {
                angleH = ( std::fabs( gpCamera->getRightView() ) <=
                           kLookAxisEpsilon )
                             ? 0.0f
                             : gpCamera->getRightView() * angleH;
            } else {
                angleH = gpCamera->getLeftView() * angleH;
            }
        }

        // Vertical head look
        if ( ( vehicleFlags & 0x2000 ) != 0 ) {
            const float upRad = (float)maxAngleUp * kDegToRad;
            const float downRad = (float)maxAngleDown * kDegToRad;
            float rangeUp = upRad;
            float rangeDown = downRad;
            if ( !bFixedCam && gpCamera->isLookingBack() ) {
                rangeUp = -upRad;
                rangeDown = -downRad;
            }
            if ( ( vehicleFlags & 0x4000 ) != 0 ) {
                rangeUp = -downRad;
                rangeDown = -upRad;
            }
            if ( gpCamera->isViewUpdated() ) {
                std::swap( rangeUp, rangeDown );
            }
            if ( std::fabs( gpCamera->getUpView() ) <= kLookAxisEpsilon ) {
                angleV =
                    ( std::fabs( gpCamera->getDownView() ) <= kLookAxisEpsilon )
                        ? 0.0f
                        : gpCamera->getDownView() * rangeDown;
            } else {
                angleV = gpCamera->getUpView() * rangeUp;
            }
        }

        // Locked interior view
        if ( bSnapAngle && ( angleH > kLookSnapHi || angleH < kLookSnapLo ) ) {
            const float snapDeg = ( angleH > kLookSnapHi )
                                      ? (float)neutralRadius.MaxOutput
                                      : -(float)neutralRadius.MaxOutput;
            angleH = snapDeg * kDegToRad;
        }

        // ShowFlags bit 5: lead the view into the corner.
        if ( ( flags & 0x20 ) != 0 ) {
            PhysicsVehicle *pPhys = ( gpCamera != nullptr )
                                        ? gpCamera->getTargetPhysEntity()
                                        : nullptr;
            const float lead = QueryLookLeadInput( pPhys );
            const float smoothed = ( lead - gLookLeadSmoothed ) *
                                       ( kOne - kLookLeadKeep ) * deltaTime *
                                       kSpeedSmoothRate +
                                   gLookLeadSmoothed;
            if ( smoothed <= 0.0f ) {
                gLookLeadSmoothed = ( smoothed <= -kLookLeadDeadZone )
                                        ? ( kLookLeadDeadZone + smoothed )
                                        : 0.0f;
                if ( gLookLeadSmoothed < -kLookLeadClamp ) {
                    gLookLeadSmoothed = -kLookLeadClamp;
                }
            } else {
                gLookLeadSmoothed = ( kLookLeadDeadZone <= smoothed )
                                        ? ( smoothed - kLookLeadDeadZone )
                                        : 0.0f;
                if ( kLookLeadClamp < gLookLeadSmoothed ) {
                    gLookLeadSmoothed = kLookLeadClamp;
                }
            }
            angleH += kLookLeadScale * gLookLeadSmoothed;
        }

        if ( gpCamera->isViewUpdated() ) {
            lastAngleHorizontal = angleH;
            lastAngleVertical = angleV;
        }

        if ( gpCamera->isHeadTrackingEnabled() ) {
            const float limitH =
                (float)angleRangeHorizontal * kDegToRad * kHeadTrackRange;
            angleH = std::min( gHeadTrackYaw, limitH );
            if ( angleH < -limitH ) {
                angleH = -limitH;
            }
            const float limitDown =
                (float)maxAngleDown * kDegToRad * kHeadTrackRange;
            const float limitUp =
                -( (float)maxAngleUp * kDegToRad * kHeadTrackRange );
            angleV = std::min( -gHeadTrackPitch, limitDown );
            if ( angleV < limitUp ) {
                angleV = limitUp;
            }
            inertia = 0.0f;
            returnRate = 0.0f;
            lastAngleHorizontal = angleH;
            lastAngleVertical = angleV;
        }
    }

    if ( !bFixedCam ) {
        if ( gbFreeLookUnlocked != gbFreeLookUnlockedPrev ) {
            gLookLockCounter = 0;
            gLookAngleBase = gbFreeLookUnlocked ? 0.0f : lastAngleHorizontal;
            gbFreeLookUnlockedPrev = gbFreeLookUnlocked;
        }
    } else {
        gbFreeLookUnlocked = true;
        gLookAngleBase = 0.0f;
        gLookLockCounter = 0;
    }
    const bool bLookUnlocked = gbFreeLookUnlocked;

    // Skip the smoothing on any axis the ground clamp moved last frame.
    bool bSmoothAngles = true;
    if ( gbCamClampedLeft &&
         kLookAxisEpsilon < std::fabs( gpCamera->getLeftView() ) ) {
        bSmoothAngles = false;
    }
    if ( gbCamClampedRight &&
         kLookAxisEpsilon < std::fabs( gpCamera->getRightView() ) ) {
        bSmoothAngles = false;
    }
    if ( gbCamClampedUp &&
         kLookAxisEpsilon < std::fabs( gpCamera->getUpView() ) ) {
        bSmoothAngles = false;
    }

    if ( gpTimer->GameSpeed > 0.0f && bSmoothAngles ) {
        float delta = ( gLookAngleBase + angleH ) - lastAngleHorizontal;
        if ( !bSnapAngle ) {
            if ( std::fabs( angleH ) < std::fabs( lastAngleHorizontal ) ) {
                returnRate = inertia;
            }
            delta = delta * ( kOne - returnRate ) * deltaTime *
                    kLookSmoothGain * kSpeedSmoothRate;
        } else {
            delta = delta * kLookSmoothGain;
        }
        lastAngleHorizontal += delta;
    }
    if ( bLookUnlocked && gpTimer->GameSpeed > 0.0f && !bSnapAngle &&
         bSmoothAngles ) {
        lastAngleVertical += ( angleV - lastAngleVertical ) *
                             ( kOne - inertia ) * deltaTime * kLookSmoothGain *
                             kSpeedSmoothRate;
    }

    const float yaw = lastAngleHorizontal;
    const float pitch = lastAngleVertical;

    Eigen::Vector4f outTgt;
    outTgt.w() = localTgt.w();

    if ( !bFixedCam ) {
        auto polarYaw = []( const Eigen::Vector4f &v ) {
            return ( v.z() < 0.0f ) ? std::atan2( v.x(), -v.z() )
                                    : std::atan2( v.x(), v.z() );
        };

        const float tgtPitch = std::atan2( localTgt.y(), localTgt.z() );
        const float tgtYaw = polarYaw( localTgt );
        const float tgtRadius = std::sqrt( localTgt.x() * localTgt.x() +
                                           localTgt.z() * localTgt.z() +
                                           localTgt.y() * localTgt.y() );
        const float tgtCosPitch = std::cos( tgtPitch + pitch );
        outTgt.x() = std::sin( tgtYaw + yaw ) * tgtCosPitch * tgtRadius;
        outTgt.z() = std::cos( tgtYaw + yaw ) * tgtCosPitch * tgtRadius;
        outTgt.y() = std::sin( tgtPitch + pitch ) * tgtRadius;

        const float posPitch = std::atan2( localPos.y(), localPos.z() );
        const float posYaw = polarYaw( localPos );
        const float posRadius = std::sqrt( localPos.x() * localPos.x() +
                                           localPos.z() * localPos.z() +
                                           localPos.y() * localPos.y() );
        const float posCosPitch = std::cos( posPitch + pitch );
        localPos.x() = std::sin( yaw - posYaw ) * posCosPitch * posRadius;
        localPos.z() = std::cos( yaw - posYaw ) * posCosPitch * posRadius;
        localPos.y() = std::sin( posPitch + pitch ) * posRadius;
    } else {
        // FIXED (in-car): the eye stays put, the target swings around it.
        const Eigen::Vector3f dir = localTgt.head<3>() - localPos.head<3>();

        float dirPitch = std::atan2( dir.y(), dir.z() );
        if ( dirPitch > kHalfPI ) {
            dirPitch -= kPI;
        }
        if ( dirPitch < kNegHalfPi ) {
            dirPitch += kPI;
        }
        float dirYaw = std::atan2( dir.x(), dir.z() );
        if ( dirYaw > kPI ) {
            dirYaw -= kTwoPI;
        }
        if ( dirYaw < kNegPI ) {
            dirYaw += kTwoPI;
        }

        const float radius = dir.norm();
        const float cosPitch = std::cos( dirPitch + pitch );
        outTgt.x() =
            std::sin( dirYaw + yaw ) * radius * cosPitch + localPos.x();
        outTgt.z() =
            std::cos( dirYaw + yaw ) * radius * cosPitch + localPos.z();
        outTgt.y() = std::sin( dirPitch + pitch ) * radius + localPos.y();
    }

    setPositionOffseted( TDUMat4x4MulVec( carDisplayMatrix, localPos ) );
    setTargetOffseted( TDUMat4x4MulVec( carDisplayMatrix, outTgt ) );
}

void CameraVehicle::applySpeedShake( float ) {
    // FUN_008d3e40
    OTDU_UNIMPLEMENTED_ONCE; // TODO: POSTFX
}

void CameraVehicle::updateMotionBlur() {
    // FUN_008d2380
    OTDU_UNIMPLEMENTED; // TODO: POSTFX
}
