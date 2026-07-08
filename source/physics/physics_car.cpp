#include "shared.h"
#include "physics_car.h"

#include "physics_layers.h"
#include "database/gs_database.h"

#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>

namespace {
    constexpr float kMmToM = 0.001f;
    constexpr float kDefaultWheelRadius = 0.34f; // TODO: from the rim/tire DB
    constexpr float kDefaultWheelWidth = 0.22f;
    constexpr float kSuspensionAttachY = -0.10f;
    // Safety clamp on the wheel-node suspension-bob deflection.
    constexpr float kMaxSuspensionBob = 0.25f;
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

    const float travelFront = db.SuspensionLengthFront > 0
                                  ? db.SuspensionLengthFront * kMmToM
                                  : 0.20f;
    const float travelRear =
        db.SuspensionLengthRear > 0 ? db.SuspensionLengthRear * kMmToM : 0.20f;

    const float bodyHalfHeight =
        ( db.Height > 0 ? db.Height : 1300 ) * kMmToM * 0.5f;
    const float suspensionAttachY =
        -Max( 0.0f, bodyHalfHeight - kDefaultWheelRadius );

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

    auto axleSpring = [&]( int32_t suspRate,
                           int32_t dampRate ) -> JPH::SpringSettings {
        const float sr = Max( 0.01f, suspRate * 0.01f );
        const float omega = std::sqrt( 72.0f * sr );
        JPH::SpringSettings s;
        s.mMode = JPH::ESpringMode::FrequencyAndDamping;
        s.mFrequency = JPH::Clamp( omega * 0.15915494f, 0.5f, 4.0f );
        s.mDamping =
            JPH::Clamp( omega * 0.0339f * ( dampRate * 0.01f ), 0.05f, 1.5f );
        return s;
    };
    const JPH::SpringSettings frontSpring =
        axleSpring( db.SuspensionRateFront, db.DamperRateFront );
    const JPH::SpringSettings rearSpring =
        axleSpring( db.SuspensionRateRear, db.DamperRateRear );

    // Static equilibrium suspension length per axle: maxLength - g/omega^2,
    // where omega = 2pi * the (clamped) spring frequency and g/omega^2 is the
    // gravity static deflection (mass-independent for a frequency spring).
    constexpr float kTwoPi = 6.28318530718f;
    constexpr float kGravity = 9.81f;
    auto restLength = [&]( float maxLength,
                           const JPH::SpringSettings &spring ) -> float {
        const float omega = kTwoPi * spring.mFrequency;
        const float deflection =
            ( omega > 1.0e-3f ) ? kGravity / ( omega * omega ) : 0.0f;
        return JPH::Clamp( maxLength - deflection, 0.10f, maxLength );
    };

    for ( int32_t i = 0; i < 4; i++ ) {
        const WheelLayout &wl = layout[i];
        const JPH::SpringSettings &spring =
            wl.bFront ? frontSpring : rearSpring;
        const float maxLength =
            0.10f + ( wl.bFront ? travelFront : travelRear );

        JPH::WheelSettingsWV *pWheel = new JPH::WheelSettingsWV();
        pWheel->mPosition = JPH::Vec3( wl.X, suspensionAttachY, wl.Z );
        pWheel->mSuspensionDirection = JPH::Vec3( 0, -1, 0 );
        pWheel->mSuspensionMinLength = 0.10f;
        pWheel->mSuspensionMaxLength = maxLength;
        pWheel->mSuspensionSpring = spring;
        pWheel->mRadius = kDefaultWheelRadius;
        pWheel->mWidth = kDefaultWheelWidth;
        pWheel->mMaxSteerAngle = wl.bFront ? steerLock : 0.0f;
        pWheel->mMaxBrakeTorque = 1500.0f; // TODO: from BrakesFront/Rear
        pWheel->mMaxHandBrakeTorque = wl.bFront ? 0.0f : 4000.0f;

        restSuspensionLength[i] = restLength( maxLength, spring );
        constraintSettings.mWheels.push_back( pWheel );
    }

    JPH::WheeledVehicleControllerSettings *pController =
        new JPH::WheeledVehicleControllerSettings();

    // Engine: peak torque + redline from the DB, with a normalized torque curve
    // built from the published peak-torque and peak-power rev points.
    pController->mEngine.mMaxTorque = static_cast<float>( db.TorqueNm );
    pController->mEngine.mMaxRPM = maxRPM;
    pController->mEngine.mMinRPM = Max( 700.0f, maxRPM * 0.12f );
    pController->mEngine.mInertia = 0.5f; // TODO: from EngineInertia
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

    // Differential: rear-wheel drive by default (drives wheels 2 + 3). TODO:
    // resolve db.DriveType to switch between FWD / RWD / AWD (two
    // differentials).
    JPH::VehicleDifferentialSettings differential;
    differential.mLeftWheel = 2;
    differential.mRightWheel = 3;
    differential.mDifferentialRatio =
        db.FinalDriveRatio > 0.1f ? db.FinalDriveRatio : 3.42f;
    differential.mEngineTorqueRatio = 1.0f;
    pController->mDifferentials.push_back( differential );

    constraintSettings.mController = pController;

    pConstraint = new JPH::VehicleConstraint( *pChassis, constraintSettings );

    pCollisionTester =
        new JPH::VehicleCollisionTesterCastCylinder( TDUPhysLayers::MOVING );
    static_cast<JPH::VehicleConstraint *>( pConstraint )
        ->SetVehicleCollisionTester( pCollisionTester );

    pSystem->AddConstraint( pConstraint );
    pSystem->AddStepListener( pConstraint );
    return true;
}

void PhysicsCar::tick( float /*deltaTime*/ ) {
    if ( pConstraint == nullptr ) {
        return;
    }

    JPH::WheeledVehicleController *pController =
        static_cast<JPH::WheeledVehicleController *>(
            pConstraint->GetController() );
    // Jolt: forward in [-1,1] (negative = reverse), brake/handbrake in [0,1],
    // right in [-1,1]. TDU brake doubles as reverse at a standstill, but that
    // lives in the (still-stubbed) input logic; here we pass the raw values.
    pController->SetDriverInput( inputThrottle, inputSteer, inputBrake, 0.0f );

    if ( pChassisBody != nullptr &&
         ( inputThrottle != 0.0f || inputBrake != 0.0f ||
           inputSteer != 0.0f ) ) {
        pPhysicsSystem->GetBodyInterface().ActivateBody(
            pChassisBody->GetID() );
    }
}

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
