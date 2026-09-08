#include "shared.h"

#include "physics_vehicle.h"

#include "core/maths_helper.h"
#include "core/random/lcg.h"

#include "input/gs_device.h"
#include "physics/physics_config.h"
#include "physics/physics_layers.h"
#include "physics/gs_physics.h"
#include "physics/physics_material.h"
#include "database/gs_database.h"
#include "database/db_aftermarket_pack.h"
#include "player_data/gs_playerdata.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>

constexpr float kMmToM = 0.001f;
constexpr float kSlipDenom = 3.0f;
constexpr int kWheelSubsteps = 5;
constexpr float kSuspDampingScale = 1.0f;
static constexpr float kPedalCurveC2 = 0.5f;        // FLOAT_00f41674
static constexpr float kPedalCurveC3 = 1.0f / 3.0f; // DAT_00f41764
static constexpr float kThrottleCurveK = 0.0f;      // DAT_00f8a22c
static constexpr float kBrakeCurveK = 50.0f;
static constexpr float kPedalGain = 0.7f;                // DAT_00fab730
static constexpr float kAutoGbxPedal = 0.1f;             // DAT_00fab720
static constexpr float kAutoGbxInput = 0.1f;             // FLOAT_00f41668
static constexpr float kAutoGbxPedalIdle = 0.01f;        // DAT_00f416d4
static constexpr float kAutoGbxHoldBrake = 0.5f;         // FLOAT_00f41674
static constexpr float kAutoGbxMovingSpeed = 0.2777778f; // DAT_00f41868, 1km/h
static constexpr float kAutoGbxMoveOffThrottle = 0.2f;   // DAT_00f41730
static constexpr float kAutoGbxReverseExitSpeed = 2.0f;  // DAT_00f416b0

int32_t gVehicleDrivingHelpOverride = -1;
int32_t gVehicleSteerControlOverride = -1;

float gVehicleLowSpeedLatGain = 0.0f;       // DAT_016a6998
constexpr float kDBHundredths = 0.01f;      // DAT_00f416d4
constexpr float kDBPerMille = 0.001f;       // FLOAT_00f4167c
constexpr float kSuspLoadCapCoef = 19.62f;  // DAT_00f41ef8 (2 g)
constexpr float kWheelVelDeadband = 0.25f;  // DAT_00f8a1d4
constexpr float kWheelOmegaDeadband = 0.1f; // FLOAT_00f41668

constexpr float kSimuBrakeTorquePerMass = 2.5f;
constexpr float kBrakeBalance = 0.6f; // fallback
constexpr float kMaxBrakeTorque = 15000.0f;
constexpr float kHandbrakeInputThreshold = 0.1f;
constexpr float kHandbrakeTorqueScale = 1.0f;
constexpr float kWheelInertiaRadiusCoef = 2.5f; // DAT_00f416c0

constexpr float kEngineFrictionLoss = 0.01f; // DAT_00f416d4
constexpr float kEngineFakeCoef = 1.0f;      // FLOAT_00f4165c
constexpr float kThousandRpmOmega = 104.72f;

constexpr float kWattToMetricHp = 0.0013597f;          // DAT_00f41694 (1/735.5)
constexpr float kRevLimiterPowerRef = 250.0f;          // DAT_00f41824
constexpr float kRevLimiterPowerSlope = -0.15f;        // DAT_00f41ed8
constexpr float kRevLimiterPowerScale = 1.0f / 350.0f; // DAT_00f41fd8
constexpr float kRevLimiterCutMin = 0.1f;              // FLOAT_00f41668
constexpr float kRevLimiterCutMax = 0.25f;             // DAT_00f416dc

constexpr float kSteerScale = 900.0f; // DAT_00f41c10 (900/gSteeringTurn = gain)
constexpr float kSteerLinearity = 5.0f;    // DAT_00fab71c
constexpr float kSteerLinearityMin = 0.1f; // FLOAT_00f41668 (expo gate)
constexpr float kSteerTurnScale = 1.0f;
constexpr float kSteerNormSpeed = 1.0f;
constexpr float kSteerSensRetailHigh = 2.0f;
constexpr float kSteerAuxDeadBand = 0.01f;        // DAT_00fab704
constexpr float kSteerCounterMinRate = 0.1f;      // DAT_00fab710
constexpr float kSteerSlipBlendLo = 5.0f;         // DAT_00fab718 (m/s)
constexpr float kSteerSlipBlendHi = 10.0f;        // DAT_00fab714 (m/s)
constexpr float kSteerHelpLerpFast = 100.0f;      // DAT_00f41708
constexpr float kSteerHelpLerpSlow = 1.0f;        // DAT_00f41c00 (0.99999785)
constexpr float kSteerHelpDtCap = 1.0f / 45.0f;   // DAT_00f41c08
constexpr float kSteerHelpDtFixed = 1.0f / 90.0f; // DAT_00f41c04
constexpr float kSteerYawBlendScale = 0.1f;       // FLOAT_00f41668
constexpr float kSteerAuxEps = 0.01f;             // DAT_00f416d4
constexpr float kSteerAuxSliderScale = 0.25f;     // DAT_00f416dc
constexpr float kSteerAuxSliderBase = 0.75f;      // DAT_00f416a4
constexpr float kSteerFrontSlipAvg = -0.5f;       // DAT_00f4186c
constexpr float kSteerFrontSlipPeak = 1.0f; // FLOAT_00f4165c / FLOAT_00f41664

// Retail Physics.ini [Car] values, used when the ini did not populate a curve
// (the original p-code divides by (X1-X0) unguarded, so an all-zero X array is
// fatal).
constexpr float kSteerCurveXDefault[3] = { 1.0f, 200.0f, 800.0f };
constexpr float kSteerCurveYDefault[PhysicsVehicle::SC_Count][3] = {
    {1.5f, 0.5f, 0.1f}, // SteeringLimit
    {1.3f, 0.3f, 0.1f}, // SteeringSpeed
    {4.0f, 4.0f, 0.1f}, // SteeringCenterSpeed
    {1.5f, 2.5f, 2.5f}, // SteeringStraightSpeed
    {3.2f, 3.0f, 3.0f}, // SteeringCounterSpeed
};

// A performance pack lifts lateral friction on both axles; the multiplier is smaller in simu mode.
constexpr float kPackLatFrictionBoost = 1.05f;     // DAT_00fab5b0
constexpr float kPackLatFrictionBoostSimu = 1.01f; // DAT_00fab5ac
constexpr float kSuspLoadCapAWD = 1.5f;

constexpr float kAeroLiftCoef = 0.2f;     // DAT_00f41730
constexpr float kAeroSimuLiftMult = 2.0f; // DAT_00f416b0, simu only
constexpr float kAeroDensityFallback = 2.0f;
bool gbSimuModeEnabled = false; // DAT_010e777c

float gVehicleAeroDragScale = 1.0f; // debug: aero drag multiplier
bool gVehicleStartChronopack =
    false; // FIXME: programatically trigger chronopack capture
           // (until input/flash logic is implemented)

bool gVehicleESPEnabled = true;

constexpr float kOmegaToRpm = 9.549296f;     // DAT_00f41698 (60 / 2pi)
constexpr float kRpmToRadPerSec = 0.10472f;  // DAT_00f418ac (2pi / 60)
constexpr float kKmhToMs = 0.2777778f;       // km/h -> m/s
constexpr float kFinalDriveRedlineFrac = 0.9f; // FLOAT_00f8a26c
constexpr float kRevRateSmoothing = 0.05f;
constexpr float kRevLogMinGearTime = 0.15f;
constexpr float kStuckClutchReportTime = 3.0f;

constexpr float kChronoMoveSpeed = 0.5f;         // FLOAT_00f41674
constexpr float kChrono60mph = 26.8225f;         // DAT_00f4211c
constexpr float kChrono100kmh = 27.7778f;        // DAT_00f42124
constexpr float kChrono200kmh = 55.5556f;        // DAT_00f41be0
constexpr float kChrono400m = 400.0f;            // DAT_00f41ca0
constexpr float kChrono1000m = 1000.0f;          // DAT_00f41718
constexpr float kMetresToMiles = 0.000621371f;   // DAT_00f42120
constexpr float kGMeterVerticalVelMax = 200.0f;  // DAT_00fab688
constexpr float kGMeterVerticalVelReset = 10.0f; // DAT_00fab684
constexpr float kGravityAccel = 9.81f;           // 1 / DAT_00f42104

constexpr float kAntiFlyGapDeadband = 0.05f; // DAT_00f41778
constexpr float kAntiFlyMinForce = 100.0f;   // DAT_00f41708 (apply gate, N)
constexpr float kAntiFlyGravityCoef = 1.0f;  // DAT_00f8a21c (simu: 0.0)
constexpr float kAntiFlyMaxGap = 1.5f;       // DAT_00f8a220 (simu: 0.0)
constexpr float kAntiFlyLeverScale = 0.75f;  // DAT_00f416a4
constexpr float kGroundProbeDepth = 10.0f;   // FLOAT_00f4166c
constexpr float kRumbleSpeedMin = 5.0f;      // DAT_00f41750 / DAT_00fab754
constexpr float kRumbleSpeedSpan = 45.0f;    // (DAT_00fab75c - DAT_00f41750)
constexpr float kRumblePeriodBase = 10.0f;   // FLOAT_00f4166c / DAT_00fab764
constexpr float kRumblePeriodScale = 0.25f;  // DAT_00fab760
constexpr float kRumbleSpread = 0.25f;       // DAT_00f416dc
constexpr float kRumbleAmountMax = 2.0f;     // DAT_00f416b0
constexpr float kRumbleAccelMin = 0.0f;      // DAT_016a69bc
constexpr float kRumbleAccelMax = 10.0f;     // DAT_00fab758

static uint32_t gPhysicsRandomSeed = 0; // DAT_010e7480

// Anti-roll bars from the original pcode (disabled in the original game)
bool gVehicleAntiRoll = false;

constexpr float kGroundAlignGainP = -100.0f;    // FLOAT_00f4189c
constexpr float kGroundAlignGainD = -10.0f;     // DAT_00f418a8
constexpr float kGroundAlignSpeedRef = 10.0f;   // DAT_00fab744
constexpr float kGroundAlignDeadband = 0.35f;   // DAT_00fab748 / DAT_00fab74c
constexpr float kGroundAlignPitchScale = 2.0f;  // DAT_00f416b0
constexpr float kGroundAlignMinNormalY = 0.75f; // DAT_00f416a4
constexpr int32_t kGroundAlignQueryPeriod = 32;
constexpr float kGroundAlignProbeUp = 1.0f;
constexpr float kGroundAlignProbeDepth = 1000.0f;

static inline float ApplyDeadband( const float value, const float band ) {
    if ( value <= 0.0f ) {
        return ( value <= -band ) ? value + band : 0.0f;
    }
    return ( band <= value ) ? value - band : 0.0f;
}
constexpr float kAntiRollFraction = 0.0f;
constexpr float kGravCenterHeightFracDefault = 0.40f; // DAT_00f4175c

// Suspension damper-velocity NaN guard (legacy body-velocity path).
constexpr float kSuspDamperVelClamp = 100.0f;
constexpr float kSuspReboundStiffen = 1.6f;    // DAT_00f41668 / DAT_00f41acc
constexpr float kSuspDamperTravelClamp = 5.0f; // FLOAT_00fabfac

static float SteerResponseCurve( float x, float k ) {
    // FUN_00824730
    if ( k < 0.0f ) {
        const float a = -k;
        return std::log( 1.0f + a * x ) / std::log( 1.0f + a );
    }
    auto poly = []( float t ) {
        return t + 0.5f * t * t + ( 1.0f / 3.0f ) * t * t * t;
    };
    const float d = poly( k );
    return ( d > 1.0e-6f ) ? ( poly( x * k ) / d ) : x;
}

static float EvalHelpCurveSqrt( const float *pThresholds,
                                const float *pValues,
                                float x,
                                float p ) {
    // FUN_0041f0a0
    const float s = std::sqrt( x );
    const float t0 = std::sqrt( pThresholds[0] );
    const float t1 = std::sqrt( pThresholds[1] );
    const float t2 = std::sqrt( pThresholds[2] );

    float a, b, lo, hi;
    if ( t1 > s ) {
        a = pValues[0] * p;
        b = pValues[1] * p;
        lo = t0;
        hi = t1;
    } else {
        a = pValues[1] * p;
        b = pValues[2] * p;
        lo = t1;
        hi = t2;
    }

    const float span = hi - lo;
    const float r = ( std::fabs( span ) > 1.0e-6f )
                        ? ( ( s - lo ) * ( b - a ) / span + a )
                        : a;
    return JPH::Clamp( r, std::min( a, b ), std::max( a, b ) );
}

inline void SeedTire( PhysicsTire &t, bool bFront ) {
    const PhysicsConfig &c = gPhysicsConfig;
    if ( bFront ) {
        t.MaxSlipRatio =
            c.WheelMaxSlipRatioFront > 0 ? c.WheelMaxSlipRatioFront : 0.15f;
        t.MaxSlipAngle =
            c.WheelMaxSlipAngleFront > 0 ? c.WheelMaxSlipAngleFront : 8.0f;
        t.FrictionEllipseThreshold = c.SlipRatioStartLooseLateralFront > 0
                                         ? c.SlipRatioStartLooseLateralFront
                                         : 0.06f;
        t.FrictionEllipseMinFactor =
            c.MaxLateralLossFront > 0 ? c.MaxLateralLossFront : 0.6f;
    } else {
        t.MaxSlipRatio =
            c.WheelMaxSlipRatioRear > 0 ? c.WheelMaxSlipRatioRear : 0.15f;
        t.MaxSlipAngle =
            c.WheelMaxSlipAngleRear > 0 ? c.WheelMaxSlipAngleRear : 8.0f;
        t.FrictionEllipseThreshold = c.SlipRatioStartLooseLateralRear > 0
                                         ? c.SlipRatioStartLooseLateralRear
                                         : 0.06f;
        t.FrictionEllipseMinFactor =
            c.MaxLateralLossRear > 0 ? c.MaxLateralLossRear : 0.6f;
    }

    // TODO: FIXME: PLACEHOLDER Magic-Formula banks
    // Neutral load normalisation (identity) for the placeholder.
    t.LoadTableCount = 2;
    t.LoadTableMaxLoad = 8000.0f;
    t.PeakLong[0] = t.PeakLong[1] = 1.0f;
    t.PeakLat[0] = t.PeakLat[1] = 1.0f;

    const float longCoeffs[11] = { 1.5f, 0.0f, 1000.0f, 0.0f, 173.0f, 0.0f,
                                   0.0f, 0.0f, 0.97f,   0.0f, 0.0f };
    const float latCoeffs[15] = { 1.3f, 0.0f, 600.0f, 900.0f, 2.0f,
                                  0.0f, 0.0f, 0.97f,  0.0f,   0.0f,
                                  0.0f, 0.0f, 0.0f,   0.0f,   0.0f };
    std::copy( std::begin( longCoeffs ), std::end( longCoeffs ), t.Long );
    std::copy( std::begin( latCoeffs ), std::end( latCoeffs ), t.Lat );
}

static void CopyTireCurve( PhysicsTire &t, const TirePacejka &src ) {
    t.LoadTableCount =
        std::min<int>( src.NumPoints, PhysicsTire::kMaxLoadTable );
    t.LoadTableMaxLoad = src.MaxLoad;
    std::copy( std::begin( src.pCurvePointsY ), std::end( src.pCurvePointsY ),
               t.Long );
    std::copy( std::begin( src.pCurvePointsX ), std::end( src.pCurvePointsX ),
               t.Lat );
    std::copy( std::begin( src.pSlipAngleCurve ),
               std::end( src.pSlipAngleCurve ), t.PeakLat );
    std::copy( std::begin( src.pSlipRatioCurve ),
               std::end( src.pSlipRatioCurve ), t.PeakLong );
}

inline int32_t SelectTireIndex( float massKg, int numProfiles ) {
    // FUN_00832420 / FUN_008345e0
    if ( numProfiles <= 0 ) {
        return -1;
    }
    int32_t idx =
        ( massKg > 3250.0f ) ? 5 : 1; // DAT_00f41ff8=3250 / _00f41efc=3500
    if ( idx >= numProfiles ) {
        idx = numProfiles - 1;
    }
    return idx;
}

// Collector mirroring TDUWheelRayTester: nearest hit on a drive-able
// surface.
class WheelRayCollector final : public JPH::CastRayCollector {
public:
    WheelRayCollector( JPH::PhysicsSystem &system,
                       const JPH::RRayCast &ray,
                       JPH::Vec3Arg up,
                       float cosMaxSlope )
        : mSystem( system ),
          mRay( ray ),
          mUp( up ),
          mCosMaxSlope( cosMaxSlope ) {}

    void AddHit( const JPH::RayCastResult &inResult ) override {
        if ( inResult.mFraction >= GetEarlyOutFraction() ) {
            return;
        }
        JPH::BodyLockRead lock( mSystem.GetBodyLockInterfaceNoLock(),
                                inResult.mBodyID );
        if ( !lock.Succeeded() ) {
            return;
        }
        const JPH::Body *pBody = &lock.GetBody();
        if ( pBody->IsSensor() ) {
            return;
        }
        const JPH::RVec3 pos = mRay.GetPointOnRay( inResult.mFraction );
        const uint64_t userData = pBody->GetUserData();
        const JPH::Vec3 normal =
            pBody->GetWorldSpaceSurfaceNormal( inResult.mSubShapeID2, pos );
        if ( normal.Dot( mUp ) > mCosMaxSlope ) {
            UpdateEarlyOutFraction( inResult.mFraction );
            mHit = true;
            mPosition = pos;
            mNormal = normal;
            mSurface = userData & TDUSurface::kClassMask; // phys-type byte
            mMaterial = TDUPhysicsMaterials::codeOf(
                pBody->GetShape()->GetMaterial( inResult.mSubShapeID2 ) );
        }
    }

    JPH::PhysicsSystem &mSystem;
    JPH::RRayCast mRay;
    JPH::Vec3 mUp;
    float mCosMaxSlope;
    bool mHit = false;
    JPH::RVec3 mPosition;
    JPH::Vec3 mNormal;
    uint64_t mSurface = 0;
    uint32_t mMaterial = TDUMaterialValue::kDefault;
};

// TODO: Move to GSPhysics?
inline Eigen::Vector4f ToVec4( JPH::Vec3Arg v ) {
    return Eigen::Vector4f( v.GetX(), v.GetY(), v.GetZ(), 0.0f );
}

static float
EspPeakInterp( const float *table, int N, float maxLoad, float load ) {
    // Per-load table interpolation (same indexing as NormalizeSlipsAndCurves).
    if ( N < 2 ) {
        return 1.0f;
    }
    const float step = maxLoad / static_cast<float>( N );
    int i = static_cast<int>( load / step );
    if ( i > N - 2 ) {
        i = N - 2;
    }
    if ( i < 0 ) {
        i = 0;
    }
    const float xL = i * step;
    const float lo = table[i], hi = table[i + 1];
    const float v = ( ( hi - lo ) * ( load - xL ) ) / step + lo;
    return JPH::Clamp( v, std::min( lo, hi ), std::max( lo, hi ) );
}

static float
EspCombinedSlipFactor( const PhysicsTire &tire, float load, float slipArg ) {
    // FUN_0043d0d0
    const int N = tire.LoadTableCount;
    const float peakGrip =
        EspPeakInterp( tire.PeakLong, N, tire.LoadTableMaxLoad, load );
    float peakSlip =
        EspPeakInterp( tire.PeakLat, N, tire.LoadTableMaxLoad, load );
    if ( std::fabs( peakSlip ) < 1.0e-6f ) {
        peakSlip = 1.0f;
    }
    const float r = slipArg / peakSlip;
    float x = 1.0f - r * r;
    if ( x < 0.0f ) {
        x = -x;
    }
    return std::sqrt( x ) * peakGrip;
}

static float EvalHelpCurve( const float *pThresholds,
                            const float *pValues,
                            float x,
                            float p ) {
    // FUN_0041f1b0
    float a, b;
    float lo, hi;
    if ( x < pThresholds[1] ) {
        a = pValues[0] * p;
        b = pValues[1] * p;
        lo = pThresholds[0];
        hi = pThresholds[1];
    } else {
        a = pValues[1] * p;
        b = pValues[2] * p;
        lo = pThresholds[1];
        hi = pThresholds[2];
    }
    // The original divides unguarded; a zero-width segment would only happen
    // with an unparsed config, but a NaN here would poison the whole solver.
    const float span = hi - lo;
    const float r = ( std::fabs( span ) > 1.0e-6f )
                        ? ( ( x - lo ) * ( b - a ) / span + a )
                        : a;
    return JPH::Clamp( r, std::min( a, b ), std::max( a, b ) );
}

static constexpr float kAidMagnitudeParked = 10.0f; // FLOAT_00f4166c

static void ApplyHandbrakeAbsOverride( PhysicsVehicleState &vehicle,
                                       float handbrake ) {
    // FUN_0083db70
    constexpr float kHandbrakeAbsGate = 0.1f;   // FLOAT_00f41668
    constexpr float kHandbrakeAbsSimu = 100.0f; // DAT_00f41708
    if ( handbrake <= kHandbrakeAbsGate ) {
        return;
    }
    if ( gbSimuModeEnabled ) {
        vehicle.AbsMagRear = kHandbrakeAbsSimu;
        return;
    }
    const PhysicsConfig &c = gPhysicsConfig;
    const float t0 = c.ActiveABSSpeed[3];
    const float t2 = c.ActiveABSSpeed[4];
    const float v0 = c.ActiveABSValue[3];
    const float v2 = c.ActiveABSValue[4];
    const float span = t2 - t0;
    const float r = ( std::fabs( span ) > 1.0e-6f )
                        ? ( ( vehicle.Mass - t0 ) * ( v2 - v0 ) / span + v0 )
                        : v0;
    vehicle.AbsMagRear =
        JPH::Clamp( r, std::min( v0, v2 ), std::max( v0, v2 ) );
}

static void UpdateBrakeGripLat( PhysicsVehicleState &vehicle, float speed ) {
    // FUN_0041b920 (inlined)
    const PhysicsConfig &c = gPhysicsConfig;
    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        PhysicsWheelState &w = vehicle.Wheels[i];
        const bool bFront = ( w.AxleIndex == 0 );
        const float vSlow = bFront ? c.LatBrakeFrictionSpeedSlowFront
                                   : c.LatBrakeFrictionSpeedSlowRear;
        const float vFast = bFront ? c.LatBrakeFrictionSpeedFastFront
                                   : c.LatBrakeFrictionSpeedFastRear;
        w.BrakeGripLat = Lerp2Clamped( speed, vSlow, vFast, w.BrakeGripLatSlow,
                                       w.BrakeGripLatFast );
    }
}

static void ComputeAbsMagnitudes( PhysicsVehicleState &vehicle,
                                  float speed,
                                  bool bReverse ) {
    // FUN_0041b920 (inlined)
    vehicle.bAbsEnabled = true;

    if ( bReverse ) {
        vehicle.AbsMagFront = 1.0f;
        vehicle.AbsMagRear = kAidMagnitudeParked;
        vehicle.bAbsEnabled = false;
        return;
    }

    vehicle.AbsMagRear =
        EvalHelpCurve( gPhysicsConfig.ActiveABSSpeed,
                       gPhysicsConfig.ActiveABSValue, speed, 2.0f );

    if ( !vehicle.bElectronicsEnabled ) {
        OTDU_UNIMPLEMENTED; // TODO: Handbrake
        vehicle.AbsMagFront = 1.5f;
        return;
    }

    // DAT_41c2ca
    static const float kFrontAbsSpeeds[3] = { 5.5557f, 41.6667f, 55.5556f };
    static const float kFrontAbsValues[3] = { 2.0f, 1.0f, 0.75f };
    vehicle.AbsMagFront =
        EvalHelpCurve( kFrontAbsSpeeds, kFrontAbsValues, speed, 1.0f );
}

static void ComputeEspMagnitudes( PhysicsVehicleState &vehicle,
                                  float speed,
                                  float deltaTime,
                                  bool bReverse ) {
    // FUN_0041b920 (inlined)
    constexpr float kEspNoTarget = 99.0f;      // DAT_00f417e8 sentinel
    constexpr float kEspMinSpeed = 8.3333f;    // DAT_00fab700, 30 km/h
    constexpr float kEspErrorGain = 0.1f;      // FLOAT_00f41668
    constexpr float kEspMagMin = 0.5f;         // FLOAT_00f41674
    constexpr float kEspMagMax = 5.0f;         // DAT_00f41750
    constexpr float kEspSmoothingRate = 20.0f; // _DAT_00f41bec, per second

    const bool bFrontWheelDrive = vehicle.bFrontDrive && !vehicle.bRearDrive;
    const int espSlot = bFrontWheelDrive ? 0 : 1;

    if ( !vehicle.bElectronicsEnabled || vehicle.ElectronicIndex < 0 ) {
        // Modes 0 and 3: the ESP branch is skipped and the enable cleared.
        vehicle.bEspEnabled = false;
        vehicle.bEspIntervening = false;
        vehicle.EspMagFront = 1.0f;
        vehicle.EspMagRear = 1.0f;
        vehicle.EspGripDiff = 0.0f;
        vehicle.EspThreshold = 0.0f;
        return;
    }

    if ( bReverse ) {
        vehicle.bEspEnabled = false;
        vehicle.bEspIntervening = false;
        vehicle.EspMagRear = kAidMagnitudeParked;
        return;
    }

    const int sideOff = vehicle.NumWheels / 4;
    const int rearBase = 1 << ( vehicle.NumWheels / 2 - 1 );

    const float gripF =
        vehicle.Wheels[0].ContactVelM0 + vehicle.Wheels[sideOff].ContactVelM0;
    const float gripR = vehicle.Wheels[rearBase].ContactVelM0 +
                        vehicle.Wheels[rearBase + sideOff].ContactVelM0;
    const float signF = ( gripF > 0.0f ) ? 1.0f : -1.0f;
    const float signR = ( gripR > 0.0f ) ? 1.0f : -1.0f;
    const float gripDiff =
        ( signF == signR ) ? ( gripF - gripR ) : ( gripF + gripR );
    const float absDiff = std::fabs( gripDiff );
    vehicle.EspGripDiff = gripDiff; // DAT_010d6f0c / DAT_010d6f10 (player only)

    const float espParam =
        gPhysicsConfig.ElectronicParamsValue[vehicle.ElectronicIndex];
    const float espThreshold =
        EvalHelpCurve( gPhysicsConfig.TCStraightTriggerAlt,
                       gPhysicsConfig.ActiveTCTrigger, speed, espParam );
    const float espGain =
        EvalHelpCurve( gPhysicsConfig.TCStraightTriggerMid,
                       gPhysicsConfig.ActiveTCValue, speed, 1.0f );
    vehicle.EspThreshold = espThreshold;

    const int baseAxle = espSlot * rearBase;
    const float base = 0.5f * ( vehicle.Wheels[baseAxle].NormLong +
                                vehicle.Wheels[baseAxle + sideOff].NormLong );

    float target = kEspNoTarget;
    if ( absDiff <= espThreshold || speed <= kEspMinSpeed ) {
        vehicle.bEspIntervening = false;
    } else {
        const float from = vehicle.bEspIntervening ? vehicle.EspMagRear : base;
        target = JPH::Clamp( from + ( absDiff - espThreshold ) * kEspErrorGain *
                                        1.0f * ( -espGain ),
                             kEspMagMin, kEspMagMax );
        vehicle.bEspIntervening = true;
    }

    if ( target == kEspNoTarget ) {
        vehicle.EspMagFiltered = base;
    } else {
        vehicle.EspMagFiltered +=
            ( target - vehicle.EspMagFiltered ) * deltaTime * kEspSmoothingRate;
    }

    vehicle.bEspEnabled = ( target != kEspNoTarget );

    if ( espSlot == 0 ) {
        vehicle.EspMagFront = vehicle.EspMagFiltered;
    } else {
        vehicle.EspMagRear = vehicle.EspMagFiltered;
    }
}

static float WheelAidSlipArg( const PhysicsWheelState &w ) {
    // FUN_0043d080
    const float vLong = w.ContactVelLong;
    const float vLat = w.ContactVelLat;
    if ( vLong > 0.0f ) {
        return std::atan( vLat / vLong );
    }
    return ( vLong < 0.0f ) ? -std::atan( vLat / vLong ) : 0.0f;
}

static float
WheelAidTargetOmega( float slipTerm, float radius, float vLong, float vLat ) {
    // FUN_0043d050
    const float speed = std::sqrt( vLong * vLong + vLat * vLat );
    return ( speed * slipTerm + vLong ) / radius;
}

static float ApplyEngineDragControl( const PhysicsWheelState &w,
                                     bool bEnabled,
                                     float reactTorque,
                                     float drive,
                                     float h ) {
    // FUN_0043e440 (L208-223)
    const float vLong = w.ContactVelLong;
    if ( !bEnabled || drive >= 0.0f || w.AngularVel <= 0.0f || vLong <= 1.0f ) {
        return drive;
    }

    const float radius = std::max( w.Radius, 0.05f );
    const float curve =
        EspCombinedSlipFactor( w.Tire, w.NormalLoad, WheelAidSlipArg( w ) );
    const float target = WheelAidTargetOmega( -( curve * 0.5f ), radius, vLong,
                                              w.ContactVelLat );

    const float vClamp = JPH::Clamp( std::fabs( vLong ), 0.0f, 1.0f );
    const float effInertia = w.Inertia + w.ReflectedInertia;
    const float candidate =
        ( vClamp * target - w.AngularVel ) * ( effInertia / h ) -
        ( reactTorque + drive );

    if ( candidate <= 0.0f ) {
        return drive;
    }
    return std::min( drive + candidate, 0.0f );
}

static void ApplyBrakeAndABS( PhysicsWheelState &w,
                              bool bAbsEnabled,
                              float absMag,
                              float reactTorque,
                              float drive,
                              float h ) {
    // FUN_0043e440 L155-207
    const float authority = w.HandbrakeCmdTorque + w.BrakeCmdTorque;

    float targetOmega = 0.0f;
    if ( bAbsEnabled ) {
        const float radius = std::max( w.Radius, 0.05f );
        const float curve =
            EspCombinedSlipFactor( w.Tire, w.NormalLoad, WheelAidSlipArg( w ) );
        const float sOmega = ( w.AngularVel > 0.0f ) ? 1.0f : -1.0f;
        const float vClamp =
            JPH::Clamp( std::fabs( w.ContactVelLong ), 0.0f, 1.0f );
        targetOmega =
            vClamp * WheelAidTargetOmega( -( curve * sOmega * absMag ), radius,
                                          w.ContactVelLong, w.ContactVelLat );
    }

    const float effInertia = w.Inertia + w.ReflectedInertia;
    const float candidate =
        ( targetOmega - w.AngularVel ) * ( effInertia / h ) -
        ( reactTorque + drive );
    w.BrakeTorqueOut = JPH::Clamp( candidate, std::min( 0.0f, authority ),
                                   std::max( 0.0f, authority ) );
}

static void ApplyESP( PhysicsWheelState &w,
                      bool bEnabled,
                      float espMag,
                      float reactTorque,
                      float drive,
                      float h ) {
    // FUN_0043e440 (L224-279)
    w.EspState = 0;
    w.EspTorqueOut = 0.0f;

    const float vLong = w.ContactVelLong;
    const float vLat = w.ContactVelLat;
    if ( !bEnabled || !w.bGroundContact || std::fabs( w.AngularVel ) <= 0.1f ||
         std::fabs( vLong ) <= 0.1f ) {
        w.EspTargetOmega = w.AngularVel;
        return;
    }
    w.EspState = 1;

    const float radius = std::max( w.Radius, 0.05f );
    const float curve =
        EspCombinedSlipFactor( w.Tire, w.NormalLoad, WheelAidSlipArg( w ) );
    const float sOmega = ( w.AngularVel > 0.0f ) ? 1.0f : -1.0f;
    const float rawTarget =
        WheelAidTargetOmega( curve * sOmega * espMag, radius, vLong, vLat );

    constexpr float kEspTargetFilter = 0.0f;
    w.EspTargetOmega = kEspTargetFilter * w.EspTargetOmega +
                       ( 1.0f - kEspTargetFilter ) * rawTarget;

    const float effInertia = w.Inertia + w.ReflectedInertia;
    const float espTorque =
        ( w.EspTargetOmega - w.AngularVel ) * ( effInertia / h ) -
        ( reactTorque + drive );

    if ( drive > 0.0f && w.AngularVel > 0.0f ) {
        if ( espTorque <= 0.0f ) {
            w.EspTorqueOut = espTorque - drive;
        }
    } else if ( drive < 0.0f && w.AngularVel < 0.0f && espTorque >= 0.0f ) {
        w.EspTorqueOut = ( -espTorque < drive ) ? -drive : espTorque;
    }

    if ( std::fabs( w.EspTorqueOut ) > 10.0f ) {
        w.EspState = 2;
    }
}

static float TireCorneringStiffness( const PhysicsWheelState &w ) {
    const float loadPM = w.NormalLoad * 0.001f; // kLoadToPerMille
    const float c4 = w.Tire.Lat[4];
    if ( std::fabs( c4 ) < 1e-8f ) {
        return 0.0f;
    }
    return w.Tire.Lat[3] * std::sin( 2.0f * std::atan( loadPM / c4 ) );
}

static float TireLateralGripFactor( const PhysicsWheelState &w ) {
    const PhysicsTire &tire = w.Tire;
    const float nLong = w.NormLongClamped;
    if ( std::fabs( tire.FrictionEllipseThreshold ) >= std::fabs( nLong ) ) {
        return 1.0f;
    }
    const float denom = tire.MaxSlipRatio - tire.FrictionEllipseThreshold;
    if ( std::fabs( denom ) <= 1e-8f ) {
        return 1.0f;
    }
    const float minF = tire.FrictionEllipseMinFactor;
    const float f = ( ( std::fabs( nLong ) - tire.FrictionEllipseThreshold ) *
                      ( minF - 1.0f ) ) /
                        denom +
                    1.0f;
    return JPH::Clamp( f, std::min( 1.0f, minF ), std::max( 1.0f, minF ) );
}

PhysicsVehicle::PhysicsVehicle()
    : pPhysicsSystem( nullptr ),
      pChassisBody( nullptr ),
      bRegistered( false ),
      steerLockRad( 0.0f ),
      rawInputThrottle( 0.0f ),
      rawInputBrake( 0.0f ),
      inputThrottle( 0.0f ),
      inputBrake( 0.0f ),
      inputSteer( 0.0f ),
      inputHandbrake( 0.0f ),
      drivingMode( DM_Player ),
      prevInputThrottle( 0.0f ),
      prevInputBrake( 0.0f ),
      prevInputSteer( 0.0f ),
      autoGearboxState( AGS_Stopped ),
      autoGearboxThrottle( 0.0f ),
      autoGearboxBrake( 0.0f ),
      bDrivingInputScripted( false ),
      bCinematicMode( false ),
      bPausePhysics( false ),
      bKeyframed( false ),
      bForceKeyframeMatrix( false ),
      keyframeMatrix( Matrix4x4f_t::Identity() ),
      prevKeyframePosition( Eigen::Vector3f::Zero() ),
      bKeyframedVelocityPrimed( false ),
      filteredSteer( 0.0f ),
      steerCommand( 0.0f ),
      steerCurveScale( 1.0f ),
      steerHelpGain( 0.0f ),
      steerHelpGain2( 0.0f ),
      steerAuxTerm( 0.0f ),
      steerAuxTermPrev( 0.0f ),
      steerYawTerm( 0.0f ),
      steerSideslipTerm( 0.0f ),
      steerDeadZone( 0.0f ),
      steerHelpState( 0 ),
      bUseSteerControl( false ),
      steerAssistBlend( 0.0f ),
      engineOmega( 0.0f ),
      currentGear( 1 ),
      finalDriveRatio( 4.0f ),
      displayRoll( 0.0f ),
      displayPitch( 0.0f ),
      displayYaw( 0.0f ),
      y3DOffset( 0.0f ),
      bodyHeight( 1.3f ),
      aeroDragArea( 0.0f ),
      aeroDownArea( 0.0f ),
      aeroDensity( kAeroDensityFallback ),
      localAccelLong( 0.0f ),
      localAccelLat( 0.0f ),
      localAccelVert( 0.0f ) {
    for ( int i = 0; i < PhysicsVehicleState::kMaxWheels; ++i ) {
        wheelAttach[i] = Eigen::Vector3f::Zero();
        suspMaxLength[i] = 0.30f;
        suspMinLength[i] = 0.05f;
        suspRestLength[i] = 0.20f;
        suspStiffness[i] = 0.0f;
        suspDamping[i] = 0.0f;
        suspDamperRebound[i] = 0.0f;
        suspDamperLP[i] = 0.0f;
        prevSuspLength[i] = suspRestLength[i];
    }
    prevWorldVelocity[0] = prevWorldVelocity[1] = prevWorldVelocity[2] = 0.0f;
    for ( int c = 0; c < SC_Count; ++c ) {
        for ( int k = 0; k < 3; ++k ) {
            steerCurveX[c][k] = kSteerCurveXDefault[k];
            steerCurveY[c][k] = kSteerCurveYDefault[c][k];
        }
    }
    steerNormal[0] = steerNormal[1] = 0.0f;
    steerHelpStraight[0] = steerHelpStraight[1] = 0.0f;
    steerHelpTurn[0] = steerHelpTurn[1] = 0.0f;
}

PhysicsVehicle::~PhysicsVehicle() { destroy(); }

PhysicsVehicle::PerfPackCoefs
PhysicsVehicle::BuildPerfPackCoefs( const DBEntryAftermarketPack *pPack ) {
    // FUN_008345e0 (inlined)
    PerfPackCoefs coefs;
    if ( pPack == nullptr ) {
        return coefs;
    }

    constexpr float kPercentToRatio = 0.01f;
    coefs.BHP = pPack->BHPRatio * kPercentToRatio + 1.0f;
    coefs.TopSpeed = pPack->SpeedRatio * kPercentToRatio + 1.0f;
    coefs.Suspension = pPack->SuspensionRatio * kPercentToRatio + 1.0f;
    coefs.Weight = pPack->WeightRatio * kPercentToRatio + 1.0f;
    coefs.Cx = pPack->CxRatio * kPercentToRatio + 1.0f;
    coefs.Brake = pPack->BrakeRatio * kPercentToRatio + 1.0f;
    coefs.Accel = pPack->AccelerationRatio * kPercentToRatio;
    coefs.bInstalled = true;
    return coefs;
}

PhysicsVehicle::SuspensionSetup PhysicsVehicle::ComputeAxleSuspension(
    const DBEntryCarPhysicsData &db, bool bFront, const PerfPackCoefs &pack ) {
    // FUN_008345e0
    constexpr float kGravity = 9.81f;
    const float springCoef =
        gPhysicsConfig.SpringCoef > 0.0f ? gPhysicsConfig.SpringCoef : 1.0f;

    const float dbWeight =
        db.Weight > 0 ? static_cast<float>( db.Weight ) : 1200.0f;
    const float weight = dbWeight * pack.Weight;
    const float frontFrac =
        db.GravityCenterLength > 0 ? db.GravityCenterLength * 0.01f : 0.5f;
    const float axleFrac = bFront ? frontFrac : ( 1.0f - frontFrac );
    const float coefSuspRaw = db.CoefSuspension > 0
                                  ? static_cast<float>( db.CoefSuspension )
                                  : 100.0f;
    const int32_t rateRaw =
        bFront ? db.SuspensionRateFront : db.SuspensionRateRear;
    const int32_t dampRaw = bFront ? db.DamperRateFront : db.DamperRateRear;
    const float suspRate = rateRaw > 0 ? static_cast<float>( rateRaw ) : 100.0f;
    const float dampRate = dampRaw > 0 ? static_cast<float>( dampRaw ) : 100.0f;

    // Base rate per corner (N/m): 0.006 = 0.01(coef)*0.01(rate)*30*2.
    float kBase =
        0.006f * coefSuspRaw * weight * axleFrac * suspRate * pack.Suspension;
    if ( dbWeight > 3500.0f ) {
        kBase *= 2.5f; // heavy car boost (DAT_00f416c0)
    }
    const float k = Max( 1000.0f, kBase * springCoef * 1.2f );
    const float d = kSuspDampingScale * kBase * 0.0625f *
                    ( coefSuspRaw * 0.01f ) * ( dampRate * 0.01f ) *
                    springCoef * pack.Suspension;

    const float cornerLoad = weight * axleFrac * kGravity * 0.5f;
    const float sag = cornerLoad / k;

    const int32_t rideHtRaw = bFront ? db.RideHeightFront : db.RideHeightBack;
    const float rideHt_m = ( rideHtRaw > 0 ? rideHtRaw : 130 ) * 0.001f;

    constexpr float kSuspLengthRideHeightScale = 2.0f;
    const int32_t travelRaw =
        bFront ? db.SuspensionLengthFront : db.SuspensionLengthRear;
    const float travel = ( travelRaw > 0 ? travelRaw : 100 ) * 0.01f *
                         rideHt_m * kSuspLengthRideHeightScale;

    constexpr float kStrutClearance = 0.05f;
    const float travelUsable = Max( travel, sag );

    SuspensionSetup s;
    s.cornerLoad = cornerLoad;
    s.minLength = kStrutClearance;
    s.staticLength = kStrutClearance + ( travelUsable - sag );
    s.maxLength = s.staticLength + sag;
    s.droopStop = s.maxLength;

    const float massPerWheel = Max( 1.0f, weight * 0.25f );
    const float springOmega = std::sqrt( k / massPerWheel );
    s.frequency = springOmega / kTwoPI;
    s.damping = d / ( 2.0f * massPerWheel * Max( 1.0e-3f, springOmega ) );
    return s;
}

void PhysicsVehicle::initialize() {
    // FUN_00828a50
    OTDU_UNIMPLEMENTED;
}

bool PhysicsVehicle::create( JPH::PhysicsSystem *pSystem,
                             JPH::Body *pChassis,
                             const DBEntryCarPhysicsData &db,
                             const void *pTorqueBinary,
                             bool bFactoryShape,
                             const DBEntryAftermarketPack *pPack ) {
    if ( pSystem == nullptr || pChassis == nullptr ) {
        return false;
    }

    const PerfPackCoefs pack = BuildPerfPackCoefs( pPack );
    if ( pack.bInstalled ) {
        OTDU_LOG_DEBUG( "Vehicle perf pack: topSpeed x%.4f bhp x%.4f "
                        "accel +%.4f cx x%.4f weight x%.4f susp x%.4f "
                        "brake x%.4f\n",
                        pack.TopSpeed, pack.BHP, pack.Accel, pack.Cx,
                        pack.Weight, pack.Suspension, pack.Brake );
    }

    if ( bRegistered ) {
        destroy();
    }

    pPhysicsSystem = pSystem;
    pChassisBody = pChassis;
    drivingMode = DM_Player;
    prevInputThrottle = 0.0f;
    prevInputBrake = 0.0f;
    prevInputSteer = 0.0f;

    for ( int i = 0; i < PhysicsVehicleState::kMaxWheels; ++i ) {
        authoredWheelY[i] = 0.0f;
    }
    bAuthoredWheelYKnown = false;

    bLowSpeedGripState = false;
    bLowSpeedLongAssist = false;
    bLowSpeedLatAssist = false;
    bIsBike = false;

    resetAutomaticGearbox();
    bDrivingInputScripted = false;
    bCinematicMode = false;
    bPausePhysics = false;
    bKeyframed = false;
    bForceKeyframeMatrix = false;
    keyframeMatrix = Matrix4x4f_t::Identity();
    prevKeyframePosition = Eigen::Vector3f::Zero();
    bKeyframedVelocityPrimed = false;
    rawInputThrottle = 0.0f;
    rawInputBrake = 0.0f;

    setDrivingHelpLevel( gVehicleDrivingHelpOverride >= 0
                             ? gVehicleDrivingHelpOverride
                             : ( gpPlayerData != nullptr
                                     ? gpPlayerData->getDrivingHelpLevel()
                                     : 0 ) );

    constexpr float kBodyHeightBias = 100.0f; // mm
    const float rideHeightFrontMM =
        (float)( db.RideHeightFront > 0 ? db.RideHeightFront : 130 );
    const float rideHeightRearMM =
        (float)( db.RideHeightBack > 0 ? db.RideHeightBack : 130 );
    rideHeightFront = rideHeightFrontMM * kMmToM;
    rideHeightRear = rideHeightRearMM * kMmToM;
    const float dbBodyHeight = ( ( db.Height > 0 ? db.Height : 1300 ) -
                                 rideHeightFrontMM - kBodyHeightBias ) *
                               kMmToM;
    bodyHeight = dbBodyHeight;
    if ( bFactoryShape && pChassisBody->GetShape() != nullptr ) {
        bodyHeight =
            pChassisBody->GetShape()->GetLocalBounds().GetSize().GetY();
    }

    const float halfTrackF =
        ( db.TrackFront > 0 ? db.TrackFront : 1500 ) * 0.5f * kMmToM;
    const float halfTrackR =
        ( db.TrackBack > 0 ? db.TrackBack : 1500 ) * 0.5f * kMmToM;
    const float wheelBaseM =
        ( db.WheelBase > 0 ? db.WheelBase : 2600 ) * kMmToM;

    constexpr float kDefaultWheelRadius = 0.32f;
    constexpr float kDefaultWheelWidth = 0.22f;
    float frontRadius = kDefaultWheelRadius;
    float rearRadius = kDefaultWheelRadius;
    float frontWidth = kDefaultWheelWidth;
    float rearWidth = kDefaultWheelWidth;
    if ( gpDatabase != nullptr ) {
        const GameDatabase &gameDB = gpDatabase->getGameDB();
        const DBEntryRim *pRim =
            static_cast<const DBEntryRim *>( db.DefaultRims.pAsRawPointer );
        const bool bInRange = gameDB.DBRim != nullptr && pRim >= gameDB.DBRim &&
                              pRim < gameDB.DBRim + gameDB.pNumItems[2];
        if ( bInRange ) {
            constexpr float kAspectPctToRatio = 0.01f;
            constexpr float kRimInchToHalfM = 0.0127f;

            frontRadius = pRim->FrontHeight * pRim->FrontWidth *
                              kAspectPctToRatio * kMmToM +
                          pRim->FrontDiameter * kRimInchToHalfM;
            rearRadius = frontRadius;

            frontWidth = pRim->FrontWidth * kMmToM;
            rearWidth = pRim->RearWidth * kMmToM;
        }
    }

    const SuspensionSetup attachSuspFront =
        ComputeAxleSuspension( db, true, pack );
    const SuspensionSetup attachSuspRear =
        ComputeAxleSuspension( db, false, pack );
    const float attachYFront =
        ( frontRadius - rideHeightFront - bodyHeight * 0.5f ) +
        attachSuspFront.staticLength;
    const float attachYRear =
        ( rearRadius - rideHeightRear - bodyHeight * 0.5f ) +
        attachSuspRear.staticLength;

    // FL, FR, RL, RR (TDU: right = +x, forward = +z).
    wheelAttach[0] =
        Eigen::Vector3f( -halfTrackF, attachYFront, wheelBaseM * 0.5f );
    wheelAttach[1] =
        Eigen::Vector3f( halfTrackF, attachYFront, wheelBaseM * 0.5f );
    wheelAttach[2] =
        Eigen::Vector3f( -halfTrackR, attachYRear, -wheelBaseM * 0.5f );
    wheelAttach[3] =
        Eigen::Vector3f( halfTrackR, attachYRear, -wheelBaseM * 0.5f );

    const float bodyWidth = ( db.Width > 0 ? db.Width : 1800 ) * kMmToM;
    const float frontalArea = Max( 0.0f, bodyWidth * dbBodyHeight );
    const float cxDrag = Max( 0.0f, db.CxLength ) * pack.Cx;
    const float czLift = Max( 0.0f, db.CxHeight ) * kAeroLiftCoef *
                         ( gbSimuModeEnabled ? kAeroSimuLiftMult : 1.0f );
    aeroDragArea = 0.5f * frontalArea * cxDrag;
    aeroDownArea = 0.5f * frontalArea * czLift;
    aeroDensity = gPhysicsConfig.AeroDensity[0] > 0.0f
                      ? gPhysicsConfig.AeroDensity[0]
                      : kAeroDensityFallback;
    wheelBase = wheelBaseM;

    char *pFWDStr = gpDatabase != nullptr
                        ? gpDatabase->getStringByHashcode( 1, 0x035bf035u )
                        : nullptr;
    char *pRWDStr = gpDatabase != nullptr
                        ? gpDatabase->getStringByHashcode( 1, 0x034cadf5u )
                        : nullptr;
    const bool bFWD =
        ( pFWDStr != nullptr && db.DriveType.pAsString == pFWDStr );
    const bool bRWD =
        ( pRWDStr != nullptr && db.DriveType.pAsString == pRWDStr );

    // Suspension
    const float mass =
        ( db.Weight > 0 ? static_cast<float>( db.Weight ) : 1200.0f ) *
        pack.Weight;
    const float massPerWheel = mass * 0.25f;
    const float brakeBalance = bFWD ? 0.75f : ( bRWD ? 0.70f : 0.72f );

    const float BrakeBalance =
        db.BrakeBalance > 0 ? db.BrakeBalance * 0.01f : kBrakeBalance;
    const float BrakeTorque = kMaxBrakeTorque;

    // Select the vehicle tire curve from tires.bnk
    const int numProfiles = gpPhysics ? gpPhysics->getNumTireProfiles() : 0;
    const int tireIdx = SelectTireIndex( mass, numProfiles );
    const TirePacejka *pFrontTire =
        gpPhysics ? gpPhysics->getTireProfile( tireIdx ) : nullptr;
    const TirePacejka *pRearTire = pFrontTire;

    if ( pFrontTire != nullptr ) {
        OTDU_LOG_DEBUG( "Vehicle tire: %d profiles, idx=%d curve=tires.bnk\n",
                        numProfiles, tireIdx );
    } else {
        OTDU_LOG_WARN( "Vehicle tire: no tires.bnk profile (%d available, "
                       "idx=%d) -- falling back to PLACEHOLDER tire curves\n",
                       numProfiles, tireIdx );
    }

    const PhysicsVehicle::SuspensionSetup suspFront =
        PhysicsVehicle::ComputeAxleSuspension( db, true, pack );
    const PhysicsVehicle::SuspensionSetup suspRear =
        PhysicsVehicle::ComputeAxleSuspension( db, false, pack );
    const float kTwoPiSusp = 6.28318530718f;

    const float groundY =
        Min( attachYFront - suspFront.staticLength - frontRadius,
             attachYRear - suspRear.staticLength - rearRadius );
    const float hullBottomY =
        pChassis->GetShape()->GetLocalBounds().mMin.GetY();
    const float clearance = hullBottomY - groundY;
    if ( clearance < 0.0f ) {
        OTDU_LOG_DEBUG( "Vehicle chassis hull penetrates the ground plane by "
                        "%.3f m at static ride height (hull bottom y=%.3f, "
                        "contact plane y=%.3f) -- expect the contact solver "
                        "to fight the suspension\n",
                        -clearance, hullBottomY, groundY );
    } else {
        OTDU_LOG_DEBUG( "Vehicle ground clearance: %.3f m (hull bottom "
                        "y=%.3f, contact plane y=%.3f)\n",
                        clearance, hullBottomY, groundY );
    }

    steerLockRad = ( db.SteerLock > 0 ? db.SteerLock : 30.0f ) * kDegToRad;

    {
        const PhysicsConfig &c = gPhysicsConfig;
        const float *pY[SC_Count] = {
            c.SteeringLimit, c.SteeringSpeed, c.SteeringCenterSpeed,
            c.SteeringStraightSpeed, c.SteeringCounterSpeed };
        for ( int i = 0; i < SC_Count; ++i ) {
            const float *pX = c.SteeringFilterSpeed[i];
            const bool bValidX = pX[1] > pX[0] && pX[2] > pX[1];
            for ( int k = 0; k < 3; ++k ) {
                steerCurveX[i][k] = bValidX ? pX[k] : kSteerCurveXDefault[k];
                steerCurveY[i][k] =
                    bValidX ? pY[i][k] : kSteerCurveYDefault[i][k];
            }
        }
        steerNormal[0] = c.SteeringNormal[0];
        steerNormal[1] = c.SteeringNormal[1];
        steerHelpStraight[0] = c.SteeringHelpStraight[0];
        steerHelpStraight[1] = c.SteeringHelpStraight[1];
        steerHelpTurn[0] = c.SteeringHelpTurn[0];
        steerHelpTurn[1] = c.SteeringHelpTurn[1];
        steerHelpGain = steerNormal[0];
        steerHelpGain2 = steerNormal[1];
        // TODO: FIXME: Should be read from active PlayerData
        bUseSteerControl = gUseSteerControl;
    }
    finalDriveRatio = db.FinalDriveRatio > 0 ? db.FinalDriveRatio : 4.0f;

    const float gripPerMille =
        db.CoefGrip > 0 ? db.CoefGrip * kDBPerMille : 1.0f;
    const float gripCal =
        ( db.CoefAccel > 0 ? db.CoefAccel * kDBHundredths : 1.0f ) + pack.Accel;
    const float brakeCal =
        ( db.CoefBrake > 0 ? db.CoefBrake * kDBHundredths : 1.0f ) * pack.Brake;
    const float axleBalance =
        db.CoefOverturning > 0 ? db.CoefOverturning * kDBHundredths : 1.0f;

    // Vehicle + per-wheel setup
    vehicle = PhysicsVehicleState();
    vehicle.NumWheels = 4;
    vehicle.Mass = mass; // auto shift near-lift traction threshold
    for ( int i = 0; i < 4; ++i ) {
        PhysicsWheelState &w = vehicle.Wheels[i];
        const bool bFront = ( i < 2 );
        const PhysicsVehicle::SuspensionSetup &s =
            bFront ? suspFront : suspRear;
        const float springOmega = kTwoPiSusp * s.frequency;
        suspMaxLength[i] = s.maxLength;
        suspMinLength[i] = s.minLength;
        suspRestLength[i] = s.staticLength;
        suspStiffness[i] = massPerWheel * springOmega * springOmega;
        suspDamping[i] = 2.0f * s.damping * massPerWheel * springOmega;
        const float reboundCoef = ( gPhysicsConfig.ReboundCoef > 0.0f )
                                      ? gPhysicsConfig.ReboundCoef
                                      : 1.0f;
        suspDamperRebound[i] =
            suspDamping[i] * kSuspReboundStiffen * reboundCoef;
        suspDamperLP[i] = 0.0f;
        suspStaticLength[i] = s.staticLength;
        suspCornerLoad[i] = s.cornerLoad;
        suspDroopStop[i] = s.droopStop;
        w.Radius = bFront ? frontRadius : rearRadius;
        wheelWidth[i] = bFront ? frontWidth : rearWidth;
        w.Inertia = w.Radius * w.Radius * kWheelInertiaRadiusCoef;
        w.SideFlag = ( i & 1 ) ? 1 : 0; // left wheels mirror lateral/steer
        w.bSteered = bFront;
        w.bDriven = bFWD ? bFront : ( bRWD ? !bFront : true );
        w.DeadbandLong = kWheelVelDeadband;
        w.DeadbandLat = kWheelVelDeadband;
        w.DeadbandOmega = kWheelOmegaDeadband;
        const float loadCapDriveCoef =
            ( !bFWD && !bRWD ) ? kSuspLoadCapAWD : 1.0f;
        w.SuspMaxLoad = mass * loadCapDriveCoef * kSuspLoadCapCoef /
                        static_cast<float>( vehicle.NumWheels );
        w.SuspMinLoad = 0.0f;
        w.LoadScaleLong = 1.0f;
        w.LoadScaleDrag = 0.0f; // v^2 drag off for the spike
        w.DynamicGrip = 1.0f;
        w.GripMult = 1.0f;

        const PhysicsConfig &c = gPhysicsConfig;
        w.AxleIndex = bFront ? 0 : 1;
        const float local70 = bFront ? axleBalance : ( 1.0f / axleBalance );

        const float frictionCoef =
            bFront ? ( c.FrontFrictionCoef > 0 ? c.FrontFrictionCoef : 1.7f )
                   : ( c.RearFrictionCoef > 0 ? c.RearFrictionCoef : 1.7f );
        float lateralCoef =
            bFront
                ? ( c.FrontLateralFrictionCoef > 0 ? c.FrontLateralFrictionCoef
                                                   : 1.4f )
                : ( c.RearLateralFrictionCoef > 0 ? c.RearLateralFrictionCoef
                                                  : 1.4f );
        if ( pack.bInstalled ) {
            lateralCoef *= gbSimuModeEnabled ? kPackLatFrictionBoostSimu
                                             : kPackLatFrictionBoost;
        }
        const float lngBrakeCoef = bFront ? ( c.FrontLngBrakeFrictionCoef > 0
                                                  ? c.FrontLngBrakeFrictionCoef
                                                  : 1.2f )
                                          : ( c.RearLngBrakeFrictionCoef > 0
                                                  ? c.RearLngBrakeFrictionCoef
                                                  : 1.2f );

        const float latBrakeCoef = bFront
                                       ? ( c.FrontLatBrakeFrictionCoefSlow > 0
                                               ? c.FrontLatBrakeFrictionCoefSlow
                                               : 1.0f )
                                       : ( c.RearLatBrakeFrictionCoefSlow > 0
                                               ? c.RearLatBrakeFrictionCoefSlow
                                               : 1.0f );
        const float latBrakeCoefFast = bFront
                                           ? ( c.FrontLatBrakeFrictionCoefFast >
                                                       0
                                                   ? c.FrontLatBrakeFrictionCoefFast
                                                   : 1.0f )
                                           : ( c.RearLatBrakeFrictionCoefFast >
                                                       0
                                                   ? c.RearLatBrakeFrictionCoefFast
                                                   : 1.0f );

        w.GripLong = frictionCoef * local70 * gripCal * gripPerMille;
        w.GripLat = lateralCoef / gripCal;
        w.BaseGripLong = w.GripLong;
        w.BaseGripLat = w.GripLat;
        w.BrakeGripLong = lngBrakeCoef * ( 1.0f / gripPerMille ) *
                          ( 1.0f / gripCal ) * brakeCal;
        w.BrakeGripLatSlow = latBrakeCoef / gripCal;
        w.BrakeGripLatFast = latBrakeCoefFast / gripCal;
        w.BrakeGripLat = w.BrakeGripLatSlow;
        w.ReactionScale =
            bFront ? ( c.ResistiveCoefFront > 0 ? c.ResistiveCoefFront : 1.0f )
                   : ( c.ResistiveCoefRear > 0 ? c.ResistiveCoefRear : 1.0f );
        w.ReactionSlipCoef =
            bFront ? c.ResistiveLateralCoefFront : c.ResistiveLateralCoefRear;

        const float axleBal = bFront ? BrakeBalance : ( 1.0f - BrakeBalance );
        w.BrakeTorque = axleBal * BrakeTorque;

        SeedTire( w.Tire, bFront );
        const TirePacejka *pProfile = bFront ? pFrontTire : pRearTire;
        if ( pProfile != nullptr ) {
            CopyTireCurve( w.Tire, *pProfile );
        }

        prevSuspLength[i] = suspRestLength[i];
    }

    arbStiffness[0] = kAntiRollFraction * suspStiffness[0]; // front
    arbStiffness[1] = kAntiRollFraction * suspStiffness[2]; // rear

    // Drivetrain routing from DriveType (AWD when neither FWD nor RWD).
    vehicle.bFrontDrive = bFWD;
    vehicle.bRearDrive = bRWD;
    if ( !bFWD && !bRWD ) {
        vehicle.bFrontDrive = vehicle.bRearDrive = vehicle.bCenterDrive = true;
    }

    bLowSpeedLatAssist = bRWD;
    bLowSpeedLongAssist = ( !bFWD && !bRWD );

    // Gearbox: reverse(0) + neutral(1) + DB gears; start in 1st (auto).
    PhysicsGearboxSim &gb = vehicle.GearBox;
    const int nDbGears =
        ( db.NumGears > 0 && db.NumGears <= 7 ) ? db.NumGears : 5;
    gb.NumGears = nDbGears + 2;
    gb.Ratios[0] = -4.0f; // reverse (DAT_00f41e80)
    gb.Ratios[1] = 0.0f;  // neutral
    for ( int i = 0; i < nDbGears; ++i ) {
        gb.Ratios[i + 2] =
            ( db.GearRatios[i] != 0.0f ) ? db.GearRatios[i] : 1.0f;
    }

    {
        const float redlineOmega =
            ( db.Redline > 0 ? db.Redline : 7000 ) * kRpmToRadPerSec;
        const float topGear = gb.Ratios[gb.NumGears - 1];

        const float topSpeedMS =
            ( db.TopSpeedKMH > 0.0f ? db.TopSpeedKMH : 200.0f ) * kKmhToMs *
            pack.TopSpeed;
        const float rearRadiusM = ( rearRadius > 1.0e-3f )
                                      ? rearRadius
                                      : kDefaultWheelRadius;
        gb.FinalDrive =
            ( topGear > 1.0e-4f && topSpeedMS > 1.0f )
                ? ( redlineOmega * kFinalDriveRedlineFrac * rearRadiusM ) /
                      ( topGear * topSpeedMS )
                : ( db.FinalDriveRatio > 0 ? db.FinalDriveRatio : 4.0f );
        finalDriveRatio = gb.FinalDrive;
    }

    const float simInertiaScale = gbSimuModeEnabled ? 0.5f : 1.0f;
    gb.BaseInertia =
        ( db.GearboxIntertia != 0 ? db.GearboxIntertia * 0.01f : 0.25f ) *
        simInertiaScale;
    gb.TransEff = 0.9f;
    gb.CurrentGear = 2; // 1st forward
    gb.bAutomatic = true;
    gb.UpshiftBand = gPhysicsConfig.RedlinePurcent > 0.0f
                         ? gPhysicsConfig.RedlinePurcent
                         : 0.7f;
    gb.DownshiftBand = ( gShift2ndTo1st > 0.0f && gShift2ndTo1st < 1.0f )
                           ? gShift2ndTo1st
                           : 0.5f;

    bool bManualBox = false;
    if ( gpDatabase != nullptr ) {
        char *pManualA = gpDatabase->getStringByHashcode( 1, 0x8c05bu );
        char *pManualB = gpDatabase->getStringByHashcode( 1, 0x84b2bu );
        bManualBox =
            ( pManualA != nullptr && db.GearboxType.pAsString == pManualA ) ||
            ( pManualB != nullptr && db.GearboxType.pAsString == pManualB );
    }
    const float *pShiftDur =
        bManualBox ? gShiftTimeManual : gShiftTimeSequential;
    gb.UpshiftDelay = pShiftDur[0] > 0.0f ? pShiftDur[0] : 0.30f;
    gb.DownshiftDelay = pShiftDur[1] > 0.0f ? pShiftDur[1] : 0.30f;

    // Engine: fixed rev references, per-car character from the curve + ratios.
    PhysicsEngineSim &en = vehicle.Engine;
    const float rpmToRad = 0.10472f;
    en.OmegaMax = ( db.Redline > 0 ? db.Redline : 7000 ) * rpmToRad;
    en.OmegaIdle = kThousandRpmOmega;
    en.Inertia = ( db.EngineInertia > 0 ? db.EngineInertia * 0.004f : 0.4f ) *
                 simInertiaScale;
    en.Omega = en.OmegaIdle;
    en.FrictionLoss = kEngineFrictionLoss;
    en.TorqueScale = kEngineFakeCoef;

    en.State = eEngineState::ES_Off;
    en.IgnTimer = 0.0f;
    en.bIgnitionOnce = false;
    en.IgnitionOn = true; // key on: auto-start + auto-restart on stall
    en.Throttle = 0.0f;
    en.bApplyIdleThrottleFloor = false;
    en.CrankDuration =
        ( db.IgnitionTimeIgnite > 0.05f && db.IgnitionTimeIgnite < 3.0f )
            ? db.IgnitionTimeIgnite
            : 0.5f;

    const float bhp = ( db.BHP > 0 ? (float)db.BHP : 200.0f ) * pack.BHP;

    const float engBrakeScale =
        0.1f * 2.0f; // = 0.2 (FLOAT_00f41668*DAT_00f416b0)
    const float frictionCoef =
        ( gPhysicsConfig.EngineBrakePower > 0 ? gPhysicsConfig.EngineBrakePower
                                              : 1.0f ) *
        engBrakeScale;

    int32_t blobCount = 0;
    if ( pTorqueBinary != nullptr ) {
        std::memcpy( &blobCount, pTorqueBinary, sizeof( blobCount ) );
    }
    if ( pTorqueBinary != nullptr && blobCount >= 2 && blobCount <= 256 ) {
        const float *pBlobPairs = reinterpret_cast<const float *>(
            static_cast<const char *>( pTorqueBinary ) + sizeof( int32_t ) );
        BuildEngineCurves( en, pBlobPairs, blobCount, bhp, 1.0f, frictionCoef );

        float peakTqBuilt = 0.0f;
        for ( int i = 0; i < en.CurveCount; ++i ) {
            peakTqBuilt = std::max( peakTqBuilt, en.CurveMax[i].Torque );
        }
        OTDU_LOG_DEBUG( "Engine curve: torque blob (%d pts, %.0f BHP) "
                        "peakTq=%.1f@%.1f rad/s, omegaPeakPower=%.1f, "
                        "first=(%.1f,%.1f) last=(%.1f,%.1f)\n",
                        blobCount, bhp, peakTqBuilt, en.PeakTorqueOmega,
                        en.PeakPowerOmega, en.CurveMax[0].Omega,
                        en.CurveMax[0].Torque,
                        en.CurveMax[en.CurveCount - 1].Omega,
                        en.CurveMax[en.CurveCount - 1].Torque );
    } else {
        // Synthesized 4-point fallback: idle / peak-torque / peak-power /
        // redline.
        const float peakTq = ( db.TorqueNm > 0 ? (float)db.TorqueNm : 300.0f );
        const float omTq =
            ( db.TorqueRPM > 0 ? db.TorqueRPM : 4000 ) * rpmToRad;
        const float omPw = ( db.RPM > 0 ? db.RPM : 6000 ) * rpmToRad;
        const float tqAtPw = ( omPw > 1.0f ) ? ( bhp * 735.5f / omPw ) : peakTq;
        const float pairs[8] = { en.OmegaIdle, peakTq * 0.5f, omTq,
                                 peakTq,       omPw,          tqAtPw,
                                 en.OmegaMax,  tqAtPw * 0.75f };
        BuildEngineCurves( en, pairs, 4, bhp, 1.0f, frictionCoef );
        OTDU_LOG_DEBUG(
            "Engine curve: synthesized (no torque blob, %.0f BHP)\n", bhp );
    }

    {
        float peakTq = 0.0f;
        for ( int i = 0; i < en.CurveCount; ++i ) {
            peakTq = std::max( peakTq, en.CurveMax[i].Torque );
        }
        const float freeRevRate = ( en.Inertia > 1.0e-4f )
                                      ? ( peakTq / en.Inertia * kOmegaToRpm )
                                      : 0.0f;
    }

    const float curvePeakPowerOmega = en.PeakPowerOmega;
    constexpr float kMaxPeakPowerFrac = 0.95f;
    if ( en.PeakPowerOmega > en.OmegaMax * kMaxPeakPowerFrac ) {
        en.PeakPowerOmega = en.OmegaMax * kMaxPeakPowerFrac;
    }

    en.ThrottleIdle = ThrottlePosForTorque( en, en.FrictionLoss * en.OmegaIdle,
                                            en.OmegaIdle );
    OTDU_LOG_DEBUG( "Engine idle: %.0f rpm, throttle %.4f (target %.2f N.m @ "
                    "%.1f rad/s, frictionLoss=%.4f)\n",
                    en.OmegaIdle / rpmToRad, en.ThrottleIdle,
                    en.FrictionLoss * en.OmegaIdle, en.OmegaIdle,
                    en.FrictionLoss );

    const float peakPowerW =
        EngineTorqueAt( en, en.PeakPowerOmega, 1.0f ) * en.PeakPowerOmega;
    const float peakPowerHp = peakPowerW * kWattToMetricHp;
    en.RevLimiterCutTime =
        JPH::Clamp( ( peakPowerHp - kRevLimiterPowerRef ) *
                            kRevLimiterPowerSlope * kRevLimiterPowerScale +
                        kRevLimiterCutMax,
                    kRevLimiterCutMin, kRevLimiterCutMax );
    if ( db.Category.pAsString != nullptr &&
         StringCmpI( db.Category.pAsString, "G" ) == 0 ) {
        en.RevLimiterCutTime = 0.0f; // no power cut, redline snap only
    }
    OTDU_LOG_DEBUG( "Engine rev limiter: cut window %.3f s (%.0f hp from the "
                    "curve, category '%s')\n",
                    en.RevLimiterCutTime, peakPowerHp,
                    db.Category.pAsString != nullptr ? db.Category.pAsString
                                                     : "?" );
    en.IgnTimer = en.RevLimiterCutTime;

    for ( PhysicsDifferentialSim *d :
          { &vehicle.DiffFront, &vehicle.DiffRear, &vehicle.DiffCenter } ) {
        d->Type = 4;
        d->Gain = 1.0f;
        d->Deadband = 0.0f;
        d->SplitRatio = 0.5f;
        d->LockCoef = 0.0f;
    }
    if ( db.TransmissionPrimacy > 0 ) {
        vehicle.DiffCenter.SplitRatio =
            Clamp( db.TransmissionPrimacy * 0.01f, 0.0f, 1.0f );
    }
    vehicle.DiffFront.RatioThreshold = 5.0f;  // unused for spool
    vehicle.DiffRear.RatioThreshold = 3.0f;   // unused for spool
    vehicle.DiffCenter.RatioThreshold = 2.0f; // unused for spool
    vehicle.DiffFront.LockCoef = vehicle.bFrontDrive ? 10.0f : 0.0f;
    vehicle.DiffRear.LockCoef = vehicle.bRearDrive ? 10.0f : 0.0f;

    const float avgRadius = 0.5f * ( frontRadius + rearRadius );
    vehicle.BrakeTorque = massPerWheel * 9.81f * avgRadius * 1.2f;

    pPhysicsSystem->AddStepListener( this );
    bRegistered = true;

    publishStepPose();
    return true;
}

void PhysicsVehicle::destroy() {
    if ( pPhysicsSystem != nullptr ) {
        if ( bRegistered ) {
            pPhysicsSystem->RemoveStepListener( this );
        }
        if ( pChassisBody != nullptr ) {
            JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
            bi.RemoveBody( pChassisBody->GetID() );
            bi.DestroyBody( pChassisBody->GetID() );
        }
    }
    bRegistered = false;
    pPhysicsSystem = nullptr;
    pChassisBody = nullptr;
}

void PhysicsVehicle::setInput( float throttle, float brake, float steer ) {
    rawInputThrottle = throttle;
    rawInputBrake = brake;
    inputSteer = steer;
}

void PhysicsVehicle::resetAutomaticGearbox() {
    autoGearboxState = AGS_Stopped;
    autoGearboxThrottle = 0.0f;
    autoGearboxBrake = 0.0f;
}

static float PedalResponseCurve( float x, float k ) {
    // FUN_00824730
    if ( k < 0.0f ) {
        const float den = std::log( 1.0f - k );
        return ( std::fabs( den ) > 1e-8f ) ? ( std::log( 1.0f - k * x ) / den )
                                            : x;
    }
    const auto taylor = []( float u ) {
        return 1.0f + u + u * u * kPedalCurveC2 + u * u * u * kPedalCurveC3;
    };
    const float den = taylor( k ) - 1.0f;
    if ( std::fabs( den ) < 1e-8f ) {
        return x; // k == 0: retail takes the unshaped branch outright
    }
    return ( taylor( x * k ) - 1.0f ) / den;
}

void PhysicsVehicle::arbitrateAutomaticGearbox( float deltaTime,
                                                float chassisSpeed ) {
    // FUN_0041a210
    if ( bDrivingInputScripted ) {
        setClutchOverride( -1.0f );
        resetAutomaticGearbox();
        inputThrottle = rawInputThrottle;
        inputBrake = rawInputBrake;
        return;
    }

    float accel = rawInputThrottle;
    float brake = rawInputBrake;
    if ( kThrottleCurveK != 0.0f ) {
        accel = PedalResponseCurve( accel, kThrottleCurveK );
    }
    brake = PedalResponseCurve( brake, kBrakeCurveK );
    if ( !gbSimuModeEnabled ) {
        accel = Clamp( accel / kPedalGain, 0.0f, 1.0f );
        brake = Clamp( brake / kPedalGain, 0.0f, 1.0f );
    }

    float outThrottle = autoGearboxThrottle;
    float outBrake = autoGearboxBrake;

    const DrivingGearbox gearbox = getDrivingGearbox();
    if ( !gearbox.bAutomatic || deltaTime <= 0.0f || !gearbox.bEngineFiring ) {
        if ( gearbox.bAutomatic ) {
            setClutchOverride( -1.0f );
        }
        autoGearboxThrottle = accel;
        autoGearboxBrake = brake;
        inputThrottle = accel;
        inputBrake = brake;
        return;
    }

    const float speed = chassisSpeed;
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
        if ( brake > kAutoGbxPedal && accel < kAutoGbxPedal &&
             drivingMode != DM_Parked && drivingMode != DM_SlowDown ) {
            autoGearboxState = AGS_EngagingReverse;
        }
        outBrake = kAutoGbxHoldBrake; // autohold, keeps the car put
        outThrottle = accel;
        setCurrentGear( 2 ); // sits in 1st, declutched
        bDeclutch = true;
        break;

    case AGS_EngagingReverse:
        if ( accel > kAutoGbxPedal ) {
            autoGearboxState = AGS_LineLock;
        }
        if ( gearbox.bShiftIdle && gearbox.Gear > 0 ) {
            if ( gearbox.Gear > 1 ) {
                setCurrentGear( 1 ); // snap to neutral first
            }
            requestDownshift(); // neutral -> reverse
        }
        if ( gearbox.Gear == 0 ) {
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
        setCurrentGear( 0 );
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
                    setCurrentGear( 1 ); // out of reverse first
                }
                requestUpshift();
            } else if ( gearbox.Gear > 2 ) {
                requestDownshift();
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
        setCurrentGear( 2 );
        bDeclutch = true;
        break;

    default:
        autoGearboxState = AGS_Stopped;
        break;
    }

    setClutchOverride( bDeclutch ? 0.0f : -1.0f );

    autoGearboxThrottle = outThrottle;
    autoGearboxBrake = outBrake;
    inputThrottle = outThrottle;
    inputBrake = outBrake;
}

void PhysicsVehicle::setHandbrake( float handbrake ) {
    inputHandbrake = JPH::Clamp( handbrake, 0.0f, 1.0f );
}

void PhysicsVehicle::setIgnition( bool bKeyOn, bool bCutEdge ) {
    // FUN_0041a210 (inlined)
    PhysicsEngineSim &e = vehicle.Engine;
    e.IgnitionOn = bKeyOn;
    if ( bCutEdge ) {
        e.bIgnitionOnce = true;
    }
}

void PhysicsVehicle::applyDrivingMode() {
    // FUN_0083db70
    switch ( drivingMode ) {
    case DM_HoldBrake:
        inputThrottle = 0.0f;
        inputBrake = 0.5f; // DAT_00f41674
        inputSteer = 0.0f;
        break;

    case DM_Parked:
        inputBrake = 1.0f; // FLOAT_00f4165c
        inputSteer = 0.0f;
        setClutchOverride( 0.0f );
        break;

    case DM_SlowDown:
        inputThrottle = 0.0f;
        inputBrake = 0.65f; // DAT_00f41b6c
        inputSteer = 0.0f;
        setClutchOverride( 0.0f );
        break;

    case DM_RepeatLast:
        inputThrottle = prevInputThrottle;
        inputBrake = prevInputBrake;
        inputSteer = prevInputSteer;
        break;

    default:
        break;
    }

    prevInputThrottle = inputThrottle;
    prevInputBrake = inputBrake;
    prevInputSteer = inputSteer;
}

PhysicsVehicle::DrivingGearbox PhysicsVehicle::getDrivingGearbox() const {
    const PhysicsGearboxSim &g = vehicle.GearBox;

    DrivingGearbox out;
    out.Gear = g.CurrentGear;
    out.NumGears = g.NumGears;
    out.ForwardSpeed = vehicle.ForwardSpeed;
    out.bAutomatic = g.bAutomatic;
    out.bEngineFiring = ( vehicle.Engine.State == eEngineState::ES_Running ||
                          vehicle.Engine.State == eEngineState::ES_Stalling );
    out.bShiftIdle = ( g.ShiftState == 1 && g.ShiftTimer <= 0.0f );
    out.bEngineRunning = ( vehicle.Engine.State == eEngineState::ES_Running );
    out.ShiftDirection = 0;
    if ( g.ShiftState == 2 ) {
        if ( g.ShiftSubstate > 0 ) {
            out.ShiftDirection = 1;
        } else if ( g.ShiftSubstate < 0 ) {
            out.ShiftDirection = -1;
        }
    }
    return out;
}

void PhysicsVehicle::setCurrentGear( int32_t gear ) {
    PhysicsGearboxSim &g = vehicle.GearBox;
    if ( gear < 0 || gear >= g.NumGears ) {
        return;
    }
    g.CurrentGear = gear;
}

void PhysicsVehicle::setAutomaticGearbox( bool bAutomatic ) {
    vehicle.GearBox.bAutomatic = bAutomatic;
}

void PhysicsVehicle::requestUpshift() {
    // FUN_00439de0
    PhysicsGearboxSim &g = vehicle.GearBox;
    if ( g.ShiftState != 1 || g.ShiftTimer > 0.0f ) {
        return;
    }
    g.ShiftState = 2;
    g.ShiftSubstate = 1;
    g.ShiftTimer = g.UpshiftDelay;
}

void PhysicsVehicle::requestDownshift() {
    // FUN_00439e30
    PhysicsGearboxSim &g = vehicle.GearBox;
    if ( g.ShiftState != 1 || g.ShiftTimer > 0.0f ) {
        return;
    }
    g.ShiftState = 2;
    g.ShiftSubstate = -1;
    g.ShiftTimer = g.DownshiftDelay;
}

void PhysicsVehicle::setClutchOverride( float engagement ) {
    vehicle.Clutch.OverrideEngagement = engagement;
}

float PhysicsVehicle::filterSteering( float rawSteer,
                                      const JPH::Body &chassis,
                                      float deltaTime ) {
    // FUN_0041ad60
    const float speed = chassis.GetLinearVelocity().Length(); // local_44
    const float normSpeed = kSteerNormSpeed;

    float sens = kSteerSensRetailHigh;
    if ( gSensitivity[1] > gSensitivity[0] &&
         gSensitivity[2] > gSensitivity[1] ) {
        float base, slope, lo, hi;
        if ( normSpeed >= gSensitivity[1] ) { // upper segment [0.5,1]
            slope = ( gSensitivity[5] - gSensitivity[4] ) /
                    ( gSensitivity[2] - gSensitivity[1] );
            base = gSensitivity[4];
            sens = slope * ( normSpeed - gSensitivity[1] ) + base;
            lo = gSensitivity[4];
            hi = gSensitivity[5];
        } else { // lower segment [0,0.5]
            slope = ( gSensitivity[4] - gSensitivity[3] ) /
                    ( gSensitivity[1] - gSensitivity[0] );
            base = gSensitivity[3];
            sens = slope * ( normSpeed - gSensitivity[0] ) + base;
            lo = gSensitivity[3];
            hi = gSensitivity[4];
        }
        sens = JPH::Clamp( sens, std::min( lo, hi ), std::max( lo, hi ) );
    }

    const float sensTurn = sens * kSteerTurnScale;

    const bool bSteerControl = ( gVehicleSteerControlOverride >= 0 )
                                   ? ( gVehicleSteerControlOverride != 0 )
                                   : bUseSteerControl;

    float linearity;
    if ( bSteerControl ) {
        linearity = gSteeringLinearity;
    } else {
        linearity = kSteerLinearity * ( 1.0f - steerAssistBlend );
        linearity = JPH::Clamp( linearity, std::min( 0.0f, kSteerLinearity ),
                                std::max( 0.0f, kSteerLinearity ) );
    }

    const float turn = ( gSteeringTurn > 0.0f ) ? gSteeringTurn : 270.0f;
    float cmd;
    if ( bSteerControl && linearity >= kSteerLinearityMin ) {
        const float lockFrac = turn * ( 1.0f / kSteerScale ); // fVar11
        const float k = SteerResponseCurve( lockFrac, linearity );
        const float sign = ( rawSteer <= 0.0f ) ? -1.0f : 1.0f;
        const float mag = std::fabs( rawSteer );
        if ( mag > lockFrac ) {
            const float den = 1.0f - lockFrac;
            const float t = ( std::fabs( den ) > 1.0e-6f )
                                ? ( ( mag - lockFrac ) / den )
                                : 0.0f;
            cmd = sign * ( SteerResponseCurve( t, k ) + ( 1.0f - t ) * k );
        } else {
            cmd = sign * SteerResponseCurve( mag, k );
        }
    } else {
        cmd = ( rawSteer / turn ) * kSteerScale * sensTurn;
        if ( linearity >= kSteerLinearityMin ) {
            const float sign = ( cmd >= 0.0f ) ? 1.0f : -1.0f;
            cmd = sign * SteerResponseCurve( std::fabs( cmd ), linearity );
        }
    }
    cmd = JPH::Clamp( cmd, -1.0f, 1.0f ); // fStack_54

    if ( !bSteerControl ) {
        steerHelpState = 0;
        steerDeadZone = 0.0f; // L191
        filteredSteer = cmd;
        return filteredSteer;
    }
    steerDeadZone = gSteeringDeadZone; // L194

    const JPH::Mat44 rot = chassis.GetWorldTransform().GetRotation();
    const JPH::Vec3 angVel = chassis.GetAngularVelocity();
    steerYawTerm = -angVel.Dot( rot.GetColumn3( 1 ) );

    const JPH::Vec3 localVel =
        rot.Transposed3x3() * chassis.GetLinearVelocity();
    const float vForward = localVel.GetZ(); // +0x4238
    float blend = steerYawTerm * kSteerYawBlendScale;
    if ( speed > kSteerSlipBlendLo ) {
        const float slip = std::atan2( localVel.GetX(), vForward );
        const float t = ( std::fabs( vForward ) - kSteerSlipBlendLo ) /
                        ( kSteerSlipBlendHi - kSteerSlipBlendLo );
        blend = JPH::Clamp( ( slip - blend ) * t + blend,
                            std::min( blend, slip ), std::max( blend, slip ) );
    }
    steerSideslipTerm = -blend; // FIXME: Has to be inverted for keyboard input (or else the steering dir never reset somehow).
    steerHelpState = 0;

    const float limit = EvalHelpCurveSqrt(
        steerCurveX[SC_Limit], steerCurveY[SC_Limit], speed, steerCurveScale );
    steerAuxTerm = -limit * cmd;

    const float rateSpeed =
        EvalHelpCurveSqrt( steerCurveX[SC_Speed], steerCurveY[SC_Speed], speed,
                           steerCurveScale ) *
        sensTurn;
    const float rateCenter = EvalHelpCurveSqrt( steerCurveX[SC_CenterSpeed],
                                                steerCurveY[SC_CenterSpeed],
                                                speed, steerCurveScale ) *
                             sensTurn;
    const float rateStraight = EvalHelpCurveSqrt( steerCurveX[SC_StraightSpeed],
                                                  steerCurveY[SC_StraightSpeed],
                                                  speed, steerCurveScale ) *
                               sensTurn;
    const float rateCounter = EvalHelpCurveSqrt( steerCurveX[SC_CounterSpeed],
                                                 steerCurveY[SC_CounterSpeed],
                                                 speed, steerCurveScale ) *
                              sensTurn;

    float rate = rateSpeed;

    if ( vForward <= 1.0f ) {
        steerHelpGain = steerNormal[0];
        steerHelpGain2 = steerNormal[1];
    } else {
        const bool bCounter =
            ( ( cmd <= 0.0f ) == ( steerSideslipTerm <= 0.0f ) ) &&
            std::fabs( steerSideslipTerm ) > kSteerCounterMinRate;

        const float lerpDt =
            ( deltaTime > kSteerHelpDtCap || gPhysicsThreadPriority == PTM_Off )
                ? kSteerHelpDtFixed
                : deltaTime;

        if ( std::fabs( cmd ) >= kSteerAuxDeadBand ) {
            if ( bCounter ) {
                steerHelpState = 3;
                const float k = lerpDt * kSteerHelpLerpFast;
                steerHelpGain += ( steerHelpTurn[0] - steerHelpGain ) * k;
                steerHelpGain2 += ( steerHelpTurn[1] - steerHelpGain2 ) * k;
                steerAuxTerm = 0.0f;
                rate = rateCounter;
            } else {
                const float k = lerpDt * kSteerHelpLerpSlow;
                steerHelpGain += ( steerNormal[0] - steerHelpGain ) * k;
                steerHelpGain2 += ( steerNormal[1] - steerHelpGain2 ) * k;
            }
        } else {
            steerHelpState = 1;
            const float k = lerpDt * kSteerHelpLerpFast;
            steerHelpGain += ( steerHelpStraight[0] - steerHelpGain ) * k;
            steerHelpGain2 += ( steerHelpStraight[1] - steerHelpGain2 ) * k;
            rate = rateStraight;
        }
    }
    steerAuxTermPrev = steerAuxTerm; // L306

    float target = steerSideslipTerm - steerAuxTerm;
    if ( steerHelpState == 0 ) {
        if ( std::fabs( steerAuxTerm ) > kSteerAuxEps &&
             ( steerAuxTerm <= 0.0f ) == ( steerCommand <= 0.0f ) ) {
            rate = rateCenter;
        }
        target = -steerAuxTerm;
    } else {
        target = steerHelpGain * target;
    }
    target = JPH::Clamp( target, -1.0f, 1.0f );

    if ( vForward < 0.0f ) {
        target = cmd;
    }

    const float targetPreSlew = target;

    bool bFrontSlipFrozen = false;
    if ( gbSimuModeEnabled && steerHelpState == 0 ) {
        const PhysicsWheelState *pWheels = vehicle.Wheels;
        const float frontSlip =
            ( pWheels[vehicle.NumWheels / 4].NormLatClamped +
              pWheels[0].NormLatClamped ) *
            kSteerFrontSlipAvg;
        if ( frontSlip > 0.0f ) {
            bFrontSlipFrozen =
                frontSlip > kSteerFrontSlipPeak && target > filteredSteer;
        } else {
            bFrontSlipFrozen =
                frontSlip < -kSteerFrontSlipPeak && target < filteredSteer;
        }
    }

    if ( bFrontSlipFrozen ) {
        target = filteredSteer;
    } else {
        const float step = rate * deltaTime;
        if ( target - filteredSteer > step ) {
            target = filteredSteer + step;
        }
        if ( target - filteredSteer < -step ) {
            target = filteredSteer - step;
        }
    }

    filteredSteer = target;
    return filteredSteer;
}

bool PhysicsVehicle::castWheel( JPH::PhysicsSystem &system,
                                const JPH::Body &chassis,
                                int i,
                                float deltaTime ) {
    PhysicsWheelState &w = vehicle.Wheels[i];

    const JPH::RMat44 world = chassis.GetWorldTransform();
    const JPH::Vec3 right = world.GetColumn3( 0 );
    const JPH::Vec3 up = world.GetColumn3( 1 );
    const JPH::Vec3 forward = world.GetColumn3( 2 );

    w.WheelUpAxis = ToVec4( up );

    const Eigen::Vector3f &a = wheelAttach[i];
    const JPH::RVec3 attachW = world * JPH::Vec3( a.x(), a.y(), a.z() );
    const JPH::Vec3 down = -up;
    const float droopStop = suspDroopStop[i];
    const float suspRayLen = droopStop + w.Radius;
    const float rayLen = suspRayLen + kGroundProbeDepth;
    JPH::RRayCast ray{ attachW, rayLen * down };

    const JPH::DefaultBroadPhaseLayerFilter bpFilter =
        system.GetDefaultBroadPhaseLayerFilter( TDUPhysLayers::VEHICLE );
    const JPH::DefaultObjectLayerFilter objFilter =
        system.GetDefaultLayerFilter( TDUPhysLayers::VEHICLE );
    const JPH::IgnoreSingleBodyFilter bodyFilter( chassis.GetID() );

    JPH::RayCastSettings settings;
    settings.mTreatConvexAsSolid = false;
    WheelRayCollector collector( system, ray, JPH::Vec3::sAxisY(),
                                 JPH::Cos( JPH::DegreesToRadians( 80.0f ) ) );
    system.GetNarrowPhaseQueryNoLock().CastRay(
        ray, settings, collector, bpFilter, objFilter, bodyFilter );

    if ( !collector.mHit ) {
        w.bGroundContact = false;
        w.SuspTotalForce = 0.0f;
        w.NormalLoad = 0.0f;
        prevSuspLength[i] = droopStop;
        suspDamperLP[i] *= 0.75f;
        wheelSurface[i] = 0;
        wheelMaterial[i] = TDUMaterialValue::kDefault;
        wheelGroundGap[i] = kAntiFlyMaxGap;
        const JPH::RVec3 droopedW = attachW + suspRayLen * down;
        w.GroundProbePoint = Eigen::Vector4f( droopedW.GetX(), droopedW.GetY(),
                                              droopedW.GetZ(), 1.0f );
        return false;
    }

    const JPH::Vec3 contact( collector.mPosition - JPH::RVec3::sZero() );
    const float hitDist = rayLen * collector.GetEarlyOutFraction();

    w.GroundProbePoint =
        Eigen::Vector4f( contact.GetX(), contact.GetY(), contact.GetZ(), 1.0f );

    const JPH::RVec3 tyreBottomW = attachW + suspRayLen * down;
    wheelGroundGap[i] =
        static_cast<float>( tyreBottomW.GetY() - collector.mPosition.GetY() );

    if ( hitDist > suspRayLen ) {
        w.bGroundContact = false;
        w.SuspTotalForce = 0.0f;
        w.NormalLoad = 0.0f;
        prevSuspLength[i] = droopStop;
        suspDamperLP[i] *= 0.75f;
        wheelSurface[i] = 0;
        wheelMaterial[i] = TDUMaterialValue::kDefault;
        return false;
    }

    const float suspLength =
        JPH::Clamp( hitDist - w.Radius, suspMinLength[i], droopStop );

    // Chassis velocity at the contact point (reused for the tire below).
    const JPH::RVec3 comPos = chassis.GetCenterOfMassPosition();
    const JPH::Vec3 linVel = chassis.GetLinearVelocity();
    const JPH::Vec3 angVel = chassis.GetAngularVelocity();
    const JPH::Vec3 rContact( collector.mPosition - comPos );
    const JPH::Vec3 contactVel = linVel + angVel.Cross( rContact );

    JPH::Vec3 normal = collector.mNormal.NormalizedOr( up );

    float spring = suspCornerLoad[i] +
                   suspStiffness[i] * ( suspStaticLength[i] - suspLength );
    if ( spring < 0.0f ) {
        spring = 0.0f;
    }

    float damper;
    if ( deltaTime > 0.0f ) {
        float v = ( prevSuspLength[i] - suspLength ) / deltaTime;
        v = JPH::Clamp( v, -kSuspDamperTravelClamp, kSuspDamperTravelClamp );
        suspDamperLP[i] = 0.75f * suspDamperLP[i] + 0.25f * v;
        const float vLP = suspDamperLP[i];
        const float coeff =
            ( vLP >= 0.0f ) ? suspDamping[i] : suspDamperRebound[i];
        damper = coeff * vLP * kSuspDampingScale;
        if ( damper < 0.0f && spring < -damper ) {
            damper = -spring; // floor total suspension force at 0
        }
    } else {
        const float suspVel =
            JPH::Clamp( -contactVel.Dot( normal ), -kSuspDamperVelClamp,
                        kSuspDamperVelClamp );
        damper = suspDamping[i] * suspVel * kSuspDampingScale;
    }

    prevSuspLength[i] = suspLength;
    w.SuspTotalForce = spring + damper;
    w.AntiRollForce = 0.0f;
    wheelSurface[i] = static_cast<uint32_t>( collector.mSurface );
    wheelMaterial[i] = collector.mMaterial;

    // Contact frame.
    float steerAngle = ( w.bSteered ? -steerCommand * steerLockRad : 0.0f );
    if ( w.SideFlag != 1 ) {
        // TODO:
    }

    const JPH::Vec3 steeredFwd =
        forward * JPH::Cos( steerAngle ) + right * JPH::Sin( steerAngle );
    JPH::Vec3 longAxis = ( steeredFwd - normal * steeredFwd.Dot( normal ) );
    longAxis = longAxis.NormalizedOr( steeredFwd );
    const JPH::Vec3 latAxis = normal.Cross( longAxis ).NormalizedOr( right );

    const float vLong = contactVel.Dot( longAxis );

    const float longSlip = ( w.AngularVel * w.Radius - vLong ) /
                           std::max( std::fabs( vLong ), kSlipDenom );

    w.ContactNormal = ToVec4( normal );
    w.ContactLongAxis = ToVec4( longAxis );
    w.ContactLatAxis = ToVec4( latAxis );
    w.ForwardDir = w.ContactNormal * longSlip;
    w.WheelLinVel = ToVec4( contactVel );
    w.ContactVelLong = vLong;
    w.ContactPoint =
        Eigen::Vector4f( contact.GetX(), contact.GetY(), contact.GetZ(), 1.0f );
    w.AttachPoint =
        Eigen::Vector4f( attachW.GetX(), attachW.GetY(), attachW.GetZ(), 1.0f );
    w.bGroundContact = true;
    return true;
}

void PhysicsVehicle::applyGroundAlignment( JPH::PhysicsSystem &system,
                                           const JPH::Body &chassis,
                                           JPH::BodyInterface &bi,
                                           float deltaTime ) {
    // FUN_00837cd0
    if ( deltaTime <= 0.0f ) {
        return;
    }

    groundAlignQueryTimer -= 1;
    if ( groundAlignQueryTimer < 0 ) {
        groundAlignQueryTimer += kGroundAlignQueryPeriod;

        bool bWheelOnProp = false;
        for ( int i = 0; i < vehicle.NumWheels; ++i ) {
            if ( wheelSurface[i] == TDUSurface::Generic ) {
                bWheelOnProp = true;
                break;
            }
        }

        if ( !bWheelOnProp ) {
            const JPH::RVec3 origin = chassis.GetPosition();
            const JPH::RRayCast ray{
                JPH::RVec3( origin.GetX(), origin.GetY() + kGroundAlignProbeUp,
                            origin.GetZ() ),
                JPH::Vec3( 0.0f,
                           -( kGroundAlignProbeDepth + kGroundAlignProbeUp ),
                           0.0f ) };

            const JPH::DefaultBroadPhaseLayerFilter bpFilter =
                system.GetDefaultBroadPhaseLayerFilter( TDUPhysLayers::WHEEL );
            const JPH::DefaultObjectLayerFilter objFilter =
                system.GetDefaultLayerFilter( TDUPhysLayers::WHEEL );
            const JPH::IgnoreSingleBodyFilter bodyFilter( chassis.GetID() );

            JPH::RayCastSettings settings;
            settings.mTreatConvexAsSolid = false;
            WheelRayCollector collector( system, ray, JPH::Vec3::sAxisY(),
                                         kGroundAlignMinNormalY );
            system.GetNarrowPhaseQueryNoLock().CastRay(
                ray, settings, collector, bpFilter, objFilter, bodyFilter );

            if ( collector.mHit ) {
                groundAlignNormal = Eigen::Vector4f(
                    collector.mNormal.GetX(), collector.mNormal.GetY(),
                    collector.mNormal.GetZ(), 0.0f );
            }
        }
    }

    if ( groundAlignNormal.y() <= kGroundAlignMinNormalY ) {
        groundAlignNormal = Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f );
    }

    const JPH::RMat44 world = chassis.GetWorldTransform();
    const JPH::Vec3 planeNormal( groundAlignNormal.x(), groundAlignNormal.y(),
                                 groundAlignNormal.z() );
    groundAlignPitch = ApplyDeadband( -world.GetColumn3( 2 ).Dot( planeNormal ),
                                      kGroundAlignDeadband );
    groundAlignRoll = ApplyDeadband( world.GetColumn3( 0 ).Dot( planeNormal ),
                                     kGroundAlignDeadband );

    groundAlignRollRate = ( groundAlignRoll - groundAlignRollPrev ) / deltaTime;
    groundAlignPitchRate =
        ( groundAlignPitch - groundAlignPitchPrev ) / deltaTime;
    groundAlignRollPrev = groundAlignRoll;
    groundAlignPitchPrev = groundAlignPitch;

    if ( groundAlignPitch == 0.0f && groundAlignRoll == 0.0f &&
         groundAlignPitchRate == 0.0f && groundAlignRollRate == 0.0f ) {
        return;
    }

    const float speedRamp =
        JPH::Clamp( chassis.GetLinearVelocity().Length() / kGroundAlignSpeedRef,
                    0.0f, 1.0f );
    const float gainP = kGroundAlignGainP * speedRamp;
    const float gainD = kGroundAlignGainD * speedRamp;

    const JPH::Vec3 torqueLocal(
        ( groundAlignPitchRate * gainD + gainP * groundAlignPitch ) *
            vehicle.Mass * kGroundAlignPitchScale,
        0.0f,
        ( groundAlignRollRate * gainD + groundAlignRoll * gainP ) *
            vehicle.Mass );

    if ( !chassis.IsActive() ) {
        bi.ActivateBody( chassis.GetID() );
    }
    bi.AddTorque( chassis.GetID(), world.Multiply3x3( torqueLocal ),
                  JPH::EActivation::DontActivate );
}

void PhysicsVehicle::applyAntiRollBars() {
    // FUN_0043aba0
    for ( int axle = 0; axle < 2; ++axle ) {
        const int a = axle * 2;     // left
        const int b = axle * 2 + 1; // right
        if ( b >= vehicle.NumWheels ) {
            break;
        }
        PhysicsWheelState &wa = vehicle.Wheels[a];
        PhysicsWheelState &wb = vehicle.Wheels[b];
        if ( !gVehicleAntiRoll ) {
            wa.AntiRollForce = 0.0f;
            wb.AntiRollForce = 0.0f;
            continue;
        }
        // Suspension compression (larger = more compressed); 0 when airborne.
        const float compA =
            wa.bGroundContact ? ( suspMaxLength[a] - prevSuspLength[a] ) : 0.0f;
        const float compB =
            wb.bGroundContact ? ( suspMaxLength[b] - prevSuspLength[b] ) : 0.0f;
        const float arb = arbStiffness[axle] * ( compB - compA );
        wa.AntiRollForce = arb;
        wb.AntiRollForce = -arb;
    }
}

void PhysicsVehicle::updateGripModulation( float chassisSpeed ) {
    // FUN_0041e6d0
    constexpr float kOne = 1.0f;                    // FLOAT_00f4165c
    constexpr float kBrakeGate = 0.1f;              // FLOAT_00f41668
    constexpr float kHalf = 0.5f;                   // FLOAT_00f41674
    constexpr float kAssistBias = 0.2f;             // DAT_00f41730
    constexpr float kPercent = 100.0f;              // DAT_00f41708
    constexpr float kAssistCeil = 1.25f;            // DAT_00f41870
    constexpr float kMsToKmh = 3.6f;                // DAT_00f41768
    constexpr float kStandstillSpeedHi = 1.0f;      // DAT_00fab5c0
    constexpr float kStandstillSpeedLo = 0.25f;     // DAT_00fab5c4
    constexpr float kStandstillGripFloor = 0.1f;    // DAT_00fab5c8
    constexpr float kLowSpeedLatMax = 6.94444465f;  // DAT_00fab5cc (25 km/h)
    constexpr float kLowSpeedExit = 13.8888893f;    // DAT_00fab5d0 (50 km/h)
    constexpr float kLowSpeedEnter = 1.38888896f;   // DAT_00fab5d4 (5 km/h)
    constexpr float kLowSpeedLatFloor = 0.3f;       // DAT_00fab5d8
    constexpr float kLowSpeedLongFloor = 0.65f;     // DAT_00fab5dc
    constexpr float kSlipRefLo = 50.0f;             // FLOAT_00f8a2d0
    constexpr float kSlipRefHi = 100.0f;            // DAT_00f8a2d4
    constexpr float kPwrBoostLo = 1.175f;           // DAT_00f8a2d8
    constexpr float kPwrBoostHi = 1.225f;           // DAT_00f8a2dc
    constexpr float kPwrRefLo = 130.0f;             // DAT_00f41bd8
    constexpr float kPwrRefScale = 1.0f / 120.0f;   // DAT_00f418b0
    constexpr float kLowSpeedLatRate = 3.33333325f; // DAT_00f41bdc

    const auto clampSpan = []( float v, float a, float b ) {
        return Clamp( v, std::min( a, b ), std::max( a, b ) );
    };

    float longMul = kOne;    // local_30, whole vehicle
    float rearLatMul = kOne; // local_2c, whole vehicle (rear axle only)
    float latMul = kOne;     // local_34, accumulates across wheels

    {
        if ( !bLowSpeedGripState ) {
            if ( chassisSpeed < kLowSpeedEnter ) {
                bLowSpeedGripState = true;
            }
        } else {
            if ( bLowSpeedLongAssist ) {
                longMul =
                    clampSpan( ( ( chassisSpeed - kLowSpeedEnter ) *
                                 ( kOne - kLowSpeedLongFloor ) ) /
                                       ( kLowSpeedExit - kLowSpeedEnter ) +
                                   kLowSpeedLongFloor,
                               kOne, kLowSpeedLongFloor );
            }
            if ( bLowSpeedLatAssist && chassisSpeed < kLowSpeedLatMax ) {
                const float latTarget = clampSpan(
                    ( gVehicleLowSpeedLatGain - kLowSpeedLatFloor ) *
                            ( std::fabs( steerCommand ) - kAssistBias ) *
                            kLowSpeedLatRate +
                        kLowSpeedLatFloor,
                    gVehicleLowSpeedLatGain, kLowSpeedLatFloor );

                float slipSum = 0.0f;
                for ( int i = 0; i < vehicle.NumWheels; ++i ) {
                    if ( vehicle.Wheels[i].AxleIndex == 1 ) {
                        slipSum +=
                            std::fabs( vehicle.Wheels[i].NormLongClamped );
                    }
                }
                rearLatMul = clampSpan( ( latTarget - kAssistCeil ) * slipSum *
                                                kHalf * kAssistBias +
                                            kAssistCeil,
                                        latTarget, kAssistCeil );
            }
            if ( chassisSpeed > kLowSpeedExit ) {
                bLowSpeedGripState = false;
            }
        }
    }

    const PhysicsEngineSim &eng = vehicle.Engine;
    const float mass = vehicle.Mass > 0.0f ? vehicle.Mass : 1.0f;
    const float powerMax =
        EngineTorqueAt( eng, eng.PeakPowerOmega, 1.0f ) * eng.PeakPowerOmega;
    const float powerToWeight = powerMax / mass;
    const float boostCeil =
        clampSpan( ( kPwrBoostHi - kPwrBoostLo ) *
                           ( powerToWeight - kPwrRefLo ) * kPwrRefScale +
                       kPwrBoostLo,
                   kPwrBoostLo, kPwrBoostHi );

    const int32_t helpLevel = appliedDrivingHelpLevel;
    const float brakeCmd = inputBrake;

    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        PhysicsWheelState &w = vehicle.Wheels[i];

        float standstill = kOne;
        if ( brakeCmd > kBrakeGate ) {
            standstill = clampSpan(
                ( ( kOne - kStandstillGripFloor ) *
                  ( std::fabs( w.ContactVelLong ) - kStandstillSpeedLo ) ) /
                        ( kStandstillSpeedHi - kStandstillSpeedLo ) +
                    kStandstillGripFloor,
                kStandstillGripFloor, kOne );
        }
        w.GripLong = standstill * longMul * w.BaseGripLong;

        if ( !bLowSpeedGripState && helpLevel >= 0 && helpLevel < 3 ) {
            const float ceil2 =
                ( helpLevel == 2 ) ? ( boostCeil + kOne ) * kHalf : boostCeil;
            float ratio = 0.0f;
            const float vLongKmh = std::fabs( w.ContactVelLong * kMsToKmh );
            if ( vLongKmh > kOne ) {
                ratio = std::fabs( w.ContactVelLat * kMsToKmh ) / vLongKmh;
            }
            latMul *= clampSpan(
                ( ( ratio * kPercent - kSlipRefLo ) * ( ceil2 - kOne ) ) /
                        ( kSlipRefHi - kSlipRefLo ) +
                    kOne,
                kOne, ceil2 );
        }

        if ( w.AxleIndex == 1 ) {
            w.GripLat = w.BaseGripLat * latMul * rearLatMul;
            if ( bIsBike ) {
                w.GripLong = ( rearLatMul + kAssistBias ) * standstill *
                             longMul * w.BaseGripLong;
            }
        } else {
            w.GripLat = w.BaseGripLat * latMul;
        }
    }
}

void PhysicsVehicle::OnStep(
    const JPH::PhysicsStepListenerContext &inContext ) {
    if ( pChassisBody == nullptr || pPhysicsSystem == nullptr ) {
        return;
    }
    JPH::PhysicsSystem &system = *inContext.mPhysicsSystem;
    const float dt = inContext.mDeltaTime;
    if ( dt <= 0.0f ) {
        return;
    }

    const JPH::Body &chassis = *pChassisBody;
    if ( bPausePhysics ) {
        return;
    }

    {
        const JPH::Vec3 v = chassis.GetLinearVelocity();
        arbitrateAutomaticGearbox(
            dt, JPH::Vec3( v.GetX(), 0.0f, v.GetZ() ).Length() );
    }
    applyDrivingMode();

    const JPH::RMat44 world = chassis.GetWorldTransform();

    Matrix4x4f_t vehicleMatrix = Matrix4x4f_t::Identity();
    for ( int c = 0; c < 3; ++c ) {
        const JPH::Vec3 col = world.GetColumn3( c );
        vehicleMatrix( c, 0 ) = col.GetX();
        vehicleMatrix( c, 1 ) = col.GetY();
        vehicleMatrix( c, 2 ) = col.GetZ();
    }

    JPH::BodyInterface &bi = system.GetBodyInterfaceNoLock();
    const JPH::BodyID id = chassis.GetID();
    float diagAppliedFwd = 0.0f;

    const JPH::Vec3 chassisVel = chassis.GetLinearVelocity();
    const bool bBodyActiveAtStepStart = chassis.IsActive();
    const auto wakeChassis = [&bi, &chassis, id]() {
        if ( !chassis.IsActive() ) {
            bi.ActivateBody( id );
        }
    };

    const float vForward = chassisVel.Dot( world.GetColumn3( 2 ) );
    steerCommand = filterSteering( inputSteer, chassis, dt );
    if ( !gbSimuModeEnabled ) {
        updateGripModulation( chassisVel.Length() );
    }

    vehicle.ForwardSpeed = vForward;
    vehicle.LateralSpeed = chassisVel.Dot( world.GetColumn3( 0 ) );
    vehicle.AnyWheelGrounded = false;
    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        if ( vehicle.Wheels[i].bGroundContact ) {
            vehicle.AnyWheelGrounded = true;
            break;
        }
    }
    StepPowertrain( vehicle, inputThrottle, inputBrake, dt );

    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        castWheel( system, chassis, i, dt );
    }
    int numWheelsOnRoad = 0;
    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        if ( TDUSurface::isOnRoad( wheelSurface[i] ) ) {
            ++numWheelsOnRoad;
        }
    }

    applyAntiRollBars();

    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        PhysicsWheelState &w = vehicle.Wheels[i];
        const bool hit = w.bGroundContact;

        if ( hit ) {
            ComputeSuspensionForce( w );
        } else {
            w.NormalLoad = 0.0f;
            w.TireForceLong = 0.0f;
            w.TireForceLat = 0.0f;
            w.DragLong = 0.0f;
            w.DragLat = 0.0f;
            w.SuspForce.setZero();
        }
        const float antiFlyCoef =
            gbSimuModeEnabled ? 0.0f : kAntiFlyGravityCoef;
        const float antiFlyMaxGap = gbSimuModeEnabled ? 0.0f : kAntiFlyMaxGap;
        const float antiFlyAccel = antiFlyCoef * -kGravityAccel;
        const float ramp =
            ( ( wheelGroundGap[i] - kAntiFlyGapDeadband ) * antiFlyAccel ) /
            ( antiFlyMaxGap - kAntiFlyGapDeadband );
        const float accel = JPH::Clamp( ramp, std::min( 0.0f, antiFlyAccel ),
                                        std::max( 0.0f, antiFlyAccel ) );
        const float force = accel * vehicle.Mass;
        if ( std::fabs( force ) > kAntiFlyMinForce ) {
            const Eigen::Vector3f &at = wheelAttach[i];
            const JPH::Vec3 arm = world.Multiply3x3(
                JPH::Vec3( at.x(), at.y(), at.z() ) * kAntiFlyLeverScale );
            wakeChassis();
            bi.AddForce( id, JPH::Vec3( 0.0f, force, 0.0f ),
                         chassis.GetCenterOfMassPosition() + arm,
                         JPH::EActivation::DontActivate );
        }

        const float hSub = dt / static_cast<float>( kWheelSubsteps );
        const float radius = std::max( w.Radius, 0.05f );
        const float driveBase = w.DriveTorque;

        if ( !hit ) {
            w.BrakeCmdTorque = 0.0f;
            w.HandbrakeCmdTorque = 0.0f;
            w.BrakeTorqueOut = 0.0f;
            w.EspTorqueOut = 0.0f;
            w.EspState = 0;
            w.NormLongClamped = 0.0f;
            w.NormLatClamped = 0.0f;
            IntegrateWheel( w, dt ); // spin freely on drive torque
            continue;
        }

        const float absMag =
            ( i < 2 ) ? vehicle.AbsMagFront : vehicle.AbsMagRear;
        const float espMag =
            ( i < 2 ) ? vehicle.EspMagFront : vehicle.EspMagRear;

        const bool bHandbraked =
            !w.bSteered && ( inputHandbrake > kHandbrakeInputThreshold );

        const float vLongStep = w.ContactVelLong;
        for ( int s = 0; s < kWheelSubsteps; ++s ) {
            const float brakeSign = ( w.AngularVel > 0.0f ) ? 1.0f : -1.0f;
            w.BrakeCmdTorque = -brakeSign * inputBrake * w.BrakeTorque;
            w.HandbrakeCmdTorque = bHandbraked ? -brakeSign * inputHandbrake *
                                                     w.BrakeTorque *
                                                     kHandbrakeTorqueScale
                                               : 0.0f;

            const float longSlip =
                ( w.AngularVel * radius - vLongStep ) /
                std::max( std::fabs( vLongStep ), kSlipDenom );
            w.ForwardDir = w.ContactNormal * longSlip;
            ComputeTireForces( w, vehicleMatrix );

            const float reactTorque = ComputeWheelReactionTorque( w, hSub );

            ApplyBrakeAndABS( w, vehicle.bAbsEnabled && !bHandbraked, absMag,
                              reactTorque, driveBase, hSub );

            w.DriveTorque = ApplyEngineDragControl(
                w, vehicle.bEdcEnabled, reactTorque, driveBase, hSub );

            ApplyESP( w, gVehicleESPEnabled && vehicle.bEspEnabled, espMag,
                      reactTorque, w.DriveTorque, hSub );

            IntegrateWheel( w, hSub );
        }

        const JPH::RVec3 point( w.ContactPoint.x(), w.ContactPoint.y(),
                                w.ContactPoint.z() );
        const JPH::Vec3 suspForce( w.SuspForce.x(), w.SuspForce.y(),
                                   w.SuspForce.z() );
        const JPH::Vec3 longAxis( w.ContactLongAxis.x(), w.ContactLongAxis.y(),
                                  w.ContactLongAxis.z() );
        const JPH::Vec3 latAxis( w.ContactLatAxis.x(), w.ContactLatAxis.y(),
                                 w.ContactLatAxis.z() );
        const JPH::Vec3 tireForce =
            longAxis * ( w.TireForceLong + w.DragLong ) +
            latAxis * ( w.TireForceLat + w.DragLat );

        // Jolt equivalent of "if (islandActiveSnapshot && bVar3)"
        if ( bBodyActiveAtStepStart ) {
            wakeChassis();
            bi.AddForce( id, suspForce, point, JPH::EActivation::DontActivate );
        }

        if ( w.bTireSlipping ) {
            wakeChassis();
            bi.AddForce( id, tireForce, point, JPH::EActivation::DontActivate );
        }
        if ( numWheelsOnRoad == 0 && !gbGrassAsTarmac && !bIsBike ) {
            const float rumbleTimer = wheelRumbleTimer[i] - dt;
            wheelRumbleTimer[i] = rumbleTimer;

            const float contactSpeed = std::fabs( w.ContactVelLong );
            if ( rumbleTimer < 0.0f ) {
                const float period =
                    kRumblePeriodBase /
                    std::max( kRumblePeriodBase, contactSpeed );
                wheelRumbleAmount[i] =
                    JPH::Clamp( ( contactSpeed - kRumbleSpeedMin ) *
                                    kRumbleAmountMax / kRumbleSpeedSpan,
                                0.0f, kRumbleAmountMax ) *
                    kRumblePeriodScale;
                wheelRumbleTimer[i] = RandGaussian( gPhysicsRandomSeed, period,
                                                    period * kRumbleSpread );

                const float ramp = kRumbleAccelMin +
                                   ( ( contactSpeed - kRumbleSpeedMin ) *
                                     ( kRumbleAccelMax - kRumbleAccelMin ) ) /
                                       kRumbleSpeedSpan;
                const float kickAccel = JPH::Clamp(
                    ramp, std::min( kRumbleAccelMin, kRumbleAccelMax ),
                    std::max( kRumbleAccelMin, kRumbleAccelMax ) );
                const JPH::Vec3 kickAxis( w.ContactNormal.x(),
                                          w.ContactNormal.y(),
                                          w.ContactNormal.z() );
                wakeChassis();
                bi.AddForce( id, kickAxis * ( vehicle.Mass * kickAccel ), point,
                             JPH::EActivation::DontActivate );
            }
        }
    }

    {
        const int gg =
            ( vehicle.GearBox.CurrentGear >= 0 &&
              vehicle.GearBox.CurrentGear < vehicle.GearBox.NumGears )
                ? vehicle.GearBox.CurrentGear
                : 1;
        const float kfb =
            vehicle.GearBox.Ratios[gg] * vehicle.GearBox.FinalDrive;
        if ( gg == 1 || std::fabs( kfb ) < 0.01f ) {
            vehicle.ReflectedWheelSpeed = 0.0f;
            vehicle.ReactionTorque = 0.0f;
        } else {
            float sumOmega = 0.0f, maxReact = -1.0e30f, minReact = 1.0e30f;
            int nDriven = 0;
            for ( int i = 0; i < vehicle.NumWheels; ++i ) {
                if ( !vehicle.Wheels[i].bDriven ) {
                    continue;
                }
                sumOmega += vehicle.Wheels[i].AngularVel;
                maxReact = std::max( maxReact, vehicle.Wheels[i].NetTorque );
                minReact = std::min( minReact, vehicle.Wheels[i].NetTorque );
                ++nDriven;
            }
            if ( nDriven > 0 ) {
                const float diffOmega =
                    sumOmega / static_cast<float>( nDriven );
                const float diffReaction =
                    2.0f * ( ( vehicle.DiffInputTorque > 0.0f ) ? maxReact
                                                                : minReact );
                vehicle.ReflectedWheelSpeed = kfb * diffOmega;
                vehicle.ReactionTorque = diffReaction / kfb;
            }
        }
    }

    if ( std::fabs( vForward ) > 1.0f ) {
        const JPH::Vec3 upAxis = world.GetColumn3( 1 );
        const JPH::Vec3 fwd = world.GetColumn3( 2 );
        const float dyn =
            aeroDensity * vForward * std::fabs( vForward ); // rho * sign * v^2
        const JPH::Vec3 aeroForce =
            upAxis * ( -aeroDownArea * dyn ) +
            fwd * ( -aeroDragArea * gVehicleAeroDragScale * dyn );
        wakeChassis();
        bi.AddForce( id, aeroForce, JPH::EActivation::DontActivate );
    }

    if ( !bIsBike && !gbSimuModeEnabled ) {
        applyGroundAlignment( system, chassis, bi, dt );
    }

    updateChrono( dt );
    updateRevRate( dt );
    reportStuckClutch( dt );
}

void PhysicsVehicle::updateRevRate( float dt ) {
    if ( dt <= 0.0f ) {
        return;
    }
    const PhysicsEngineSim &e = vehicle.Engine;
    const float rpm = e.Omega * kOmegaToRpm;
    const float rate = e.AngAccel * kOmegaToRpm; // rpm/s
    revRateSmoothed += ( rate - revRateSmoothed ) * kRevRateSmoothing;

    const int gear = vehicle.GearBox.CurrentGear;
    if ( gear == revGearTracked ) {
        revGearTime += dt;
        revGearPeakRate = Max( revGearPeakRate, rate );
        return;
    }

    revGearTracked = gear;
    revGearTime = 0.0f;
    revGearEntryRpm = rpm;
    revGearPeakRate = 0.0f;
}

void PhysicsVehicle::reportStuckClutch( float dt ) {
    const PhysicsEngineSim &e = vehicle.Engine;
    const PhysicsClutchSim &c = vehicle.Clutch;
    const float rpm = e.Omega * kOmegaToRpm;

    float horizontalSpeed = 0.0f;
    if ( pChassisBody != nullptr ) {
        const JPH::Vec3 vel = pChassisBody->GetLinearVelocity();
        horizontalSpeed = JPH::Vec3( vel.GetX(), 0.0f, vel.GetZ() ).Length();
    }

    const bool bLaunching =
        ( vehicle.GearBox.CurrentGear == 2 ) && inputThrottle > 0.5f;
    if ( !bLaunching ) {
        stuckClutchTimer = 0.0f;
        bStuckClutchReported = false;
        launchEntryRpm = rpm;
        launchEntrySpeed = horizontalSpeed;
        return;
    }

    if ( stuckClutchTimer <= 0.0f ) {
        launchEntryRpm = rpm;
        launchEntrySpeed = horizontalSpeed;
    }
    stuckClutchTimer += dt;
    if ( stuckClutchTimer < kStuckClutchReportTime || bStuckClutchReported ) {
        return;
    }
    bStuckClutchReported = true;

    const float inv = 1.0f / stuckClutchTimer;
    const float peakPowerRpm = e.PeakPowerOmega * kOmegaToRpm;
    float peakTq = 0.0f;
    for ( int i = 0; i < e.CurveCount; ++i ) {
        peakTq = Max( peakTq, e.CurveMax[i].Torque );
    }
    const float freeRevRate =
        ( e.Inertia > 1.0e-4f ) ? ( peakTq / e.Inertia * kOmegaToRpm ) : 0.0f;
    static const char *kClutchStateNames[] = { "open", "armed", "modulating",
                                               "LOCKED" };
    const char *pClutch =
        ( c.State >= 0 && c.State < 4 ) ? kClutchStateNames[c.State] : "?";

    const char *pDrive = vehicle.bCenterDrive  ? "AWD"
                         : vehicle.bFrontDrive ? "FWD"
                                               : "RWD";
    int numDriven = 0;
    float reflectedI = 0.0f;
    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        if ( vehicle.Wheels[i].bDriven ) {
            ++numDriven;
            reflectedI = vehicle.Wheels[i].ReflectedInertia;
        }
    }
    const int gg = vehicle.GearBox.CurrentGear;
    const float k = vehicle.GearBox.Ratios[gg] * vehicle.GearBox.FinalDrive;
}

void PhysicsVehicle::armChrono() {
    chrono = Chrono();
    chrono.State = 2; // "First you must stop the vehicle"
}

void PhysicsVehicle::updateChrono( float dt ) {
    // FUN_0083db70
    if ( gVehicleStartChronopack ) {
        gVehicleStartChronopack = false;
        armChrono();
    }
    if ( pChassisBody == nullptr ) {
        return;
    }
    Chrono &c = chrono;
    const JPH::Vec3 vel = pChassisBody->GetLinearVelocity();
    const float speed =
        JPH::Vec3( vel.GetX(), 0.0f, vel.GetZ() ).Length(); // m/s
    c.Odometer += speed * dt;

    if ( c.State == 2 && speed < kChronoMoveSpeed ) {
        c.State = 1; // stopped: start
    }
    if ( c.State == 1 && speed > kChronoMoveSpeed ) {
        c.State = 3; // moved off: run
        c.Timer = 0.0f;
        c.StartDistance = c.Odometer;
    }
    if ( c.State != 3 ) {
        return;
    }

    c.Timer += dt;
    const float dist = c.Odometer - c.StartDistance;

    // Is the engine driving the road, or just spinning the tyres?
    float slipSum = 0.0f;
    int numDriven = 0;
    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        const PhysicsWheelState &w = vehicle.Wheels[i];
        if ( !w.bDriven ) {
            continue;
        }
        const float surface = std::fabs( w.AngularVel * w.Radius );
        c.MaxDrivenSlip = Max( c.MaxDrivenSlip, surface - speed );
        slipSum += Max( surface - speed, 0.0f );
        ++numDriven;
    }
    if ( numDriven > 0 ) {
        c.SlipIntegral += ( slipSum / numDriven ) * dt;
    }
    c.MaxOmega = Max( c.MaxOmega, vehicle.Engine.Omega );
    if ( vehicle.Engine.Omega >= 0.99f * vehicle.Engine.OmegaMax ) {
        c.LimiterTime += dt;
    }

    auto latch = [&]( bool reached, float &slot, const char *pLabel ) {
        if ( reached && slot <= 0.0f ) {
            slot = c.Timer;
            const float inv = ( slot > 0.0f ) ? ( 1.0f / slot ) : 0.0f;
            OTDU_LOG_INFO(
                "Chronopack: %s = %.2f s (gear %d, %.0f rpm, peak slip %.1f "
                "m/s, "
                "peak %.0f rpm, mean slip %.1f m/s, limiter %.0f%%)\n",
                pLabel, slot, vehicle.GearBox.CurrentGear - 1,
                vehicle.Engine.Omega * 9.5493f, c.MaxDrivenSlip,
                c.MaxOmega * 9.5493f, c.SlipIntegral * inv,
                c.LimiterTime * inv * 100.0f );
        }
    };
    latch( speed > kChrono60mph, c.T60mph, "0 to 60 mph" );
    latch( speed > kChrono100kmh, c.T100kmh, "0 to 100 km/h" );
    latch( speed > kChrono200kmh, c.T200kmh, "0 to 200 km/h" );
    latch( dist > kChrono400m, c.T400m, "0 to 400 m" );
    latch( dist > kChrono1000m, c.T1000m, "0 to 1000 m" );
    latch( dist * kMetresToMiles > 0.25f, c.TQuarterMile, "0 to 1/4 mile" );
}

void PhysicsVehicle::setDrivingHelpLevel( int32_t level ) {
    // FUN_008287d0
    const int32_t maxElectronicParams =
        gPhysicsConfig.NumElectronicParamsValues;
    if ( level >= 0 && level < maxElectronicParams ) {
        vehicle.bElectronicsEnabled = true;
        vehicle.ElectronicIndex = level;
    } else {
        vehicle.bElectronicsEnabled = false;
        vehicle.ElectronicIndex = -1;
    }
    appliedDrivingHelpLevel = level;
}

void PhysicsVehicle::tick( float deltaTime ) {
    if ( pChassisBody == nullptr ) {
        return;
    }

    updateKeyframedVelocity( deltaTime );

    const int32_t requestedLevel =
        ( gVehicleDrivingHelpOverride >= 0 )
            ? gVehicleDrivingHelpOverride
            : ( gpPlayerData != nullptr ? gpPlayerData->getDrivingHelpLevel()
                                        : 0 );
    if ( requestedLevel != appliedDrivingHelpLevel ) {
        setDrivingHelpLevel( requestedLevel );
    }

    if ( pPhysicsSystem != nullptr ) {
        const JPH::Vec3 v =
            pPhysicsSystem->GetBodyInterface().GetLinearVelocity(
                pChassisBody->GetID() );
        vehicle.bEdcEnabled = true;

        const bool bReverse = ( vehicle.GearBox.CurrentGear == 0 );
        UpdateBrakeGripLat( vehicle, v.Length() );
        ComputeAbsMagnitudes( vehicle, v.Length(), bReverse );
        ComputeEspMagnitudes( vehicle, v.Length(), deltaTime, bReverse );
        ApplyHandbrakeAbsOverride( vehicle, inputHandbrake );
    }

    // G-Meter telemetry
    if ( pPhysicsSystem != nullptr && deltaTime > 1.0e-5f ) {
        JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
        const JPH::Vec3 vel = bi.GetLinearVelocity( pChassisBody->GetID() );

        JPH::Vec3 sample = vel;
        if ( sample.GetY() > kGMeterVerticalVelMax ) {
            sample.SetY( kGMeterVerticalVelReset );
        }

        const JPH::Vec3 prev( prevWorldVelocity[0], prevWorldVelocity[1],
                              prevWorldVelocity[2] );
        const JPH::Vec3 worldAccel = ( sample - prev ) / deltaTime;
        const JPH::Quat rot = bi.GetRotation( pChassisBody->GetID() );
        const JPH::Vec3 localAccel = rot.Conjugated() * worldAccel;
        localAccelLong = localAccel.GetZ();
        localAccelLat = localAccel.GetX();
        localAccelVert = localAccel.GetY();
        prevWorldVelocity[0] = vel.GetX();
        prevWorldVelocity[1] = vel.GetY();
        prevWorldVelocity[2] = vel.GetZ();
    }
}

Matrix4x4f_t PhysicsVehicle::getChassisShiftedMatrix() const {
    Matrix4x4f_t m = Matrix4x4f_t::Identity();
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return m;
    }
    const JPH::RMat44 world =
        pPhysicsSystem->GetBodyInterface().GetWorldTransform(
            pChassisBody->GetID() );
    for ( int c = 0; c < 4; ++c ) {
        const JPH::Vec3 col = world.GetColumn3( c );
        m( c, 0 ) = col.GetX();
        m( c, 1 ) = col.GetY();
        m( c, 2 ) = col.GetZ();
    }
    return m;
}

Matrix4x4f_t PhysicsVehicle::getChassisWorldMatrix() const {
    Matrix4x4f_t m = getChassisShiftedMatrix();
    const Eigen::Vector4f origin = GSPhysics::GetShiftOrigin();
    m( 3, 0 ) += origin.x();
    m( 3, 1 ) += origin.y();
    m( 3, 2 ) += origin.z();
    return m;
}

void PhysicsVehicle::onWorldShifted( const Eigen::Vector3f &worldShift ) {
    const Eigen::Vector4f shift( worldShift.x(), worldShift.y(), worldShift.z(),
                                 0.0f );
    for ( int32_t i = 0; i < vehicle.NumWheels; i++ ) {
        PhysicsWheelState &w = vehicle.Wheels[i];
        w.AttachPoint += shift;
        w.ContactPoint += shift;
        w.GroundProbePoint += shift;
    }

    auto translate = []( Matrix4x4f_t &m, const Eigen::Vector3f &t ) {
        m( 3, 0 ) += t.x();
        m( 3, 1 ) += t.y();
        m( 3, 2 ) += t.z();
    };

    translate( physicsMatrix, worldShift );
    translate( displayMatrix, worldShift );
    translate( keyframeMatrix, worldShift );
    invChassisMatrix = OrthoInvert( physicsMatrix );
}

static inline void RotateAboutOffset( Matrix4x4f_t &accum,
                                      const Matrix4x4f_t &rotation,
                                      const Eigen::Vector3f &preTranslate ) {
    accum( 3, 0 ) += preTranslate.x();
    accum( 3, 1 ) += preTranslate.y();
    accum( 3, 2 ) += preTranslate.z();
    accum = accum * rotation;
    accum( 3, 0 ) -= preTranslate.x();
    accum( 3, 1 ) -= preTranslate.y();
    accum( 3, 2 ) -= preTranslate.z();
}

Matrix4x4f_t
PhysicsVehicle::computeDisplayMatrix( const Matrix4x4f_t &sourceMatrix ) const {
    // FUN_0082f8e0
    constexpr float kRollPivotBias = -0.05f;   // DAT_00fab5bc
    constexpr float kPitchPivotBias = 0.13f;   // DAT_00fab5b4
    constexpr float kPitchRadiusScale = -2.0f; // DAT_00fab5b8
    constexpr float kDisplayOriginY = 0.0f;    // DAT_016a6994
    constexpr float kDisplayOriginZ = 0.0f;    // DAT_016a6990

    const Eigen::Vector3f displayOrigin(
        0.0f, kDisplayOriginY - bodyHeight * 0.5f, kDisplayOriginZ );

    const int32_t numWheels = vehicle.NumWheels;
    uint32_t pivotWheel = 0;
    if ( numWheels >= 2 && displayPitch < 0.0f ) {
        pivotWheel = 1u << ( ( numWheels / 2 ) - 1 );
    }

    const float wheelCenterY = -getWheelRestCenterDrop( pivotWheel );
    const float wheelCenterZ = wheelAttach[pivotWheel].z();
    const float wheelRadius = vehicle.Wheels[pivotWheel].Radius;

    const float rollOffsetY = -wheelCenterY - wheelRadius - kRollPivotBias;
    const float pitchOffsetY =
        kPitchRadiusScale * wheelRadius - wheelCenterY + kPitchPivotBias;

    Matrix4x4f_t displayTransform = Matrix4x4f_t::Identity();
    RotateAboutOffset( displayTransform, MakeRotationMatZ( -displayRoll ),
                       Eigen::Vector3f( 0.0f, rollOffsetY, 0.0f ) );
    RotateAboutOffset( displayTransform, MakeRotationMatX( displayPitch ),
                       Eigen::Vector3f( 0.0f, pitchOffsetY, -wheelCenterZ ) );

    // Yaw pivots on the body origin, so it takes the display origin with it.
    displayTransform( 3, 0 ) += displayOrigin.x();
    displayTransform( 3, 1 ) += displayOrigin.y();
    displayTransform( 3, 2 ) += displayOrigin.z();
    displayTransform = displayTransform * MakeRotationMatY( displayYaw );

    Matrix4x4f_t outMatrix = displayTransform * sourceMatrix;
    outMatrix( 3, 0 ) += outMatrix( 1, 0 ) * y3DOffset;
    outMatrix( 3, 1 ) += outMatrix( 1, 1 ) * y3DOffset;
    outMatrix( 3, 2 ) += outMatrix( 1, 2 ) * y3DOffset;
    outMatrix( 3, 3 ) = 1.0f;
    return outMatrix;
}

void PhysicsVehicle::publishStepPose() {
    // FUN_0081dfd0
    physicsMatrix = ( bKeyframed && bForceKeyframeMatrix )
                        ? keyframeMatrix
                        : getChassisShiftedMatrix();

    // TODO: REPLAY
    displayMatrix = computeDisplayMatrix( physicsMatrix );
    invChassisMatrix = OrthoInvert( physicsMatrix );
}

void PhysicsVehicle::calibrateDisplayYOffset( float modelWheelRestY ) {
    (void)modelWheelRestY;
    y3DOffset = 0.0f;
}

void PhysicsVehicle::setAuthoredWheelHeights( const float *pAuthoredWheelY,
                                              uint32_t numAuthored ) {
    if ( bAuthoredWheelYKnown || pAuthoredWheelY == nullptr ) {
        return;
    }
    const uint32_t numWheels =
        Min( (uint32_t)vehicle.NumWheels, Min( numAuthored, 4u ) );
    if ( numWheels == 0 ) {
        return;
    }
    for ( uint32_t i = 0; i < numWheels; i++ ) {
        if ( pAuthoredWheelY[i] == 0.0f ) {
            return;
        }
    }

    for ( uint32_t i = 0; i < numWheels; i++ ) {
        authoredWheelY[i] = pAuthoredWheelY[i];
    }
    bAuthoredWheelYKnown = true;
}

void PhysicsVehicle::resetToTransform( const Matrix4x4f_t &transform ) {
    // FUN_0083d880
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return;
    }

    JPH::Mat44 world = JPH::Mat44::sIdentity();
    for ( int c = 0; c < 4; ++c ) {
        world.SetColumn3( c, JPH::Vec3( transform( c, 0 ), transform( c, 1 ),
                                        transform( c, 2 ) ) );
    }
    const Eigen::Vector4f origin = GSPhysics::GetShiftOrigin();
    world.SetTranslation( world.GetTranslation() -
                          JPH::Vec3( origin.x(), origin.y(), origin.z() ) );

    JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
    const JPH::BodyID id = pChassisBody->GetID();
    bi.SetPositionAndRotation( id, world.GetTranslation(),
                               world.GetQuaternion().Normalized(),
                               JPH::EActivation::Activate );
    bi.SetLinearAndAngularVelocity( id, JPH::Vec3::sZero(),
                                    JPH::Vec3::sZero() );

    publishStepPose();
}

void PhysicsVehicle::setKeyframed( bool bInKeyframed, bool bForceMatrix ) {
    // FUN_0082e400
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return;
    }

    if ( bKeyframed != bInKeyframed ) {
        JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
        bi.SetMotionType( pChassisBody->GetID(),
                          bInKeyframed ? JPH::EMotionType::Kinematic
                                       : JPH::EMotionType::Dynamic,
                          JPH::EActivation::Activate );
        bKeyframed = bInKeyframed;
        bKeyframedVelocityPrimed = false;
        OTDU_LOG_DEBUG( "Vehicle body is now %s\n",
                        bInKeyframed ? "KEYFRAMED (cinematic owns the pose)"
                                     : "dynamic again" );
    }

    bForceKeyframeMatrix = bInKeyframed && bForceMatrix;
    if ( bForceKeyframeMatrix ) {
        keyframeMatrix = getChassisShiftedMatrix();
    }
}

void PhysicsVehicle::updateKeyframedVelocity( float deltaTime ) {
    if ( !bKeyframed || pPhysicsSystem == nullptr || pChassisBody == nullptr ||
         deltaTime <= 0.0f ) {
        return;
    }

    const Eigen::Vector3f position( keyframeMatrix( 3, 0 ),
                                    keyframeMatrix( 3, 1 ),
                                    keyframeMatrix( 3, 2 ) );
    if ( bKeyframedVelocityPrimed ) {
        const Eigen::Vector3f velocity =
            ( position - prevKeyframePosition ) / deltaTime;
        pPhysicsSystem->GetBodyInterface().SetLinearVelocity(
            pChassisBody->GetID(),
            JPH::Vec3( velocity.x(), velocity.y(), velocity.z() ) );
    }
    prevKeyframePosition = position;
    bKeyframedVelocityPrimed = true;
}

void PhysicsVehicle::moveKeyframed( const Matrix4x4f_t &worldTransform ) {
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return;
    }

    const float lift = bodyHeight * 0.5f;
    Matrix4x4f_t chassis = worldTransform;
    for ( int c = 0; c < 3; ++c ) {
        chassis( 3, c ) += worldTransform( 1, c ) * lift;
    }

    Matrix4x4f_t shifted = chassis;
    const Eigen::Vector4f origin = GSPhysics::GetShiftOrigin();
    shifted( 3, 0 ) -= origin.x();
    shifted( 3, 1 ) -= origin.y();
    shifted( 3, 2 ) -= origin.z();

    keyframeMatrix = shifted;
    bForceKeyframeMatrix = bKeyframed;

    JPH::Mat44 world = JPH::Mat44::sIdentity();
    for ( int c = 0; c < 4; ++c ) {
        world.SetColumn3( c, JPH::Vec3( shifted( c, 0 ), shifted( c, 1 ),
                                        shifted( c, 2 ) ) );
    }

    JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
    const JPH::BodyID id = pChassisBody->GetID();
    bi.SetPositionAndRotation( id, world.GetTranslation(),
                               world.GetQuaternion().Normalized(),
                               JPH::EActivation::Activate );
    bi.SetLinearAndAngularVelocity( id, JPH::Vec3::sZero(),
                                    JPH::Vec3::sZero() );

    publishStepPose();
}

uint32_t PhysicsVehicle::getNumWheels() const {
    return static_cast<uint32_t>( vehicle.NumWheels );
}

PhysicsVehicle::WheelReadout
PhysicsVehicle::getWheelReadout( uint32_t index ) const {
    PhysicsVehicle::WheelReadout out = {};
    out.LocalPosition = Eigen::Vector3f::Zero();
    if ( index >= static_cast<uint32_t>( vehicle.NumWheels ) ) {
        return out;
    }
    const PhysicsWheelState &w = vehicle.Wheels[index];
    out.LocalPosition = wheelAttach[index];
    out.AngularVelocity = w.AngularVel;
    out.SteerAngle = w.bSteered ? -steerCommand * steerLockRad : 0.0f;
    out.RotationAngle = w.SpinAngle;
    out.SuspensionLength = prevSuspLength[index];
    const float deflection =
        bAuthoredWheelYKnown
            ? ( wheelAttach[index].y() + bodyHeight * 0.5f -
                authoredWheelY[index] - prevSuspLength[index] )
            : ( suspStaticLength[index] - prevSuspLength[index] );
    out.SuspensionDeflection = JPH::Clamp( deflection, -0.25f, 0.25f );
    out.SuspensionStaticLength = suspStaticLength[index];
    out.SuspensionDroopStop = suspDroopStop[index];
    out.SuspensionBumpStop = suspMinLength[index];
    out.LongitudinalSlip = w.NormLongClamped;
    out.LateralSlip = w.NormLatClamped;
    out.LongitudinalGroundSpeed = w.ContactVelLong;
    out.LateralGroundSpeed = w.ContactVelLat;
    out.SuspensionLoad = w.NormalLoad;
    out.Radius = w.Radius;
    out.Width = wheelWidth[index];
    out.bGrounded = w.bGroundContact;
    out.ContactVelocity = w.WheelLinVel;
    out.UpAxis = w.WheelUpAxis;
    out.ForwardAxis = w.ContactLongAxis;
    out.EspState = w.EspState;
    out.BrakeTorqueOut = w.BrakeTorqueOut;
    out.EspTorqueOut = w.EspTorqueOut;
    out.ContactSurface = wheelSurface[index];
    out.ContactMaterial = wheelMaterial[index];
    out.ContactPoint = w.bGroundContact ? w.ContactPoint : w.GroundProbePoint;
    out.ContactNormal = w.ContactNormal;
    return out;
}

float PhysicsVehicle::getChassisSpeed() const {
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return 0.0f;
    }
    const JPH::Vec3 v = pPhysicsSystem->GetBodyInterface().GetLinearVelocity(
        pChassisBody->GetID() );
    return JPH::Vec3( v.GetX(), 0.0f, v.GetZ() ).Length();
}

Eigen::Vector4f PhysicsVehicle::getChassisVelocity() const {
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return Eigen::Vector4f::Zero();
    }
    const JPH::Vec3 v = pPhysicsSystem->GetBodyInterface().GetLinearVelocity(
        pChassisBody->GetID() );
    return Eigen::Vector4f( v.GetX(), v.GetY(), v.GetZ(), 0.0f );
}

float PhysicsVehicle::getEngineAngularVelocity() const {
    return vehicle.Engine.Omega;
}

int32_t PhysicsVehicle::getCurrentGear() const {
    return vehicle.GearBox.CurrentGear - 1;
}

float PhysicsVehicle::getLateralG() const {
    return localAccelLat / kGravityAccel;
}

float PhysicsVehicle::getLongitudinalG() const {
    return localAccelLong / kGravityAccel;
}

float PhysicsVehicle::getVerticalG() const {
    return localAccelVert / kGravityAccel;
}
