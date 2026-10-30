#include "shared.h"

#include "physics_vehicle.h"

#include "bike_roll_limit_constraint.h"

#include "core/maths_helper.h"
#include "core/random/lcg.h"

#include "input/gs_device.h"
#include "physics/physics_config.h"
#include "physics/physics_layers.h"
#include "physics/gs_physics.h"
#include "physics/gs_physicsfx.h"
#include "physics/physics_material.h"
#include "physics/replay.h"
#include "database/gs_database.h"
#include "database/db_aftermarket_pack.h"
#include "player_data/gs_playerdata.h"
#include "game/vehicle/gs_car_state.h"
#include "ai/ai_vehicle.h"
#include "core/mutex.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/MutableCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>

constexpr float kMmToM = 0.001f;
constexpr float kSlipDenom = 3.0f;
constexpr float kSuspDampingScale = 1.0f;
static TestDriveMutex gCollisionEventMutex;

static constexpr uint64_t kWheelSphereTag = 0x57484C5300000000ull;
static constexpr uint64_t kWheelSphereTagMask = 0xFFFFFFFF00000000ull;

class IgnoreOwnVehicleBodiesFilter final : public JPH::BodyFilter {
public:
    explicit IgnoreOwnVehicleBodiesFilter( const JPH::Body &chassis )
        : chassisID( chassis.GetID() ), group( chassis.GetCollisionGroup() ) {}

    bool ShouldCollide( const JPH::BodyID &inBodyID ) const override {
        return inBodyID != chassisID;
    }

    bool ShouldCollideLocked( const JPH::Body &inBody ) const override {
        if ( group.GetGroupFilter() == nullptr ) {
            return true;
        }
        const JPH::CollisionGroup &other = inBody.GetCollisionGroup();
        return other.GetGroupFilter() != group.GetGroupFilter() ||
               other.GetGroupID() != group.GetGroupID();
    }

private:
    JPH::BodyID chassisID;
    JPH::CollisionGroup group;
};

static constexpr float kPedalCurveC2 = 0.5f;        // FLOAT_00f41674
static constexpr float kPedalCurveC3 = 1.0f / 3.0f; // DAT_00f41764
static constexpr float kThrottleCurveK = 0.0f;      // DAT_00f8a22c
static constexpr float kBrakeCurveK = 50.0f;
static constexpr float kPedalGain = 0.7f;                 // DAT_00fab730
static constexpr float kPedalFlooredThreshold = 0.99f;    // DAT_00fab72c
static constexpr float kAutoGbxPedal = 0.1f;              // DAT_00fab720
static constexpr float kAutoGbxInput = 0.1f;              // FLOAT_00f41668
static constexpr float kIgnitionThrottleThreshold = 0.1f; // FLOAT_00f41668
static constexpr float kAutoGbxPedalIdle = 0.01f;         // DAT_00f416d4
static constexpr float kAutoGbxHoldBrake = 0.5f;          // FLOAT_00f41674
static constexpr float kAutoGbxMovingSpeed = 0.2777778f;  // DAT_00f41868, 1km/h
static constexpr float kAutoGbxMoveOffThrottle = 0.2f;    // DAT_00f41730
static constexpr float kAutoGbxReverseExitSpeed = 2.0f;   // DAT_00f416b0
static constexpr float kAutoGbxCoastLockSpeed =
    2.7777779f;                                      // DAT_00f41bf4, 10km/h
static constexpr float kManualClutchCurveK = 500.0f; // DAT_00fab728
static constexpr float kManualClutchEngaged = 0.5f;  // DAT_00fab724

int32_t gVehicleDrivingHelpOverride = -1;
int32_t gVehicleSteerControlOverride = -1;

float gVehicleLowSpeedLatGain = 0.0f;       // DAT_016a6998
constexpr float kDBHundredths = 0.01f;      // DAT_00f416d4
constexpr float kDBPerMille = 0.001f;       // FLOAT_00f4167c
constexpr float kSuspLoadCapCoef = 19.62f;  // DAT_00f41ef8 (2 g)
constexpr float kWheelVelDeadband = 0.25f;  // DAT_00f8a1d4
constexpr float kWheelOmegaDeadband = 0.1f; // FLOAT_00f41668

constexpr float kBrakeBalance = 0.6f;                     // fallback
constexpr float kAWDBrakeCoef = 1.5f;                     // DAT_00f416b8
float gBrakeTorquePerMass = 20.0f;                        // DAT_00f8a27c
static constexpr float kHardImpactEnergyPerMass = 100.0f; // DAT_00fab540
constexpr float kWheelInertiaRadiusCoef = 2.5f;           // DAT_00f416c0

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

// A performance pack lifts lateral friction on both axles; the multiplier is
// smaller in simu mode.
constexpr float kPackLatFrictionBoost = 1.05f;     // DAT_00fab5b0
constexpr float kPackLatFrictionBoostSimu = 1.01f; // DAT_00fab5ac

constexpr float kAeroLiftCoef = 0.2f;     // DAT_00f41730
constexpr float kAeroSimuLiftMult = 2.0f; // DAT_00f416b0, simu only
constexpr float kAeroDensityFallback = 2.0f;
bool gbSimuModeEnabled = false; // DAT_010e777c

float gVehicleAeroDragScale = 1.0f;   // debug: aero drag multiplier
bool gVehicleStartChronopack = false; // debug: arm the chronopack (ImGui)

bool gVehicleESPEnabled = true;

constexpr float kOmegaToRpm = 9.549296f;       // DAT_00f41698 (60 / 2pi)
constexpr float kRpmToRadPerSec = 0.10472f;    // DAT_00f418ac (2pi / 60)
constexpr float kKmhToMs = 0.2777778f;         // km/h -> m/s
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
constexpr float kChrono80mph = 35.755554f;       // DAT_00f42114
constexpr float kChrono70mph = 31.286112f;       // DAT_00f42110
constexpr float kChrono60mphBrake = 26.816668f;  // DAT_00f4210c
constexpr float kChronoMphToMs = 0.44694445f;    // DAT_00f41860
constexpr float kChronoQuarterMile = 0.25f;      // DAT_00f416dc
constexpr float kMetresToFeet = 3.28084f;        // DAT_00f42118
constexpr float kMetresToMiles = 0.000621371f;   // DAT_00f42120
constexpr float kGMeterVerticalVelMax = 200.0f;  // DAT_00fab688
constexpr float kGMeterVerticalVelReset = 10.0f; // DAT_00fab684
constexpr float kGravityAccel = 9.81f;           // 1 / DAT_00f42104
constexpr float kInvGravityAccel = 0.101936795f; // DAT_00f42104

constexpr float kAntiFlyGapDeadband = 0.05f;   // DAT_00f41778
constexpr float kAntiFlyMinForce = 100.0f;     // DAT_00f41708 (apply gate, N)
constexpr float kAntiFlyGravityCoef = 1.0f;    // DAT_00f8a21c (simu: 0.0)
constexpr float kAntiFlyMaxGap = 1.5f;         // DAT_00f8a220 (simu: 0.0)
constexpr float kNoGroundY = -999.0f;          // DAT_00f41f98
constexpr float kNoPreviousGap = -9999.0f;     // DAT_00f41ef4
constexpr float kAntiFlyLeverScale = 0.75f;    // DAT_00f416a4
constexpr float kGroundProbeDepth = 10.0f;     // FLOAT_00f4166c
constexpr float kProbeRise = 0.5f;             // FLOAT_00f41674
constexpr float kPlaneNormalEpsilon = 0.01f;   // DAT_00f416d4
constexpr float kUprightMinY = 0.75f;          // DAT_00f416a4
constexpr float kGroundHeightSlewRate = 20.0f; // DAT_00fab780 / DAT_00fab784
constexpr float kAcceptAnySlope = -2.0f;
constexpr float kRumbleSpeedMin = 5.0f;     // DAT_00f41750 / DAT_00fab754
constexpr float kRumbleSpeedSpan = 45.0f;   // (DAT_00fab75c - DAT_00f41750)
constexpr float kRumblePeriodBase = 10.0f;  // FLOAT_00f4166c / DAT_00fab764
constexpr float kRumblePeriodScale = 0.25f; // DAT_00fab760
constexpr float kRumbleSpread = 0.25f;      // DAT_00f416dc
constexpr float kRumbleAmountMax = 2.0f;    // DAT_00f416b0
constexpr float kRumbleAccelMin = 0.0f;     // DAT_016a69bc
constexpr float kRumbleAccelMax = 10.0f;    // DAT_00fab758

constexpr float kTireHeatMinSlip = 3.5f;        // DAT_00f41b4c
constexpr float kTireHeatRate = 0.2f;           // DAT_00f41730
constexpr float kTireCoolRate = 0.8f;           // DAT_00f416d0
constexpr float kTireBurnThreshold = 0.5f;      // DAT_00f41674
constexpr float kTireHeatMinSpeed = 2.7777779f; // DAT_00f41bf4
constexpr float kTireHeatMinOmega = 2.0943952f; // DAT_00f42024

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

inline void SeedTire( PhysicsTire &t, const PhysicsConfig &c, bool bFront ) {
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
        JPH::SubShapeID remainder;
        if ( PhysicsVehicle::IsWheelSphere( pBody->GetShape()->GetLeafShape(
                 inResult.mSubShapeID2, remainder ) ) ) {
            return;
        }
        const uint64_t userData = pBody->GetUserData();
        const JPH::RVec3 pos = mRay.GetPointOnRay( inResult.mFraction );
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
            mbIgnored = IsHavokCapsuleBody( *pBody );
        }
    }

    static bool IsHavokCapsuleBody( const JPH::Body &body ) {
        const JPH::Shape *pShape = body.GetShape();
        if ( pShape->GetSubType() != JPH::EShapeSubType::RotatedTranslated ) {
            return false;
        }
        const JPH::Shape *pInner =
            static_cast<const JPH::RotatedTranslatedShape *>( pShape )
                ->GetInnerShape();
        return pInner->GetSubType() == JPH::EShapeSubType::Capsule;
    }

    JPH::PhysicsSystem &mSystem;
    JPH::RRayCast mRay;
    JPH::Vec3 mUp;
    float mCosMaxSlope;
    bool mHit = false;
    bool mbIgnored = false;
    JPH::RVec3 mPosition;
    JPH::Vec3 mNormal;
    uint64_t mSurface = 0;
    uint32_t mMaterial = TDUMaterialValue::kDefault;
};

// TODO: Move to GSPhysics?
inline Eigen::Vector4f ToVec4( JPH::Vec3Arg v ) {
    return Eigen::Vector4f( v.GetX(), v.GetY(), v.GetZ(), 0.0f );
}

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
    const PhysicsConfig &c = *vehicle.pConfig;
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
      bIsAI( false ),
      bIsNetwork( false ),
      bCinematicMode( false ),
      bPausePhysics( false ),
      bVirtual( false ),
      bWheelsActive( true ),
      virtualSpeed( 0.0f ),
      bKeyframed( false ),
      bForceKeyframeMatrix( false ),
      keyframeMatrix( Matrix4x4f_t::Identity() ),
      prevKeyframePosition( Eigen::Vector3f::Zero() ),
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
      steerSensitivity( 1.0f ),
      rumbleMax( 0.0f ),
      bHShifterEnabled( false ),
      bManualClutch( false ),
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
      worldAcceleration( Eigen::Vector4f::Zero() ),
      localAcceleration( Eigen::Vector4f::Zero() ),
      localAccelerationG( Eigen::Vector4f::Zero() ) {
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
        prevGroundHeight[i] = suspRestLength[i] + vehicle.Wheels[i].Radius;
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

PhysicsVehicle::SuspensionSetup
PhysicsVehicle::ComputeAxleSuspension( const PhysicsConfig &config,
                                       const DBEntryCarPhysicsData &db,
                                       bool bFront,
                                       bool bBike,
                                       const PerfPackCoefs &pack ) {
    // FUN_008345e0
    constexpr float kGravity = 9.81f;
    const float springCoef =
        config.SpringCoef > 0.0f ? config.SpringCoef : 1.0f;

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

    const float kRateScale = bBike ? 0.008f : 0.006f;
    float kBase = kRateScale * coefSuspRaw * weight * axleFrac * suspRate *
                  pack.Suspension;
    if ( bBike && !bFront ) {
        kBase *= 2.0f;
    }
    if ( !bBike && dbWeight > 3500.0f ) {
        kBase *= 2.5f; // heavy car boost (DAT_00f416c0)
    }
    const float wheelRateScale = bBike ? ( bFront ? 2.0f : 1.0f ) : 1.2f;
    const float k = Max( 1000.0f, kBase * springCoef * wheelRateScale );
    const float d = kSuspDampingScale * kBase * 0.0625f *
                    ( coefSuspRaw * 0.01f ) * ( dampRate * 0.01f ) *
                    springCoef * pack.Suspension;

    const float cornerLoad =
        weight * axleFrac * kGravity * ( bBike ? 1.0f : 0.5f );
    const float sag = cornerLoad / k;

    const int32_t rideHtRaw = bFront ? db.RideHeightFront : db.RideHeightBack;
    const float rideHt_m = ( rideHtRaw > 0 ? rideHtRaw : 130 ) * 0.001f;

    constexpr float kSuspLengthRideHeightScale = 2.0f;
    const int32_t travelRaw =
        bFront ? db.SuspensionLengthFront : db.SuspensionLengthRear;
    const float travel = ( travelRaw > 0 ? travelRaw : 100 ) * 0.01f *
                         rideHt_m * kSuspLengthRideHeightScale;

    constexpr float kStrutClearance = 0.05f;

    SuspensionSetup s;
    s.cornerLoad = cornerLoad;
    s.minLength = kStrutClearance;
    s.staticLength = kStrutClearance + ( travel - sag );
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
                             const DBEntryAftermarketPack *pPack,
                             bool bIsAIDriven,
                             bool bIsNetworkDriven ) {
    if ( pSystem == nullptr || pChassis == nullptr ) {
        return false;
    }

    bIsAI = bIsAIDriven;
    bIsNetwork = bIsNetworkDriven;

    pConfig = ( gpDatabase != nullptr && gpDatabase->isBike( &db ) )
                  ? &gBikePhysicsConfig
                  : &gPhysicsConfig;

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
    pDrivingControl = nullptr;
    pSecondaryDrivingControl = nullptr;
    bBackView = false;
    bChangeView = false;
    prevInputThrottle = 0.0f;
    prevInputBrake = 0.0f;
    prevInputSteer = 0.0f;

    for ( int i = 0; i < PhysicsVehicleState::kMaxWheels; ++i ) {
        wheelTyreHeat[i] = 0.0f;
    }

    bLowSpeedGripState = false;
    bLowSpeedLongAssist = false;
    bLowSpeedLatAssist = false;
    bIsBike = ( pConfig == &gBikePhysicsConfig );
    const int32_t numWheels = bIsBike ? 2 : 4;

    resetAutomaticGearbox();
    bDrivingInputScripted = false;
    bCinematicMode = false;
    bPausePhysics = false;
    bVirtual = false;
    bWheelsActive = true;
    virtualSpeed = 0.0f;
    bKeyframed = false;
    bForceKeyframeMatrix = false;
    keyframeMatrix = Matrix4x4f_t::Identity();
    bKeyframedWheelMatrices = false;
    prevKeyframePosition = Eigen::Vector3f::Zero();
    keyframeVelocity = Eigen::Vector3f::Zero();
    prevKeyframeForward = Eigen::Vector3f::UnitZ();
    keyframeSteer = 0.0f;
    rawInputThrottle = 0.0f;
    rawInputBrake = 0.0f;

    constexpr float kBodyHeightBias = 100.0f; // mm
    const float rideHeightFrontMM =
        (float)( db.RideHeightFront > 0 ? db.RideHeightFront : 130 );
    const float rideHeightRearMM =
        (float)( db.RideHeightBack > 0 ? db.RideHeightBack : 130 );
    rideHeightFront = rideHeightFrontMM * kMmToM;
    rideHeightRear = rideHeightRearMM * kMmToM;
    rideHeightMax[0] = static_cast<float>( db.RideHeightMaxFront ) * kMmToM;
    rideHeightMax[1] = static_cast<float>( db.RideHeightMaxRear ) * kMmToM;
    steerSharpness = static_cast<float>( db.CoefSharpness ) * 0.01f;
    const float dbBodyHeight = ( ( db.Height > 0 ? db.Height : 1300 ) -
                                 rideHeightFrontMM - kBodyHeightBias ) *
                               kMmToM;
    bodyHeight = dbBodyHeight;
    bodyDimensions = Eigen::Vector3f(
        ( db.Width > 0 ? db.Width : 1800 ) * kMmToM, dbBodyHeight,
        ( db.Length > 0 ? db.Length : 4000 ) * kMmToM );
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

    float frontOverhangM;
    if ( db.OverhangFront != 0 ) {
        frontOverhangM = db.OverhangFront * kMmToM;
    } else if ( db.OverhangBack != 0 ) {
        frontOverhangM =
            ( db.Length - db.WheelBase - db.OverhangBack ) * kMmToM;
    } else {
        frontOverhangM = ( db.Length - db.WheelBase ) * kMmToM * 0.5f;
    }
    const float frontAxleZ = db.Length * kMmToM * 0.5f - frontOverhangM;
    const float rearAxleZ = frontAxleZ - wheelBaseM;

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

            if ( gpDatabase->isTraffic( &db ) ) {
                constexpr float kDiameterMmToRadiusM = 0.0005f; // DAT_00f42004
                frontRadius = db.RotorDimFront * kDiameterMmToRadiusM;
                rearRadius = db.RotorDimRear * kDiameterMmToRadiusM;
            } else {
                frontRadius = pRim->FrontHeight * pRim->FrontWidth *
                                  kAspectPctToRatio * kMmToM +
                              pRim->FrontDiameter * kRimInchToHalfM;
                rearRadius = frontRadius;
            }

            frontWidth = pRim->FrontWidth * kMmToM;
            rearWidth = pRim->RearWidth * kMmToM;
        }
    }

    const SuspensionSetup attachSuspFront =
        ComputeAxleSuspension( *pConfig, db, true, bIsBike, pack );
    const SuspensionSetup attachSuspRear =
        ComputeAxleSuspension( *pConfig, db, false, bIsBike, pack );
    const float attachYFront =
        ( frontRadius - rideHeightFront - bodyHeight * 0.5f ) +
        attachSuspFront.staticLength;
    const float attachYRear =
        ( rearRadius - rideHeightRear - bodyHeight * 0.5f ) +
        attachSuspRear.staticLength;

    // FL, FR, RL, RR (TDU: left = +x, forward = +z). FUN_00832420 places
    // wheel i at X = (1 - (2 & 2i)) * halfTrack, matching the FORK_* nodes.
    if ( bIsBike ) {
        wheelAttach[0] = Eigen::Vector3f( 0.0f, attachYFront, frontAxleZ );
        wheelAttach[1] = Eigen::Vector3f( 0.0f, attachYRear, rearAxleZ );
    } else {
        wheelAttach[0] =
            Eigen::Vector3f( halfTrackF, attachYFront, frontAxleZ );
        wheelAttach[1] =
            Eigen::Vector3f( -halfTrackF, attachYFront, frontAxleZ );
        wheelAttach[2] = Eigen::Vector3f( halfTrackR, attachYRear, rearAxleZ );
        wheelAttach[3] = Eigen::Vector3f( -halfTrackR, attachYRear, rearAxleZ );
    }

    const float bodyWidth = ( db.Width > 0 ? db.Width : 1800 ) * kMmToM;
    const float frontalArea = Max( 0.0f, bodyWidth * dbBodyHeight );
    const float cxDrag = Max( 0.0f, db.CxLength ) * pack.Cx;
    const float czLift = Max( 0.0f, db.CxHeight ) * kAeroLiftCoef *
                         ( gbSimuModeEnabled ? kAeroSimuLiftMult : 1.0f );
    aeroDragArea = 0.5f * frontalArea * cxDrag;
    aeroDownArea = 0.5f * frontalArea * czLift;
    aeroDensity = pConfig->AeroDensity[0] > 0.0f ? pConfig->AeroDensity[0]
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
    // FUN_008345e0 @0x008349bb-0x00834a05, 0x0083507a-0x0083522c and
    // 0x00835342-0x008353ea
    float BrakeBalance =
        db.BrakeBalance > 0 ? db.BrakeBalance * 0.01f : kBrakeBalance;
    float brakeCoef = ( bFWD || bRWD ) ? 1.0f : kAWDBrakeCoef;
    if ( gbSimuModeEnabled ) {
        gBrakeTorquePerMass = bIsBike ? 20.0f : 10.0f; // DAT_00fab594/598
        BrakeBalance = bFWD ? 0.75f : ( bRWD ? 0.70f : 0.72f );
        brakeCoef = 1.0f;
    }
    const float BrakeTorque = mass * brakeCoef * gBrakeTorquePerMass /
                              static_cast<float>( numWheels );

    // Select the vehicle tire curve from tires.bnk
    const int numProfiles = gpPhysics ? gpPhysics->getNumTireProfiles() : 0;
    const int tireIdx = SelectTireIndex(
        static_cast<float>( db.Weight > 0 ? db.Weight : 1200 ), numProfiles );
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
        PhysicsVehicle::ComputeAxleSuspension( *pConfig, db, true, bIsBike,
                                               pack );
    const PhysicsVehicle::SuspensionSetup suspRear =
        PhysicsVehicle::ComputeAxleSuspension( *pConfig, db, false, bIsBike,
                                               pack );
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
        const PhysicsConfig &c = *pConfig;
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
    vehicle.pConfig = pConfig;
    numCollisionEvents = 0;
    timeSinceHardImpact = 0.0f;
    resetLandingState();
    phantomRequest = 0;
    phantomMode = 1;
    phantomTimer = 0.0f;
    bCollisionInertiaRequest.store( false );
    collisionInertiaTimer = 0.0f;
    bTrafficBody = gpDatabase != nullptr && gpDatabase->isTraffic( &db );
    initializeElectronics();
    vehicle.NumWheels = numWheels;
    vehicle.bBike = bIsBike;
    vehicle.PowertrainSubsteps = bIsAI ? 5 : ( bIsNetwork ? 1 : 10 );
    vehicle.Mass = mass; // auto shift near-lift traction threshold
    const int32_t axleShift = numWheels / 2 - 1;
    for ( int i = 0; i < numWheels; ++i ) {
        PhysicsWheelState &w = vehicle.Wheels[i];
        const bool bFront = ( i >> axleShift ) == 0;
        const PhysicsVehicle::SuspensionSetup &s =
            bFront ? suspFront : suspRear;
        const float springOmega = kTwoPiSusp * s.frequency;
        suspMaxLength[i] = s.maxLength;
        suspMinLength[i] = s.minLength;
        suspRestLength[i] = s.staticLength;
        suspStiffness[i] = massPerWheel * springOmega * springOmega;
        suspDamping[i] = 2.0f * s.damping * massPerWheel * springOmega;
        const float reboundCoef =
            ( pConfig->ReboundCoef > 0.0f ) ? pConfig->ReboundCoef : 1.0f;
        suspDamperRebound[i] =
            suspDamping[i] * kSuspReboundStiffen * reboundCoef;
        suspDamperLP[i] = 0.0f;
        suspStaticLength[i] = s.staticLength;
        suspCornerLoad[i] = s.cornerLoad;
        suspDroopStop[i] = s.droopStop;
        w.Radius = bFront ? frontRadius : rearRadius;
        wheelWidth[i] = bFront ? frontWidth : rearWidth;
        w.Inertia = w.Radius * w.Radius * kWheelInertiaRadiusCoef;
        w.SideFlag = i & axleShift; // left wheels mirror lateral/steer
        w.bSteered = bFront;
        if ( bIsBike ) {
            w.bDriven = !bFront;
        } else {
            w.bDriven = bFWD ? bFront : ( bRWD ? !bFront : true );
        }
        w.DeadbandLong = kWheelVelDeadband;
        w.DeadbandLat = kWheelVelDeadband;
        w.DeadbandOmega = kWheelOmegaDeadband;
        // @0x008332fe
        w.SuspMaxLoad =
            mass * kSuspLoadCapCoef / static_cast<float>( vehicle.NumWheels );
        w.SuspMinLoad = 0.0f;
        w.LoadScaleLong = 1.0f;
        w.LoadScaleDrag = 0.0f; // v^2 drag off for the spike
        w.DynamicGrip = 1.0f;
        w.GripMult = 1.0f;

        const PhysicsConfig &c = *pConfig;
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
        const float latBrakeCoefFast =
            bFront ? ( c.FrontLatBrakeFrictionCoefFast > 0
                           ? c.FrontLatBrakeFrictionCoefFast
                           : 1.0f )
                   : ( c.RearLatBrakeFrictionCoefFast > 0
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
        // @0x00833122
        w.ReactionScale =
            ( bFront
                  ? ( c.ResistiveCoefFront > 0 ? c.ResistiveCoefFront : 1.0f )
                  : ( c.ResistiveCoefRear > 0 ? c.ResistiveCoefRear : 1.0f ) ) /
            gripCal;
        w.ReactionSlipCoef =
            bFront ? c.ResistiveLateralCoefFront : c.ResistiveLateralCoefRear;

        const float axleBal = bFront ? BrakeBalance : ( 1.0f - BrakeBalance );
        w.BrakeTorque = axleBal * BrakeTorque;

        SeedTire( w.Tire, *pConfig, bFront );
        const TirePacejka *pProfile = bFront ? pFrontTire : pRearTire;
        if ( pProfile != nullptr ) {
            CopyTireCurve( w.Tire, *pProfile );
        }

        prevSuspLength[i] = suspRestLength[i];
        prevGroundHeight[i] = suspRestLength[i] + vehicle.Wheels[i].Radius;
    }

    if ( bIsBike ) {
        arbStiffness[0] = 0.0f;
        arbStiffness[1] = 0.0f;
    } else {
        arbStiffness[0] = kAntiRollFraction * suspStiffness[0]; // front
        arbStiffness[1] = kAntiRollFraction * suspStiffness[2]; // rear
    }

    // Drivetrain routing from DriveType (AWD when neither FWD nor RWD).
    vehicle.bFrontDrive = !bIsBike && bFWD;
    vehicle.bRearDrive = !bIsBike && bRWD;
    if ( !bIsBike && !bFWD && !bRWD ) {
        vehicle.bFrontDrive = vehicle.bRearDrive = vehicle.bCenterDrive = true;
    }

    bLowSpeedLatAssist = bRWD;
    bLowSpeedLongAssist = ( !bFWD && !bRWD );

    // Gearbox: reverse(0) + neutral(1) + DB gears; start in 1st (auto).
    PhysicsGearboxSim &gb = vehicle.GearBox;
    const int nDbGears =
        ( db.NumGears > 0 && db.NumGears <= 7 ) ? db.NumGears : 5;
    gb.NumGears = nDbGears + 2;
    gb.Ratios[0] = bIsBike ? -2.0f : -4.0f; // reverse (DAT_00f417d0/0x00f41e80)
    gb.Ratios[1] = 0.0f;                    // neutral
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
        const float rearRadiusM =
            ( rearRadius > 1.0e-3f ) ? rearRadius : kDefaultWheelRadius;
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
    gb.TransEff = 1.0f;
    gb.CurrentGear = 2; // 1st forward
    gb.ReportedGear = gb.CurrentGear;
    gb.bAutomatic = true;
    gb.UpshiftBand =
        pConfig->RedlinePurcent > 0.0f ? pConfig->RedlinePurcent : 0.7f;
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
    gb.bShiftLockout = bManualBox;
    bMechanicalGearbox = bManualBox;

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
    en.IgnitionOn = false;
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
        ( pConfig->EngineBrakePower > 0 ? pConfig->EngineBrakePower : 1.0f ) *
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

    en.ThrottleIdle = ThrottlePosForTorque( en, en.FrictionLoss * en.OmegaIdle,
                                            en.OmegaIdle );
    OTDU_LOG_DEBUG( "Engine idle: %.0f rpm, throttle %.4f (target %.2f N.m @ "
                    "%.1f rad/s, frictionLoss=%.4f)\n",
                    en.OmegaIdle / rpmToRad, en.ThrottleIdle,
                    en.FrictionLoss * en.OmegaIdle, en.OmegaIdle,
                    en.FrictionLoss );

    const float peakPowerW =
        EngineTorqueAt( en, en.PeakPowerOmega, 1.0f, false ) *
        en.PeakPowerOmega;
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
    en.OmegaPrev = en.Omega;

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

    vehicle.BrakeTorque = BrakeTorque;
    vehicle.HandbrakeTorque = gbSimuModeEnabled ? 1000.0f : 0.0f;

    bRollLimitRestoreRequest = false;
    if ( bIsBike ) {
        BikeRollLimitConstraintSettings settings;
        pRollLimitConstraint =
            settings.Create( JPH::Body::sFixedToWorld, *pChassisBody );
        pPhysicsSystem->AddConstraint( pRollLimitConstraint );
    }

    installWheelCollision( !bFactoryShape );

    pPhysicsSystem->AddStepListener( this );
    bRegistered = true;

    publishStepPose();
    return true;
}

bool PhysicsVehicle::IsWheelSphere( const JPH::Shape *pShape ) {
    return pShape != nullptr &&
           ( pShape->GetUserData() & kWheelSphereTagMask ) == kWheelSphereTag;
}

void PhysicsVehicle::installWheelCollision( bool bProceduralShape ) {
    // FUN_0082fc80
    static constexpr float kSphereDrop = 0.4f; // DAT_00f4175c

    pChassisCompound = nullptr;
    numBikeCollisionBoxes = 0;
    bWheelSpheresActive = true;
    wheelSphereLayer.store( 15 );

    const JPH::Shape *pShape = pChassisBody->GetShape();
    if ( pShape->GetSubType() != JPH::EShapeSubType::OffsetCenterOfMass ) {
        return;
    }
    const JPH::Shape *pInner =
        static_cast<const JPH::OffsetCenterOfMassShape *>( pShape )
            ->GetInnerShape();
    if ( pInner->GetSubType() != JPH::EShapeSubType::MutableCompound ) {
        return;
    }
    pChassisCompound = const_cast<JPH::MutableCompoundShape *>(
        static_cast<const JPH::MutableCompoundShape *>( pInner ) );
    chassisCompoundCOM = pChassisCompound->GetCenterOfMass();

    for ( int32_t i = 0; i < vehicle.NumWheels; i++ ) {
        JPH::SphereShape *pSphere = new JPH::SphereShape(
            wheelWidth[i] * 0.5f,
            TDUPhysicsMaterials::get(
                static_cast<uint32_t>( eMaterialType::MT_Tires ) ) );
        pSphere->SetUserData( kWheelSphereTag | static_cast<uint64_t>( i ) );
        const JPH::Vec3 position( wheelAttach[i].x(),
                                  wheelAttach[i].y() - suspStaticLength[i] -
                                      kSphereDrop,
                                  wheelAttach[i].z() );
        pChassisCompound->AddShape( position, JPH::Quat::sIdentity(), pSphere,
                                    0, i );
    }

    if ( bIsBike && bProceduralShape &&
         pChassisCompound->GetNumSubShapes() ==
             static_cast<uint32_t>( vehicle.NumWheels ) + 3 ) {
        numBikeCollisionBoxes = 3;
        for ( int32_t k = 0; k < numBikeCollisionBoxes; k++ ) {
            const JPH::CompoundShape::SubShape &box =
                pChassisCompound->GetSubShape( vehicle.NumWheels + k );
            const JPH::Mat44 local = JPH::Mat44::sRotationTranslation(
                box.GetRotation(), box.GetPositionCOM() + chassisCompoundCOM );
            Matrix4x4f_t &rest = bikeCollisionBoxRest[k];
            rest = Matrix4x4f_t::Identity();
            for ( int32_t c = 0; c < 4; c++ ) {
                const JPH::Vec4 column = local.GetColumn4( c );
                rest.row( c ) = Eigen::Vector4f( column.GetX(), column.GetY(),
                                                 column.GetZ(), column.GetW() );
            }
        }
    }

    pPhysicsSystem->GetBodyInterfaceNoLock().NotifyShapeChanged(
        pChassisBody->GetID(), pChassisBody->GetShape()->GetCenterOfMass(),
        false, JPH::EActivation::DontActivate );
}

void PhysicsVehicle::updateWheelCollision() {
    // FUN_0083a560
    static constexpr float kRoadLift = 0.05f; // DAT_00fab76c
    if ( pChassisCompound == nullptr ) {
        return;
    }

    for ( int32_t i = 0; i < vehicle.NumWheels; i++ ) {
        const float lift = ( wheelSurface[i] == TDUSurface::Paved ||
                             wheelSurface[i] == TDUSurface::RoadMesh )
                               ? kRoadLift
                               : 0.0f;
        const JPH::Vec3 position( wheelAttach[i].x(),
                                  ( wheelAttach[i].y() - prevSuspLength[i] ) -
                                      vehicle.Wheels[i].Radius +
                                      wheelWidth[i] * 0.5f + lift,
                                  wheelAttach[i].z() );
        pChassisCompound->ModifyShape( i, position, JPH::Quat::sIdentity() );
    }

    JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterfaceNoLock();
    if ( bWheelSpheresActive != bWheelsActive ) {
        bWheelSpheresActive = bWheelsActive;
        wheelSphereLayer.store( bWheelsActive ? 15 : 5 );
        bi.InvalidateContactCache( pChassisBody->GetID() );
    }
    bi.NotifyShapeChanged( pChassisBody->GetID(),
                           pChassisBody->GetShape()->GetCenterOfMass(), false,
                           JPH::EActivation::DontActivate );
}

void PhysicsVehicle::updateBikeCollision() {
    // FUN_0041c470
    static constexpr float kRollPivotDrop = -0.05f;   // _DAT_00fab69c
    static constexpr float kPitchPivotRadius = -2.0f; // _DAT_00fab698
    static constexpr float kPitchPivotLift = 0.13f;   // _DAT_00fab694
    if ( pChassisCompound == nullptr || numBikeCollisionBoxes == 0 ) {
        return;
    }

    const int32_t pivot = ( displayPitch > 0.0f ) ? vehicle.NumWheels / 2 : 0;
    const float wheelY = wheelAttach[pivot].y() - prevSuspLength[pivot];
    const float radius = vehicle.Wheels[pivot].Radius;
    const Eigen::Vector3f wheelPivot( 0.0f, -wheelY - radius - kRollPivotDrop,
                                      0.0f );
    const Eigen::Vector3f rollPivot( 0.0f, bodyDimensions.y() * 0.5f, 0.0f );
    const Eigen::Vector3f pitchPivot(
        0.0f, radius * kPitchPivotRadius - wheelY + kPitchPivotLift,
        wheelAttach[pivot].z() );

    const auto translation = []( const Eigen::Vector3f &t ) {
        Matrix4x4f_t m = Matrix4x4f_t::Identity();
        m.row( 3 ) = Eigen::Vector4f( t.x(), t.y(), t.z(), 1.0f );
        return m;
    };

    for ( int32_t k = 0; k < numBikeCollisionBoxes; k++ ) {
        const Matrix4x4f_t &rest = bikeCollisionBoxRest[k];
        const Eigen::Vector3f boxPosition = rest.row( 3 ).head<3>();
        const Matrix4x4f_t local =
            translation( wheelPivot + boxPosition + rollPivot ) *
            MakeRotationMatZ( -displayRoll ) *
            translation( pitchPivot - wheelPivot ) *
            MakeRotationMatX( displayPitch ) *
            translation( -rollPivot - boxPosition - pitchPivot ) * rest;

        JPH::Mat44 joltLocal = JPH::Mat44::sIdentity();
        for ( int32_t c = 0; c < 3; c++ ) {
            joltLocal.SetColumn3(
                c, JPH::Vec3( local( c, 0 ), local( c, 1 ), local( c, 2 ) ) );
        }
        const JPH::Vec3 position( local( 3, 0 ), local( 3, 1 ), local( 3, 2 ) );
        pChassisCompound->ModifyShape( vehicle.NumWheels + k, position,
                                       joltLocal.GetQuaternion().Normalized() );
    }
}

void PhysicsVehicle::destroy() {
    if ( pPhysicsSystem != nullptr ) {
        if ( bRegistered ) {
            pPhysicsSystem->RemoveStepListener( this );
        }
        if ( pRollLimitConstraint != nullptr ) {
            pPhysicsSystem->RemoveConstraint( pRollLimitConstraint );
            pRollLimitConstraint = nullptr;
        }
        destroyBikeRider();
        JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
        for ( const AttachedPart &part : attachedParts ) {
            gpPhysics->removePropConstraintsFor( part.pBody );
            if ( bi.IsAdded( part.pBody->GetID() ) ) {
                bi.RemoveBody( part.pBody->GetID() );
            }
            bi.DestroyBody( part.pBody->GetID() );
        }
        if ( pChassisBody != nullptr ) {
            if ( bi.IsAdded( pChassisBody->GetID() ) ) {
                bi.RemoveBody( pChassisBody->GetID() );
            }
            bi.DestroyBody( pChassisBody->GetID() );
        }
    }
    attachedParts.clear();
    bodyRenderKey = 0;
    bHasBrokenPart = false;
    bRegistered = false;
    pPhysicsSystem = nullptr;
    pChassisBody = nullptr;
    pChassisCompound = nullptr;
    numBikeCollisionBoxes = 0;
    numCollisionEvents = 0;
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

void PhysicsVehicle::applyDrivingOptions( const DrivingOptions &options ) {
    // FUN_00897790 (inlined)
    vehicle.GearBox.bAutomatic = options.bAutomatic;
    vehicle.GearBox.ShiftState = 1;
    vehicle.GearBox.ShiftTimer = 0.0f;
    vehicle.Clutch.Engagement = 1.0f;
    vehicle.Clutch.State = 0;

    steerSensitivity = options.Sensitivity;
    steerAssistBlend = options.Linearity;
    bUseSteerControl = options.bSteerControl;
    rumbleMax = options.RumbleMax;
    setDrivingHelpLevel( options.DrivingHelpLevel ); // @0x0089780b
    bHShifterEnabled = options.bHShifter;
    bManualClutch = options.bManualClutch;
}

void PhysicsVehicle::applyAIDrivingOptions() {
    // FUN_00968d10
    bUseSteerControl = false;
    pDrivingControl = nullptr;
    autoGearboxState = AGS_Forward;
    vehicle.GearBox.ShiftTimer = 0.0f;
    vehicle.GearBox.bAutomatic = true;
    vehicle.GearBox.ShiftState = 1;
    vehicle.Clutch.Engagement = 1.0f;
    vehicle.Clutch.State = 0;
    steerSensitivity = 1.0f;
    bIsAI = true;
    bAutoReverseAllowed = false;
    vehicle.ElectronicMode = 2;
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
    if ( bIsAI || bIsNetwork ) {
        PhysicsEngineSim &engine = vehicle.Engine;
        engine.IgnitionOn = kIgnitionThrottleThreshold < rawInputThrottle;
        engine.bIgnitionOnce = false;
    }

    float accel = rawInputThrottle;
    float brake = rawInputBrake;
    if ( kThrottleCurveK != 0.0f ) {
        accel = PedalResponseCurve( accel, kThrottleCurveK );
    }
    brake = PedalResponseCurve( brake, kBrakeCurveK );

    vehicle.bThrottleFloored = true;
    vehicle.bBrakeFloored = true;
    if ( !gbSimuModeEnabled ) {
        vehicle.bThrottleFloored = !( kPedalFlooredThreshold > accel );
        vehicle.bBrakeFloored = !( kPedalFlooredThreshold > brake );
    }

    PhysicsClutchSim &clutch = vehicle.Clutch;
    if ( bDrivingInputScripted ) {
        clutch.bManualControl = false;
        resetAutomaticGearbox();
        inputThrottle = rawInputThrottle;
        inputBrake = rawInputBrake;
        return;
    }

    if ( !gbSimuModeEnabled ) {
        accel = Clamp( accel / kPedalGain, 0.0f, 1.0f );
        brake = Clamp( brake / kPedalGain, 0.0f, 1.0f );
    }

    PhysicsEngineSim &engine = vehicle.Engine;
    PhysicsGearboxSim &gearbox = vehicle.GearBox;
    const float speed = chassisSpeed;
    const bool bEngineRunning = engine.State == eEngineState::ES_Running;
    const float idleHalf = engine.OmegaIdle * kAutoGbxHoldBrake;

    const auto consumeShiftRequests = [&]() {
        bGearUpRequest = false;
        bGearDownRequest = false;
    };
    const auto armCoastLock = [&]() {
        if ( clutch.State == 0 && engine.Throttle < kAutoGbxPedalIdle &&
             inputBrake < kAutoGbxPedalIdle ) {
            const float firstRatio =
                ( gearbox.NumGears > 2 )
                    ? gearbox.Ratios[2] * gearbox.FinalDrive
                    : 0.0f;
            if ( ( vehicle.Wheels[0].Radius / firstRatio ) * engine.OmegaIdle <
                 speed ) {
                clutch.State = 3;
            }
        }
    };
    const auto holdIdle = [&]() {
        if ( bEngineRunning && engine.Omega < idleHalf ) {
            engine.Omega = idleHalf;
        }
    };

    if ( !gearbox.bAutomatic && bMechanicalGearbox ) {
        inputBrake = brake;
        inputThrottle = accel;
        engine.Throttle = accel;
        if ( bManualClutch ) {
            gearbox.ShiftState = PhysicsGearboxSim::SS_Settle;
            clutch.bManualControl = true;
            clutch.Target =
                PedalResponseCurve( 1.0f - clutchPedal, kManualClutchCurveK );
            clutch.Engagement = clutch.Target;
            const bool bClutchEngaged = kManualClutchEngaged <= clutch.Target;
            if ( bEngineRunning && engine.Omega < idleHalf ) {
                engine.State = eEngineState::ES_Stalling;
            }
            if ( !bHShifterEnabled ) {
                if ( bGearUpRequest && !bClutchEngaged &&
                     gearbox.CurrentGear < gearbox.NumGears - 1 ) {
                    ++gearbox.CurrentGear;
                }
                if ( bGearDownRequest && !bClutchEngaged &&
                     gearbox.CurrentGear > 0 ) {
                    --gearbox.CurrentGear;
                }
                consumeShiftRequests();
                return;
            }
            if ( bGearSelectBlocked ) {
                if ( gearSelectRequest == 1 ) {
                    bGearSelectBlocked = false;
                }
                return;
            }
            if ( gearbox.CurrentGear == gearSelectRequest ) {
                return;
            }
            if ( bClutchEngaged && gearSelectRequest != 1 ) {
                bGearSelectBlocked = true;
                return;
            }
            if ( gearSelectRequest < gearbox.NumGears ) {
                gearbox.CurrentGear = gearSelectRequest;
            }
            return;
        }

        clutch.bManualControl = false;
        if ( clutch.State == 3 ) {
            if ( engine.OmegaIdle <= engine.Omega ) {
                if ( gearbox.CurrentGear != 0 || inputBrake <= kAutoGbxInput ||
                     kAutoGbxCoastLockSpeed <= speed ) {
                    clutch.Target = 1.0f;
                } else {
                    clutch.State = 0;
                }
            } else {
                clutch.State = 0;
            }
        }
        armCoastLock();
        holdIdle();
        if ( drivingMode == DM_Parked ) {
            clutch.bManualControl = true;
            clutch.State = 0;
            clutch.Target = 0.0f;
            clutch.Engagement = 0.0f;
        }
        if ( !bHShifterEnabled ) {
            if ( bGearUpRequest ) {
                requestUpshift();
            }
            if ( bGearDownRequest ) {
                requestDownshift();
            }
            consumeShiftRequests();
            return;
        }
        const int32_t request = gearSelectRequest;
        if ( gearbox.CurrentGear == request || request >= gearbox.NumGears ) {
            return;
        }
        if ( request == 1 ) {
            gearbox.CurrentGear = 1;
            return;
        }
        if ( gearbox.ShiftState == PhysicsGearboxSim::SS_InGear ) {
            gearbox.ShiftState = ( request > gearbox.CurrentGear )
                                     ? PhysicsGearboxSim::SS_UpshiftStart
                                     : PhysicsGearboxSim::SS_DownshiftStart;
            gearbox.ShiftSubstate = request - gearbox.CurrentGear;
        }
        return;
    }

    clutch.bManualControl = false;
    if ( !gearbox.bAutomatic || deltaTime <= 0.0f || !bEngineRunning ) {
        inputBrake = brake;
        inputThrottle = accel;
    } else {
        clutchPedal = 0.0f;
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
                 bAutoReverseAllowed && drivingMode != DM_Parked &&
                 drivingMode != DM_SlowDown ) {
                autoGearboxState = AGS_EngagingReverse;
            }
            inputBrake = kAutoGbxHoldBrake; // autohold, keeps the car put
            inputThrottle = accel;
            gearbox.CurrentGear = 2; // sits in 1st, declutched
            clutchPedal = 1.0f;
            if ( clutch.State == 3 ) {
                clutch.State = 0;
            }
            break;

        case AGS_EngagingReverse:
            if ( accel > kAutoGbxPedal ) {
                autoGearboxState = AGS_LineLock;
            }
            if ( gearbox.ShiftState == PhysicsGearboxSim::SS_InGear &&
                 gearbox.CurrentGear > 0 ) {
                if ( gearbox.CurrentGear > 1 ) {
                    gearbox.CurrentGear = 1; // snap to neutral first
                }
                requestDownshift(); // neutral -> reverse
            }
            if ( gearbox.CurrentGear == 0 ) {
                inputBrake = accel;
                inputThrottle = brake;
            } else {
                inputBrake = kAutoGbxHoldBrake;
                inputThrottle = 0.0f;
            }
            if ( speed > kAutoGbxMovingSpeed && gearbox.CurrentGear == 0 ) {
                autoGearboxState = AGS_Reverse;
            } else if ( brake < kAutoGbxPedal ) {
                autoGearboxState = AGS_Stopped;
            }
            break;

        case AGS_Reverse:
            gearbox.CurrentGear = 0;
            inputBrake = accel;
            inputThrottle = brake;
            if ( std::fabs( vehicle.ForwardSpeed ) <
                 kAutoGbxReverseExitSpeed ) {
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
            if ( gearbox.ShiftState == PhysicsGearboxSim::SS_InGear ) {
                if ( gearbox.CurrentGear < 2 ) {
                    if ( gearbox.CurrentGear == 0 ) {
                        gearbox.CurrentGear = 1; // out of reverse first
                    }
                    requestUpshift();
                } else if ( gearbox.CurrentGear > 2 ) {
                    requestDownshift();
                }
            }
            if ( gearbox.CurrentGear == 2 ) {
                inputBrake = ( brake < kAutoGbxPedal ) ? 0.0f : brake;
                inputThrottle = accel;
            } else {
                inputBrake = kAutoGbxHoldBrake;
                inputThrottle = kAutoGbxMoveOffThrottle; // keep the revs up
            }
            if ( speed > kAutoGbxMovingSpeed && gearbox.CurrentGear == 2 ) {
                autoGearboxState = AGS_Forward;
            } else if ( accel < kAutoGbxPedal ) {
                autoGearboxState = AGS_Stopped;
            }
            break;

        case AGS_Forward: {
            inputBrake = brake;
            inputThrottle = accel;
            bool bCheckStop = true;
            if ( speed < kAutoGbxMovingSpeed ) {
                if ( accel > kAutoGbxPedal && brake > kAutoGbxPedal ) {
                    autoGearboxState = AGS_LineLock;
                    bCheckStop = false;
                } else if ( clutch.State == 3 ) {
                    clutch.State = 0;
                }
            }
            if ( bCheckStop &&
                 ( ( brake < kAutoGbxPedalIdle &&
                     speed < kAutoGbxMovingSpeed ) ||
                   ( gearbox.CurrentGear == 1 && accel > kAutoGbxInput ) ) ) {
                autoGearboxState = AGS_Stopped;
            }
        } break;

        case AGS_LineLock:
            if ( brake < kAutoGbxPedal ) {
                autoGearboxState = AGS_MovingOff;
            }
            inputBrake = kAutoGbxHoldBrake;
            inputThrottle = accel;
            gearbox.CurrentGear = 2;
            clutchPedal = 1.0f;
            clutch.State = 0;
            break;

        default:
            break;
        }
    }
    engine.Throttle = inputThrottle;

    if ( clutch.State == 3 ) {
        if ( engine.Omega < engine.OmegaIdle ) {
            clutch.State = 0;
        }
        if ( gearbox.CurrentGear == 0 && kAutoGbxInput < inputBrake &&
             speed < kAutoGbxCoastLockSpeed ) {
            clutch.State = 0;
        }
    }
    armCoastLock();
    holdIdle();
    if ( bGearUpRequest ) {
        requestUpshift();
    }
    if ( bGearDownRequest ) {
        requestDownshift();
    }
    consumeShiftRequests();

    if ( kAutoGbxHoldBrake < inputHandbrake && vehicle.bRearDrive ) {
        clutch.bManualControl = true;
        clutch.Target = 0.0f;
        return;
    }
    if ( kAutoGbxInput < clutchPedal ) {
        clutch.bManualControl = true;
        clutch.Target = 1.0f - clutchPedal;
        return;
    }
    if ( drivingMode != DM_Parked ) {
        clutch.bManualControl = false;
        if ( clutch.State == 3 ) {
            clutch.Target = 1.0f;
        }
    }
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

void PhysicsVehicle::declutchForDrivingMode() {
    // FUN_0083db70
    clutchPedal = 1.0f;
    vehicle.Clutch.Engagement = 0.0f;
    vehicle.Clutch.Target = 0.0f;
    vehicle.Clutch.bManualControl = true;
}

void PhysicsVehicle::applyDrivingMode() {
    // FUN_0083db70
    switch ( drivingMode ) {
    case DM_HoldBrake:
        rawInputThrottle = 0.0f;
        rawInputBrake = 0.5f; // DAT_00f41674
        inputSteer = 0.0f;
        inputHandbrake = 0.0f;
        inputLean = 0.0f;
        break;

    case DM_Parked:
        rawInputBrake = 1.0f; // FLOAT_00f4165c
        inputSteer = 0.0f;
        inputHandbrake = 0.0f;
        inputLean = -1.0f; // FLOAT_00f41664
        bChangeView = false;
        declutchForDrivingMode();
        break;

    case DM_SlowDown:
        rawInputThrottle = 0.0f;
        rawInputBrake = 0.65f; // DAT_00f41b6c
        inputSteer = 0.0f;
        inputHandbrake = 0.0f;
        inputLean = 0.0f;
        bBackView = false;
        bChangeView = false;
        declutchForDrivingMode();
        break;

    case DM_RepeatLast:
        rawInputThrottle = prevInputThrottle;
        rawInputBrake = prevInputBrake;
        inputSteer = prevInputSteer;
        inputLean = prevInputLean;
        break;

    default:
        break;
    }

    prevInputThrottle = rawInputThrottle;
    prevInputBrake = rawInputBrake;
    prevInputSteer = inputSteer;
    prevInputLean = inputLean;

    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        vehicle.Wheels[i].bFreeRoll = true;
    }
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
    out.bShiftIdle = ( g.ShiftState == PhysicsGearboxSim::SS_InGear );
    out.ShiftState = g.ShiftState;
    out.bEngineRunning = ( vehicle.Engine.State == eEngineState::ES_Running );
    out.ShiftDirection = 0;
    if ( g.ShiftState == PhysicsGearboxSim::SS_UpshiftStart ||
         g.ShiftState == PhysicsGearboxSim::SS_Upshifting ) {
        out.ShiftDirection = 1;
    } else if ( g.ShiftState == PhysicsGearboxSim::SS_DownshiftStart ||
                g.ShiftState == PhysicsGearboxSim::SS_Downshifting ) {
        out.ShiftDirection = -1;
    }
    return out;
}

void PhysicsVehicle::setCurrentGear( int32_t gear ) {
    PhysicsGearboxSim &g = vehicle.GearBox;
    if ( gear < 0 || gear >= g.NumGears ) {
        return;
    }
    g.CurrentGear = gear;
    g.ReportedGear = gear;
}

void PhysicsVehicle::setAutomaticGearbox( bool bAutomatic ) {
    vehicle.GearBox.bAutomatic = bAutomatic;
}

void PhysicsVehicle::requestUpshift() {
    // FUN_00439de0
    PhysicsGearboxSim &g = vehicle.GearBox;
    if ( g.ShiftState == PhysicsGearboxSim::SS_InGear ) {
        g.ShiftState = PhysicsGearboxSim::SS_UpshiftStart;
        g.ShiftSubstate = 1;
        return;
    }
    // A second request during an upshift stacks another gear onto the one
    // already in flight rather than being dropped.
    if ( g.ShiftState == PhysicsGearboxSim::SS_Upshifting &&
         g.CurrentGear + g.ShiftSubstate < g.NumGears - 1 ) {
        ++g.ShiftSubstate;
    }
}

void PhysicsVehicle::requestDownshift() {
    // FUN_00439e30
    PhysicsGearboxSim &g = vehicle.GearBox;
    if ( g.ShiftState == PhysicsGearboxSim::SS_InGear ) {
        g.ShiftState = PhysicsGearboxSim::SS_DownshiftStart;
        g.ShiftSubstate = -1;
        return;
    }
    if ( g.ShiftState == PhysicsGearboxSim::SS_Downshifting &&
         g.CurrentGear + g.ShiftSubstate > 1 ) {
        --g.ShiftSubstate;
    }
}

float PhysicsVehicle::filterSteering( float rawSteer,
                                      const JPH::Body &chassis,
                                      float deltaTime ) {
    // FUN_0041ad60
    const float speed = chassis.GetLinearVelocity().Length(); // local_44
    const float sensSlider = steerSensitivity;

    float sens = kSteerSensRetailHigh;
    if ( gSensitivity[1] > gSensitivity[0] &&
         gSensitivity[2] > gSensitivity[1] ) {
        float base, slope, lo, hi;
        if ( sensSlider >= gSensitivity[1] ) { // upper segment [0.5,1]
            slope = ( gSensitivity[5] - gSensitivity[4] ) /
                    ( gSensitivity[2] - gSensitivity[1] );
            base = gSensitivity[4];
            sens = slope * ( sensSlider - gSensitivity[1] ) + base;
            lo = gSensitivity[4];
            hi = gSensitivity[5];
        } else { // lower segment [0,0.5]
            slope = ( gSensitivity[4] - gSensitivity[3] ) /
                    ( gSensitivity[1] - gSensitivity[0] );
            base = gSensitivity[3];
            sens = slope * ( sensSlider - gSensitivity[0] ) + base;
            lo = gSensitivity[3];
            hi = gSensitivity[4];
        }
        sens = JPH::Clamp( sens, std::min( lo, hi ), std::max( lo, hi ) );
    }

    const float sensTurn = sens * steerSharpness;

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
    steerSideslipTerm = blend;
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
    // FUN_0083a560
    PhysicsWheelState &w = vehicle.Wheels[i];

    const JPH::RMat44 world = chassis.GetWorldTransform();
    const JPH::Vec3 up = world.GetColumn3( 1 );

    w.WheelUpAxis = ToVec4( up );

    const Eigen::Vector3f &a = wheelAttach[i];
    const JPH::RVec3 attachW = world * JPH::Vec3( a.x(), a.y(), a.z() );
    const JPH::Vec3 down = -up;
    const float droopStop = suspDroopStop[i];
    const float suspRayLen = droopStop + w.Radius;

    const JPH::RVec3 probeOrigin =
        world * JPH::Vec3( a.x(), a.y() + kProbeRise, a.z() );
    JPH::RRayCast ray{ probeOrigin,
                       JPH::Vec3( 0.0f, -kGroundProbeDepth, 0.0f ) };

    const JPH::DefaultBroadPhaseLayerFilter bpFilter =
        system.GetDefaultBroadPhaseLayerFilter( chassis.GetObjectLayer() );
    const JPH::DefaultObjectLayerFilter objFilter =
        system.GetDefaultLayerFilter( chassis.GetObjectLayer() );
    const IgnoreOwnVehicleBodiesFilter bodyFilter( chassis );

    JPH::RayCastSettings settings;
    settings.mTreatConvexAsSolid = false;
    WheelRayCollector collector( system, ray, JPH::Vec3::sAxisY(),
                                 kAcceptAnySlope );
    system.GetNarrowPhaseQueryNoLock().CastRay(
        ray, settings, collector, bpFilter, objFilter, bodyFilter );

    const auto setAirborne = [&]() {
        w.bGroundContact = false;
        w.SuspTotalForce = 0.0f;
        w.NormalLoad = 0.0f;
        prevSuspLength[i] = droopStop;
        prevGroundHeight[i] = droopStop + w.Radius;
        suspDamperLP[i] *= 0.75f;
        wheelSurface[i] = 0;
        wheelMaterial[i] = TDUMaterialValue::kDefault;
    };

    if ( !collector.mHit || collector.mbIgnored ) {
        setAirborne();
        wheelGroundGap[i] = kAntiFlyMaxGap;
        const JPH::RVec3 droopedW = attachW + suspRayLen * down;
        w.GroundProbePoint = Eigen::Vector4f( droopedW.GetX(), droopedW.GetY(),
                                              droopedW.GetZ(), 1.0f );
        updateWheelLandingGap(
            i, static_cast<float>( droopedW.GetY() ) - kNoGroundY, 0.0f,
            deltaTime );
        return false;
    }

    const JPH::Vec3 contact( collector.mPosition - JPH::RVec3::sZero() );

    const JPH::Vec3 planeN =
        collector.mNormal.NormalizedOr( JPH::Vec3::sAxisY() );
    const JPH::Vec3 wheelPoint( attachW - JPH::RVec3::sZero() );
    float hitDist;
    if ( JPH::abs( planeN.GetY() ) > kPlaneNormalEpsilon ) {
        const float verticalDrop =
            planeN.Dot( wheelPoint - contact ) / planeN.GetY();
        hitDist = up.GetY() * verticalDrop;
    } else {
        hitDist = JPH::Vec3( collector.mPosition - attachW ).Dot( down );
    }

    const float heightStep = hitDist - prevGroundHeight[i];
    if ( deltaTime > 0.0f ) {
        const float maxStep = kGroundHeightSlewRate * deltaTime;
        hitDist = JPH::Clamp( hitDist, prevGroundHeight[i] - maxStep,
                              prevGroundHeight[i] + maxStep );
    }

    w.GroundProbePoint =
        Eigen::Vector4f( contact.GetX(), contact.GetY(), contact.GetZ(), 1.0f );

    const JPH::RVec3 tyreBottomW = attachW + suspRayLen * down;
    wheelGroundGap[i] =
        static_cast<float>( tyreBottomW.GetY() - collector.mPosition.GetY() );

    if ( hitDist > suspRayLen || up.GetY() < kUprightMinY ) {
        setAirborne();
        updateWheelLandingGap( i, wheelGroundGap[i], heightStep, deltaTime );
        return false;
    }

    prevGroundHeight[i] =
        JPH::Clamp( hitDist - w.Radius, suspMinLength[i], droopStop ) +
        w.Radius;
    const bool bContact = applyWheelContact(
        chassis, i, deltaTime, attachW, collector.mPosition, collector.mNormal,
        hitDist, static_cast<uint32_t>( collector.mSurface ),
        collector.mMaterial );
    const float compression = ( droopStop - prevSuspLength[i] ) * up.GetY();
    updateWheelLandingGap( i, wheelGroundGap[i] + compression, heightStep,
                           deltaTime );
    return bContact;
}

void PhysicsVehicle::updateWheelLandingGap( int i,
                                            float gap,
                                            float heightStep,
                                            float deltaTime ) {
    // FUN_0083a560
    static constexpr float kGapRateDeadband = 0.1f; // DAT_00fab788
    static constexpr float kLandedGap = 0.05f;      // DAT_00f41778

    groundHeightStep[i] = heightStep;
    if ( landingPrevGap[i] == kNoPreviousGap ) {
        gap = landingGap[i];
    }
    const float gapRate =
        deltaTime > 0.0f ? ( gap - landingGap[i] ) / deltaTime : 0.0f;
    landingGapRate[i] = gapRate;
    landingPrevGap[i] = landingGap[i];
    landingGap[i] = gap;

    if ( gapRate > kGapRateDeadband ) {
        landingPeakGap[i] = std::max( landingPeakGap[i], gap );
    } else if ( gapRate >= -kGapRateDeadband && gap < kLandedGap ) {
        landingPeakGap[i] = 0.0f;
    }
}

void PhysicsVehicle::resetLandingState() {
    for ( int32_t i = 0; i < PhysicsVehicleState::kMaxWheels; i++ ) {
        landingGap[i] = 0.0f;
        landingPrevGap[i] = kNoPreviousGap;
        landingGapRate[i] = 0.0f;
        landingPeakGap[i] = 0.0f;
        groundHeightStep[i] = 0.0f;
    }
    landingScore = 0.0f;
    landingImpact = 0.0f;
    bLandingDetected = false;
    landingWindowSteps = 0;
    landingCooldownSteps = 0;
}

void PhysicsVehicle::updateLandingImpact() {
    // FUN_0041e300
    static constexpr float kLandedGap = 0.05f;                 // DAT_00f41778
    static constexpr float kGapRateLimit = 1000.0f;            // DAT_00f41718
    static constexpr float kPeakGapHigh = 2.5f;                // DAT_00fab5f0
    static constexpr float kPeakGapMedium = 1.5f;              // DAT_00fab5f4
    static constexpr float kPeakGapLow = 0.3f;                 // DAT_00fab5f8
    static constexpr float kScoreHigh = 300.0f;                // DAT_00f4183c
    static constexpr float kScoreMedium = 200.0f;              // DAT_00f4170c
    static constexpr float kScoreLow = 100.0f;                 // DAT_00f41708
    static constexpr float kStepHeight = 0.125f;               // DAT_00fab5e0
    static constexpr float kStepScoreMax = 50.0f;              // DAT_00f41848
    static constexpr float kStepGapEpsilon = 0.01f;            // DAT_00f416d4
    static constexpr float kStepGapRate = 10.0f;               // FLOAT_00f4166c
    static constexpr float kStepPeakGapMax = 0.3f;             // FLOAT_00f416bc
    static constexpr float kStepUpScore = 10.0f;               // FLOAT_00f4166c
    static constexpr float kStepDownScore = 15.0f;             // DAT_00f41700
    static constexpr float kStepFastSpeed = 13.888889f;        // DAT_00fab5ec
    static constexpr float kStepFastScale = 2.0f;              // DAT_00f416b0
    static constexpr float kImpactScoreOffset = 40.0f;         // DAT_00f41b70
    static constexpr float kImpactScoreScale0 = 0.75f;         // DAT_00f416a4
    static constexpr float kImpactScoreScale1 = 0.0047619049f; // DAT_00f41de4
    static constexpr float kImpactMin = 0.25f;                 // DAT_00f416dc
    static constexpr uint8_t kCooldownSteps = 10;              // DAT_00fab5e4
    static constexpr uint8_t kWindowSteps = 10;                // DAT_00fab5e8

    static constexpr float kFallRateMax = 0.0f;

    if ( bVirtual ) {
        return;
    }

    landingImpact = 0.0f;

    int32_t numWheelsOnRoad = 0;
    for ( int32_t i = 0; i < vehicle.NumWheels; i++ ) {
        if ( TDUSurface::isOnRoad( wheelSurface[i] ) ) {
            numWheelsOnRoad++;
        }
    }

    if ( landingCooldownSteps != 0 ) {
        landingCooldownSteps--;
        return;
    }

    if ( landingWindowSteps == 0 ) {
        if ( landingScore > 0.0f ) {
            pushLandingFXEvent();
            landingImpact =
                std::clamp( ( landingScore - kImpactScoreOffset ) *
                                    kImpactScoreScale0 * kImpactScoreScale1 +
                                kImpactMin,
                            kImpactMin, 1.0f );
            bLandingDetected = true;
            landingCooldownSteps = kCooldownSteps;
        }
        landingScore = 0.0f;
        landingWindowSteps = kWindowSteps;
        return;
    }

    bool bAllWheelsAirborne = true;
    for ( int32_t i = 0; i < vehicle.NumWheels; i++ ) {
        const float gap = landingGap[i];
        const float gapRate = landingGapRate[i];
        const float peakGap = landingPeakGap[i];

        if ( !bLandingDetected && gap < kLandedGap && gapRate < kFallRateMax &&
             std::fabs( gapRate ) < kGapRateLimit ) {
            if ( peakGap > kPeakGapHigh && landingScore < kScoreHigh ) {
                landingScore = kScoreHigh;
            } else if ( peakGap > kPeakGapMedium &&
                        landingScore < kScoreMedium ) {
                landingScore = kScoreMedium;
            } else if ( peakGap > kPeakGapLow && landingScore < kScoreLow ) {
                landingScore = kScoreLow;
            }
        }

        const float heightStep = groundHeightStep[i];
        if ( numWheelsOnRoad != 0 && std::fabs( heightStep ) > kStepHeight &&
             landingScore < kStepScoreMax &&
             gap - heightStep < kStepGapEpsilon &&
             std::fabs( gapRate ) > kStepGapRate &&
             peakGap < kStepPeakGapMax ) {
            landingScore = heightStep < 0.0f ? kStepUpScore : kStepDownScore;
            if ( std::fabs( vehicle.Wheels[i].ContactVelLong ) >
                 kStepFastSpeed ) {
                landingScore *= kStepFastScale;
            }
        }

        if ( gap < kPeakGapLow ) {
            bAllWheelsAirborne = false;
        }
    }

    if ( bAllWheelsAirborne ) {
        bLandingDetected = false;
    }
    landingWindowSteps--;
}

void PhysicsVehicle::pushLandingFXEvent() {
    // FUN_007ca480
    static constexpr int32_t kRawMaterialChassis = 14;
    static constexpr int32_t kRawMaterialWorld = 0;

    if ( gpPhysicsFX == nullptr || !gpPhysicsFX->isReady() ||
         pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return;
    }

    PhysicsFXEvent event;
    event.FXMaterialA = GSPhysicsFX::RemapMaterial( kRawMaterialChassis );
    event.FXMaterialB = GSPhysicsFX::RemapMaterial( kRawMaterialWorld );
    if ( !gpPhysicsFX->hasEffectFor( event.FXMaterialA, event.FXMaterialB,
                                     PFXEK_Impact ) ) {
        return;
    }

    const JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
    const JPH::RMat44 world = bi.GetWorldTransform( pChassisBody->GetID() );
    const JPH::Vec3 up = world.GetColumn3( 1 );
    const JPH::Vec3 position( world.GetTranslation() - up );
    const JPH::Vec3 velocity = bi.GetLinearVelocity( pChassisBody->GetID() );

    event.pBodyA = pChassisBody;
    event.pBodyB = nullptr;
    event.MassA = vehicle.Mass;
    event.MassB = vehicle.Mass;
    event.VelocityA = Eigen::Vector4f( velocity.GetX(), velocity.GetY(),
                                       velocity.GetZ(), 0.0f );
    event.VelocityB = Eigen::Vector4f::Zero();
    event.Kind = PFXEK_Impact;
    event.ContactPosition = Eigen::Vector4f( position.GetX(), position.GetY(),
                                             position.GetZ(), 1.0f ) +
                            GSPhysics::GetShiftOrigin();
    event.ContactPosition.w() = 1.0f;
    event.Normal = Eigen::Vector4f( up.GetX(), up.GetY(), up.GetZ(), 0.0f );
    event.Energy = landingScore;

    gpPhysicsFX->getEventQueue().push( event );
}

bool PhysicsVehicle::applyWheelContact( const JPH::Body &chassis,
                                        int i,
                                        float deltaTime,
                                        const JPH::RVec3 &attachW,
                                        const JPH::RVec3 &hitPosition,
                                        const JPH::Vec3 &hitNormal,
                                        float hitDist,
                                        uint32_t surface,
                                        uint32_t material ) {
    PhysicsWheelState &w = vehicle.Wheels[i];

    const JPH::RMat44 world = chassis.GetWorldTransform();
    const JPH::Vec3 right = world.GetColumn3( 0 );
    const JPH::Vec3 up = world.GetColumn3( 1 );
    const JPH::Vec3 forward = world.GetColumn3( 2 );
    const float droopStop = suspDroopStop[i];
    const JPH::Vec3 contact( hitPosition - JPH::RVec3::sZero() );

    const float suspLength =
        JPH::Clamp( hitDist - w.Radius, suspMinLength[i], droopStop );

    // Chassis velocity at the contact point (reused for the tire below).
    const JPH::RVec3 comPos = chassis.GetCenterOfMassPosition();
    const JPH::Vec3 linVel = chassis.GetLinearVelocity();
    const JPH::Vec3 angVel = chassis.GetAngularVelocity();
    const JPH::Vec3 rContact( hitPosition - comPos );
    const JPH::Vec3 contactVel = linVel + angVel.Cross( rContact );

    JPH::Vec3 normal = hitNormal.NormalizedOr( up );

    float spring = suspCornerLoad[i] +
                   suspStiffness[i] * ( suspStaticLength[i] - suspLength );
    if ( spring < 0.0f ) {
        spring = 0.0f;
    }
    const float springGroundScale =
        Max( 0.0f, hitNormal.NormalizedOr( up ).Dot( up ) );

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
    w.SuspTotalForce = spring * springGroundScale + damper;
    w.AntiRollForce = 0.0f;
    wheelSurface[i] = surface;
    wheelMaterial[i] = material;

    // Contact frame.
    float steerAngle = computeWheelSteerAngle( w ) + w.ImpactSteerOffset;

    const JPH::Vec3 steeredFwd =
        forward * JPH::Cos( steerAngle ) + right * JPH::Sin( steerAngle );
    JPH::Vec3 longAxis = ( steeredFwd - normal * steeredFwd.Dot( normal ) );
    longAxis = longAxis.NormalizedOr( steeredFwd );
    const JPH::Vec3 latAxis = normal.Cross( longAxis ).NormalizedOr( right );

    const float vLong = contactVel.Dot( longAxis );

    const float longSlip = ( w.AngularVel * w.Radius - vLong ) /
                           std::max( std::fabs( vLong ), kSlipDenom );

    w.ContactNormal = ToVec4( normal );
    w.SuspAxis = w.ContactNormal;
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

// Piecewise-linear curve through (0, 0), (x[0], y[0]), (x[1], y[1]), each
// segment clamped to its end values.
static float
EvaluateLeanSegment( float x, float x0, float x1, float y0, float y1 ) {
    const float span = x1 - x0;
    float y;
    if ( span == 0.0f ) {
        // LEAN_CURVE_RANGE stores the same value twice.
        y = ( x > x0 ) ? ( ( y1 > y0 ) ? FLT_MAX : -FLT_MAX ) : y0;
    } else {
        y = ( ( y1 - y0 ) * ( x - x0 ) ) / span + y0;
    }
    return Clamp( y, std::min( y0, y1 ), std::max( y0, y1 ) );
}

void PhysicsVehicle::resetBikeLean() {
    // FUN_0083d220
    displayYaw = 0.0f;
    displayPitch = 0.0f;
    displayPitchRate = 0.0f;
    displayRoll = 0.0f;
    pitchLandingSide = -1;
    bPitchCrossedZero = true;
}

void PhysicsVehicle::updateBikeLean( float deltaTime ) {
    // FUN_0083db70
    static constexpr float kLeanInputKeep = 0.8f;   // _DAT_00fab66c
    static constexpr float kRatePerSecond = 100.0f; // DAT_00f41708
    bikeLeanSmoothed += ( inputLean - bikeLeanSmoothed ) *
                        ( 1.0f - kLeanInputKeep ) * deltaTime * kRatePerSecond;

    // FUN_0041c470
    static constexpr float kLeanRateMax = 100.0f;              // DAT_00fab6dc
    static constexpr float kSteerLeanSpeedScale = 0.1f;        // FLOAT_00f41668
    static constexpr float kSlipLeanBlendStart = 15.0f;        // DAT_00f41700
    static constexpr float kSlipLeanBlendScale = 1.0f / 15.0f; // DAT_00f41c50
    static constexpr float kLeanKeep = 0.975f;                 // DAT_00fab6d8
    static constexpr float kAccelToGrams = 0.10193679f;        // DAT_00f42104
    static constexpr float kGramsScale = 1000.0f;              // DAT_00f41718
    static constexpr float kYawAccelMin = 300.0f;              // DAT_00fab6cc
    static constexpr float kYawAccelRange = 750.0f;            // DAT_00fab6d0
    static constexpr float kYawMax = 0.3f;                     // DAT_00fab6d4
    static constexpr float kYawLeanMin = 0.1f;                 // DAT_00fab6c0
    static constexpr float kYawKeepCoasting = 0.95f;           // DAT_00fab6c4
    static constexpr float kYawKeepAccelerating = 0.975f;      // DAT_00fab6c8
    if ( !bIsBike ) {
        displayRoll = 0.0f;
        return;
    }
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return;
    }

    JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
    const JPH::Vec3 velocity = bi.GetLinearVelocity( pChassisBody->GetID() );
    const JPH::Quat rotation = bi.GetRotation( pChassisBody->GetID() );
    const JPH::Vec3 localVelocity = rotation.Conjugated() * velocity;
    const float speed = velocity.Length();

    float speedLean;
    if ( speed < gLeanSpeedRange[0] ) {
        speedLean = Clamp( ( gLeanSpeedValue[0] * speed ) / gLeanSpeedRange[0],
                           std::min( 0.0f, gLeanSpeedValue[0] ),
                           std::max( 0.0f, gLeanSpeedValue[0] ) );
    } else if ( speed < gLeanSpeedRange[1] ) {
        speedLean =
            EvaluateLeanSegment( speed, gLeanSpeedRange[0], gLeanSpeedRange[1],
                                 gLeanSpeedValue[0], gLeanSpeedValue[1] );
    } else {
        speedLean =
            EvaluateLeanSegment( speed, gLeanSpeedRange[1], gLeanSpeedRange[2],
                                 gLeanSpeedValue[1], gLeanSpeedValue[2] );
    }

    const float slipAngle =
        std::atan2( localVelocity.GetX(), localVelocity.GetZ() );
    const float steerLean = inputSteer *
                            Clamp( speed * kSteerLeanSpeedScale, 0.0f, 1.0f ) *
                            gLeanCurveValue[0];

    const float absSlip = std::fabs( slipAngle );
    float slipCurve;
    if ( absSlip < gLeanCurveRange[0] ) {
        slipCurve =
            Clamp( ( gLeanCurveValue[0] * absSlip ) / gLeanCurveRange[0],
                   std::min( 0.0f, gLeanCurveValue[0] ),
                   std::max( 0.0f, gLeanCurveValue[0] ) );
    } else {
        slipCurve = EvaluateLeanSegment( absSlip, gLeanCurveRange[0],
                                         gLeanCurveRange[1], gLeanCurveValue[0],
                                         gLeanCurveValue[1] );
    }
    const float slipSign = ( slipAngle > 0.0f ) ? 1.0f : -1.0f;
    const float slipLean = -( slipSign * slipCurve * speedLean );

    float targetLean = Clamp(
        ( slipLean - steerLean ) * ( speed - kSlipLeanBlendStart ) *
                kSlipLeanBlendScale +
            steerLean,
        std::min( steerLean, slipLean ), std::max( steerLean, slipLean ) );
    targetLean = Clamp( targetLean, -gLeanSpeedValueHigh, gLeanSpeedValueHigh );

    const bool bBothWheelsGrounded =
        vehicle.Wheels[0].bGroundContact && vehicle.Wheels[1].bGroundContact;
    float lean = displayRoll;
    if ( bBothWheelsGrounded ) {
        lean += ( targetLean - lean ) * ( 1.0f - kLeanKeep ) * deltaTime *
                kRatePerSecond;
    }
    const float maxStep = kLeanRateMax * deltaTime;
    lean = Clamp( lean, displayRoll - maxStep, displayRoll + maxStep );
    displayRoll = lean;

    bikeHeadLean = gLeanHead[0] * lean;
    if ( bikeHeadLean > gLeanHead[0] ) {
        bikeHeadLean = gLeanHead[1];
    } else if ( bikeHeadLean < -gLeanHead[0] ) {
        bikeHeadLean = -gLeanHead[1];
    }

    float accel = localAcceleration.z() * kAccelToGrams * kGramsScale;
    if ( accel < kYawAccelMin ) {
        accel = 0.0f;
    }
    const float leanSign = ( lean > 0.0f ) ? 1.0f : -1.0f;
    const float leanAmount =
        Clamp( ( std::fabs( lean ) - kYawLeanMin ) / ( 1.0f - kYawLeanMin ),
               0.0f, 1.0f );
    const float yawAmount =
        Clamp( ( kYawMax * accel ) / kYawAccelRange, 0.0f, kYawMax );
    const float yawKeep =
        ( accel > 0.0f ) ? kYawKeepAccelerating : kYawKeepCoasting;
    displayYaw +=
        ( yawAmount * leanAmount * vehicle.Engine.Throttle * leanSign -
          displayYaw ) *
        ( 1.0f - yawKeep );

    if ( !bIsNetwork ) {
        updateBikePitch( deltaTime );
    }
}

void PhysicsVehicle::updateBikePitch( float deltaTime ) {
    // FUN_0041c470
    static constexpr float kSteerFade = 4.0f;               // DAT_00f418d4
    static constexpr float kLeanDeadzone = 0.2f;            // DAT_00fab6ac
    static constexpr float kPitchFlat = 0.001f;             // DAT_00fab6b0
    static constexpr float kForceSpeedMin = 2.0f;           // _DAT_00fab6a8
    static constexpr float kForceSpeedMax = 10.0f;          // _DAT_00fab6a4
    static constexpr float kForceScale = 2.5f;              // _DAT_00fab6bc
    static constexpr float kGravityTorque = 15.0f;          // DAT_00fab6b8
    static constexpr float kPitchDamping = 15.0f;           // DAT_00fab6b4
    static constexpr float kLandingRate = 1.0f;             // DAT_00fab6a0
    static constexpr float kRestZ = 0.0f, kRestY = -0.15f;  // DAT_016a8214/8
    static constexpr float kBackZ = 0.6f, kBackY = 0.0f;    // DAT_016a820c/10
    static constexpr float kFrontZ = -0.4f, kFrontY = 0.0f; // DAT_016a8204/8

    const float mass = ( vehicle.Mass > 0.0f ) ? vehicle.Mass : 1.0f;
    const float steerFade =
        Clamp( 1.0f - std::fabs( steerCommand ) * kSteerFade, 0.0f, 1.0f );

    float leanBlend;
    if ( bikeLeanSmoothed <= 0.0f ) {
        leanBlend = ( ( bikeLeanSmoothed + kLeanDeadzone ) * -1.0f ) /
                    ( -1.0f + kLeanDeadzone );
        leanBlend = ( leanBlend < 0.0f ) ? std::max( leanBlend, -1.0f ) : 0.0f;
    } else {
        leanBlend = Clamp( ( bikeLeanSmoothed - kLeanDeadzone ) /
                               ( 1.0f - kLeanDeadzone ),
                           0.0f, 1.0f );
    }
    float cogZ;
    float cogY;
    if ( bikeLeanSmoothed >= 0.0f ) {
        cogZ =
            Clamp( ( kFrontZ - kRestZ ) * leanBlend + kRestZ,
                   std::min( kRestZ, kFrontZ ), std::max( kRestZ, kFrontZ ) );
        cogY =
            Clamp( ( kFrontY - kRestY ) * leanBlend + kRestY,
                   std::min( kRestY, kFrontY ), std::max( kRestY, kFrontY ) );
    } else {
        cogZ = Clamp( kRestZ - ( kBackZ - kRestZ ) * leanBlend,
                      std::min( kRestZ, kBackZ ), std::max( kRestZ, kBackZ ) );
        cogY = Clamp( kRestY - ( kBackY - kRestY ) * leanBlend,
                      std::min( kRestY, kBackY ), std::max( kRestY, kBackY ) );
    }

    const auto wheelCentreY = [this]( int32_t i ) {
        return wheelAttach[i].y() - prevSuspLength[i];
    };
    const int32_t pivot = ( displayPitch >= 0.0f ) ? 0 : 1;
    const float pivotZ = wheelAttach[pivot].z();
    const float pivotY = wheelCentreY( pivot );
    const float sinPitch = std::sin( displayPitch );
    const float cosPitch = std::cos( displayPitch );
    const auto rotateAboutPivot = [&]( float z, float y, float &outZ,
                                       float &outY ) {
        outY = cosPitch * ( y - pivotY ) - sinPitch * ( z - pivotZ ) + pivotY;
        outZ = cosPitch * ( z - pivotZ ) + sinPitch * ( y - pivotY ) + pivotZ;
    };

    float cogRotZ;
    float cogRotY;
    rotateAboutPivot( cogZ, cogY, cogRotZ, cogRotY );

    float driveTorque = 0.0f;
    float gravityTorque[2] = {};
    for ( int32_t i = 0; i < 2; i++ ) {
        float wheelZ;
        float wheelY;
        rotateAboutPivot( wheelAttach[i].z(), wheelCentreY( i ), wheelZ,
                          wheelY );
        float forceZ = 0.0f;
        if ( ( i == 0 && -kPitchFlat < displayPitch ) ||
             ( i == 1 && displayPitch < kPitchFlat ) ) {
            const PhysicsWheelState &w = vehicle.Wheels[i];
            const float speedScale =
                Clamp( ( std::fabs( w.ContactVelLong ) - kForceSpeedMin ) /
                           ( kForceSpeedMax - kForceSpeedMin ),
                       0.0f, 1.0f );
            forceZ = -w.TireForceLong * speedScale * kForceScale / mass;
        }
        driveTorque -= ( wheelY - cogRotY ) * forceZ;
        gravityTorque[i] = ( wheelZ - cogRotZ ) * kGravityTorque;
    }
    driveTorque *= steerFade;

    pitchLandingSide = -1;
    if ( bPitchCrossedZero ) {
        if ( displayPitchRate > kLandingRate ) {
            pitchLandingSide = 0;
        } else if ( displayPitchRate < -kLandingRate ) {
            pitchLandingSide = 1;
        }
        displayPitchRate = 0.0f;
        displayPitch = 0.0f;
    }

    float restoringTorque = gravityTorque[1];
    if ( displayPitch <= kPitchFlat ) {
        if ( -kPitchFlat <= displayPitch && driveTorque >= 0.0f ) {
            restoringTorque = ( driveTorque > 0.0f ) ? gravityTorque[1] : 0.0f;
        } else {
            restoringTorque = gravityTorque[0];
        }
    }
    float totalTorque = restoringTorque + driveTorque;
    if ( std::fabs( displayPitch ) < kPitchFlat ) {
        if ( driveTorque > 0.0f && totalTorque < 0.0f ) {
            totalTorque = 0.0f;
        }
        if ( driveTorque < 0.0f && totalTorque > 0.0f ) {
            totalTorque = 0.0f;
        }
    }

    const float pitchSign = ( displayPitch > 0.0f ) ? 1.0f : -1.0f;
    const float rateSign = ( displayPitchRate > 0.0f ) ? 1.0f : -1.0f;
    const float damping = ( pitchSign != rateSign ) ? 0.0f : kPitchDamping;
    const float previousPitch = displayPitch;
    displayPitchRate +=
        ( totalTorque - damping * displayPitchRate ) * deltaTime;
    displayPitch = displayPitchRate * deltaTime + previousPitch;

    if ( previousPitch == 0.0f ) {
        bPitchCrossedZero = false;
    } else {
        const bool bWasPositive = previousPitch > 0.0f;
        const bool bIsPositive = displayPitch > 0.0f;
        bPitchCrossedZero = bWasPositive != bIsPositive;
    }
}

void PhysicsVehicle::updateBikeRollLimit( const JPH::Body &chassis ) {
    // FUN_00837cd0
    static constexpr float kTiltMax = 0.9f;               // DAT_00f416a0
    static constexpr float kCrashImpactEnergy = 50000.0f; // DAT_00fab740
    static constexpr int32_t kLastReservedSlot = 7;
    static constexpr float kPitchMin = -1.5f;   // DAT_00fab73c
    static constexpr float kPitchMax = 1.2f;    // DAT_00fab738
    static constexpr float kSlideSpeed = 10.0f; // FLOAT_00f4166c
    static constexpr float kSlideRatio = 1.0f;  // DAT_00fab734
    if ( pRollLimitConstraint == nullptr ) {
        return;
    }

    if ( bIsNetwork ) {
        pRollLimitConstraint->SetEnabled( true );
        bRollLimitRestoreRequest = false;
        return;
    }

    const JPH::Quat rotation = chassis.GetRotation();
    const JPH::Vec3 forward = rotation * JPH::Vec3::sAxisZ();
    bool bCrashed = std::fabs( forward.GetY() ) > kTiltMax;
    {
        ScopedMutexLock lock( &gCollisionEventMutex );
        for ( int32_t i = 0; i < numCollisionEvents && !bCrashed; i++ ) {
            const CollisionEvent &event = collisionEvents[i];
            if ( event.Type == eCollisionType::CT_None ||
                 event.Type == eCollisionType::CT_DynamicBody ) {
                continue;
            }
            if ( event.Type == eCollisionType::CT_Vehicle ) {
                const PhysicsVehicle *pOther = event.pOtherVehicle;
                const int32_t otherSlot =
                    ( pOther != nullptr && gpPhysics != nullptr )
                        ? static_cast<int32_t>( pOther -
                                                gpPhysics->getVehiclesBegin() )
                        : -1;
                if ( otherSlot <= kLastReservedSlot ) {
                    continue;
                }
            }
            bCrashed = event.Energy > kCrashImpactEnergy;
        }
    }
    if ( displayPitch < kPitchMin || kPitchMax < displayPitch ) {
        bCrashed = true;
    }

    bool bSliding = false;
    if ( vehicle.LaunchState != 1 ) {
        const JPH::Vec3 velocity = chassis.GetLinearVelocity();
        if ( velocity.Length() > kSlideSpeed ) {
            const JPH::Vec3 localVelocity = rotation.Conjugated() * velocity;
            bSliding = std::fabs( localVelocity.GetX() ) /
                           std::fabs( localVelocity.GetZ() ) >
                       kSlideRatio;
        }
    }

    if ( pRollLimitConstraint->GetEnabled() && ( bCrashed || bSliding ) ) {
        pRollLimitConstraint->SetEnabled( false );
    }
    if ( !pRollLimitConstraint->GetEnabled() &&
         ( ( !bCrashed && !bSliding ) || bRollLimitRestoreRequest ) ) {
        pRollLimitConstraint->SetEnabled( true );
    }
    bRollLimitRestoreRequest = false;
}

void PhysicsVehicle::updateBikeRider() {
    // FUN_0083db70
    const bool bRollLimitOut =
        pRollLimitConstraint != nullptr && !pRollLimitConstraint->GetEnabled();
    if ( bRollLimitOut ) {
        if ( pBikeRiderBody == nullptr ) {
            spawnBikeRider();
        }
    } else if ( pBikeRiderBody != nullptr ) {
        destroyBikeRider();
    }
}

void PhysicsVehicle::updateWheelsActive() {
    // FUN_0083a560
    static constexpr float kDriveTorqueMin = 10.0f;      // FLOAT_00f4166c
    static constexpr float kLoadMin = 10.0f;             // FLOAT_00f4166c
    static constexpr float kSuspensionTravelMin = 0.01f; // _DAT_00fab770
    static constexpr float kRollingSpeedMin = 0.25f;     // DAT_00f8a1d4
    static constexpr float kBikePitchMin = 0.01f;        // DAT_00f416d4

    bWheelsActive = false;
    for ( int32_t i = 0; i < vehicle.NumWheels; i++ ) {
        const PhysicsWheelState &w = vehicle.Wheels[i];
        bWheelsActive |= std::fabs( w.DriveTorque ) > kDriveTorqueMin ||
                         w.NormalLoad < kLoadMin ||
                         std::fabs( prevSuspLength[i] - suspRestLength[i] ) >
                             kSuspensionTravelMin ||
                         std::fabs( w.ContactVelLong ) > kRollingSpeedMin;
    }
    if ( bIsBike ) {
        bWheelsActive |=
            std::fabs( displayPitch ) > kBikePitchMin || pitchLandingSide != -1;
        if ( pRollLimitConstraint != nullptr &&
             !pRollLimitConstraint->GetEnabled() ) {
            bWheelsActive = false;
        }
    }
}

bool PhysicsVehicle::requestCollisionInertia() {
    // FUN_0082e740
    if ( collisionInertiaTimer == 0.0f && !bKeyframed && !bTrafficBody ) {
        bCollisionInertiaRequest.store( true );
    }
    return bCollisionInertiaRequest.load();
}

void PhysicsVehicle::updateCollisionInertia( float deltaTime ) {
    // FUN_0082e740
    static constexpr float kInertia = 20000.0f; // DAT_00fab808
    static constexpr float kDuration = 0.05f;   // DAT_00fab804

    JPH::MotionProperties *pMotion = pChassisBody->GetMotionProperties();
    if ( bCollisionInertiaRequest.exchange( false ) && pMotion != nullptr ) {
        savedInvInertiaDiagonal = pMotion->GetInverseInertiaDiagonal();
        savedInertiaRotation = pMotion->GetInertiaRotation();
        pMotion->SetInverseInertia( JPH::Vec3::sReplicate( 1.0f / kInertia ),
                                    JPH::Quat::sIdentity() );
        collisionInertiaTimer = kDuration;
    }

    // FUN_0083db70
    if ( collisionInertiaTimer > 0.0f && !bKeyframed ) {
        collisionInertiaTimer -= deltaTime;
        if ( collisionInertiaTimer <= 0.0f ) {
            if ( pMotion != nullptr ) {
                pMotion->SetInverseInertia( savedInvInertiaDiagonal,
                                            savedInertiaRotation );
            }
            collisionInertiaTimer = 0.0f;
        }
    }
}

void PhysicsVehicle::spawnBikeRider() {
    // FUN_0083db70
    static constexpr float kThrowPitch = 0.7853982f;    // 0x3f490fdb
    static constexpr float kSpawnHeight = 0.2f;         // 0x3e4ccccd
    static constexpr float kThrowUpSpeed = 0.5f;        // FLOAT_00f41674
    static constexpr float kThrowScaleMax = 1.5f;       // DAT_00fab668
    static constexpr float kThrowPitchBias = -0.5f;     // DAT_00f4186c
    static constexpr float kRiderMass = 80.0f;          // 0x42a00000
    static constexpr float kRiderFriction = 0.9f;       // 0x3f666666
    static constexpr float kRiderConvexRadius = 0.05f;  // 0x3d4ccccd
    static constexpr float kRiderLinearDamping = 0.1f;  // FLOAT_00f41668
    static constexpr float kRiderAngularDamping = 5.0f; // DAT_00f41750
    static const JPH::Vec3 kRiderHalfExtents( 0.2f, 0.65f,
                                              0.06f ); // FUN_0081d760

    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return;
    }

    Matrix4x4f_t spawn = MakeRotationMatX( kThrowPitch ) *
                         ( bikeRiderSeatMatrix * displayMatrix );
    const Eigen::Vector3f position =
        TDUMat4x4MulVec( spawn, Eigen::Vector3f( 0.0f, kSpawnHeight, 0.0f ) );

    JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
    JPH::Vec3 velocity = bi.GetLinearVelocity( pChassisBody->GetID() );
    velocity.SetY( velocity.GetY() + kThrowUpSpeed );

    const float minScale = 1.0f / kThrowScaleMax;
    const float throwScale =
        std::min( std::max( ( kThrowScaleMax - minScale ) *
                                    ( displayPitch - kThrowPitchBias ) +
                                minScale,
                            minScale ),
                  kThrowScaleMax );
    velocity *= throwScale;

    JPH::Mat44 rotation = JPH::Mat44::sIdentity();
    for ( int32_t r = 0; r < 3; r++ ) {
        rotation.SetColumn3(
            r, JPH::Vec3( spawn( r, 0 ), spawn( r, 1 ), spawn( r, 2 ) ) );
    }

    JPH::BodyCreationSettings settings(
        new JPH::BoxShapeSettings( kRiderHalfExtents, kRiderConvexRadius ),
        JPH::RVec3( position.x(), position.y(), position.z() ),
        rotation.GetQuaternion().Normalized(), JPH::EMotionType::Dynamic,
        TDUPhysLayers::MOVING );
    settings.mOverrideMassProperties =
        JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = kRiderMass;
    settings.mFriction = kRiderFriction;
    settings.mLinearDamping = kRiderLinearDamping;
    settings.mAngularDamping = kRiderAngularDamping;
    settings.mLinearVelocity = velocity;
    settings.mUserData =
        TDUSurface::BikeRider |
        ( reinterpret_cast<uint64_t>( this ) << TDUSurface::kOwnerShift );

    pBikeRiderBody = bi.CreateBody( settings );
    if ( pBikeRiderBody != nullptr ) {
        bi.AddBody( pBikeRiderBody->GetID(), JPH::EActivation::Activate );
    }
}

void PhysicsVehicle::destroyBikeRider() {
    // FUN_0083db70
    if ( pBikeRiderBody == nullptr || pPhysicsSystem == nullptr ) {
        pBikeRiderBody = nullptr;
        return;
    }

    JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
    bi.RemoveBody( pBikeRiderBody->GetID() );
    bi.DestroyBody( pBikeRiderBody->GetID() );
    pBikeRiderBody = nullptr;
}

void PhysicsVehicle::resetEjectedBikeRider() {
    // FUN_00828670
    PhysicsLock lock;
    if ( !vehicle.bBike || pRollLimitConstraint == nullptr ||
         pRollLimitConstraint->GetEnabled() ) {
        return;
    }
    if ( !bPausePhysics ) { // +0xe48
        pRollLimitConstraint->SetEnabled( true );
    }
    if ( pBikeRiderBody != nullptr ) {
        destroyBikeRider();
    }
}

void PhysicsVehicle::publishBikeRider() {
    // FUN_0083bbd0
    bBikeRiderPresent = pBikeRiderBody != nullptr;
    if ( !bBikeRiderPresent ) {
        bBikeRiderOnGround = false;
        bBikeRiderMoving = false;
        bikeRiderMatrix = Matrix4x4f_t::Identity();
        return;
    }

    JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
    const JPH::BodyID id = pBikeRiderBody->GetID();
    const JPH::Vec3 velocity = bi.GetLinearVelocity( id );
    bikeRiderVelocity = Eigen::Vector4f( velocity.GetX(), velocity.GetY(),
                                         velocity.GetZ(), 0.0f );
    const JPH::RMat44 world = bi.GetWorldTransform( id );
    for ( int32_t c = 0; c < 4; c++ ) {
        const JPH::Vec3 column = world.GetColumn3( c );
        bikeRiderMatrix( c, 0 ) = column.GetX();
        bikeRiderMatrix( c, 1 ) = column.GetY();
        bikeRiderMatrix( c, 2 ) = column.GetZ();
        bikeRiderMatrix( c, 3 ) = ( c == 3 ) ? 1.0f : 0.0f;
    }
    bBikeRiderMoving = bi.IsActive( id );
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
                system.GetDefaultBroadPhaseLayerFilter(
                    chassis.GetObjectLayer() );
            const JPH::DefaultObjectLayerFilter objFilter =
                system.GetDefaultLayerFilter( chassis.GetObjectLayer() );
            const IgnoreOwnVehicleBodiesFilter bodyFilter( chassis );

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
        EngineTorqueAt( eng, eng.PeakPowerOmega, 1.0f, false ) *
        eng.PeakPowerOmega;
    const float powerToWeight = powerMax / mass;
    const float boostCeil =
        clampSpan( ( kPwrBoostHi - kPwrBoostLo ) *
                           ( powerToWeight - kPwrRefLo ) * kPwrRefScale +
                       kPwrBoostLo,
                   kPwrBoostLo, kPwrBoostHi );

    const int32_t helpLevel = vehicle.ElectronicIndex;
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

void PhysicsVehicle::forwardDrivingInput( const float deltaTime ) {
    // FUN_0081c7b0 (inlined)
    if ( pDrivingControl != nullptr && !gReplay.isPlayingBack() ) {
        pDrivingControl->forwardInput( *this, deltaTime );
    }
    if ( pSecondaryDrivingControl != nullptr ) {
        pSecondaryDrivingControl->forwardInput( *this, deltaTime );
    }
}

void PhysicsVehicle::updateReplayDriving() {
    // FUN_0081af70 / FUN_0081b820 (Driving record)
    ReplayPlayback &replay = replayPlayback;
    replay.bLatchedGearUp |= bGearUpRequest;
    replay.bLatchedGearDown |= bGearDownRequest;
    if ( replay.pInputSlot != nullptr ) {
        ReplayDriving driving = captureReplayDriving();
        driving.bGearUp = replay.bLatchedGearUp;
        driving.bGearDown = replay.bLatchedGearDown;
        driving.BackOnTrack = replay.bTeleported ? 1 : 0;
        driving.bBackView = 0;
        driving.bChangeView = 0;
        Replay::PackDriving( driving, replay.pInputSlot );
        replay.pInputSlot = nullptr;
        replay.bLatchedGearUp = false;
        replay.bLatchedGearDown = false;
        replay.bTeleported = false;
    }
    if ( replay.bDrivingOverride ) {
        applyReplayDriving( replay.Driving );
        replay.bDrivingOverride = false;
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
        if ( bVirtual ) {
            forwardDrivingInput( dt );
            updateReplayDriving();
            stepVirtual( dt );
        }
        return;
    }

    forwardDrivingInput( dt );
    updateReplayDriving();
    if ( bKeyframed ) {
        if ( drivingMode == DM_Parked ) {
            applyDrivingMode();
            arbitrateAutomaticGearbox( dt, 0.0f );
            StepPowertrain( vehicle, inputThrottle, inputBrake, inputHandbrake,
                            dt );
        }
        updateKeyframedWheels( dt );
        updateTyreHeat( dt );
        return;
    }

    applyDrivingMode();
    {
        const JPH::Vec3 v = chassis.GetLinearVelocity();
        arbitrateAutomaticGearbox(
            dt, JPH::Vec3( v.GetX(), 0.0f, v.GetZ() ).Length() );
    }

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
    {
        ElectronicsInput electronics;
        electronics.Speed = chassisVel.Length();
        electronics.Throttle = inputThrottle;
        electronics.FilteredSteer = filteredSteer;
        electronics.DeltaTime = dt;
        electronics.bSimuMode = gbSimuModeEnabled;
        electronics.bBike = bIsBike;
        StepElectronics( vehicle, electronics );
        vehicle.HandbrakeAbsInput = inputHandbrake;
        ApplyHandbrakeAbsOverride( vehicle, inputHandbrake );
    }
    if ( !gbSimuModeEnabled ) {
        updateGripModulation( chassisVel.Length() );
    }

    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        castWheel( system, chassis, i, dt );
    }
    updateWheelsActive();
    int numWheelsOnRoad = 0;
    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        if ( TDUSurface::isOnRoad( wheelSurface[i] ) ) {
            ++numWheelsOnRoad;
        }
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

    applyAntiRollBars();
    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        PhysicsWheelState &w = vehicle.Wheels[i];
        if ( w.bGroundContact ) {
            ComputeSuspensionForce( w );
        } else {
            w.NormalLoad = 0.0f;
            w.TireForceLong = 0.0f;
            w.TireForceLat = 0.0f;
            w.DragLong = 0.0f;
            w.DragLat = 0.0f;
            w.SuspForce.setZero();
        }
    }

    {
        VehicleSolveInput solveInput;
        solveInput.Throttle = inputThrottle;
        solveInput.Brake = inputBrake;
        solveInput.Handbrake = inputHandbrake;
        solveInput.bEspEnabled = gVehicleESPEnabled;
        StepVehicleSolve( vehicle, vehicleMatrix, solveInput, dt );
    }

    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        PhysicsWheelState &w = vehicle.Wheels[i];
        const bool hit = w.bGroundContact;

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

        if ( !hit ) {
            continue;
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
    if ( bIsBike ) {
        updateBikeRollLimit( chassis );
    }

    updateChrono( dt );
    updateOdometer( dt );
    updateTyreHeat( dt );
    updateRevRate( dt );
    updateGearLeverAnticipation( dt );
    applyImpactWheelDamage();
    reportStuckClutch( dt );

    virtualSpeed = vForward;
}

void PhysicsVehicle::setVirtualWheelContact( const JPH::Body &chassis,
                                             int i,
                                             float deltaTime ) {
    // FUN_00840640
    PhysicsWheelState &w = vehicle.Wheels[i];
    const JPH::RMat44 world = chassis.GetWorldTransform();
    const JPH::Vec3 up = world.GetColumn3( 1 );
    w.WheelUpAxis = ToVec4( up );

    const Eigen::Vector3f &a = wheelAttach[i];
    const JPH::RVec3 attachW = world * JPH::Vec3( a.x(), a.y(), a.z() );
    const JPH::Vec3 down = -up;
    const float suspRayLen = suspDroopStop[i] + w.Radius;
    const float hitDist = suspStaticLength[i] + w.Radius;
    const JPH::RVec3 hitPosition = attachW + hitDist * down;

    w.GroundProbePoint =
        Eigen::Vector4f( static_cast<float>( hitPosition.GetX() ),
                         static_cast<float>( hitPosition.GetY() ),
                         static_cast<float>( hitPosition.GetZ() ), 1.0f );
    const JPH::RVec3 tyreBottomW = attachW + suspRayLen * down;
    wheelGroundGap[i] =
        static_cast<float>( tyreBottomW.GetY() - hitPosition.GetY() );
    prevSuspLength[i] = suspStaticLength[i];
    prevGroundHeight[i] = hitDist;

    applyWheelContact( chassis, i, deltaTime, attachW, hitPosition,
                       JPH::Vec3::sAxisY(), hitDist, wheelSurface[i],
                       wheelMaterial[i] );
}

void PhysicsVehicle::stepVirtual( float dt ) {
    // FUN_00840640
    JPH::Body &chassis = *pChassisBody;
    const JPH::RMat44 world = chassis.GetWorldTransform();
    const JPH::Vec3 forward = world.GetColumn3( 2 );
    chassis.SetLinearVelocity( forward * virtualSpeed );
    chassis.SetAngularVelocity( JPH::Vec3::sZero() );
    for ( const AttachedPart &part : attachedParts ) {
        part.pBody->SetLinearVelocity( forward * virtualSpeed );
    }

    applyDrivingMode();
    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        vehicle.Wheels[i].bFreeRoll = false;
    }
    arbitrateAutomaticGearbox( dt, std::fabs( virtualSpeed ) );

    Matrix4x4f_t vehicleMatrix = Matrix4x4f_t::Identity();
    for ( int c = 0; c < 3; ++c ) {
        const JPH::Vec3 col = world.GetColumn3( c );
        vehicleMatrix( c, 0 ) = col.GetX();
        vehicleMatrix( c, 1 ) = col.GetY();
        vehicleMatrix( c, 2 ) = col.GetZ();
    }

    steerCommand = filterSteering( inputSteer, chassis, dt );
    vehicle.ForwardSpeed = virtualSpeed;
    vehicle.LateralSpeed = 0.0f;

    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        setVirtualWheelContact( chassis, i, dt );
    }
    vehicle.AnyWheelGrounded = true;
    applyAntiRollBars();
    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        ComputeSuspensionForce( vehicle.Wheels[i] );
    }

    {
        VehicleSolveInput solveInput;
        solveInput.Throttle = inputThrottle;
        solveInput.Brake = inputBrake;
        solveInput.Handbrake = inputHandbrake;
        solveInput.bEspEnabled = gVehicleESPEnabled;
        StepVehicleSolve( vehicle, vehicleMatrix, solveInput, dt );
    }

    float wheelLongForce = 0.0f;
    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        const PhysicsWheelState &w = vehicle.Wheels[i];
        if ( w.bTireSlipping ) {
            wheelLongForce += w.TireForceLong + w.DragLong;
        }
    }

    float bodyForceLong = 0.0f;
    if ( std::fabs( virtualSpeed ) > 1.0f ) {
        bodyForceLong = -aeroDragArea * gVehicleAeroDragScale * aeroDensity *
                        virtualSpeed * std::fabs( virtualSpeed );
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_0043c000 / wheel +0x2b4 (grounded
                             // test), assumed always grounded
    bWheelsActive = true;
    virtualSpeed = bWheelsActive
                       ? virtualSpeed + ( bodyForceLong + wheelLongForce ) /
                                            vehicle.Mass * dt
                       : 0.0f;

    updateChrono( dt );
    updateOdometer( dt );
    updateTyreHeat( dt );
    updateRevRate( dt );
    updateGearLeverAnticipation( dt );
    applyImpactWheelDamage();
    reportStuckClutch( dt );
}

void PhysicsVehicle::setVirtual( bool bInVirtual ) {
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return;
    }
    if ( bInVirtual ) {
        pauseSimulation();
        bVirtual = true;
    } else {
        resumeSimulation();
        bVirtual = false;
        bWheelsActive = true;
    }
}

void PhysicsVehicle::pauseSimulation() {
    // FUN_0082d4f0
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return;
    }
    JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
    const JPH::BodyID id = pChassisBody->GetID();
    if ( bi.IsAdded( id ) ) {
        bi.RemoveBody( id );
    }
    for ( const AttachedPart &part : attachedParts ) {
        if ( bi.IsAdded( part.pBody->GetID() ) ) {
            bi.RemoveBody( part.pBody->GetID() );
        }
    }
    // FUN_00828f30
    if ( pRollLimitConstraint != nullptr &&
         pRollLimitConstraint->GetEnabled() ) {
        pRollLimitConstraint->SetEnabled( false );
    }
    bPausePhysics = true;
}

void PhysicsVehicle::resumeSimulation() {
    // FUN_0082d460
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return;
    }
    JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
    const JPH::BodyID id = pChassisBody->GetID();
    if ( !bi.IsAdded( id ) ) {
        bi.AddBody( id, JPH::EActivation::Activate );
    }
    for ( const AttachedPart &part : attachedParts ) {
        if ( !bi.IsAdded( part.pBody->GetID() ) ) {
            bi.AddBody( part.pBody->GetID(), JPH::EActivation::Activate );
        }
    }
    // FUN_00828ed0
    if ( pRollLimitConstraint != nullptr &&
         !pRollLimitConstraint->GetEnabled() ) {
        pRollLimitConstraint->SetEnabled( true );
    }
    bPausePhysics = false;
}

void PhysicsVehicle::setChassisPose( const JPH::RMat44 &world,
                                     const bool bActivate ) {
    // FUN_00455090 (mode 2)
    gpPhysics->moveVehicleParts( *this, world, bActivate );
    pPhysicsSystem->GetBodyInterface().SetPositionAndRotation(
        pChassisBody->GetID(), world.GetTranslation(),
        world.GetQuaternion().Normalized(),
        ( bActivate &&
          pPhysicsSystem->GetBodyInterface().IsAdded( pChassisBody->GetID() ) )
            ? JPH::EActivation::Activate
            : JPH::EActivation::DontActivate );
}

void PhysicsVehicle::setPostponedMatrix( const Matrix4x4f_t &worldTransform ) {
    // FUN_0083a1a0
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return;
    }

    JPH::Mat44 world = JPH::Mat44::sIdentity();
    for ( int c = 0; c < 4; ++c ) {
        world.SetColumn3( c, JPH::Vec3( worldTransform( c, 0 ),
                                        worldTransform( c, 1 ),
                                        worldTransform( c, 2 ) ) );
    }
    const Eigen::Vector4f origin = GSPhysics::GetShiftOrigin();
    world.SetTranslation( world.GetTranslation() -
                          JPH::Vec3( origin.x(), origin.y(), origin.z() ) );

    setChassisPose( world, false );
}

float PhysicsVehicle::computeGroundToOriginOffset() const {
    float offset = 0.0f;
    for ( int32_t i = 0; i < vehicle.NumWheels; i++ ) {
        offset += vehicle.Wheels[i].Radius -
                  ( wheelAttach[i].y() - suspStaticLength[i] );
    }
    return ( vehicle.NumWheels > 0 )
               ? offset / static_cast<float>( vehicle.NumWheels )
               : 0.0f;
}

void PhysicsVehicle::updateTyreHeat( float dt ) {
    // FUN_0083db70
    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        const PhysicsWheelState &w = vehicle.Wheels[i];
        const float slip = std::fabs( w.NormLongClamped );
        const bool bRolling = kTireHeatMinSpeed < w.ContactVelLong ||
                              kTireHeatMinOmega < w.AngularVel;

        float heat;
        if ( kTireHeatMinSlip < slip && bRolling ) {
            heat = kTireHeatRate * slip * dt + wheelTyreHeat[i];
        } else {
            heat = wheelTyreHeat[i] - kTireCoolRate * dt;
        }
        wheelTyreHeat[i] = JPH::Clamp( heat, 0.0f, 1.0f );
    }
}

float PhysicsVehicle::getWheelBurn( int32_t index ) const {
    // FUN_00828850
    if ( index < 0 || vehicle.NumWheels < index ||
         PhysicsVehicleState::kMaxWheels <= index ) {
        return 0.0f;
    }
    const float heat = wheelTyreHeat[index];
    if ( heat <= kTireBurnThreshold ) {
        return 0.0f;
    }
    return ( heat - kTireBurnThreshold ) / ( 1.0f - kTireBurnThreshold );
}

void PhysicsVehicle::pushCollisionEvent( const CollisionEvent &event ) {
    // FUN_00827c80
    ScopedMutexLock lock( &gCollisionEventMutex );
    if ( numCollisionEvents >= kMaxCollisionEvents ) {
        return;
    }
    if ( event.Energy > vehicle.Mass * kHardImpactEnergyPerMass ) {
        timeSinceHardImpact = 0.0f;
    }
    collisionEvents[numCollisionEvents] = event;
    numCollisionEvents++;
}

float PhysicsVehicle::getMaxCollisionEnergy() const {
    ScopedMutexLock lock( &gCollisionEventMutex );
    float maxEnergy = 0.0f;
    for ( int32_t i = 0; i < numCollisionEvents; i++ ) {
        maxEnergy = std::max( maxEnergy, collisionEvents[i].Energy );
    }
    return maxEnergy;
}

void PhysicsVehicle::flushCollisionEvents(
    std::vector<CollisionEvent> &outEvents ) {
    // FUN_0083bbd0
    applyImpactWheelDamage();
    ScopedMutexLock lock( &gCollisionEventMutex );
    outEvents.assign( collisionEvents.begin(),
                      collisionEvents.begin() + numCollisionEvents );
    numCollisionEvents = 0;
}

float PhysicsVehicle::getCameraDomeHeight(
    const Eigen::Vector4f &shiftedPosition,
    const float lengthFactor,
    const float widthFactor,
    const float heightFactor ) const {
    // FUN_0082d990
    const Eigen::Vector4f local =
        TDUMat4x4MulVec( invChassisMatrix, shiftedPosition );
    const float halfLength = lengthFactor * bodyDimensions.z();
    const float halfWidth = bodyDimensions.x() * widthFactor;
    const float baseHeight = rideHeightFront;

    if ( std::abs( local.z() ) >= halfLength ||
         std::abs( local.x() ) >= halfWidth ) {
        return kNoSurfaceSentinel;
    }

    const float angleX = Clamp(
        ( ( local.x() + halfWidth ) * kTwoPI ) / ( halfWidth * 2.0f ) - kPI,
        kNegPI, kPI );
    const float angleZ = Clamp(
        ( ( local.z() + halfLength ) * kTwoPI ) / ( halfLength * 2.0f ) - kPI,
        kNegPI, kPI );

    return ( ( bodyHeight * heightFactor + baseHeight ) *
                 ( std::cos( angleX ) + 1.0f ) * ( std::cos( angleZ ) + 1.0f ) *
                 0.25f -
             bodyHeight * 0.5f - baseHeight ) +
           physicsMatrix( 3, 1 );
}

void PhysicsVehicle::requestPhantomMode( uint8_t mode, float duration ) {
    // FUN_00828780
    PhysicsLock lock;
    phantomRequest = mode;
    phantomRequestDuration = duration;
}

void PhysicsVehicle::applyPhantomRequest() {
    // FUN_0083fcb0
    if ( phantomRequest == 0 || bPausePhysics || pPhysicsSystem == nullptr ) {
        return;
    }
    const JPH::ObjectLayer layer = ( phantomRequest == 1 )
                                       ? TDUPhysLayers::VEHICLE
                                       : TDUPhysLayers::VEHICLE_GHOST;
    if ( phantomRequest == 1 ) {
        bPhantomFadeIn =
            static_cast<float>( phantomMode ) != phantomRequestDuration;
    }
    pPhysicsSystem->GetBodyInterface().SetObjectLayer( pChassisBody->GetID(),
                                                       layer );
    phantomMode = phantomRequest;
    phantomDuration = phantomRequestDuration;
    phantomTimer = 0.0f;
    phantomRequest = 0;
}

static bool IsInsideVehicleBox( const PhysicsVehicle &sampled,
                                const PhysicsVehicle &box ) {
    // FUN_0083db70
    static constexpr float kSampleX[9] = {
        1.0f,  1.0f,  1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f, 0.0f }; // DAT_00fab644
    static constexpr float kSampleY[9] = {
        1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,
        1.0f, -1.0f, -1.0f, 0.0f }; // DAT_00fab620
    static constexpr float kSampleZ[9] = {
        1.0f,  -1.0f, 1.0f,  -1.0f, 1.0f,
        -1.0f, 1.0f,  -1.0f, 0.0f };          // DAT_00fab5fc
    static constexpr float kBoxScale = 0.75f; // DAT_00f416a4

    const Eigen::Vector3f &sampledSize = sampled.getBodyDimensions();
    const Eigen::Vector3f &boxSize = box.getBodyDimensions();
    for ( int32_t i = 0; i < 9; i++ ) {
        const Eigen::Vector4f local( kSampleX[i] * sampledSize.x(),
                                     kSampleY[i] * sampledSize.y(),
                                     kSampleZ[i] * sampledSize.z(), 1.0f );
        const Eigen::Vector4f world =
            ( local.transpose() * sampled.getPhysicsMatrix() ).transpose();
        const Eigen::Vector4f inBox =
            ( world.transpose() * box.getInvChassisMatrix() ).transpose();
        if ( std::fabs( inBox.x() ) < boxSize.x() * kBoxScale &&
             std::fabs( inBox.y() ) < boxSize.y() &&
             std::fabs( inBox.z() ) < boxSize.z() * kBoxScale ) {
            return true;
        }
    }
    return false;
}

bool PhysicsVehicle::updatePhantomAlpha( float dt,
                                         bool bForceOpaque,
                                         float &outAlpha ) {
    // FUN_0083bbd0
    static constexpr float kFadeOutRate = 5.0f;    // DAT_00f41750
    static constexpr float kGhostAlpha = 0.5f;     // FLOAT_00f41674
    static constexpr float kMinDuration = 0.0001f; // DAT_00f41760

    switch ( phantomMode ) {
    case 2:
        outAlpha = bForceOpaque ? 1.0f
                                : Clamp( 1.0f - phantomTimer * kFadeOutRate,
                                         kGhostAlpha, 1.0f );
        return true;
    case 4:
        outAlpha = bForceOpaque ? 1.0f : kGhostAlpha;
        return true;
    case 1: {
        if ( !bPhantomFadeIn ) {
            return false;
        }
        float alpha = 1.0f;
        if ( kMinDuration < phantomDuration ) {
            alpha = Clamp( phantomTimer / phantomDuration * kGhostAlpha +
                               kGhostAlpha,
                           kGhostAlpha, 1.0f );
        }
        outAlpha = bForceOpaque ? 1.0f : alpha;
        phantomTimer += dt;
        if ( alpha >= 1.0f ) {
            phantomTimer = 0.0f;
            phantomDuration = 0.0f;
            bPhantomFadeIn = false;
        }
        return true;
    }
    default:
        return false;
    }
}

void PhysicsVehicle::updatePhantomMode( float dt ) {
    // FUN_0083db70
    static constexpr float kGhostTime = 2.0f;       // DAT_00f416b0
    static constexpr float kRestoreDuration = 0.5f; // FLOAT_00f41674

    bool bOverlapping = false;
    for ( PhysicsVehicle *it = gpPhysics->getVehiclesBegin();
          it != gpPhysics->getVehiclesEnd(); ++it ) {
        if ( it == this || !it->isActive() || it->bPausePhysics ) {
            continue;
        }
        if ( IsInsideVehicleBox( *it, *this ) ||
             IsInsideVehicleBox( *this, *it ) ) {
            bOverlapping = true;
            break;
        }
    }

    phantomTimer += dt;
    if ( phantomMode == 2 ) {
        if ( kGhostTime < phantomTimer ) {
            phantomMode = 4;
            phantomTimer = 0.0f;
        }
    } else if ( phantomMode == 4 ) {
        if ( bOverlapping ) {
            phantomTimer = 0.0f;
        }
        if ( phantomDuration < phantomTimer ) {
            requestPhantomMode( 1, kRestoreDuration );
        }
    }
}

bool PhysicsVehicle::isInWater() const {
    // FUN_00829a80
    static constexpr float kSunkHeight = -2.0f; // DAT_00f417d0
    const Matrix4x4f_t world = getChassisWorldMatrix();
    for ( int32_t i = 0; i < vehicle.NumWheels; i++ ) {
        const Eigen::Vector4f attach( wheelAttach[i].x(), wheelAttach[i].y(),
                                      wheelAttach[i].z(), 1.0f );
        const Eigen::Vector4f position =
            ( attach.transpose() * world ).transpose();
        if ( position.y() - vehicle.Wheels[i].Radius < kSunkHeight ||
             wheelMaterial[i] ==
                 static_cast<uint32_t>( eMaterialType::MT_Water ) ) {
            return true;
        }
    }
    return false;
}

void PhysicsVehicle::applyImpactWheelDamage() {
    // FUN_0083db70
    static constexpr float kImpactEnergy = 2000000.0f; // DAT_00fab690
    static constexpr float kImpactDamage = 0.35f;      // DAT_00f419fc
    static constexpr float kImpactSteerOffset = 0.25f; // DAT_00f416dc

    bool bStaticBehaviour = false;
    if ( gpCarState != nullptr ) {
        const CarState *pCarState = gpCarState->findCarState( this );
        if ( pCarState != nullptr && pCarState->getAIController() != nullptr ) {
            const AiVehicle *pAiVehicle =
                static_cast<const AiVehicle *>( pCarState->getAIController() );
            bStaticBehaviour = pAiVehicle->getBehaviourType() == 0;
        }
    }
    if ( !bTrafficBody || bStaticBehaviour ) {
        return;
    }

    ScopedMutexLock lock( &gCollisionEventMutex );
    for ( int32_t i = 0; i < numCollisionEvents; i++ ) {
        const CollisionEvent &event = collisionEvents[i];
        if ( event.Energy <= kImpactEnergy || event.pOtherObject == nullptr ) {
            continue;
        }
        const int32_t rear = ( event.ContactPosition.z() > 0.0f ) ? 0 : 1;
        const int32_t right = ( event.ContactPosition.x() > 0.0f ) ? 0 : 1;
        const int32_t index = ( rear << ( vehicle.NumWheels / 2 - 1 ) ) +
                              ( vehicle.NumWheels / 4 ) * right;
        PhysicsWheelState &w = vehicle.Wheels[index];
        w.ImpactDamage = kImpactDamage;
        w.ImpactSteerOffset = kImpactSteerOffset;
    }
}

void PhysicsVehicle::updateGearLeverAnticipation( float dt ) {
    // FUN_0083db70
    static constexpr float kThrottleGate = 0.1f;      // DAT_00f41668
    static constexpr float kOnWheel = 10.0f;          // DAT_00f4166c
    static constexpr float kQuarterTurn = 1.5707964f; // DAT_00f418a8
    static constexpr float kBandFirstGear = 0.3f;     // DAT_00f416bc
    static constexpr float kBandPerGear = 0.075f;     // DAT_00f42128
    static constexpr float kSecondGearBand = 1.0f;    // DAT_00f8a280
    static constexpr float kRateGate = 1.0f;          // FLOAT_00f4165c

    if ( dt <= 0.0f ) {
        return;
    }

    const PhysicsEngineSim &e = vehicle.Engine;
    const PhysicsGearboxSim &g = vehicle.GearBox;

    const float omega = e.Omega;
    const float rate = ( omega - gearLeverPrevOmega ) / dt;
    gearLeverPrevOmega = omega;

    const int32_t gear = g.CurrentGear;
    const float band = JPH::Clamp(
        kBandFirstGear - static_cast<float>( gear - 2 ) * kBandPerGear,
        kBandPerGear, kBandFirstGear );
    const float margin = band * e.OmegaMax;
    const bool bInGear = ( g.ShiftState == PhysicsGearboxSim::SS_InGear );

    switch ( gearLeverState ) {
    case 0:
        gearLeverAnticipation = kOnWheel;
        gearLeverState = ( gear >= 2 ) ? 1 : 0;
        break;

    case 1: {
        if ( e.Throttle <= kThrottleGate ) {
            gearLeverState = 2;
            break;
        }
        if ( !bInGear ) {
            gearLeverState = 3;
            break;
        }
        if ( rate <= kRateGate ) {
            break;
        }
        if ( gear >= g.NumGears - 1 ) {
            gearLeverAnticipation = kOnWheel;
            break;
        }
        const float shiftPoint = ( 1.0f - g.UpshiftBand ) * e.PeakPowerOmega +
                                 g.UpshiftBand * e.OmegaMax;
        const float shiftStart = shiftPoint - margin;
        gearLeverAnticipation =
            JPH::Clamp( kOnWheel + ( omega - shiftStart ) * kQuarterTurn /
                                       ( shiftPoint - shiftStart ),
                        0.0f, kOnWheel );
        break;
    }

    case 2: {
        if ( e.Throttle > kThrottleGate ) {
            gearLeverState = 1;
        }
        if ( gear <= 2 ) {
            gearLeverState = 0;
            break;
        }
        if ( rate == 0.0f ) {
            gearLeverState = 0;
            break;
        }
        if ( !bInGear ) {
            gearLeverState = 3;
            break;
        }
        float postShiftOmega;
        if ( gear == 3 ) {
            postShiftOmega = kSecondGearBand * e.OmegaMax;
        } else {
            postShiftOmega = ( g.Ratios[gear] / g.Ratios[gear - 1] ) *
                             ( e.PeakTorqueOmega + e.OmegaMax ) * 0.5f;
        }
        const float target = postShiftOmega + margin;
        gearLeverAnticipation =
            JPH::Clamp( kOnWheel + ( omega - target ) * kQuarterTurn /
                                       ( postShiftOmega - target ),
                        0.0f, kOnWheel );
        break;
    }

    case 3:
        gearLeverAnticipation = 0.0f;
        if ( bInGear ) {
            gearLeverState = 0;
        }
        break;

    default:
        break;
    }
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

void PhysicsVehicle::resetChrono() {
    // FUN_00827ea0
    chrono.StartDistance = odometerDistance;
    chrono.Timer = -1.0f;
    chrono.T100kmh = -1.0f;
    chrono.T200kmh = -1.0f;
    chrono.T1000m = -1.0f;
    chrono.T400m = -1.0f;
    chrono.T60mph = -1.0f;
    chrono.TQuarterMile = -1.0f;
    chrono.Brake100kmhDistance = 0.0f;
    chrono.Brake80mphDistance = 0.0f;
    chrono.Brake70mphDistance = 0.0f;
    chrono.Brake60mphDistance = 0.0f;
    chrono.AccelState = CAS_Off;
    chrono.BrakeState = CBS_Off;
}

void PhysicsVehicle::armChrono() {
    // FUN_00827fb0
    chrono.Timer = -1.0f;
    chrono.T100kmh = -1.0f;
    chrono.T200kmh = -1.0f;
    chrono.T1000m = -1.0f;
    chrono.T400m = -1.0f;
    chrono.T60mph = -1.0f;
    chrono.TQuarterMile = -1.0f;
    chrono.Brake100kmhDistance = 0.0f;
    chrono.Brake80mphDistance = 0.0f;
    chrono.Brake70mphDistance = 0.0f;
    chrono.Brake60mphDistance = 0.0f;
    chrono.StartDistance = odometerDistance;
    chrono.AccelState = CAS_StopFirst;

    chrono.MaxDrivenSlip = 0.0f;
    chrono.MaxOmega = 0.0f;
    chrono.SlipIntegral = 0.0f;
    chrono.LimiterTime = 0.0f;
}

void PhysicsVehicle::armBrakeChrono( float targetSpeedMph ) {
    // FUN_00827f20
    chrono.Timer = -1.0f;
    chrono.T100kmh = -1.0f;
    chrono.T200kmh = -1.0f;
    chrono.T1000m = -1.0f;
    chrono.T400m = -1.0f;
    chrono.T60mph = -1.0f;
    chrono.TQuarterMile = -1.0f;
    chrono.Brake100kmhDistance = 0.0f;
    chrono.Brake80mphDistance = 0.0f;
    chrono.Brake70mphDistance = 0.0f;
    chrono.Brake60mphDistance = 0.0f;
    chrono.StartDistance = odometerDistance;
    chrono.BrakeState = CBS_WaitForSpeed;
    chrono.BrakeTargetSpeed = targetSpeedMph;
    chrono.PreviousSpeed = 0.0f;
}

void PhysicsVehicle::updateOdometer( float dt ) {
    // FUN_0083db70
    const float speed = ( pChassisBody != nullptr )
                            ? pChassisBody->GetLinearVelocity().Length()
                            : 0.0f;
    odometerDistance += speed * dt;

    odometerKms = static_cast<int32_t>( odometerDistance * 0.001f );
    odometerTenMeters = static_cast<int32_t>(
        ( odometerDistance - static_cast<float>( odometerKms ) * 1000.0f ) *
        0.1f );
}

void PhysicsVehicle::updateChrono( float dt ) {
    // FUN_0083db70
    if ( gVehicleStartChronopack ) {
        gVehicleStartChronopack = false;
        armChrono();
        armBrakeChrono( 60.0f );
    }
    const float speed = ( pChassisBody != nullptr )
                            ? pChassisBody->GetLinearVelocity().Length()
                            : 0.0f;
    Chrono &c = chrono;

    if ( c.AccelState == CAS_StopFirst && speed < kChronoMoveSpeed ) {
        c.AccelState = CAS_Ready;
    }
    if ( c.AccelState == CAS_Ready && speed > kChronoMoveSpeed ) {
        c.AccelState = CAS_Running;
        c.Timer = 0.0f;
        c.StartDistance = odometerDistance;
    }

    if ( c.AccelState == CAS_Running || c.BrakeState == CBS_Braking ) {
        c.Timer += dt;
    }

    if ( c.AccelState == CAS_Running ) {
        updateChronoDiagnostics( speed, dt );

        const float dist = odometerDistance - c.StartDistance;
        auto latch = [&]( bool bReached, float &split, const char *pLabel ) {
            if ( bReached && split < 0.0f ) {
                split = c.Timer;
                logChronoSplit( pLabel, split );
            }
        };
        latch( speed > kChrono100kmh, c.T100kmh, "0 to 100 km/h" );
        latch( speed > kChrono200kmh, c.T200kmh, "0 to 200 km/h" );
        latch( dist > kChrono1000m, c.T1000m, "0 to 1000 m" );
        latch( dist > kChrono400m, c.T400m, "0 to 400 m" );
        latch( dist * kMetresToMiles > kChronoQuarterMile, c.TQuarterMile,
               "0 to 1/4 mile" );
        latch( speed > kChrono60mph, c.T60mph, "0 to 60 mph" );
    }

    if ( c.BrakeState == CBS_WaitForSpeed ) {
        const float targetSpeed = c.BrakeTargetSpeed * kChronoMphToMs;
        if ( c.PreviousSpeed > targetSpeed && targetSpeed > speed &&
             inputBrake > 0.0f ) {
            c.BrakeState = CBS_Braking;
        }
        c.PreviousSpeed = speed;
    }

    if ( c.BrakeState == CBS_Braking ) {
        const float distanceFeet = speed * dt * kMetresToFeet;
        if ( speed < kChrono100kmh && speed > kChronoMoveSpeed ) {
            c.Brake100kmhDistance += distanceFeet;
        }
        if ( speed < kChrono80mph && speed > kChronoMoveSpeed ) {
            c.Brake80mphDistance += distanceFeet;
        }
        if ( speed < kChrono70mph && speed > kChronoMoveSpeed ) {
            c.Brake70mphDistance += distanceFeet;
        }
        if ( speed < kChrono60mphBrake && speed > kChronoMoveSpeed ) {
            c.Brake60mphDistance += distanceFeet;
        }
        if ( speed < kChronoMoveSpeed ) {
            c.BrakeState = CBS_Off;
            OTDU_LOG_INFO( "Chronopack: braking 100kmh/80mph/70mph/60mph to 0 "
                           "= %.2f/%.2f/%.2f/%.2f ft\n",
                           c.Brake100kmhDistance, c.Brake80mphDistance,
                           c.Brake70mphDistance, c.Brake60mphDistance );
        }
    }
}

void PhysicsVehicle::updateChronoDiagnostics( float speed, float dt ) {
    // Is the engine driving the road, or just spinning the tyres?
    Chrono &c = chrono;
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
}

void PhysicsVehicle::logChronoSplit( const char *pLabel, float split ) const {
    const Chrono &c = chrono;
    const float inv = ( split > 0.0f ) ? ( 1.0f / split ) : 0.0f;
    OTDU_LOG_INFO( "Chronopack: %s = %.2f s (gear %d, %.0f rpm, peak slip %.1f "
                   "m/s, peak %.0f rpm, mean slip %.1f m/s, limiter %.0f%%)\n",
                   pLabel, split, vehicle.GearBox.CurrentGear - 1,
                   vehicle.Engine.Omega * kOmegaToRpm, c.MaxDrivenSlip,
                   c.MaxOmega * kOmegaToRpm, c.SlipIntegral * inv,
                   c.LimiterTime * inv * 100.0f );
}

void PhysicsVehicle::setDrivingHelpLevel( int32_t level ) {
    // FUN_008287d0
    const int32_t maxElectronicParams = pConfig->NumElectronicParamsValues;
    if ( level >= 0 && level < maxElectronicParams ) {
        vehicle.bElectronicsEnabled = true;
        vehicle.ElectronicMode = 1;
        vehicle.ElectronicIndex = level;
    } else {
        vehicle.bElectronicsEnabled = false;
        vehicle.ElectronicMode = 0;
        vehicle.ElectronicIndex = -1;
    }
    appliedDrivingHelpLevel = level;
}

void PhysicsVehicle::initializeElectronics() {
    // FUN_008345e0
    static constexpr int32_t kMaxElectronicValues = 9;
    static constexpr float kSimuElectronicScale = 0.4f; // DAT_00f4175c

    const PhysicsConfig &c = *pConfig;
    for ( int32_t i = 0; i < kMaxElectronicValues; i++ ) {
        vehicle.ElectronicValues[i] = c.ElectronicParamsValue[i];
    }
    if ( gbSimuModeEnabled ) {
        for ( int32_t i = 0; i < 3; i++ ) {
            vehicle.ElectronicValues[i] *= kSimuElectronicScale;
        }
    }

    int32_t defaultLevel = kMaxElectronicValues;
    for ( int32_t i = 0; i < kMaxElectronicValues; i++ ) {
        if ( strcmp( c.ElectronicParams, c.ElectronicParamsValueName[i] ) ==
             0 ) {
            defaultLevel = i;
            break;
        }
    }
    vehicle.ElectronicIndex = defaultLevel;
    vehicle.bElectronicsEnabled = true;
    vehicle.ElectronicMode = ( bIsAI || bIsNetwork ) ? 2 : 1;
    appliedDrivingHelpLevel = -2;
}

void PhysicsVehicle::tick( float deltaTime ) {
    if ( pChassisBody == nullptr ) {
        return;
    }

    // FUN_0083fcb0 @0x0083fdb6
    if ( postponedPause == 1 ) {
        pauseSimulation();
    } else if ( postponedPause == 2 ) {
        resumeSimulation();
    }
    postponedPause = 0;
    applyPhantomRequest();
    if ( isActive() && !bPausePhysics ) {
        updatePhantomMode( deltaTime );
    }
    // FUN_0083db70 (inlined)
    if ( !bPausePhysics && !bHasBrokenPart && !attachedParts.empty() ) {
        bHasBrokenPart = gpPhysics->hasBrokenVehiclePart( *this );
    }

    if ( gVehicleDrivingHelpOverride >= 0 && !bIsAI && !bIsNetwork &&
         gVehicleDrivingHelpOverride != appliedDrivingHelpLevel ) {
        setDrivingHelpLevel( gVehicleDrivingHelpOverride );
    }

    // FUN_0083db70 @0x0083ec2a
    if ( pPhysicsSystem != nullptr && deltaTime > 1.0e-5f && !bPausePhysics ) {
        JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
        JPH::Vec3 velocity = bi.GetLinearVelocity( pChassisBody->GetID() );
        if ( velocity.GetY() > kGMeterVerticalVelMax ) {
            velocity.SetY( kGMeterVerticalVelReset );
            setVelocity( Eigen::Vector3f( velocity.GetX(), velocity.GetY(),
                                          velocity.GetZ() ),
                         false, -1 );
        }

        const JPH::Vec3 prev( prevWorldVelocity[0], prevWorldVelocity[1],
                              prevWorldVelocity[2] );
        const JPH::Vec3 worldAccel = ( velocity - prev ) * ( 1.0f / deltaTime );
        const JPH::Quat rot = bi.GetRotation( pChassisBody->GetID() );
        const JPH::Vec3 localAccel = rot.Conjugated() * worldAccel;
        worldAcceleration = Eigen::Vector4f(
            worldAccel.GetX(), worldAccel.GetY(), worldAccel.GetZ(), 0.0f );
        localAcceleration = Eigen::Vector4f(
            localAccel.GetX(), localAccel.GetY(), localAccel.GetZ(), 0.0f );
        localAccelerationG.head<3>() =
            localAcceleration.head<3>() * kInvGravityAccel;

        const JPH::Vec3 current = bi.GetLinearVelocity( pChassisBody->GetID() );
        prevWorldVelocity[0] = current.GetX();
        prevWorldVelocity[1] = current.GetY();
        prevWorldVelocity[2] = current.GetZ();
    }

    timeSinceHardImpact += deltaTime;

    if ( bPausePhysics ) {
        return;
    }

    updateCollisionInertia( deltaTime );

    updateLandingImpact();

    if ( !bKeyframed ) {
        updateBikeLean( deltaTime );
        if ( bIsBike ) {
            updateBikeCollision();
        }
    }
    updateWheelCollision();
    bBikeRiderOnGround = bBikeRiderGroundContact.exchange( false );
    if ( bIsBike ) {
        updateBikeRider();
    }
}

bool PhysicsVehicle::isAirborne() const {
    // FUN_008282c0
    for ( int32_t i = 0; i < vehicle.NumWheels; i++ ) {
        if ( vehicle.Wheels[i].bGroundContact ) {
            return false;
        }
    }

    return true;
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

void PhysicsVehicle::publishStepPose( const bool bReplayStep ) {
    // FUN_0081dfd0
    physicsMatrix = ( bKeyframed && bForceKeyframeMatrix )
                        ? keyframeMatrix
                        : getChassisShiftedMatrix();

    ReplayPlayback &replay = replayPlayback;
    if ( gReplay.isPlayingBack() && bInInstantReplay ) {
        if ( bReplayStep ) {
            updateReplayPose();
        }
    } else {
        if ( bReplayStep ) {
            replay.Elapsed = 1.0f;
            replay.BlendStart = 0.0f;
            replay.Period = 0.0f;
        }
        replay.SourceMatrix = physicsMatrix;
    }

    displayMatrix = computeDisplayMatrix( replay.SourceMatrix );
    invChassisMatrix = OrthoInvert( physicsMatrix );
    publishBikeRider();
}

void PhysicsVehicle::updateReplayPose() {
    // FUN_0081dfd0 (inlined)
    static constexpr float kMinBlendPeriod = 0.99f; // DAT_00f41a44

    ReplayPlayback &replay = replayPlayback;
    replay.Mode = ( replay.PoseFlags[1] == 1 ) ? 3 : 2;
    if ( replay.PoseFlags[0] == 2 || replay.PoseFlags[1] == 2 ||
         !gReplay.isCursorBeforeLastPose() ||
         replay.Poses[0].row( 3 ).norm() < 1.0f ||
         replay.Poses[1].row( 3 ).norm() < 1.0f ) {
        replay.Mode = 1;
    }
    if ( gReplayModeDebugIndex != 0 ) {
        replay.Mode = gReplayModeDebugIndex;
    }

    const float curveT =
        std::clamp( replay.Elapsed / replay.Period, 0.0f, 1.0f );
    if ( replay.Mode == 2 ) {
        if ( replay.Elapsed < replay.BlendStart ||
             replay.Period <= kMinBlendPeriod ) {
            replay.SourceMatrix = physicsMatrix;
        } else {
            const float blendS =
                std::clamp( ( replay.Elapsed - replay.BlendStart ) /
                                ( ( replay.Period - gReplay.getTimeScale() ) -
                                  replay.BlendStart ),
                            0.0f, 1.0f );
            blendReplayPose( curveT, blendS );
        }
    }
    if ( replay.Mode == 3 ) {
        blendReplayPose( curveT, 0.0f );
        physicsMatrix = replay.SourceMatrix;
        JPH::RMat44 world = JPH::RMat44::sIdentity();
        for ( int32_t c = 0; c < 4; c++ ) {
            world.SetColumn3( c, JPH::Vec3( physicsMatrix( c, 0 ),
                                            physicsMatrix( c, 1 ),
                                            physicsMatrix( c, 2 ) ) );
        }
        setChassisPose( world, false );
    }
    if ( replay.Mode == 1 ) {
        replay.SourceMatrix = physicsMatrix;
    }

    if ( gReplay.getSlowDownFactor() < 2 ) {
        replay.Elapsed += 1.0f;
    } else {
        replay.Elapsed += gReplay.getTimeScale();
    }
}

static bool ProbeGroundUnder( const Eigen::Vector4f &point,
                              Eigen::Vector4f &outPoint,
                              Eigen::Vector4f &outNormal ) {
    // FUN_00834410
    outPoint = point;
    outNormal = Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f );
    if ( gpPhysics == nullptr ) {
        return false;
    }

    Eigen::Vector4f probe = point;
    probe.y() += 1.0f; // FLOAT_00f4165c

    float height = kNoSurfaceSentinel;
    gpPhysics->probeSurface( probe, false, height, outNormal, true );
    if ( height == kNoSurfaceSentinel ) {
        outNormal = Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f );
        return false;
    }

    const Eigen::Vector4f onSurface( point.x(), height, point.z(), 1.0f );
    outPoint = point - outNormal * outNormal.dot( point - onSurface );
    return true;
}

bool PhysicsVehicle::castToGround( const Matrix4x4f_t &source,
                                   Matrix4x4f_t &outTransform ) const {
    // FUN_00839920
    outTransform = source;

    const uint32_t numWheels = getNumWheels();
    if ( numWheels < 2 || numWheels > PhysicsVehicleState::kMaxWheels ) {
        return false;
    }

    Eigen::Vector4f contact[PhysicsVehicleState::kMaxWheels];
    Eigen::Vector4f normal[PhysicsVehicleState::kMaxWheels];
    int32_t numGrounded = 0;
    for ( uint32_t i = 0; i < numWheels; i++ ) {
        const Eigen::Vector3f &attach = wheelAttach[i];
        Eigen::Vector4f anchor( 0.0f, 0.0f, 0.0f, 1.0f );
        for ( int32_t c = 0; c < 3; c++ ) {
            anchor[c] = source( 0, c ) * attach.x() +
                        source( 1, c ) * attach.y() +
                        source( 2, c ) * attach.z() + source( 3, c );
        }
        numGrounded +=
            ProbeGroundUnder( anchor, contact[i], normal[i] ) ? 1 : 0;
    }

    if ( numGrounded == 0 ) {
        return false;
    }

    Eigen::Vector3f bodyCentre = Eigen::Vector3f::Zero();
    for ( uint32_t i = 0; i < numWheels; i++ ) {
        bodyCentre += Eigen::Vector3f( wheelAttach[i].x(),
                                       wheelAttach[i].y() - suspStaticLength[i],
                                       wheelAttach[i].z() );
    }
    bodyCentre /= static_cast<float>( numWheels );

    Eigen::Vector4f side;
    Eigen::Vector4f up;
    Eigen::Vector4f forward;
    Eigen::Vector4f centre;

    if ( numWheels == 4 ) {
        Eigen::Vector4f wheel[4];
        for ( uint32_t i = 0; i < 4; i++ ) {
            wheel[i] = contact[i] + normal[i] * vehicle.Wheels[i].Radius;
        }

        const Eigen::Vector4f axis = Normalize3(
            ( wheel[0] + wheel[1] ) * 0.5f - ( wheel[2] + wheel[3] ) * 0.5f );
        const Eigen::Vector4f lateral = Normalize3(
            ( wheel[0] + wheel[2] ) * 0.5f - ( wheel[1] + wheel[3] ) * 0.5f );

        up = Normalize3( Cross3( axis, lateral ) );
        side = Normalize3( Cross3( up, axis ) );
        forward = Normalize3( Cross3( side, up ) );
        centre = ( wheel[0] + wheel[1] + wheel[2] + wheel[3] ) * 0.25f;
    } else {
        if ( numGrounded < 2 ) {
            return true;
        }

        const Eigen::Vector4f axis = Normalize3( contact[0] - contact[1] );
        const Eigen::Vector4f lateral = Normalize3( source.row( 0 ) );

        up = Normalize3( Cross3( axis, lateral ) );
        side = Normalize3( Cross3( up, axis ) );
        forward = Normalize3( Cross3( side, up ) );
        centre =
            ( contact[0] + contact[1] ) * 0.5f + up * vehicle.Wheels[0].Radius;
    }

    const Eigen::Vector4f origin = centre - side * bodyCentre.x() -
                                   up * bodyCentre.y() -
                                   forward * bodyCentre.z();

    outTransform.row( 0 ) = side;
    outTransform.row( 1 ) = up;
    outTransform.row( 2 ) = forward;
    outTransform.row( 3 ) << origin.x(), origin.y(), origin.z(), 1.0f;
    return true;
}

void PhysicsVehicle::resetSimulationState() {
    // FUN_00439a10
    for ( int32_t i = 0; i < vehicle.NumWheels; i++ ) {
        PhysicsWheelState &wheel = vehicle.Wheels[i];
        wheel.bGroundContact = false;
        wheel.AngularVel = 0.0f;
        wheel.AngAccel = 0.0f;
        wheel.DriveTorque = 0.0f;
        wheel.NetTorque = 0.0f;
        wheel.BrakeCmdTorque = 0.0f;
        wheel.HandbrakeCmdTorque = 0.0f;
        wheel.BrakeTorqueOut = 0.0f;
        wheel.EspTorqueOut = 0.0f;
        wheel.EspState = 0;
        wheel.SuspTotalForce = 0.0f;
        wheel.AntiRollForce = 0.0f;
        wheel.NormalLoad = 0.0f;
        wheel.SuspForce.setZero();
        wheel.TireForceLong = 0.0f;
        wheel.TireForceLat = 0.0f;
        wheel.DragLong = 0.0f;
        wheel.DragLat = 0.0f;
        wheel.ContactVelLong = 0.0f;
        wheel.ContactVelLat = 0.0f;
        wheel.bTireSlipping = false;
        wheel.NormLong = 0.0f;
        wheel.NormLat = 0.0f;
        wheel.NormLongClamped = 0.0f;
        wheel.NormLatClamped = 0.0f;
        wheel.WorldForceLong.setZero();
        wheel.WorldForceLat.setZero();
        prevSuspLength[i] = suspStaticLength[i];
        prevGroundHeight[i] = suspStaticLength[i] + wheel.Radius;
    }
    resetLandingState();

    PhysicsEngineSim &engine = vehicle.Engine;
    engine.State = eEngineState::ES_Off;
    engine.IgnitionOn = false;
    engine.bIgnitionOnce = false;
    engine.IgnTimer = engine.RevLimiterCutTime;
    engine.Throttle = 0.0f;
    engine.TorqueOut = 0.0f;
    engine.SmoothedTorque = 0.0f;
    engine.AngAccel = 0.0f;
    engine.OmegaPrev = engine.Omega;

    vehicle.GearBox.CurrentGear = 1;
    vehicle.GearBox.ReportedGear = 1;
    vehicle.Clutch.bManualControl = false;

    filteredSteer = 0.0f;
    inputBrake = 0.0f;

    prevWorldVelocity[0] = prevWorldVelocity[1] = prevWorldVelocity[2] = 0.0f;
    worldAcceleration = Eigen::Vector4f::Zero();
    localAcceleration = Eigen::Vector4f::Zero();
    localAccelerationG = Eigen::Vector4f::Zero();
}

void PhysicsVehicle::resetToTransform( const Matrix4x4f_t &transform,
                                       bool bCastToGround ) {
    // FUN_0083d880
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return;
    }

    // FUN_0083d220
    resetSimulationState();
    resetBikeLean();

    Matrix4x4f_t placed = transform;
    if ( bCastToGround ) {
        castToGround( transform, placed );
    }

    JPH::Mat44 world = JPH::Mat44::sIdentity();
    for ( int c = 0; c < 4; ++c ) {
        world.SetColumn3(
            c, JPH::Vec3( placed( c, 0 ), placed( c, 1 ), placed( c, 2 ) ) );
    }
    const Eigen::Vector4f origin = GSPhysics::GetShiftOrigin();
    world.SetTranslation( world.GetTranslation() -
                          JPH::Vec3( origin.x(), origin.y(), origin.z() ) );

    setChassisPose( world, true );
    pPhysicsSystem->GetBodyInterface().SetLinearAndAngularVelocity(
        pChassisBody->GetID(), JPH::Vec3::sZero(), JPH::Vec3::sZero() );

    setVelocity( Eigen::Vector3f::Zero(), true, -1 );
    vehicle.GearBox.CurrentGear = 1;
    resetAutomaticGearbox();
    OTDU_UNIMPLEMENTED; // TODO: Handbrake hold for manual gearbox

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
        OTDU_LOG_DEBUG( "Vehicle body is now %s\n",
                        bInKeyframed ? "KEYFRAMED (cinematic owns the pose)"
                                     : "dynamic again" );
    }

    bForceKeyframeMatrix = bInKeyframed && bForceMatrix;
    if ( bForceKeyframeMatrix ) {
        keyframeMatrix = getChassisShiftedMatrix();
    }
}

void PhysicsVehicle::updateKeyframedWheels( float deltaTime ) {
    if ( pChassisBody == nullptr ) {
        return;
    }

    PhysicsEngineSim &engine = vehicle.Engine;
    if ( engine.State == eEngineState::ES_Off ) {
        engine.Omega = 0.0f;
        engine.TorqueOut = 0.0f;
    }

    // TODO: Implement HMap raycast test logic
    if ( pPhysicsSystem != nullptr ) {
        for ( int32_t i = 0; i < vehicle.NumWheels; i++ ) {
            castWheel( *pPhysicsSystem, *pChassisBody, i, deltaTime );
            prevSuspLength[i] = suspStaticLength[i];
            prevGroundHeight[i] =
                suspStaticLength[i] + vehicle.Wheels[i].Radius;
        }
    }

    for ( int32_t i = 0; i < vehicle.NumWheels; i++ ) {
        PhysicsWheelState &wheel = vehicle.Wheels[i];
        wheel.SpinAngle += wheel.AngularVel * deltaTime;
        wheel.NormLongClamped = 0.0f;
        wheel.NormLatClamped = 0.0f;
        wheel.NormLong = 0.0f;
        wheel.NormLat = 0.0f;
        wheel.bTireSlipping = false;
        wheel.BrakeTorqueOut = 0.0f;
        wheel.EspTorqueOut = 0.0f;
        wheel.DriveTorque = 0.0f;
    }
}

static void SetBodyVelocity( JPH::BodyInterface &bi,
                             JPH::Body &body,
                             const JPH::Vec3 &linearVelocity,
                             const JPH::Vec3 *pAngularVelocity ) {
    if ( !bi.IsAdded( body.GetID() ) ) {
        if ( body.IsDynamic() ) {
            body.SetLinearVelocityClamped( linearVelocity );
            if ( pAngularVelocity != nullptr ) {
                body.SetAngularVelocityClamped( *pAngularVelocity );
            }
        }
        return;
    }
    bi.SetLinearVelocity( body.GetID(), linearVelocity );
    if ( pAngularVelocity != nullptr ) {
        bi.SetAngularVelocity( body.GetID(), *pAngularVelocity );
    }
}

void PhysicsVehicle::setVelocity( const Eigen::Vector3f &linearVelocity,
                                  bool bSyncPowertrain,
                                  int32_t preferredGear ) {
    // FUN_0082dd60
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return;
    }

    JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
    const JPH::Vec3 velocity( linearVelocity.x(), linearVelocity.y(),
                              linearVelocity.z() );
    SetBodyVelocity( bi, *pChassisBody, velocity, nullptr );
    for ( const AttachedPart &part : attachedParts ) {
        SetBodyVelocity( bi, *part.pBody, velocity, nullptr );
    }

    const Matrix4x4f_t chassis = getChassisShiftedMatrix();
    const Eigen::Vector3f right = chassis.row( 0 ).head<3>().transpose();
    const Eigen::Vector3f forward = chassis.row( 2 ).head<3>().transpose();
    const float forwardSpeed = linearVelocity.dot( forward );

    float wheelOmega = 0.0f;
    for ( int32_t i = 0; i < vehicle.NumWheels; i++ ) {
        PhysicsWheelState &wheel = vehicle.Wheels[i];
        wheelOmega = forwardSpeed / wheel.Radius;
        wheel.AngularVel = wheelOmega;
        wheel.ContactVelLong = forwardSpeed;
        wheel.ContactVelLat = linearVelocity.dot( right );
    }

    if ( !bSyncPowertrain ) {
        return;
    }

    PhysicsEngineSim &engine = vehicle.Engine;
    PhysicsGearboxSim &gearbox = vehicle.GearBox;
    PhysicsClutchSim &clutch = vehicle.Clutch;

    static constexpr float kGearSelectRedlineRatio = 0.75f; // DAT_00f416a4

    int32_t gear = 2;
    float omega = 0.0f;
    for ( ; gear < gearbox.NumGears; gear++ ) {
        omega = gearbox.Ratios[gear] * gearbox.FinalDrive * wheelOmega;
        if ( omega < engine.OmegaMax * kGearSelectRedlineRatio ||
             gear == preferredGear ) {
            clutch.State = 3;
            clutch.Engagement = 1.0f;
            clutch.Target = 1.0f;
            break;
        }
    }
    if ( gear >= gearbox.NumGears ) {
        gear = gearbox.NumGears - 1;
    }

    if ( omega < engine.OmegaIdle ) {
        if ( preferredGear < 0 ) {
            gear = 1;
        }
        omega = engine.OmegaIdle;
        clutch.State = 0;
        clutch.Engagement = 0.0f;
        clutch.Target = 0.0f;
    }

    gearbox.CurrentGear = gear;
    gearbox.ReportedGear = gear;
    engine.Omega = omega;
    vehicle.ReflectedWheelSpeed = omega;
    gearbox.ShiftState = PhysicsGearboxSim::SS_Settle;
    gearbox.ShiftTimer = 1.0f;
    autoGearboxState = AGS_Forward;
}

void PhysicsVehicle::moveKeyframed( const Matrix4x4f_t &worldTransform,
                                    float deltaTime,
                                    bool bTeleport,
                                    const Matrix4x4f_t *pForkMatrices,
                                    const Matrix4x4f_t *pDiscMatrices ) {
    // FUN_0083ff50
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return;
    }

    static constexpr float kYawRateToSteer = 2.0f;     // _DAT_00fab564
    static constexpr float kSteerFilterRate = 0.05f;   // _DAT_00f42170
    static constexpr float kSteerSlewPerSecond = 2.0f; // DAT_00fab560
    static constexpr float kReverseSpeed = -0.01f;     // DAT_00f41f60
    static constexpr float kStoppedSpeed = 0.001f;     // FLOAT_00f4167c
    static constexpr float kEngineStartSpeed = 0.25f;  // DAT_00f416dc

    const Eigen::Vector3f position = worldTransform.row( 3 ).head<3>();
    const Eigen::Vector3f forward = worldTransform.row( 2 ).head<3>();

    if ( bTeleport ) {
        prevKeyframePosition = position;
    }

    const Eigen::Vector3f velocity =
        ( deltaTime > 0.0f )
            ? Eigen::Vector3f( ( position - prevKeyframePosition ) / deltaTime )
            : Eigen::Vector3f::Zero();
    if ( bTeleport ) {
        prevKeyframeForward = forward;
        keyframeSteer = 0.0f;
    }

    float yawRate = 0.0f;
    if ( deltaTime > 0.0f ) {
        yawRate = ( prevKeyframeForward.cross( forward ).y() / deltaTime ) *
                  kYawRateToSteer;
    }
    if ( vehicle.GearBox.CurrentGear == 0 ) {
        yawRate = -yawRate;
    }
    yawRate = Clamp( yawRate, -1.0f, 1.0f );

    const float maxSlew = kSteerSlewPerSecond * deltaTime;
    float steer =
        ( yawRate - keyframeSteer ) * kSteerFilterRate + keyframeSteer;
    if ( steer - keyframeSteer > maxSlew ) {
        steer = keyframeSteer + maxSlew;
    }
    if ( steer - keyframeSteer < -maxSlew ) {
        steer = keyframeSteer - maxSlew;
    }
    steer = Clamp( steer, -1.0f, 1.0f );

    prevKeyframeForward = forward;
    prevKeyframePosition = position;
    keyframeVelocity = velocity;
    keyframeSteer = steer;
    filteredSteer = steer;
    steerCommand = steer;
    if ( bIsBike ) {
        // FUN_0083ff50
        displayRoll = steer * 0.5f; // FLOAT_00f41674
        displayPitch = 0.0f;
        displayPitchRate = 0.0f;
        displayYaw = 0.0f;
    }

    const float forwardSpeed = keyframeVelocity.dot( forward );
    int32_t preferredGear = ( forwardSpeed < kReverseSpeed ) ? 0 : 2;
    if ( keyframeVelocity.norm() < kStoppedSpeed ) {
        keyframeVelocity = Eigen::Vector3f::Zero();
        preferredGear = 1;
    }
    setVelocity( keyframeVelocity, true, preferredGear );
    if ( forwardSpeed < kReverseSpeed ) {
        vehicle.GearBox.CurrentGear = 0;
        vehicle.GearBox.ReportedGear = 0;
    }

    PhysicsEngineSim &engine = vehicle.Engine;
    if ( engine.State == eEngineState::ES_Off ) {
        if ( forwardSpeed > kEngineStartSpeed ) {
            engine.State = eEngineState::ES_Running;
        } else {
            engine.Omega = 0.0f;
        }
    }
    if ( engine.State == eEngineState::ES_Crank ) {
        if ( forwardSpeed > kEngineStartSpeed ) {
            engine.State = eEngineState::ES_Running;
        }
        engine.Omega = 0.0f;
        rawInputThrottle = 1.0f;
    }
    vehicle.GearBox.ShiftState = PhysicsGearboxSim::SS_Settle;

    if ( pForkMatrices != nullptr && pDiscMatrices != nullptr ) {
        Matrix4x4f_t chassis = worldTransform;
        chassis.row( 3 ).head<3>() +=
            chassis.row( 1 ).head<3>() * ( bodyDimensions.y() * 0.5f );
        setPostponedMatrix( chassis );

        const Eigen::Vector4f origin = GSPhysics::GetShiftOrigin();
        keyframeMatrix = chassis;
        keyframeMatrix( 3, 0 ) -= origin.x();
        keyframeMatrix( 3, 1 ) -= origin.y();
        keyframeMatrix( 3, 2 ) -= origin.z();
        bForceKeyframeMatrix = bKeyframed;

        bKeyframedWheelMatrices = true;
        setVelocity( keyframeVelocity, true, -1 );
        for ( int32_t i = 0; i < vehicle.NumWheels; i++ ) {
            keyframedForkMatrices[i] = pForkMatrices[i];
            keyframedDiscMatrices[i] = pDiscMatrices[i];
        }

        publishStepPose();
        return;
    }

    // FUN_0083d950
    Matrix4x4f_t chassis = worldTransform;
    castToGround( worldTransform, chassis );
    for ( int32_t i = 0; i < vehicle.NumWheels; i++ ) {
        prevSuspLength[i] = suspStaticLength[i];
    }

    const Eigen::Vector4f origin = GSPhysics::GetShiftOrigin();
    Matrix4x4f_t shifted = chassis;
    shifted( 3, 0 ) -= origin.x();
    shifted( 3, 1 ) -= origin.y();
    shifted( 3, 2 ) -= origin.z();

    keyframeMatrix = shifted;
    bForceKeyframeMatrix = bKeyframed;

    JPH::Mat44 world = JPH::Mat44::sIdentity();
    for ( int c = 0; c < 4; ++c ) {
        world.SetColumn3(
            c, JPH::Vec3( shifted( c, 0 ), shifted( c, 1 ), shifted( c, 2 ) ) );
    }

    setChassisPose( world, true );
    pPhysicsSystem->GetBodyInterface().SetAngularVelocity(
        pChassisBody->GetID(), JPH::Vec3::sZero() );

    publishStepPose();
}

uint32_t PhysicsVehicle::getNumWheels() const {
    return static_cast<uint32_t>( vehicle.NumWheels );
}

float PhysicsVehicle::computeWheelSteerAngle(
    const PhysicsWheelState &w ) const {
    // FUN_0043bd90
    static constexpr float kAckermannRatio = 1.5f; // DAT_00f416b8
    if ( !w.bSteered ) {
        return 0.0f;
    }
    float angle = steerCommand * steerLockRad;
    if ( !bIsBike ) {
        if ( steerCommand > 0.0f && w.SideFlag == 1 ) {
            angle /= kAckermannRatio;
        }
        if ( steerCommand < 0.0f && w.SideFlag == 0 ) {
            angle /= kAckermannRatio;
        }
    }
    return angle;
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
    out.SteerAngle = computeWheelSteerAngle( w ) + w.ImpactSteerOffset;
    out.RotationAngle = w.SpinAngle;
    out.SuspensionLength = prevSuspLength[index];

    const uint32_t axle = index >> ( ( vehicle.NumWheels / 2 ) - 1 );
    const float droopY = wheelAttach[index].y() - suspDroopStop[index];
    const float compression = suspDroopStop[index] - prevSuspLength[index];
    const float maxTravel = -rideHeightMax[axle] - droopY;
    const float displayTravel =
        ( compression < maxTravel ) ? compression : maxTravel;
    out.ReleasedPosition = droopY;
    out.SuspensionTravel = displayTravel;
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

float PhysicsVehicle::getReportedSpeed( const bool bPlanar ) const {
    // FUN_0083bbd0
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return 0.0f;
    }

    JPH::Vec3 v = pPhysicsSystem->GetBodyInterface().GetLinearVelocity(
        pChassisBody->GetID() );
    if ( bPlanar ) {
        v.SetY( 0.0f );
    }
    const float chassisSpeed = v.Length();

    if ( vehicle.NumWheels <= 0 ) {
        return chassisSpeed;
    }

    const PhysicsWheelState &wheel = vehicle.Wheels[0];
    const float wheelSurfaceSpeed =
        std::fabs( wheel.AngularVel * wheel.Radius );

    return std::min( chassisSpeed, wheelSurfaceSpeed );
}

Eigen::Vector4f PhysicsVehicle::getChassisVelocity() const {
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return Eigen::Vector4f::Zero();
    }
    const JPH::Vec3 v = pPhysicsSystem->GetBodyInterface().GetLinearVelocity(
        pChassisBody->GetID() );
    return Eigen::Vector4f( v.GetX(), v.GetY(), v.GetZ(), 0.0f );
}

Eigen::Vector4f PhysicsVehicle::getChassisAngularVelocity() const {
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return Eigen::Vector4f::Zero();
    }
    const JPH::Vec3 v = pPhysicsSystem->GetBodyInterface().GetAngularVelocity(
        pChassisBody->GetID() );
    return Eigen::Vector4f( v.GetX(), v.GetY(), v.GetZ(), 0.0f );
}

ReplayDriving PhysicsVehicle::captureReplayDriving() const {
    ReplayDriving driving;
    driving.DrivingMode = drivingMode;
    driving.Accelerate = rawInputThrottle;
    driving.Brake = rawInputBrake;
    driving.Handbrake = inputHandbrake;
    driving.Steer = inputSteer;
    driving.Lean = inputLean;
    driving.Clutch = clutchPedal;
    driving.bIgnition = vehicle.Engine.IgnitionOn;
    driving.bIgnitionOnce = vehicle.Engine.bIgnitionOnce;
    driving.bGearUp = bGearUpRequest;
    driving.bGearDown = bGearDownRequest;
    driving.bBackView = bBackView;
    driving.bChangeView = bChangeView;
    driving.GearSelect = static_cast<uint8_t>( gearSelectRequest );
    driving.bFlashLights = bLightsRequest;
    driving.bFlashLightsOnce = bFlashingLightsRequest;
    driving.OpenWindows = windowsDirection;
    driving.bHonk = bHonkRequest;
    // TODO: Reset is carried by CarBodyState (main thread) in the port
    return driving;
}

void PhysicsVehicle::applyReplayDriving( const ReplayDriving &driving ) {
    drivingMode = driving.DrivingMode;
    rawInputThrottle = driving.Accelerate;
    rawInputBrake = driving.Brake;
    inputHandbrake = driving.Handbrake;
    inputSteer = driving.Steer;
    inputLean = driving.Lean;
    clutchPedal = driving.Clutch;
    setIgnition( driving.bIgnition != 0, driving.bIgnitionOnce != 0 );
    bGearUpRequest = driving.bGearUp != 0;
    bGearDownRequest = driving.bGearDown != 0;
    bBackView = driving.bBackView != 0;
    bChangeView = driving.bChangeView != 0;
    bLightsRequest = driving.bFlashLights != 0;
    bFlashingLightsRequest = driving.bFlashLightsOnce != 0;
    windowsDirection = driving.OpenWindows;
    bHonkRequest = driving.bHonk != 0;
    gearSelectRequest = driving.GearSelect;
}

void PhysicsVehicle::setReplayBodyVelocities(
    const Eigen::Vector4f &linearVelocity,
    const Eigen::Vector4f &angularVelocity ) {
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return;
    }
    JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
    const JPH::Vec3 linear( linearVelocity.x(), linearVelocity.y(),
                            linearVelocity.z() );
    const JPH::Vec3 angular( angularVelocity.x(), angularVelocity.y(),
                             angularVelocity.z() );
    if ( bi.IsAdded( pChassisBody->GetID() ) ) {
        bi.ActivateBody( pChassisBody->GetID() );
    }
    SetBodyVelocity( bi, *pChassisBody, linear, &angular );
    for ( const AttachedPart &part : attachedParts ) {
        if ( bi.IsAdded( part.pBody->GetID() ) ) {
            bi.ActivateBody( part.pBody->GetID() );
        }
        SetBodyVelocity( bi, *part.pBody, linear, nullptr );
    }
}

void PhysicsVehicle::saveReplaySnapshot( uint8_t *pRecord ) const {
    // FUN_0082f640
    ReplaySnapshot snapshot;
    snapshot.Vehicle = vehicle;
    snapshot.World = getChassisWorldMatrix();
    snapshot.LinearVelocity = getChassisVelocity();
    snapshot.AngularVelocity = getChassisAngularVelocity();
    memcpy( pRecord, static_cast<const void *>( &snapshot ),
            sizeof( ReplaySnapshot ) );
}

void PhysicsVehicle::restoreReplaySnapshot( const uint8_t *pRecord ) {
    // FUN_0083b9b0
    ReplaySnapshot snapshot;
    memcpy( static_cast<void *>( &snapshot ), pRecord,
            sizeof( ReplaySnapshot ) );
    vehicle = snapshot.Vehicle;
    setReplayBodyVelocities( snapshot.LinearVelocity,
                             snapshot.AngularVelocity );
    setPostponedMatrix( snapshot.World );
}

void PhysicsVehicle::blendReplayPose( const float curveT, const float blendS ) {
    // FUN_0082db30
    ReplayPlayback &replay = replayPlayback;
    const Eigen::Quaternionf fromRotation =
        BuildQuaternionFromMatrix( replay.Poses[0] );
    const Eigen::Quaternionf toRotation =
        BuildQuaternionFromMatrix( replay.Poses[1] );
    const Eigen::Quaternionf physicsRotation =
        BuildQuaternionFromMatrix( physicsMatrix );

    const Eigen::Vector4f shift = GSPhysics::GetShiftOrigin();
    const Eigen::Vector4f physicsPosition =
        physicsMatrix.row( 3 ).transpose() + shift;

    // FUN_0043c850
    const float invT = 1.0f - curveT;
    const float weight = curveT * 3.0f * invT;
    Eigen::Vector4f curvePosition =
        replay.Curves[0][0] * ( invT * invT * invT ) +
        replay.Curves[0][1] * ( weight * invT ) +
        replay.Curves[0][2] * ( weight * curveT ) +
        replay.Curves[0][3] * ( curveT * curveT * curveT );
    curvePosition.w() = 1.0f;

    // FUN_0041ebd0
    Eigen::Vector4f blendedPosition( 0.0f, 0.0f, 0.0f, 1.0f );
    for ( int32_t i = 0; i < 3; i++ ) {
        const float from = physicsPosition[i];
        const float to = curvePosition[i];
        blendedPosition[i] =
            std::clamp( ( to - from ) * blendS + from, std::min( from, to ),
                        std::max( from, to ) );
    }

    const Eigen::Quaternionf recordedRotation =
        QuaternionSlerpShortest( fromRotation, toRotation, curveT );
    replay.CurveT = curveT;
    replay.BlendS = blendS;
    const Eigen::Quaternionf rotation =
        QuaternionSlerpShortest( physicsRotation, recordedRotation, blendS );

    if ( replay.Mode == 1 ) {
        replay.SourceMatrix = physicsMatrix;
    } else if ( replay.Mode == 2 ) {
        replay.SourceMatrix = BuildRotationMatrix( rotation );
        replay.SourceMatrix.row( 3 ) = Eigen::Vector4f(
            blendedPosition.x() - shift.x(), blendedPosition.y() - shift.y(),
            blendedPosition.z() - shift.z(), 1.0f );
    } else if ( replay.Mode == 3 ) {
        replay.SourceMatrix = BuildRotationMatrix( recordedRotation );
        replay.SourceMatrix.row( 3 ) = Eigen::Vector4f(
            curvePosition.x() - shift.x(), curvePosition.y() - shift.y(),
            curvePosition.z() - shift.z(), 1.0f );
    }
}

float PhysicsVehicle::getEngineAngularVelocity() const {
    return vehicle.Engine.Omega;
}

int32_t PhysicsVehicle::getCurrentGear() const {
    return vehicle.GearBox.CurrentGear - 1;
}

int32_t PhysicsVehicle::getReportedGear() const {
    return vehicle.GearBox.ReportedGear - 1;
}

float PhysicsVehicle::getLateralG() const { return localAccelerationG.x(); }

float PhysicsVehicle::getLongitudinalG() const {
    return localAccelerationG.z();
}

float PhysicsVehicle::getVerticalG() const { return localAccelerationG.y(); }
