#include "shared.h"

#include "physics_vehicle.h"

#include "physics/physics_config.h"
#include "physics/physics_layers.h"
#include "physics/gs_physics.h"
#include "database/gs_database.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>

#include <algorithm>
#include <cmath>
#include <cstring>

constexpr float kMmToM = 0.001f;
constexpr float kDegToRad = 0.01745329252f;
constexpr float kSlipDenom =
    3.0f; // min |vLong| for the longitudinal-slip ratio

constexpr int kWheelSubsteps = 5;

bool gVehicleDrivingAids = true;

constexpr float kEspPeakSlipAngle = 0.3f; // rad (~17 deg)
constexpr float kEspAuthorityFrac = 0.25f;
constexpr float kSteerRatePerSec = 4.0f; // full lock in ~0.25 s

bool gVehicleYawDamper = false;
constexpr float kYawDamping = 3000.0f; // N.m per rad/s of yaw rate

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
        const JPH::Vec3 normal =
            pBody->GetWorldSpaceSurfaceNormal( inResult.mSubShapeID2, pos );
        if ( normal.Dot( mUp ) > mCosMaxSlope ) {
            UpdateEarlyOutFraction( inResult.mFraction );
            mHit = true;
            mPosition = pos;
            mNormal = normal;
            mSurface = pBody->GetUserData();
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
};

// TODO: Move to GSPhysics?
inline Eigen::Vector4f ToVec4( JPH::Vec3Arg v ) {
    return Eigen::Vector4f( v.GetX(), v.GetY(), v.GetZ(), 0.0f );
}

PhysicsVehicle::PhysicsVehicle()
    : pPhysicsSystem( nullptr ),
      pChassisBody( nullptr ),
      bRegistered( false ),
      steerLockRad( 0.0f ),
      inputThrottle( 0.0f ),
      inputBrake( 0.0f ),
      inputSteer( 0.0f ),
      filteredSteer( 0.0f ),
      engineOmega( 0.0f ),
      currentGear( 1 ),
      finalDriveRatio( 4.0f ),
      localAccelLong( 0.0f ),
      localAccelLat( 0.0f ) {
    for ( int i = 0; i < PhysicsVehicleState::kMaxWheels; ++i ) {
        wheelAttach[i] = Eigen::Vector3f::Zero();
        suspMaxLength[i] = 0.30f;
        suspMinLength[i] = 0.05f;
        suspRestLength[i] = 0.20f;
        suspStiffness[i] = 0.0f;
        suspDamping[i] = 0.0f;
        prevSuspLength[i] = suspRestLength[i];
    }
    prevWorldVelocity[0] = prevWorldVelocity[1] = prevWorldVelocity[2] = 0.0f;
}

PhysicsVehicle::~PhysicsVehicle() { destroy(); }

PhysicsVehicle::SuspensionSetup
PhysicsVehicle::ComputeAxleSuspension( const DBEntryCarPhysicsData &db,
                                       bool bFront ) {
    constexpr float kGravity = 9.81f;
    constexpr float kTwoPi = 6.28318530718f;

    const int32_t rateRaw =
        bFront ? db.SuspensionRateFront : db.SuspensionRateRear;
    const int32_t dampRaw = bFront ? db.DamperRateFront : db.DamperRateRear;
    const int32_t lenRaw =
        bFront ? db.SuspensionLengthFront : db.SuspensionLengthRear;

    const float cfgSpringCoef =
        gPhysicsConfig.SpringCoef > 0.0f ? gPhysicsConfig.SpringCoef : 1.0f;
    const float cfgReboundCoef =
        gPhysicsConfig.ReboundCoef > 0.0f ? gPhysicsConfig.ReboundCoef : 1.0f;
    const float coefSuspension = JPH::Clamp(
        db.CoefSuspension > 0 ? db.CoefSuspension * 0.01f : 1.0f, 0.25f, 4.0f );

    const float sr = Max( 0.01f, ( rateRaw > 0 ? rateRaw : 100 ) * 0.01f );
    const float omega =
        std::sqrt( 72.0f * sr * cfgSpringCoef * coefSuspension );

    const float freq = JPH::Clamp( omega * 0.15915494f, 1.0f, 3.0f );
    const float springOmega = kTwoPi * freq;
    const float sag = kGravity / ( springOmega * springOmega ); // static, m

    const float bumpTravel =
        JPH::Clamp( lenRaw > 0 ? lenRaw * kMmToM : 0.12f, 0.08f, 0.20f );

    SuspensionSetup s;
    s.minLength = 0.05f;
    s.restLength = s.minLength + bumpTravel;
    s.maxLength = s.restLength + sag;
    s.frequency = freq;
    s.damping =
        JPH::Clamp( omega * 0.0339f * ( dampRaw > 0 ? dampRaw * 0.01f : 1.0f ) *
                        cfgReboundCoef,
                    0.05f, 1.5f );
    return s;
}

void PhysicsVehicle::initialize() {
    // FUN_00828a50
    OTDU_UNIMPLEMENTED;
}

bool PhysicsVehicle::create( JPH::PhysicsSystem *pSystem,
                             JPH::Body *pChassis,
                             const DBEntryCarPhysicsData &db,
                             const void *pTorqueBinary ) {
    if ( pSystem == nullptr || pChassis == nullptr ) {
        return false;
    }
    pPhysicsSystem = pSystem;
    pChassisBody = pChassis;

    const float halfTrackF =
        ( db.TrackFront > 0 ? db.TrackFront : 1500 ) * 0.5f * kMmToM;
    const float halfTrackR =
        ( db.TrackBack > 0 ? db.TrackBack : 1500 ) * 0.5f * kMmToM;
    const float wheelBaseM =
        ( db.WheelBase > 0 ? db.WheelBase : 2600 ) * kMmToM;
    const float radius = 0.32f; // TODO: from the rim/tire DB
    const float attachY = -0.10f;

    // FL, FR, RL, RR (TDU: right = +x, forward = +z).
    wheelAttach[0] = Eigen::Vector3f( -halfTrackF, attachY, wheelBaseM * 0.5f );
    wheelAttach[1] = Eigen::Vector3f( halfTrackF, attachY, wheelBaseM * 0.5f );
    wheelAttach[2] =
        Eigen::Vector3f( -halfTrackR, attachY, -wheelBaseM * 0.5f );
    wheelAttach[3] = Eigen::Vector3f( halfTrackR, attachY, -wheelBaseM * 0.5f );

    // DriveTrain
    const uint32_t driveHash =
        static_cast<uint32_t>( db.DriveType.Hashcode & 0xffffffffu );
    const bool bFWD = ( driveHash == 0x035bf035u );
    const bool bRWD = ( driveHash == 0x034cadf5u );

    // Suspension
    const float mass =
        db.Weight > 0 ? static_cast<float>( db.Weight ) : 1200.0f;
    const float massPerWheel = mass * 0.25f;

    // Select the vehicle tire curve from tires.bnk
    const int numProfiles = gpPhysics ? gpPhysics->getNumTireProfiles() : 0;
    const int tireIdx = SelectTireIndex( mass, numProfiles );
    const TirePacejka *pFrontTire =
        gpPhysics ? gpPhysics->getTireProfile( tireIdx ) : nullptr;
    const TirePacejka *pRearTire = pFrontTire;

    OTDU_LOG_DEBUG( "Vehicle tire: %d profiles, idx=%d curve=%s\n", numProfiles,
                    tireIdx,
                    pFrontTire != nullptr ? "tires.bnk" : "placeholder" );

    const PhysicsVehicle::SuspensionSetup suspFront =
        PhysicsVehicle::ComputeAxleSuspension( db, true );
    const PhysicsVehicle::SuspensionSetup suspRear =
        PhysicsVehicle::ComputeAxleSuspension( db, false );
    const float kTwoPiSusp = 6.28318530718f;

    steerLockRad = ( db.SteerLock > 0 ? db.SteerLock : 30.0f ) * kDegToRad;
    finalDriveRatio = db.FinalDriveRatio > 0 ? db.FinalDriveRatio : 4.0f;

    // Vehicle + per-wheel setup
    vehicle = PhysicsVehicleState();
    vehicle.NumWheels = 4;
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
        w.Radius = radius;
        w.Inertia = 1.2f;
        w.SideFlag = ( i & 1 ) ? 2 : 1; // right wheels mirror lateral/steer
        w.bSteered = bFront;
        w.bDriven = bFWD ? bFront : ( bRWD ? !bFront : true );
        w.SuspMaxLoad = 3.5f * massPerWheel * 9.81f;
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
        SeedTire( w.Tire, bFront );
        const TirePacejka *pProfile = bFront ? pFrontTire : pRearTire;
        if ( pProfile != nullptr ) {
            CopyTireCurve( w.Tire, *pProfile );
        }

        prevSuspLength[i] = suspRestLength[i];
    }

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

    float finalDrive = db.FinalDriveRatio > 0 ? db.FinalDriveRatio : 4.0f;
    const float redlineRad = ( db.Redline > 0 ? db.Redline : 7000 ) * 0.10472f;
    const float usableRedline =
        std::max( 1.0f, ( redlineRad - 104.72f ) * 0.9f );
    const float topGearRatio =
        ( nDbGears > 0 && db.GearRatios[nDbGears - 1] > 0.0f )
            ? db.GearRatios[nDbGears - 1]
            : 0.0f;
    const float topSpeedMs = db.TopSpeedKMH / 3.6f;
    if ( topGearRatio > 0.01f && topSpeedMs > 5.0f ) {
        const float derived =
            usableRedline * radius / ( topGearRatio * topSpeedMs );
        if ( derived > 1.5f && derived < 8.0f ) {
            finalDrive = derived;
        }
        OTDU_LOG_DEBUG( "FinalDrive: db=%.2f derived=%.2f (topGear=%.3f "
                        "topSpeed=%.0fkm/h r=%.2f) -> %.2f\n",
                        db.FinalDriveRatio, derived, topGearRatio,
                        db.TopSpeedKMH, radius, finalDrive );
    }
    gb.FinalDrive = finalDrive;

    gb.TransEff =
        db.TransmissionPrimacy > 0 ? db.TransmissionPrimacy * 0.01f : 0.9f;
    gb.CurrentGear = 2; // 1st forward
    gb.bAutomatic = true;

    // Engine: redline/idle/inertia from the DB, curve normalised to BHP.
    PhysicsEngineSim &en = vehicle.Engine;
    const float rpmToRad = 0.10472f;
    en.OmegaMax = ( db.Redline > 0 ? db.Redline : 7000 ) * rpmToRad;
    en.OmegaIdle = 850.0f * rpmToRad;
    en.Inertia = ( db.EngineInertia > 0 ? db.EngineInertia * 0.004f : 0.4f );
    en.Omega = en.OmegaIdle;

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

    // Open differentials (L/R split 0.5); center 0.5 for AWD.
    vehicle.DiffFront.SplitRatio = 0.5f;
    vehicle.DiffRear.SplitRatio = 0.5f;
    vehicle.DiffCenter.SplitRatio = 0.5f;

    vehicle.BrakeTorque = massPerWheel * 9.81f * radius * 1.2f;

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

bool PhysicsVehicle::castWheel( JPH::PhysicsSystem &system,
                                const JPH::Body &chassis,
                                int i,
                                float deltaTime ) {
    PhysicsWheelState &w = vehicle.Wheels[i];

    const JPH::RMat44 world = chassis.GetWorldTransform();
    const JPH::Vec3 right = world.GetColumn3( 0 );
    const JPH::Vec3 up = world.GetColumn3( 1 );
    const JPH::Vec3 forward = world.GetColumn3( 2 );

    const Eigen::Vector3f &a = wheelAttach[i];
    const JPH::RVec3 attachW = world * JPH::Vec3( a.x(), a.y(), a.z() );
    const JPH::Vec3 down = -up;

    const float rayLen = suspMaxLength[i] + w.Radius;
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
        prevSuspLength[i] = suspMaxLength[i];
        wheelSurface[i] = 0;
        return false;
    }

    const JPH::Vec3 contact( collector.mPosition - JPH::RVec3::sZero() );
    const float suspLength =
        JPH::Clamp( rayLen * collector.GetEarlyOutFraction() - w.Radius,
                    suspMinLength[i], suspMaxLength[i] );

    // Suspension spring + damper -> total force
    const float spring = suspStiffness[i] * ( suspMaxLength[i] - suspLength );
    float suspVel = ( deltaTime > 1.0e-5f )
                        ? ( prevSuspLength[i] - suspLength ) / deltaTime
                        : 0.0f;

    // Clamp the compression velocity
    suspVel = JPH::Clamp( suspVel, -3.0f, 3.0f );
    const float damper = suspDamping[i] * suspVel;
    prevSuspLength[i] = suspLength;
    w.SuspTotalForce = spring + damper;
    w.AntiRollForce = 0.0f;
    wheelSurface[i] = static_cast<uint32_t>( collector.mSurface );

    // Contact frame.
    const JPH::Vec3 normal = collector.mNormal.NormalizedOr( up );
    float steerAngle = ( w.bSteered ? -filteredSteer * steerLockRad : 0.0f );
    if ( w.SideFlag != 1 ) {
        // TODO:
    }

    const JPH::Vec3 steeredFwd =
        forward * JPH::Cos( steerAngle ) + right * JPH::Sin( steerAngle );
    JPH::Vec3 longAxis = ( steeredFwd - normal * steeredFwd.Dot( normal ) );
    longAxis = longAxis.NormalizedOr( steeredFwd );
    const JPH::Vec3 latAxis = normal.Cross( longAxis ).NormalizedOr( right );

    // Hub / contact velocity = v_com + w x (contact - com).
    const JPH::RVec3 comPos = chassis.GetCenterOfMassPosition();
    const JPH::Vec3 linVel = chassis.GetLinearVelocity();
    const JPH::Vec3 angVel = chassis.GetAngularVelocity();
    const JPH::Vec3 r( collector.mPosition - comPos );
    const JPH::Vec3 contactVel = linVel + angVel.Cross( r );
    const float vLong = contactVel.Dot( longAxis );

    // TODO: FIXME: RE actual Longitudinal slip calc (from pcode)
    const float longSlip = ( w.AngularVel * w.Radius - vLong ) /
                           std::max( std::fabs( vLong ), kSlipDenom );

    w.ContactNormal = ToVec4( normal );
    w.ContactLongAxis = ToVec4( longAxis );
    w.ContactLatAxis = ToVec4( latAxis );
    w.ForwardDir = w.ContactNormal * longSlip;
    w.WheelLinVel = ToVec4( contactVel );
    w.ContactVelLong = vLong; // fixed for this OnStep; the sub-loop recomputes
                              // slip from the evolving omega against it
    w.ContactPoint =
        Eigen::Vector4f( contact.GetX(), contact.GetY(), contact.GetZ(), 1.0f );
    w.WheelPos =
        Eigen::Vector4f( attachW.GetX(), attachW.GetY(), attachW.GetZ(), 1.0f );
    w.SpinAxis = ToVec4( up );
    w.bGroundContact = true;
    return true;
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
    filteredSteer +=
        JPH::Clamp( inputSteer - filteredSteer, -kSteerRatePerSec * dt,
                    kSteerRatePerSec * dt );

    vehicle.ForwardSpeed =
        chassis.GetLinearVelocity().Dot( world.GetColumn3( 2 ) );
    StepPowertrain( vehicle, inputThrottle, dt );

    for ( int i = 0; i < vehicle.NumWheels; ++i ) {
        PhysicsWheelState &w = vehicle.Wheels[i];
        const bool hit = castWheel( system, chassis, i, dt );

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

        if ( inputBrake > 0.0f ) {
            const float brakeTq = inputBrake * vehicle.BrakeTorque;
            if ( w.AngularVel > 0.0f ) {
                w.DriveTorque -= brakeTq;
            } else if ( w.AngularVel < 0.0f ) {
                w.DriveTorque += brakeTq;
            }
        }

        if ( gVehicleDrivingAids && hit && w.bDriven ) {
            const float vLong = w.ContactVelLong;
            const float vLat = w.ContactVelLat;
            if ( std::fabs( w.AngularVel ) > 0.1f &&
                 std::fabs( vLong ) > 0.1f ) {
                const float radius = std::max( w.Radius, 0.05f );
                const float slipAngle = std::atan( vLat / vLong );
                const float xrel =
                    JPH::Clamp( slipAngle / kEspPeakSlipAngle, -1.0f, 1.0f );
                const float curve =
                    std::sqrt( std::max( 0.0f, 1.0f - xrel * xrel ) );
                const float sOmega = ( w.AngularVel > 0.0f ) ? 1.0f : -1.0f;
                const float speed = std::sqrt( vLat * vLat + vLong * vLong );
                const float newVal =
                    ( speed * curve * sOmega + vLong ) / radius; // target omega
                const float effInertia = w.Inertia + w.ReflectedInertia;
                const float authority =
                    kEspAuthorityFrac * w.GripLong * w.NormalLoad * radius;
                const float espTorque =
                    JPH::Clamp( ( newVal - w.AngularVel ) * ( effInertia / dt ),
                                -authority, authority );
                // Intervene only when it reduces the forward drive (retard a
                // wheel that is fast for its combined slip).
                if ( sOmega * espTorque < sOmega * w.DriveTorque ) {
                    w.DriveTorque = espTorque;
                }
            }
        }

        if ( hit ) {
            const float hSub = dt / static_cast<float>( kWheelSubsteps );
            const float vLong = w.ContactVelLong;
            const float radius = std::max( w.Radius, 0.05f );
            for ( int s = 0; s < kWheelSubsteps; ++s ) {
                const float longSlip =
                    ( w.AngularVel * radius - vLong ) /
                    std::max( std::fabs( vLong ), kSlipDenom );
                w.ForwardDir = w.ContactNormal * longSlip;
                ComputeTireForces( w, vehicleMatrix );
                IntegrateWheel( w, hSub );
            }
        } else {
            IntegrateWheel( w, dt ); // airborne: spin freely on drive torque
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
                    2.0f * ( ( inputThrottle > 0.0f ) ? maxReact : minReact );
                vehicle.ReflectedWheelSpeed = kfb * diffOmega; // responsive
                const float kTwSmooth = 0.2f;
                vehicle.ReactionTorque +=
                    ( diffReaction / kfb - vehicle.ReactionTorque ) * kTwSmooth;
            }
        }
    }

    const JPH::Vec3 fwdAxis = world.GetColumn3( 2 );
    const float rollRate = chassis.GetAngularVelocity().Dot( fwdAxis );
    const float kRollDamping = 1500.0f;
    bi.AddTorque( id, fwdAxis * ( -kRollDamping * rollRate ) );

    if ( gVehicleYawDamper ) {
        const JPH::Vec3 upAxis = world.GetColumn3( 1 );
        const float yawRate = chassis.GetAngularVelocity().Dot( upAxis );
        bi.AddTorque( id, upAxis * ( -kYawDamping * yawRate ) );
    }

    bi.ActivateBody( id );
}

void PhysicsVehicle::tick( float deltaTime ) {
    if ( pChassisBody == nullptr ) {
        return;
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

Matrix4x4f_t PhysicsVehicle::getChassisWorldMatrix() const {
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
    out.SteerAngle = w.bSteered ? -filteredSteer * steerLockRad : 0.0f;
    out.RotationAngle = w.SpinAngle;
    out.SuspensionLength = prevSuspLength[index];
    out.SuspensionDeflection = JPH::Clamp(
        suspRestLength[index] - prevSuspLength[index], -0.25f, 0.25f );
    out.LongitudinalSlip = w.NormLongClamped;
    out.LateralSlip = w.NormLatClamped;
    out.LongitudinalGroundSpeed = w.ContactVelLong;
    out.SuspensionLoad = w.NormalLoad;
    out.Radius = w.Radius;
    out.bGrounded = w.bGroundContact;
    out.ContactSurface = wheelSurface[index];
    return out;
}

float PhysicsVehicle::getChassisSpeed() const {
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return 0.0f;
    }
    return pPhysicsSystem->GetBodyInterface()
        .GetLinearVelocity( pChassisBody->GetID() )
        .Length();
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
