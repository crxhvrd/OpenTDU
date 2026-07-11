#include "shared.h"
#include "physics_car.h"

#include "physics_layers.h"
#include "physics_config.h"
#include "input/gs_device.h"
#include "database/gs_database.h"

#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <algorithm>
#include <cmath>

bool gVehicleDriverAids = true;
float gVehicleAidsLevel = 1.0f;
bool gVehicleSteeringFilter = false;

namespace {
    constexpr float kMmToM = 0.001f;
    constexpr float kDefaultWheelRadius = 0.34f; // TODO: from the rim/tire DB
    constexpr float kDefaultWheelWidth = 0.22f;
    constexpr float kSuspensionAttachY = -0.10f;
    // Safety clamp on the wheel-node suspension-bob deflection.
    constexpr float kMaxSuspensionBob = 0.25f;

    // Wheel raycast tester that reproduces Havok's
    // hkpVehicleRaycastWheelCollide
    class TDUWheelRayTester final : public JPH::VehicleCollisionTester {
    public:
        explicit TDUWheelRayTester(
            JPH::ObjectLayer inObjectLayer,
            JPH::Vec3Arg inUp = JPH::Vec3::sAxisY(),
            float inMaxSlopeAngle = JPH::DegreesToRadians( 80.0f ) )
            : JPH::VehicleCollisionTester( inObjectLayer ),
              mUp( inUp ),
              mCosMaxSlopeAngle( JPH::Cos( inMaxSlopeAngle ) ) {}

        bool Collide( JPH::PhysicsSystem &inPhysicsSystem,
                      const JPH::VehicleConstraint &inVehicleConstraint,
                      JPH::uint inWheelIndex,
                      JPH::RVec3Arg inOrigin,
                      JPH::Vec3Arg inDirection,
                      const JPH::BodyID &inVehicleBodyID,
                      JPH::Body *&outBody,
                      JPH::SubShapeID &outSubShapeID,
                      JPH::RVec3 &outContactPosition,
                      JPH::Vec3 &outContactNormal,
                      float &outSuspensionLength ) const override {
            const JPH::DefaultBroadPhaseLayerFilter
                default_broadphase_layer_filter =
                    inPhysicsSystem.GetDefaultBroadPhaseLayerFilter(
                        mObjectLayer );
            const JPH::BroadPhaseLayerFilter &broadphase_layer_filter =
                mBroadPhaseLayerFilter != nullptr
                    ? *mBroadPhaseLayerFilter
                    : default_broadphase_layer_filter;

            const JPH::DefaultObjectLayerFilter default_object_layer_filter =
                inPhysicsSystem.GetDefaultLayerFilter( mObjectLayer );
            const JPH::ObjectLayerFilter &object_layer_filter =
                mObjectLayerFilter != nullptr ? *mObjectLayerFilter
                                              : default_object_layer_filter;

            const JPH::IgnoreSingleBodyFilter default_body_filter(
                inVehicleBodyID );
            const JPH::BodyFilter &body_filter =
                mBodyFilter != nullptr ? *mBodyFilter : default_body_filter;

            const JPH::WheelSettings *wheel_settings =
                inVehicleConstraint.GetWheel( inWheelIndex )->GetSettings();
            float wheel_radius = wheel_settings->mRadius;
            float ray_length =
                wheel_settings->mSuspensionMaxLength + wheel_radius;
            JPH::RRayCast ray{ inOrigin, ray_length * inDirection };

            class MyCollector : public JPH::CastRayCollector {
            public:
                MyCollector( JPH::PhysicsSystem &inPhysicsSystem,
                             const JPH::RRayCast &inRay,
                             JPH::Vec3Arg inUpDirection,
                             float inCosMaxSlopeAngle )
                    : mPhysicsSystem( inPhysicsSystem ),
                      mRay( inRay ),
                      mUpDirection( inUpDirection ),
                      mCosMaxSlopeAngle( inCosMaxSlopeAngle ) {}

                void AddHit( const JPH::RayCastResult &inResult ) override {
                    if ( inResult.mFraction < GetEarlyOutFraction() ) {
                        JPH::BodyLockRead lock(
                            mPhysicsSystem.GetBodyLockInterfaceNoLock(),
                            inResult.mBodyID );
                        JPH_ASSERT( lock.Succeeded() );
                        const JPH::Body *body = &lock.GetBody();

                        if ( body->IsSensor() ) {
                            return;
                        }

                        JPH::RVec3 contact_pos =
                            mRay.GetPointOnRay( inResult.mFraction );
                        JPH::Vec3 normal = body->GetWorldSpaceSurfaceNormal(
                            inResult.mSubShapeID2, contact_pos );
                        if ( normal.Dot( mUpDirection ) > mCosMaxSlopeAngle ) {
                            UpdateEarlyOutFraction( inResult.mFraction );
                            mBody = body;
                            mSubShapeID2 = inResult.mSubShapeID2;
                            mContactPosition = contact_pos;
                            mContactNormal = normal;
                        }
                    }
                }

                JPH::PhysicsSystem &mPhysicsSystem;
                JPH::RRayCast mRay;
                JPH::Vec3 mUpDirection;
                float mCosMaxSlopeAngle;
                const JPH::Body *mBody = nullptr;
                JPH::SubShapeID mSubShapeID2;
                JPH::RVec3 mContactPosition;
                JPH::Vec3 mContactNormal;
            };

            JPH::RayCastSettings settings;
            settings.mTreatConvexAsSolid = false;

            MyCollector collector( inPhysicsSystem, ray, mUp,
                                   mCosMaxSlopeAngle );
            inPhysicsSystem.GetNarrowPhaseQueryNoLock().CastRay(
                ray, settings, collector, broadphase_layer_filter,
                object_layer_filter, body_filter );
            if ( collector.mBody == nullptr ) {
                return false;
            }

            outBody = const_cast<JPH::Body *>( collector.mBody );
            outSubShapeID = collector.mSubShapeID2;
            outContactPosition = collector.mContactPosition;
            outContactNormal = collector.mContactNormal;
            outSuspensionLength =
                std::max( 0.0f, ray_length * collector.GetEarlyOutFraction() -
                                    wheel_radius );
            return true;
        }

        void PredictContactProperties(
            JPH::PhysicsSystem &inPhysicsSystem,
            const JPH::VehicleConstraint &inVehicleConstraint,
            JPH::uint inWheelIndex,
            JPH::RVec3Arg inOrigin,
            JPH::Vec3Arg inDirection,
            const JPH::BodyID &inVehicleBodyID,
            JPH::Body *&ioBody,
            JPH::SubShapeID &ioSubShapeID,
            JPH::RVec3 &ioContactPosition,
            JPH::Vec3 &ioContactNormal,
            float &ioSuspensionLength ) const override {
            const JPH::WheelSettings *wheel_settings =
                inVehicleConstraint.GetWheel( inWheelIndex )->GetSettings();
            float d_dot_n = inDirection.Dot( ioContactNormal );
            if ( d_dot_n < -1.0e-6f ) {
                ioContactPosition =
                    inOrigin + JPH::Vec3( ioContactPosition - inOrigin )
                                       .Dot( ioContactNormal ) /
                                   d_dot_n * inDirection;
                ioSuspensionLength =
                    JPH::Clamp( JPH::Vec3( ioContactPosition - inOrigin )
                                        .Dot( inDirection ) -
                                    wheel_settings->mRadius,
                                0.0f, wheel_settings->mSuspensionMaxLength );
            } else {
                ioSuspensionLength = wheel_settings->mSuspensionMaxLength;
            }
        }

    private:
        JPH::Vec3 mUp;
        float mCosMaxSlopeAngle;
    };
} // namespace

PhysicsCar::PhysicsCar()
    : pPhysicsSystem( nullptr ),
      pChassisBody( nullptr ),
      inputThrottle( 0.0f ),
      inputBrake( 0.0f ),
      inputSteer( 0.0f ) {
    for ( float &rest : restSuspensionLength ) {
        rest = -1.0f;
    }
    for ( int32_t axle = 0; axle < 2; axle++ ) {
        axleDriveGrip[axle] = 1.0f;
        axleBrakeGrip[axle] = 1.0f;
    }
    for ( bool &driven : wheelDriven ) {
        driven = true;
    }
    prevWorldVelocity[0] = 0.0f;
    prevWorldVelocity[1] = 0.0f;
    prevWorldVelocity[2] = 0.0f;
    localAccelLong = 0.0f;
    localAccelLat = 0.0f;
    aeroDragCoef = 0.0f;
    aeroDownforceCoef = 0.0f;
    aeroHalfWheelBase = 0.0f;
    aeroFrontFraction = 0.5f;
    steerLockRad = 0.0f;
    filteredSteer = 0.0f;
}

PhysicsCar::~PhysicsCar() { destroy(); }

void PhysicsCar::initialize() {
    // FUN_00828a50
    OTDU_UNIMPLEMENTED;
}

void PhysicsCar::setInput( float throttle, float brake, float steer ) {
    inputThrottle = throttle;
    inputBrake = brake;
    inputSteer = steer;
}

Matrix4x4f_t PhysicsCar::getChassisWorldMatrix() const {
    Matrix4x4f_t m = Matrix4x4f_t::Identity();
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return m;
    }
    // Inverse of createVehicle's placement: Jolt column c == TDU row c.
    JPH::RMat44 world = pPhysicsSystem->GetBodyInterface().GetWorldTransform(
        pChassisBody->GetID() );
    for ( int32_t c = 0; c < 4; c++ ) {
        JPH::Vec3 col = world.GetColumn3( c );
        m( c, 0 ) = col.GetX();
        m( c, 1 ) = col.GetY();
        m( c, 2 ) = col.GetZ();
    }
    return m;
}

uint32_t PhysicsCar::getNumWheels() const {
    if ( pConstraint == nullptr ) {
        return 0;
    }
    return static_cast<uint32_t>( pConstraint->GetWheels().size() );
}

PhysicsCar::WheelReadout PhysicsCar::getWheelReadout( uint32_t index ) const {
    WheelReadout out = {};
    out.LocalPosition = Eigen::Vector3f::Zero();
    if ( pConstraint == nullptr || index >= pConstraint->GetWheels().size() ) {
        return out;
    }

    const JPH::WheelWV *pWheel =
        static_cast<const JPH::WheelWV *>( pConstraint->GetWheel( index ) );
    out.bGrounded = pWheel->HasContact();
    out.AngularVelocity = pWheel->GetAngularVelocity();
    out.SteerAngle = pWheel->GetSteerAngle();
    out.RotationAngle = pWheel->GetRotationAngle();
    out.SuspensionLength = pWheel->GetSuspensionLength();
    out.SuspensionLoad = pWheel->GetSuspensionLambda();
    out.SuspensionDeflection =
        ( restSuspensionLength[index] >= 0.0f )
            ? JPH::Clamp( restSuspensionLength[index] - out.SuspensionLength,
                          -kMaxSuspensionBob, kMaxSuspensionBob )
            : 0.0f;
    out.LongitudinalSlip = pWheel->mLongitudinalSlip;
    out.LateralSlip = pWheel->mLateralSlip;

    const JPH::WheelSettings *pSettings = pWheel->GetSettings();
    out.Radius = pSettings->mRadius;
    const JPH::Vec3 p = pSettings->mPosition;
    out.LocalPosition = Eigen::Vector3f( p.GetX(), p.GetY(), p.GetZ() );

    if ( out.bGrounded ) {
        const JPH::Vec3 v = pWheel->GetContactPointVelocity();
        const JPH::Vec3 fwd = pWheel->GetContactLongitudinal();
        out.LongitudinalGroundSpeed = v.Dot( fwd );
        if ( pPhysicsSystem != nullptr ) {
            out.ContactSurface = static_cast<uint32_t>(
                pPhysicsSystem->GetBodyInterface().GetUserData(
                    pWheel->GetContactBodyID() ) );
        }
    }
    return out;
}

float PhysicsCar::getChassisSpeed() const {
    if ( pPhysicsSystem == nullptr || pChassisBody == nullptr ) {
        return 0.0f;
    }
    return pPhysicsSystem->GetBodyInterface()
        .GetLinearVelocity( pChassisBody->GetID() )
        .Length();
}

float PhysicsCar::getEngineAngularVelocity() const {
    if ( pConstraint == nullptr ) {
        return 0.0f;
    }
    const JPH::WheeledVehicleController *pController =
        static_cast<const JPH::WheeledVehicleController *>(
            pConstraint->GetController() );
    return pController->GetEngine().GetAngularVelocity();
}

int32_t PhysicsCar::getCurrentGear() const {
    if ( pConstraint == nullptr ) {
        return 0;
    }
    const JPH::WheeledVehicleController *pController =
        static_cast<const JPH::WheeledVehicleController *>(
            pConstraint->GetController() );
    return pController->GetTransmission().GetCurrentGear();
}

PhysicsCar::SuspensionSetup
PhysicsCar::ComputeAxleSuspension( const DBEntryCarPhysicsData &db,
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
    // Keep DB rate, but floor the frequency at 1.0 Hz so the natural
    // sag can't exceed ~0.25 m
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

bool PhysicsCar::create( JPH::PhysicsSystem *pSystem,
                         JPH::Body *pChassis,
                         const DBEntryCarPhysicsData &db ) {
    if ( pSystem == nullptr || pChassis == nullptr || pConstraint != nullptr ) {
        return false;
    }
    pPhysicsSystem = pSystem;
    pChassisBody = pChassis;

    const float maxRPM = Max( 1000.0f, static_cast<float>( db.Redline ) );
    const float halfTrackFront = db.TrackFront * kMmToM * 0.5f;
    const float halfTrackRear = db.TrackBack * kMmToM * 0.5f;
    const float halfWheelBase = db.WheelBase * kMmToM * 0.5f;
    const float steerLock =
        JPH::DegreesToRadians( static_cast<float>( db.SteerLock ) );
    steerLockRad = steerLock;

    {
        const float frontalArea = ( db.Width > 0 ? db.Width : 1800 ) *
                                  ( db.Height > 0 ? db.Height : 1300 ) *
                                  1.0e-6f;
        const float airDensity = 1.2f;
        aeroDragCoef =
            0.5f * airDensity * Max( 0.0f, db.CxLength ) * frontalArea;
        aeroDownforceCoef =
            0.5f * airDensity * Max( 0.0f, db.CxHeight ) * frontalArea;
        aeroHalfWheelBase = halfWheelBase;
        aeroFrontFraction = 0.17f;
        OTDU_LOG_DEBUG( "Aero: CxLength=%.3f CxHeight=%.3f area=%.2fm2 "
                        "downforce@50m/s=%.0fN\n",
                        db.CxLength, db.CxHeight, frontalArea,
                        aeroDownforceCoef * 2500.0f );
    }

    const float bodyHalfHeight =
        ( db.Height > 0 ? db.Height : 1300 ) * kMmToM * 0.5f;

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
        if ( !bInRange ) {
            pRim = nullptr;
        }
        if ( pRim != nullptr ) {
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
            auto axleWidth = []( int32_t sectionWidthMM,
                                 float fallback ) -> float {
                return sectionWidthMM > 0
                           ? JPH::Clamp( sectionWidthMM / 1000.0f, 0.12f,
                                         0.40f )
                           : fallback;
            };
            frontRadius =
                axleRadius( pRim->FrontWidth, pRim->FrontHeight,
                            pRim->FrontDiameter, kDefaultWheelRadius );
            rearRadius = axleRadius( pRim->RearWidth, pRim->RearHeight,
                                     pRim->RearDiameter, kDefaultWheelRadius );
            frontWidth = axleWidth( pRim->FrontWidth, kDefaultWheelWidth );
            rearWidth = axleWidth( pRim->RearWidth, kDefaultWheelWidth );
        }
    }

    JPH::VehicleConstraintSettings constraintSettings;
    constraintSettings.mUp = JPH::Vec3( 0, 1, 0 );
    constraintSettings.mForward = JPH::Vec3( 0, 0, 1 );
    constraintSettings.mMaxPitchRollAngle = JPH::DegreesToRadians( 60.0f );

    // Wheel order: 0 = front-left, 1 = front-right, 2 = rear-left, 3 =
    // rear-right (forward = +Z, left = -X). Front wheels steer; rears carry the
    // hand brake.
    struct WheelLayout {
        float X;
        float Z;
        bool bFront;
    };
    const WheelLayout layout[4] = {
        {-halfTrackFront, +halfWheelBase, true },
        {+halfTrackFront, +halfWheelBase, true },
        {-halfTrackRear,  -halfWheelBase, false},
        {+halfTrackRear,  -halfWheelBase, false},
    };

    // Per-axle suspension geometry (band-limited ride frequency + real
    // compression headroom; see ComputeAxleSuspension). Shared with
    // GSPhysics::createVehicle so the chassis CoM sits at the correct height.
    const PhysicsCar::SuspensionSetup frontSusp =
        ComputeAxleSuspension( db, true );
    const PhysicsCar::SuspensionSetup rearSusp =
        ComputeAxleSuspension( db, false );

    constexpr float kTwoPi = 6.28318530718f;
    constexpr float kGravity = 9.81f;

    const float mass = Max( 500.0f, static_cast<float>( db.Weight ) );
    const float brakeBalance = JPH::Clamp(
        db.BrakeBalance > 0 ? db.BrakeBalance * 0.01f : 0.6f, 0.1f, 0.9f );

    auto cfgOr = []( float value, float fallback ) {
        return value > 0.0f ? value : fallback;
    };

    const float powerToWeight =
        db.Weight > 0 ? static_cast<float>( db.BHP ) / db.Weight : 0.15f;
    const float gripFactor =
        JPH::Clamp( ( powerToWeight - 0.15f ) * 2.0f + 0.7f, 0.7f, 1.0f );
    OTDU_LOG_DEBUG( "Grip: BHP=%d Weight=%d p/w=%.3f -> factor=%.2f\n", db.BHP,
                    db.Weight, powerToWeight, gripFactor );

    for ( int32_t i = 0; i < 4; i++ ) {
        const WheelLayout &wl = layout[i];
        const PhysicsCar::SuspensionSetup &susp =
            wl.bFront ? frontSusp : rearSusp;
        JPH::SpringSettings spring;
        spring.mMode = JPH::ESpringMode::FrequencyAndDamping;
        spring.mFrequency = susp.frequency;
        spring.mDamping = susp.damping;

        const float wheelRadius = wl.bFront ? frontRadius : rearRadius;
        const float wheelWidth = wl.bFront ? frontWidth : rearWidth;
        const float attachY = -Max( 0.0f, bodyHalfHeight - wheelRadius );

        JPH::WheelSettingsWV *pWheel = new JPH::WheelSettingsWV();
        pWheel->mPosition = JPH::Vec3( wl.X, attachY, wl.Z );
        pWheel->mSuspensionDirection = JPH::Vec3( 0, -1, 0 );
        pWheel->mSuspensionMinLength = susp.minLength;
        pWheel->mSuspensionMaxLength = susp.maxLength;
        pWheel->mSuspensionSpring = spring;
        pWheel->mRadius = wheelRadius;
        pWheel->mWidth = wheelWidth;
        pWheel->mMaxSteerAngle = wl.bFront ? steerLock : 0.0f;

        const float gripCoef =
            gripFactor * cfgOr( wl.bFront ? gPhysicsConfig.FrontFrictionCoef
                                          : gPhysicsConfig.RearFrictionCoef,
                                1.5f );
        const float lateralCoef =
            gripFactor * cfgOr( wl.bFront
                                    ? gPhysicsConfig.FrontLateralFrictionCoef
                                    : gPhysicsConfig.RearLateralFrictionCoef,
                                1.3f );
        const float brakeCoef =
            gripFactor * cfgOr( wl.bFront
                                    ? gPhysicsConfig.FrontLngBrakeFrictionCoef
                                    : gPhysicsConfig.RearLngBrakeFrictionCoef,
                                1.2f );
        const float peakSlipAngle =
            JPH::Clamp( cfgOr( wl.bFront ? gPhysicsConfig.WheelMaxSlipAngleFront
                                         : gPhysicsConfig.WheelMaxSlipAngleRear,
                               5.0f ),
                        2.0f, 30.0f );

        axleDriveGrip[wl.bFront ? 0 : 1] = gripCoef;
        axleBrakeGrip[wl.bFront ? 0 : 1] = brakeCoef;

        pWheel->mLongitudinalFriction.Clear();
        pWheel->mLongitudinalFriction.AddPoint( 0.0f, 0.0f );
        pWheel->mLongitudinalFriction.AddPoint( 0.06f, gripCoef );
        pWheel->mLongitudinalFriction.AddPoint( 0.20f, gripCoef * 0.85f );
        pWheel->mLongitudinalFriction.Sort();

        pWheel->mLateralFriction.Clear();
        pWheel->mLateralFriction.AddPoint( 0.0f, 0.0f );
        pWheel->mLateralFriction.AddPoint( peakSlipAngle, lateralCoef );
        pWheel->mLateralFriction.AddPoint( peakSlipAngle * 4.0f,
                                           lateralCoef * 0.9f );
        pWheel->mLateralFriction.Sort();

        const float axleBrakeBalance =
            wl.bFront ? brakeBalance : ( 1.0f - brakeBalance );
        pWheel->mMaxBrakeTorque =
            0.5f * mass * kGravity * wheelRadius * brakeCoef * axleBrakeBalance;
        pWheel->mMaxHandBrakeTorque = wl.bFront ? 0.0f : 4000.0f;

        restSuspensionLength[i] = susp.restLength;
        constraintSettings.mWheels.push_back( pWheel );
    }

    const float massPerWheel = mass * 0.25f;
    auto axleAntiRollStiffness = [&]( float frequency ) {
        const float omega = kTwoPi * frequency;
        const float wheelRate = massPerWheel * omega * omega;
        return JPH::Clamp( wheelRate * 0.25f, 1500.0f, 30000.0f );
    };
    JPH::VehicleAntiRollBar frontAntiRoll;
    frontAntiRoll.mLeftWheel = 0;
    frontAntiRoll.mRightWheel = 1;
    frontAntiRoll.mStiffness = axleAntiRollStiffness( frontSusp.frequency );
    constraintSettings.mAntiRollBars.push_back( frontAntiRoll );
    JPH::VehicleAntiRollBar rearAntiRoll;
    rearAntiRoll.mLeftWheel = 2;
    rearAntiRoll.mRightWheel = 3;
    rearAntiRoll.mStiffness = axleAntiRollStiffness( rearSusp.frequency );
    constraintSettings.mAntiRollBars.push_back( rearAntiRoll );

    JPH::WheeledVehicleControllerSettings *pController =
        new JPH::WheeledVehicleControllerSettings();

    // Engine: peak torque + redline from the DB, with a normalized torque curve
    // built from the published peak-torque and peak-power rev points.
    pController->mEngine.mMaxTorque = static_cast<float>( db.TorqueNm );
    pController->mEngine.mMaxRPM = maxRPM;
    pController->mEngine.mMinRPM = Max( 700.0f, maxRPM * 0.12f );
    pController->mEngine.mInertia =
        db.EngineInertia > 0 ? Max( 0.05f, db.EngineInertia * 0.004f ) : 0.5f;
    {
        const float torquePeakX =
            JPH::Clamp( db.TorqueRPM / maxRPM, 0.05f, 0.95f );
        const float powerPeakX =
            JPH::Clamp( db.RPM / maxRPM, torquePeakX + 0.01f, 1.0f );
        JPH::LinearCurve &curve = pController->mEngine.mNormalizedTorque;
        curve.Clear();
        curve.AddPoint( 0.0f, 0.60f );
        curve.AddPoint( torquePeakX, 1.00f );
        curve.AddPoint( powerPeakX, 0.92f );
        curve.AddPoint( 1.0f, 0.72f );
        curve.Sort();
    }

    // Transmission: the DB gear ratios + final drive (the final drive goes into
    // the differential ratio below).
    pController->mTransmission.mMode = JPH::ETransmissionMode::Auto;
    pController->mTransmission.mGearRatios.clear();
    const int32_t numGears = std::clamp( db.NumGears, 1, 7 );
    for ( int32_t g = 0; g < numGears; g++ ) {
        if ( db.GearRatios[g] > 0.0f ) {
            pController->mTransmission.mGearRatios.push_back(
                db.GearRatios[g] );
        }
    }
    if ( pController->mTransmission.mGearRatios.empty() ) {
        pController->mTransmission.mGearRatios.push_back( 3.0f );
    }
    pController->mTransmission.mShiftUpRPM = maxRPM * 0.90f;
    pController->mTransmission.mShiftDownRPM = maxRPM * 0.45f;

    // Differential drive layout from db.DriveType (FUN_008345e0:
    // 0x035bf035 = FWD, 0x034cadf5 = RWD, otherwise all-wheel drive).
    const float finalDrive =
        db.FinalDriveRatio > 0.1f ? db.FinalDriveRatio : 3.42f;
    const uint32_t driveType = static_cast<uint32_t>( db.DriveType.Hashcode );

    auto addDifferential = [&]( int32_t leftWheel, int32_t rightWheel,
                                float torqueRatio ) {
        JPH::VehicleDifferentialSettings differential;
        differential.mLeftWheel = leftWheel;
        differential.mRightWheel = rightWheel;
        differential.mDifferentialRatio = finalDrive;
        differential.mEngineTorqueRatio = torqueRatio;
        pController->mDifferentials.push_back( differential );
    };

    if ( driveType == 0x035bf035u ) {
        addDifferential( 0, 1, 1.0f );
        wheelDriven[0] = wheelDriven[1] = true;
        wheelDriven[2] = wheelDriven[3] = false;
    } else if ( driveType == 0x034cadf5u ) {
        addDifferential( 2, 3, 1.0f );
        wheelDriven[0] = wheelDriven[1] = false;
        wheelDriven[2] = wheelDriven[3] = true;
    } else {
        addDifferential( 0, 1, 0.5f );
        addDifferential( 2, 3, 0.5f );
        wheelDriven[0] = wheelDriven[1] = wheelDriven[2] = wheelDriven[3] =
            true;
    }

    constraintSettings.mController = pController;

    pConstraint = new JPH::VehicleConstraint( *pChassis, constraintSettings );

    JPH::WheeledVehicleController *pRuntimeController =
        static_cast<JPH::WheeledVehicleController *>(
            static_cast<JPH::VehicleConstraint *>( pConstraint )
                ->GetController() );
    pRuntimeController->SetTireMaxImpulseCallback(
        [this]( JPH::uint wheelIndex, float &outLongitudinalImpulse,
                float &outLateralImpulse, float suspensionImpulse,
                float longitudinalFriction, float lateralFriction,
                float longitudinalSlip, float lateralSlip, float deltaTime ) {
            float longitudinalCoef = longitudinalFriction;
            if ( inputBrake > 0.0f && inputThrottle <= 0.0f ) {
                const int32_t axle = wheelIndex < 2 ? 0 : 1;
                const float drive = axleDriveGrip[axle];
                const float brake = axleBrakeGrip[axle];
                if ( drive > 0.0f ) {
                    longitudinalCoef *= brake / drive;
                }
            }
            outLongitudinalImpulse = longitudinalCoef * suspensionImpulse;
            outLateralImpulse = lateralFriction * suspensionImpulse;
        } );

    pCollisionTester = new TDUWheelRayTester( TDUPhysLayers::WHEEL );
    static_cast<JPH::VehicleConstraint *>( pConstraint )
        ->SetVehicleCollisionTester( pCollisionTester );

    pSystem->AddConstraint( pConstraint );
    pSystem->AddStepListener( pConstraint );
    return true;
}

void PhysicsCar::applyDriverAids( float steer,
                                  float &throttle,
                                  float &brake,
                                  float &espYawTorqueLocalUp ) const {
    espYawTorqueLocalUp = 0.0f;
    if ( !gVehicleDriverAids || pConstraint == nullptr ||
         pChassisBody == nullptr ) {
        return;
    }

    const float aidsLevel = JPH::Clamp( gVehicleAidsLevel, 0.0f, 1.0f );

    const float speed = getChassisSpeed(); // m/s
    const uint32_t numWheels = getNumWheels();

    float worstBrakeSlip = 0.0f; // over grounded wheels (while braking)
    float worstDriveSlip =
        0.0f; // over driven, grounded wheels (while on power)
    int32_t groundedCount = 0;
    for ( uint32_t i = 0; i < numWheels && i < 4; i++ ) {
        const WheelReadout w = getWheelReadout( i );
        if ( !w.bGrounded ) {
            continue;
        }
        groundedCount++;
        worstBrakeSlip = Max( worstBrakeSlip, w.LongitudinalSlip );
        if ( wheelDriven[i] ) {
            worstDriveSlip = Max( worstDriveSlip, w.LongitudinalSlip );
        }
    }

    // ABS
    if ( brake > 0.0f && speed > 3.0f ) {
        const float *pSpd = gPhysicsConfig.ActiveABSSpeed; // [0..2] m/s
        const float *pVal = gPhysicsConfig.ActiveABSValue; // [0..2] target slip
        float target = 0.6f;
        if ( pSpd[0] > 0.0f && pSpd[2] > pSpd[0] ) {
            if ( speed <= pSpd[0] ) {
                target = pVal[0];
            } else if ( speed >= pSpd[2] ) {
                target = pVal[2];
            } else if ( speed < pSpd[1] ) {
                const float t = ( speed - pSpd[0] ) / ( pSpd[1] - pSpd[0] );
                target = pVal[0] + t * ( pVal[1] - pVal[0] );
            } else {
                const float t = ( speed - pSpd[1] ) / ( pSpd[2] - pSpd[1] );
                target = pVal[1] + t * ( pVal[2] - pVal[1] );
            }
        }
        target = JPH::Clamp( target, 0.12f, 0.8f );
        const float excess = worstBrakeSlip - target;
        if ( excess > 0.0f ) {
            brake *= 1.0f - JPH::Clamp( excess * 2.5f, 0.0f, 0.85f );
        }
    }

    // TC
    if ( throttle > 0.0f && speed > 2.5f && aidsLevel > 0.0f ) {
        const bool bStraight = std::abs( steer ) < 0.15f;
        // [0] = trigger slip, value[0] = cut gain (fallbacks when .ini is
        // silent).
        const float trigger = bStraight
                                  ? ( gPhysicsConfig.TCStraightTrigger[0] > 0.0f
                                          ? gPhysicsConfig.TCStraightTrigger[0]
                                          : 0.35f )
                                  : ( gPhysicsConfig.ActiveTCTrigger[0] > 0.0f
                                          ? gPhysicsConfig.ActiveTCTrigger[0]
                                          : 0.5f );
        const float gain = bStraight
                               ? ( gPhysicsConfig.TCStraightValue[0] > 0.0f
                                       ? gPhysicsConfig.TCStraightValue[0]
                                       : 1.0f )
                               : ( gPhysicsConfig.ActiveTCValue[0] > 0.0f
                                       ? gPhysicsConfig.ActiveTCValue[0]
                                       : 1.0f );
        const float excess = worstDriveSlip - trigger;
        if ( excess > 0.0f ) {
            throttle *=
                1.0f - JPH::Clamp( excess * gain * aidsLevel, 0.0f, 0.75f );
        }
    }

    // ESP
    if ( groundedCount > 0 && speed > 3.0f && aidsLevel > 0.0f ) {
        JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
        const JPH::Quat rot = bi.GetRotation( pChassisBody->GetID() );
        const JPH::Vec3 localAngVel =
            rot.Conjugated() * bi.GetAngularVelocity( pChassisBody->GetID() );
        const float actualYaw = localAngVel.GetY();

        const float wheelBase = Max( 1.0f, 2.0f * aeroHalfWheelBase );
        const float steerAngle = steer * steerLockRad;
        float desiredYaw = speed * std::tan( steerAngle ) / wheelBase;
        constexpr float kEspMaxLatG = 1.1f;
        const float yawLimit = ( kEspMaxLatG * 9.81f ) / Max( speed, 1.0f );
        desiredYaw = JPH::Clamp( desiredYaw, -yawLimit, yawLimit );

        const float yawErr = actualYaw - desiredYaw;
        const float absA = std::abs( actualYaw );
        const float absD = std::abs( desiredYaw );
        constexpr float kEspDeadband = 0.06f; // rad/s (~3.4 deg/s tolerance)

        if ( absA > absD + kEspDeadband ) {
            // Oversteering
            constexpr float kEspYawGain = 4.0f;
            float izz = 900.0f; // fallback yaw inertia, kg.m^2
            const JPH::MotionProperties *pMotion =
                pChassisBody->GetMotionProperties();
            if ( pMotion != nullptr ) {
                const float invIyy =
                    pMotion->GetInverseInertiaDiagonal().GetY();
                if ( invIyy > 1.0e-8f ) {
                    izz = 1.0f / invIyy;
                }
            }
            espYawTorqueLocalUp = -izz * kEspYawGain * yawErr * aidsLevel;
            if ( throttle > 0.0f ) {
                throttle *=
                    1.0f - JPH::Clamp( ( absA - absD ) * 1.2f * aidsLevel, 0.0f,
                                       0.7f );
            }
        } else if ( absD > absA + kEspDeadband && throttle > 0.0f ) {
            // Understeering
            throttle *= 1.0f - JPH::Clamp( ( absD - absA ) * 1.5f * aidsLevel,
                                           0.0f, 0.6f );
        }
    }
}

float PhysicsCar::filterSteering( float rawTarget, float deltaTime ) {
    // FUN_0041ad60
    // NOTE: Incomplete for now
    if ( !gVehicleSteeringFilter ) {
        filteredSteer = rawTarget; // keep state coherent for a clean toggle
        return rawTarget;
    }
    const float dt = Max( 0.0f, deltaTime );
    const float sign = rawTarget >= 0.0f ? 1.0f : -1.0f;
    const float mag = std::abs( rawTarget );

    // DeadZone
    const float deadZone = JPH::Clamp( gSteeringDeadZone, 0.0f, 0.9f );
    float shaped =
        mag <= deadZone ? 0.0f : ( mag - deadZone ) / ( 1.0f - deadZone );

    // SteeringLinearity
    auto responseCurve = []( float t ) {
        return t + 0.5f * t * t + ( 1.0f / 3.0f ) * t * t * t;
    };
    const float expo = responseCurve( shaped ) / responseCurve( 1.0f );
    const float linearFrac =
        JPH::Clamp( gSteeringLinearity * 0.01f, 0.0f, 1.0f );
    shaped = linearFrac * shaped + ( 1.0f - linearFrac ) * expo;

    // SteeringTurn
    const float speed = getChassisSpeed(); // m/s
    const float speedScale =
        JPH::Clamp( 1.0f - ( speed - 12.0f ) * 0.011f, 0.35f, 1.0f );
    const float target = sign * shaped * speedScale;

    if ( dt > 0.0f ) {
        constexpr float kTurnRate = 3.2f;   // /s
        constexpr float kCenterRate = 5.0f; // /s (faster self-centre)
        const bool returning = std::abs( target ) < std::abs( filteredSteer );
        const float maxDelta = ( returning ? kCenterRate : kTurnRate ) * dt;
        filteredSteer +=
            JPH::Clamp( target - filteredSteer, -maxDelta, maxDelta );
    } else {
        filteredSteer = target;
    }
    filteredSteer = JPH::Clamp( filteredSteer, -1.0f, 1.0f );

    float output = filteredSteer;
    if ( speed > 5.0f && pChassisBody != nullptr ) {
        JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
        const JPH::Quat rot = bi.GetRotation( pChassisBody->GetID() );
        const JPH::Vec3 localVel =
            rot.Conjugated() * bi.GetLinearVelocity( pChassisBody->GetID() );
        // Slip angle beta = atan2(lateral, forward); + = sliding to the right,
        // so +beta steers right = into the slide (correct counter-steer either
        // way).
        const float beta =
            std::atan2( localVel.GetX(), std::abs( localVel.GetZ() ) + 0.1f );
        constexpr float kBetaDead = 0.06f; // rad (~3.4 deg) before helping
        float betaEff = 0.0f;
        if ( beta > kBetaDead ) {
            betaEff = beta - kBetaDead;
        } else if ( beta < -kBetaDead ) {
            betaEff = beta + kBetaDead;
        }
        constexpr float kHelpGain = 0.35f;
        constexpr float kHelpMax = 0.20f;
        output += JPH::Clamp( betaEff * kHelpGain, -kHelpMax, kHelpMax );
    }
    return JPH::Clamp( output, -1.0f, 1.0f );
}

void PhysicsCar::tick( float deltaTime ) {
    if ( pConstraint == nullptr ) {
        return;
    }

    JPH::WheeledVehicleController *pController =
        static_cast<JPH::WheeledVehicleController *>(
            pConstraint->GetController() );
    // Jolt: forward in [-1,1] (negative = reverse), brake/handbrake in [0,1],
    // right in [-1,1]. TDU brake doubles as reverse at a standstill, but that
    // lives in the (still-stubbed) input logic; here we pass the raw values.
    const float effectiveSteer = filterSteering( inputSteer, deltaTime );
    float aidedThrottle = inputThrottle;
    float aidedBrake = inputBrake;
    float espYawTorqueLocalUp = 0.0f;
    applyDriverAids( effectiveSteer, aidedThrottle, aidedBrake,
                     espYawTorqueLocalUp );
    pController->SetDriverInput( aidedThrottle, effectiveSteer, aidedBrake,
                                 0.0f );

    if ( pChassisBody != nullptr &&
         ( inputThrottle != 0.0f || inputBrake != 0.0f ||
           effectiveSteer != 0.0f || espYawTorqueLocalUp != 0.0f ) ) {
        pPhysicsSystem->GetBodyInterface().ActivateBody(
            pChassisBody->GetID() );
    }

    if ( pChassisBody != nullptr && deltaTime > 1.0e-5f ) {
        JPH::BodyInterface &bodyInterface = pPhysicsSystem->GetBodyInterface();
        const JPH::Vec3 velocity =
            bodyInterface.GetLinearVelocity( pChassisBody->GetID() );

        const float speedSq = velocity.LengthSq();
        if ( speedSq > 1.0f ) {
            const float speed = std::sqrt( speedSq );
            const float downTotal = aeroDownforceCoef * speedSq;
            const JPH::Vec3 aeroForce = velocity * ( -aeroDragCoef * speed ) +
                                        JPH::Vec3( 0.0f, -downTotal, 0.0f );
            bodyInterface.AddForce( pChassisBody->GetID(), aeroForce );

            const JPH::Quat aeroRotation =
                bodyInterface.GetRotation( pChassisBody->GetID() );
            const JPH::Vec3 rightAxis =
                aeroRotation * JPH::Vec3( 1.0f, 0.0f, 0.0f );
            const float pitchMagnitude = downTotal *
                                         ( 1.0f - 2.0f * aeroFrontFraction ) *
                                         aeroHalfWheelBase;
            bodyInterface.AddTorque( pChassisBody->GetID(),
                                     rightAxis * pitchMagnitude );
        }

        const JPH::Vec3 previous( prevWorldVelocity[0], prevWorldVelocity[1],
                                  prevWorldVelocity[2] );
        const JPH::Vec3 worldAccel = ( velocity - previous ) / deltaTime;
        const JPH::Quat rotation =
            bodyInterface.GetRotation( pChassisBody->GetID() );
        if ( espYawTorqueLocalUp != 0.0f ) {
            bodyInterface.AddTorque(
                pChassisBody->GetID(),
                rotation * JPH::Vec3( 0.0f, espYawTorqueLocalUp, 0.0f ) );
        }

        const JPH::Vec3 localAccel = rotation.Conjugated() * worldAccel;
        const float kSmoothing = 0.2f;
        localAccelLong += ( localAccel.GetZ() - localAccelLong ) * kSmoothing;
        localAccelLat += ( localAccel.GetX() - localAccelLat ) * kSmoothing;
        prevWorldVelocity[0] = velocity.GetX();
        prevWorldVelocity[1] = velocity.GetY();
        prevWorldVelocity[2] = velocity.GetZ();
    }
}

float PhysicsCar::getLateralG() const { return localAccelLat / 9.81f; }

float PhysicsCar::getLongitudinalG() const { return localAccelLong / 9.81f; }

void PhysicsCar::destroy() {
    if ( pPhysicsSystem != nullptr ) {
        if ( pConstraint != nullptr ) {
            pPhysicsSystem->RemoveStepListener( pConstraint );
            pPhysicsSystem->RemoveConstraint( pConstraint );
        }
        if ( pChassisBody != nullptr ) {
            JPH::BodyInterface &bodyInterface =
                pPhysicsSystem->GetBodyInterface();
            bodyInterface.RemoveBody( pChassisBody->GetID() );
            bodyInterface.DestroyBody( pChassisBody->GetID() );
        }
    }
    pConstraint = nullptr;
    pCollisionTester = nullptr;
    pPhysicsSystem = nullptr;
    pChassisBody = nullptr;
}
