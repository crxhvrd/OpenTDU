#include "shared.h"
#include "physics_car.h"

#include "physics_layers.h"
#include "physics_config.h"
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
    for ( int32_t axle = 0; axle < 2; axle++ ) {
        axleDriveGrip[axle] = 1.0f;
        axleBrakeGrip[axle] = 1.0f;
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

    const float cfgSpringCoef =
        gPhysicsConfig.SpringCoef > 0.0f ? gPhysicsConfig.SpringCoef : 1.0f;
    const float cfgReboundCoef =
        gPhysicsConfig.ReboundCoef > 0.0f ? gPhysicsConfig.ReboundCoef : 1.0f;
    const float coefSuspension = JPH::Clamp(
        db.CoefSuspension > 0 ? db.CoefSuspension * 0.01f : 1.0f, 0.25f, 4.0f );

    auto axleSpring = [&]( int32_t suspRate,
                           int32_t dampRate ) -> JPH::SpringSettings {
        const float sr = Max( 0.01f, suspRate * 0.01f );
        const float omega =
            std::sqrt( 72.0f * sr * cfgSpringCoef * coefSuspension );
        JPH::SpringSettings s;
        s.mMode = JPH::ESpringMode::FrequencyAndDamping;
        s.mFrequency = JPH::Clamp( omega * 0.15915494f, 0.5f, 4.0f );
        s.mDamping =
            JPH::Clamp( omega * 0.0339f * ( dampRate * 0.01f ) * cfgReboundCoef,
                        0.05f, 1.5f );
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
        const JPH::SpringSettings &spring =
            wl.bFront ? frontSpring : rearSpring;
        const float maxLength =
            0.10f + ( wl.bFront ? travelFront : travelRear );

        const float wheelRadius = wl.bFront ? frontRadius : rearRadius;
        const float wheelWidth = wl.bFront ? frontWidth : rearWidth;
        const float attachY = -Max( 0.0f, bodyHalfHeight - wheelRadius );

        JPH::WheelSettingsWV *pWheel = new JPH::WheelSettingsWV();
        pWheel->mPosition = JPH::Vec3( wl.X, attachY, wl.Z );
        pWheel->mSuspensionDirection = JPH::Vec3( 0, -1, 0 );
        pWheel->mSuspensionMinLength = 0.10f;
        pWheel->mSuspensionMaxLength = maxLength;
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

        restSuspensionLength[i] = restLength( maxLength, spring );
        constraintSettings.mWheels.push_back( pWheel );
    }

    const float massPerWheel = mass * 0.25f;
    auto axleAntiRollStiffness = [&]( const JPH::SpringSettings &spring ) {
        const float omega = kTwoPi * spring.mFrequency;
        const float wheelRate = massPerWheel * omega * omega;
        return JPH::Clamp( wheelRate * 0.5f, 2000.0f, 60000.0f );
    };
    JPH::VehicleAntiRollBar frontAntiRoll;
    frontAntiRoll.mLeftWheel = 0;
    frontAntiRoll.mRightWheel = 1;
    frontAntiRoll.mStiffness = axleAntiRollStiffness( frontSpring );
    constraintSettings.mAntiRollBars.push_back( frontAntiRoll );
    JPH::VehicleAntiRollBar rearAntiRoll;
    rearAntiRoll.mLeftWheel = 2;
    rearAntiRoll.mRightWheel = 3;
    rearAntiRoll.mStiffness = axleAntiRollStiffness( rearSpring );
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
    } else if ( driveType == 0x034cadf5u ) {
        addDifferential( 2, 3, 1.0f );
    } else {
        addDifferential( 0, 1, 0.5f );
        addDifferential( 2, 3, 0.5f );
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

    pCollisionTester =
        new JPH::VehicleCollisionTesterCastCylinder( TDUPhysLayers::MOVING );
    static_cast<JPH::VehicleConstraint *>( pConstraint )
        ->SetVehicleCollisionTester( pCollisionTester );

    pSystem->AddConstraint( pConstraint );
    pSystem->AddStepListener( pConstraint );
    return true;
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
    pController->SetDriverInput( inputThrottle, inputSteer, inputBrake, 0.0f );

    if ( pChassisBody != nullptr &&
         ( inputThrottle != 0.0f || inputBrake != 0.0f ||
           inputSteer != 0.0f ) ) {
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
