#pragma once

#include "shared.h"

#include "vehicle_simulation.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsStepListener.h>

namespace JPH {
    class PhysicsSystem;
    class Body;
} // namespace JPH

struct DBEntryCarPhysicsData;

class PhysicsVehicle final : public JPH::PhysicsStepListener {
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
    PhysicsVehicle();
    ~PhysicsVehicle() override;

    static SuspensionSetup
    ComputeAxleSuspension( const DBEntryCarPhysicsData &db, bool bFront );

    void initialize();
    bool create( JPH::PhysicsSystem *pSystem,
                 JPH::Body *pChassis,
                 const DBEntryCarPhysicsData &db,
                 const void *pTorqueBinary = nullptr );
    void destroy();
    bool isActive() const { return pChassisBody != nullptr; }

    void setInput( float throttle, float brake, float steer );
    void tick( float deltaTime );

    void OnStep( const JPH::PhysicsStepListenerContext &inContext ) override;

    Matrix4x4f_t getChassisWorldMatrix() const;
    uint32_t getNumWheels() const;
    WheelReadout getWheelReadout( uint32_t index ) const;
    float getChassisSpeed() const;
    float getEngineAngularVelocity() const;
    int32_t getCurrentGear() const;
    float getLateralG() const;
    float getLongitudinalG() const;

private:
    bool castWheel( JPH::PhysicsSystem &system,
                    const JPH::Body &chassis,
                    int wheelIndex,
                    float deltaTime );

private:
    JPH::PhysicsSystem *pPhysicsSystem;
    JPH::Body *pChassisBody;
    bool bRegistered;

    PhysicsVehicleState vehicle;

    Eigen::Vector3f
        wheelAttach[PhysicsVehicleState::kMaxWheels]; // suspension hard point,
                                                      // body space
    float
        suspMaxLength[PhysicsVehicleState::kMaxWheels]; // full-droop length, m
    float suspMinLength[PhysicsVehicleState::kMaxWheels]; // full-bump length, m
    float suspRestLength[PhysicsVehicleState::kMaxWheels]; // static ride-height
                                                           // length, m
    float suspStiffness[PhysicsVehicleState::kMaxWheels];  // N/m
    float suspDamping[PhysicsVehicleState::kMaxWheels];    // N/(m/s)
    float steerLockRad;
    float prevSuspLength[PhysicsVehicleState::kMaxWheels]; // last step's length
                                                           // (damper velocity)
    uint32_t wheelSurface[PhysicsVehicleState::kMaxWheels] =
        {}; // raycast contact surface class

    // Driver input.
    float inputThrottle;
    float inputBrake;
    float inputSteer;
    float filteredSteer;

    float engineOmega; // rad/s, for the tacho / CarBodyState::Omega
    int32_t currentGear;
    float finalDriveRatio;

    // G-meter accel tracking
    float prevWorldVelocity[3];
    float localAccelLong;
    float localAccelLat;
};
