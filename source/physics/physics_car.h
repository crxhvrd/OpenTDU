#pragma once

#include "shared.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>

namespace JPH {
    class PhysicsSystem;
    class Body;
} // namespace JPH

struct DBEntryCarPhysicsData;

// Master toggle for the electronic driver aids (ABS + traction control + ESP).
extern bool gVehicleDriverAids;

// 1 = full assistance
// 0 = none (HyperSport / Hardcore).
// (Scales ESP and TC intervention strength; ABS is always ON)
extern float gVehicleAidsLevel;

extern bool gVehicleSteeringFilter;

class PhysicsCar {
public:
    struct WheelReadout {
        Eigen::Vector3f LocalPosition; // suspension attach point, body space
        float AngularVelocity;         // rad/s (+ = rolling forward)
        float SteerAngle;              // rad
        float RotationAngle;           // rad
        float SuspensionLength;        // m (current, from the hard point)
        float SuspensionDeflection;    // m, rest - current (+ = compressed/up)
        float LongitudinalSlip;
        float LateralSlip;
        float LongitudinalGroundSpeed; // m/s along contact-forward (0 airborne)
        float SuspensionLoad;
        float Radius; // m
        bool bGrounded;
        uint32_t ContactSurface;
    };

    struct SuspensionSetup {
        float minLength;  // full-bump length, m
        float maxLength;  // full-droop length, m
        float restLength; // static ride-height length, m
        float frequency;  // ride frequency, Hz
        float damping;    // damping ratio
    };

public:
    PhysicsCar();
    ~PhysicsCar();

    void initialize();

    // Build a Jolt wheeled vehicle on `pChassis` (which must be a dynamic body
    // with the car's mass) from the DB tuning, and register it with the system.
    bool create( JPH::PhysicsSystem *pSystem,
                 JPH::Body *pChassis,
                 const DBEntryCarPhysicsData &db );
    void destroy();

    inline bool isActive() const { return pConstraint != nullptr; }

    // Driver input: throttle/brake in [0,1], steer in [-1,1].
    void setInput( float throttle, float brake, float steer );
    void tick( float deltaTime );

    Matrix4x4f_t getChassisWorldMatrix() const;

    static SuspensionSetup
    ComputeAxleSuspension( const DBEntryCarPhysicsData &db, bool bFront );

    uint32_t getNumWheels() const;
    WheelReadout getWheelReadout( uint32_t index ) const;
    float getChassisSpeed() const; // chassis linear-velocity magnitude, m/s
    float getEngineAngularVelocity() const; // rad/s (CarBodyState::Omega unit)
    int32_t getCurrentGear() const;
    float getLateralG() const;
    float getLongitudinalG() const;

private:
    JPH::PhysicsSystem *pPhysicsSystem;
    JPH::Body *pChassisBody;
    JPH::Ref<JPH::VehicleConstraint> pConstraint;
    JPH::Ref<JPH::VehicleCollisionTester> pCollisionTester;

    float inputThrottle;
    float inputBrake;
    float inputSteer;

    // Per-wheel ride-height suspension length.
    float restSuspensionLength[4];

    float axleDriveGrip[2];
    float axleBrakeGrip[2];

    bool wheelDriven[4];

    float prevWorldVelocity[3];
    float localAccelLong;
    float localAccelLat;

    float aeroDragCoef;
    float aeroDownforceCoef;
    float aeroHalfWheelBase;
    float aeroFrontFraction;
    float steerLockRad;
    float filteredSteer;

private:
    void applyDriverAids( float steer,
                          float &throttle,
                          float &brake,
                          float &espYawTorqueLocalUp ) const;
    float filterSteering( float rawTarget, float deltaTime );
};
