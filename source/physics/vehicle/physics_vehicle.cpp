#include "shared.h"

#include "physics_vehicle.h"

#include "core/maths_helper.h"

#include "input/gs_device.h"
#include "physics/physics_config.h"
#include "physics/physics_layers.h"
#include "physics/gs_physics.h"
#include "physics/physics_material.h"
#include "database/gs_database.h"
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

#include <algorithm>
#include <cmath>
#include <cstring>

constexpr float kMmToM = 0.001f;
constexpr float kSlipDenom = 3.0f;
constexpr int kWheelSubsteps = 5;
constexpr float kSuspDampingScale = 1.0f;

int32_t gVehicleDrivingHelpOverride = -1;

bool gVehicleABS = true;

constexpr float kSimuBrakeTorquePerMass = 2.5f;
constexpr float kBrakeBalance = 0.6f; // fallback
constexpr float kMaxBrakeTorque = 15000.0f;
constexpr float kWheelInertiaRadiusCoef = 2.5f; // DAT_00f416c0

constexpr float kSteerScale = 900.0f; // DAT_00f41c10 (900/gSteeringTurn = gain)
constexpr float kSteerLinearity = 5.0f;    // DAT_00fab71c
constexpr float kSteerLinearityMin = 0.1f; // FLOAT_00f41668 (expo gate)
constexpr float kSteerTurnScale = 1.0f;
constexpr float kSteerNormSpeed = 1.0f;
constexpr float kSteerSensRetailHigh = 2.0f;

constexpr float kAeroLiftFront = 0.4f; // DAT_00f41730(0.2) * DAT_00f416b0(2.0)
constexpr float kAeroDensitySimu = 1.7f;   // DAT_00f417d4
constexpr float kAeroDensityArcade = 1.2f; // DAT_00f8a248
bool gbSimuModeEnabled = false;            // DAT_010e777c

// Anti-roll bars from the original pcode (disabled in the original game)
bool gVehicleAntiRoll = false;
constexpr float kAntiRollFraction = 0.0f;
constexpr float kGravCenterHeightFracDefault = 0.40f; // DAT_00f4175c

// Suspension damper-velocity NaN guard (legacy body-velocity path).
constexpr float kSuspDamperVelClamp = 100.0f;
constexpr float kSuspReboundStiffen = 1.6f;    // 0.10 / 0.0625
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

static void ComputeAbsMagnitudes( PhysicsVehicleState &vehicle,
                                  float speed,
                                  bool bReverse ) {
    // FUN_0041b920
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
    // FUN_0041b920
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
        vehicle.bEspEnabled = false;
    } else {
        vehicle.EspMagFiltered +=
            ( target - vehicle.EspMagFiltered ) * deltaTime * kEspSmoothingRate;
        vehicle.bEspEnabled = true;
    }

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

PhysicsVehicle::PhysicsVehicle()
    : pPhysicsSystem( nullptr ),
      pChassisBody( nullptr ),
      bRegistered( false ),
      steerLockRad( 0.0f ),
      inputThrottle( 0.0f ),
      inputBrake( 0.0f ),
      inputSteer( 0.0f ),
      drivingMode( DM_Player ),
      prevInputThrottle( 0.0f ),
      prevInputBrake( 0.0f ),
      prevInputSteer( 0.0f ),
      autoReverseState( 0 ),
      filteredSteer( 0.0f ),
      steerCommand( 0.0f ),
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
      aeroDensity( kAeroDensitySimu ),
      localAccelLong( 0.0f ),
      localAccelLat( 0.0f ) {
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
}

PhysicsVehicle::~PhysicsVehicle() { destroy(); }

PhysicsVehicle::SuspensionSetup
PhysicsVehicle::ComputeAxleSuspension( const DBEntryCarPhysicsData &db,
                                       bool bFront ) {
    // FUN_008345e0
    constexpr float kGravity = 9.81f;
    const float springCoef =
        gPhysicsConfig.SpringCoef > 0.0f ? gPhysicsConfig.SpringCoef : 1.0f;

    const float weight =
        db.Weight > 0 ? static_cast<float>( db.Weight ) : 1200.0f;
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
    float kBase = 0.006f * coefSuspRaw * weight * axleFrac * suspRate;
    if ( weight > 3500.0f ) {
        kBase *= 2.5f; // heavy-car boost (DAT_00f416c0)
    }
    const float k = Max( 1000.0f, kBase * springCoef * 1.2f );
    const float d = kSuspDampingScale * kBase * 0.0625f *
                    ( coefSuspRaw * 0.01f ) * ( dampRate * 0.01f ) * springCoef;

    const float cornerLoad = weight * axleFrac * kGravity * 0.5f;
    const float sag = cornerLoad / k;

    constexpr float kSuspHardpoint =
        0.4f; // DAT_00f4175c: strut top above wheel
    SuspensionSetup s;
    s.minLength = 0.05f;
    s.restLength = JPH::Clamp( kSuspHardpoint - sag, 0.18f, 0.26f );
    s.maxLength = Max( s.restLength + 0.04f, kSuspHardpoint );

    const int32_t rideHtRaw = bFront ? db.RideHeightFront : db.RideHeightBack;
    const int32_t rideHtMaxRaw =
        bFront ? db.RideHeightMaxFront : db.RideHeightMaxRear;
    const float rideHt_m = ( rideHtRaw > 0 ? rideHtRaw : 130 ) * 0.001f;
    const float rideHtMax_m =
        ( rideHtMaxRaw > 0 ? rideHtMaxRaw : 200 ) * 0.001f;
    const float droopTravel =
        JPH::Clamp( rideHtMax_m - rideHt_m, Max( sag, 0.04f ), 0.15f );

    s.cornerLoad = cornerLoad;
    s.staticLength = s.maxLength - sag; // == old spring settle -> ride height
    s.droopStop = s.staticLength + droopTravel;

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
                             bool bFactoryShape ) {
    if ( pSystem == nullptr || pChassis == nullptr ) {
        return false;
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

    setDrivingHelpLevel( gVehicleDrivingHelpOverride >= 0
                             ? gVehicleDrivingHelpOverride
                             : ( gpPlayerData != nullptr
                                     ? gpPlayerData->getDrivingHelpLevel()
                                     : 0 ) );

    bodyHeight = ( db.Height > 0 ? db.Height : 1300 ) * kMmToM;
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
    float frontRadius = kDefaultWheelRadius;
    float rearRadius = kDefaultWheelRadius;
    if ( gpDatabase != nullptr ) {
        const GameDatabase &gameDB = gpDatabase->getGameDB();
        const DBEntryRim *pRim =
            static_cast<const DBEntryRim *>( db.DefaultRims.pAsRawPointer );
        const bool bInRange = gameDB.DBRim != nullptr && pRim >= gameDB.DBRim &&
                              pRim < gameDB.DBRim + gameDB.pNumItems[2];
        if ( bInRange ) {
            auto axleRadius = []( int32_t sectionWidthMM, int32_t aspectPct,
                                  int32_t rimInch, float fallback ) -> float {
                if ( sectionWidthMM <= 0 || rimInch <= 0 || rimInch > 40 ) {
                    return fallback;
                }
                const float rimMM = rimInch * 25.4f;
                const float sidewallMM =
                    ( aspectPct > 0 && aspectPct < 100 )
                        ? sectionWidthMM * ( aspectPct * 0.01f )
                        : 0.0f;
                return JPH::Clamp( ( rimMM * 0.5f + sidewallMM ) / 1000.0f,
                                   0.20f, 0.45f );
            };
            frontRadius =
                axleRadius( pRim->FrontWidth, pRim->FrontHeight,
                            pRim->FrontDiameter, kDefaultWheelRadius );
            rearRadius = axleRadius( pRim->RearWidth, pRim->RearHeight,
                                     pRim->RearDiameter, kDefaultWheelRadius );
        }
    }

    const float attachY = -0.10f;

    // FL, FR, RL, RR (TDU: right = +x, forward = +z).
    wheelAttach[0] = Eigen::Vector3f( -halfTrackF, attachY, wheelBaseM * 0.5f );
    wheelAttach[1] = Eigen::Vector3f( halfTrackF, attachY, wheelBaseM * 0.5f );
    wheelAttach[2] =
        Eigen::Vector3f( -halfTrackR, attachY, -wheelBaseM * 0.5f );
    wheelAttach[3] = Eigen::Vector3f( halfTrackR, attachY, -wheelBaseM * 0.5f );

    // Aerodynamics: F = 0.5 * rho * A * C * v^2 (FUN_008345e0 / Simulate).
    const float frontalArea = ( db.Width > 0 ? db.Width : 1800 ) *
                              ( db.Height > 0 ? db.Height : 1300 ) * 1.0e-6f;
    const float cxDrag = Max( 0.0f, db.CxLength );
    const float czLift = Max( 0.0f, db.CxHeight );
    aeroDragArea = 0.5f * frontalArea * cxDrag;
    aeroDownArea = 0.5f * frontalArea * czLift;
    aeroDensity = gbSimuModeEnabled ? kAeroDensitySimu : kAeroDensityArcade;
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
        db.Weight > 0 ? static_cast<float>( db.Weight ) : 1200.0f;
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
        PhysicsVehicle::ComputeAxleSuspension( db, true );
    const PhysicsVehicle::SuspensionSetup suspRear =
        PhysicsVehicle::ComputeAxleSuspension( db, false );
    const float kTwoPiSusp = 6.28318530718f;

    const float groundY = Min( attachY - suspFront.staticLength - frontRadius,
                               attachY - suspRear.staticLength - rearRadius );
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
    finalDriveRatio = db.FinalDriveRatio > 0 ? db.FinalDriveRatio : 4.0f;

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
        suspRestLength[i] = s.restLength;
        suspStiffness[i] = massPerWheel * springOmega * springOmega;
        suspDamping[i] = 2.0f * s.damping * massPerWheel * springOmega;
        suspDamperRebound[i] = suspDamping[i] * kSuspReboundStiffen;
        suspDamperLP[i] = 0.0f;
        suspStaticLength[i] = s.staticLength;
        suspCornerLoad[i] = s.cornerLoad;
        suspDroopStop[i] = s.droopStop;
        w.Radius = bFront ? frontRadius : rearRadius;
        w.Inertia = w.Radius * w.Radius * kWheelInertiaRadiusCoef;
        w.SideFlag = ( i & 1 ) ? 2 : 1; // right wheels mirror lateral/steer
        w.bSteered = bFront;
        w.bDriven = bFWD ? bFront : ( bRWD ? !bFront : true );
        w.SuspMaxLoad = 2.0f * massPerWheel * 9.81f;
        w.SuspMinLoad = 0.0f;
        w.LoadScaleLong = 1.0f;
        w.LoadScaleDrag = 0.0f; // v^2 drag off for the spike
        w.DynamicGrip = 1.0f;
        w.GripMult = 1.0f;

        const PhysicsConfig &c = gPhysicsConfig;
        w.GripLong =
            bFront ? ( c.FrontFrictionCoef > 0 ? c.FrontFrictionCoef : 1.7f )
                   : ( c.RearFrictionCoef > 0 ? c.RearFrictionCoef : 1.7f );
        w.GripLat =
            bFront
                ? ( c.FrontLateralFrictionCoef > 0 ? c.FrontLateralFrictionCoef
                                                   : 1.4f )
                : ( c.RearLateralFrictionCoef > 0 ? c.RearLateralFrictionCoef
                                                  : 1.4f );
        w.BrakeGripLong = bFront ? ( c.FrontLngBrakeFrictionCoef > 0
                                         ? c.FrontLngBrakeFrictionCoef
                                         : 1.2f )
                                 : ( c.RearLngBrakeFrictionCoef > 0
                                         ? c.RearLngBrakeFrictionCoef
                                         : 1.2f );
        w.BrakeGripLat = w.GripLat;

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

    gb.FinalDrive = db.FinalDriveRatio > 0 ? db.FinalDriveRatio : 4.0f;

    const float simInertiaScale = gbSimuModeEnabled ? 0.5f : 1.0f;
    gb.BaseInertia =
        ( db.GearboxIntertia != 0 ? db.GearboxIntertia * 0.01f : 0.25f ) *
        simInertiaScale;
    gb.TransEff =
        db.TransmissionPrimacy > 0 ? db.TransmissionPrimacy * 0.01f : 0.9f;
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

    // Engine: redline/idle/inertia from the DB, curve normalised to BHP.
    PhysicsEngineSim &en = vehicle.Engine;
    const float rpmToRad = 0.10472f;
    en.OmegaMax = ( db.Redline > 0 ? db.Redline : 7000 ) * rpmToRad;
    en.OmegaIdle = 850.0f * rpmToRad;
    en.Inertia = ( db.EngineInertia > 0 ? db.EngineInertia * 0.004f : 0.4f ) *
                 simInertiaScale; // *0.5 in Simu mode
    en.Omega = en.OmegaIdle;
    en.State = eEngineState::ES_Off;
    en.IgnTimer = 0.0f;
    en.IgnitionOn = true; // key on: auto-start + auto-restart on stall
    en.CrankDuration =
        ( db.IgnitionTimeIgnite > 0.05f && db.IgnitionTimeIgnite < 3.0f )
            ? db.IgnitionTimeIgnite
            : 0.5f;
    en.RevUpDuration =
        ( db.IgnitionTimeRevUp > 0.05f && db.IgnitionTimeRevUp < 3.0f )
            ? db.IgnitionTimeRevUp
            : 0.4f;

    const float bhp = ( db.BHP > 0 ? (float)db.BHP : 200.0f );

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

        OTDU_LOG_DEBUG( "Engine curve: torque blob (%d pts, %.0f BHP)\n",
                        blobCount, bhp );
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

    for ( PhysicsDifferentialSim *d :
          { &vehicle.DiffFront, &vehicle.DiffRear, &vehicle.DiffCenter } ) {
        d->Type = 4;
        d->Gain = 1.0f;
        d->Deadband = 0.0f;
        d->SplitRatio = 0.5f;
        d->LockCoef = 0.0f;
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
    inputThrottle = throttle;
    inputBrake = brake;
    inputSteer = steer;
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
    out.ForwardSpeed = vehicle.ForwardSpeed;
    out.bAutomatic = g.bAutomatic;
    out.bEngineFiring = ( vehicle.Engine.State == eEngineState::ES_RevUp ||
                          vehicle.Engine.State == eEngineState::ES_Running );
    out.bShiftIdle = ( g.ShiftState == 1 && g.ShiftTimer <= 0.0f );
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

float PhysicsVehicle::filterSteering( float rawSteer ) {
    // FUN_0041ad60
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

    const float turn = ( gSteeringTurn > 0.0f ) ? gSteeringTurn : 270.0f;
    float cmd = ( rawSteer / turn ) * kSteerScale * sens * kSteerTurnScale;
    if ( kSteerLinearity >= kSteerLinearityMin ) {
        const float s = ( cmd >= 0.0f ) ? 1.0f : -1.0f;
        cmd = s * SteerResponseCurve( std::fabs( cmd ), kSteerLinearity );
    }
    filteredSteer = JPH::Clamp( cmd, -1.0f, 1.0f );
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
    const float rayLen = droopStop + w.Radius;
    JPH::RRayCast ray{ attachW, rayLen * down };

    const JPH::DefaultBroadPhaseLayerFilter bpFilter =
        system.GetDefaultBroadPhaseLayerFilter( TDUPhysLayers::WHEEL );
    const JPH::DefaultObjectLayerFilter objFilter =
        system.GetDefaultLayerFilter( TDUPhysLayers::WHEEL );
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
        return false;
    }

    const JPH::Vec3 contact( collector.mPosition - JPH::RVec3::sZero() );
    const float suspLength =
        JPH::Clamp( rayLen * collector.GetEarlyOutFraction() - w.Radius,
                    suspMinLength[i], droopStop );

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

    // TODO: FIXME: RE actual Longitudinal slip calc (from pcode)
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
    w.WheelPos =
        Eigen::Vector4f( attachW.GetX(), attachW.GetY(), attachW.GetZ(), 1.0f );
    w.bGroundContact = true;
    return true;
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

    applyDrivingMode();

    const JPH::Body &chassis = *pChassisBody;
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
    const float vForward = chassisVel.Dot( world.GetColumn3( 2 ) );
    steerCommand = filterSteering( inputSteer );

    vehicle.ForwardSpeed = vForward;
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

        const float hSub = dt / static_cast<float>( kWheelSubsteps );
        const float radius = std::max( w.Radius, 0.05f );
        const float driveBase = w.DriveTorque;

        if ( !hit ) {
            w.BrakeCmdTorque = 0.0f;
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

        const float vLongStep = w.ContactVelLong;
        for ( int s = 0; s < kWheelSubsteps; ++s ) {
            w.BrakeCmdTorque = -( ( w.AngularVel > 0.0f ) ? 1.0f : -1.0f ) *
                               inputBrake * w.BrakeTorque;

            const float longSlip =
                ( w.AngularVel * radius - vLongStep ) /
                std::max( std::fabs( vLongStep ), kSlipDenom );
            w.ForwardDir = w.ContactNormal * longSlip;
            ComputeTireForces( w, vehicleMatrix );

            const float reactTorque = ComputeWheelReactionTorque( w, hSub );

            ApplyBrakeAndABS( w, gVehicleABS && vehicle.bAbsEnabled, absMag,
                              reactTorque, driveBase, hSub );

            w.DriveTorque = ApplyEngineDragControl(
                w, vehicle.bEdcEnabled, reactTorque, driveBase, hSub );

            ApplyESP( w, vehicle.bEspEnabled, espMag, reactTorque,
                      w.DriveTorque, hSub );

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

        bi.AddForce( id, suspForce, point );
        bi.AddForce( id, tireForce, point );
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
            upAxis * ( -aeroDownArea * kAeroLiftFront * dyn ) +
            fwd * ( -aeroDragArea * dyn );
        bi.AddForce( id, aeroForce );
    }

    bi.ActivateBody( id );
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
        ComputeAbsMagnitudes( vehicle, v.Length(), bReverse );
        ComputeEspMagnitudes( vehicle, v.Length(), deltaTime, bReverse );
    }

    if ( pPhysicsSystem != nullptr && deltaTime > 1.0e-5f ) {
        JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
        const JPH::Vec3 vel = bi.GetLinearVelocity( pChassisBody->GetID() );
        const JPH::Vec3 prev( prevWorldVelocity[0], prevWorldVelocity[1],
                              prevWorldVelocity[2] );
        const JPH::Vec3 worldAccel = ( vel - prev ) / deltaTime;
        const JPH::Quat rot = bi.GetRotation( pChassisBody->GetID() );
        const JPH::Vec3 localAccel = rot.Conjugated() * worldAccel;
        const float kSmoothing = 0.2f;
        localAccelLong += ( localAccel.GetZ() - localAccelLong ) * kSmoothing;
        localAccelLat += ( localAccel.GetX() - localAccelLat ) * kSmoothing;
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
        w.WheelPos += shift;
        w.ContactPoint += shift;
    }
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

    Matrix4x4f_t displayMatrix = displayTransform * sourceMatrix;
    displayMatrix( 3, 0 ) += displayMatrix( 1, 0 ) * y3DOffset;
    displayMatrix( 3, 1 ) += displayMatrix( 1, 1 ) * y3DOffset;
    displayMatrix( 3, 2 ) += displayMatrix( 1, 2 ) * y3DOffset;
    displayMatrix( 3, 3 ) = 1.0f;
    return displayMatrix;
}

void PhysicsVehicle::calibrateDisplayYOffset( float modelWheelRestY ) {
    const int32_t numWheels = vehicle.NumWheels;
    if ( numWheels <= 0 ) {
        return;
    }
    float sum = 0.0f;
    for ( int32_t i = 0; i < numWheels; i++ ) {
        sum += -getWheelRestCenterDrop( static_cast<uint32_t>( i ) );
    }
    const float physicsWheelRestY = sum / numWheels;
    y3DOffset = ( physicsWheelRestY - modelWheelRestY ) + bodyHeight * 0.5f;
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
    out.SuspensionDeflection = JPH::Clamp(
        suspRestLength[index] - prevSuspLength[index], -0.25f, 0.25f );
    out.LongitudinalSlip = w.NormLongClamped;
    out.LateralSlip = w.NormLatClamped;
    out.LongitudinalGroundSpeed = w.ContactVelLong;
    out.LateralGroundSpeed = w.ContactVelLat;
    out.SuspensionLoad = w.NormalLoad;
    out.Radius = w.Radius;
    out.bGrounded = w.bGroundContact;
    out.EspState = w.EspState;
    out.BrakeTorqueOut = w.BrakeTorqueOut;
    out.EspTorqueOut = w.EspTorqueOut;
    out.ContactSurface = wheelSurface[index];
    out.ContactMaterial = wheelMaterial[index];
    out.ContactPoint =
        w.bGroundContact
            ? w.ContactPoint
            : Eigen::Vector4f( w.WheelPos.x(), w.WheelPos.y() - w.Radius,
                               w.WheelPos.z(), 1.0f );
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

float PhysicsVehicle::getLateralG() const { return localAccelLat / 9.81f; }

float PhysicsVehicle::getLongitudinalG() const {
    return localAccelLong / 9.81f;
}
