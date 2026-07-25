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
        float LateralGroundSpeed;      // m/s along contact-lateral (0 airborne)
        float SuspensionLoad;
        float Radius; // m
        bool bGrounded;
        uint32_t ContactSurface;
        Eigen::Vector4f ContactPoint;
    };

    struct SuspensionSetup {
        float minLength;  // full-bump length, m
        float maxLength;  // old full-droop length (= static + sag), m
        float restLength; // static ride-height length, m
        float frequency;  // ride frequency, Hz
        float damping;    // damping ratio
        float cornerLoad; // static per-wheel load, N (spring force at static)
        float
            staticLength; // suspLength at ride height (spring == cornerLoad), m
        float droopStop;  // full-droop travel stop = static + preload, m
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

    struct DrivingGearbox {
        int32_t Gear;       // 0 = reverse, 1 = neutral, 2.. = forward gears
        float ForwardSpeed; // m/s, signed (+ = forwards)
        bool bAutomatic;
        bool bEngineFiring; // engine making torque (orig engine state 2 or 3)
        bool bShiftIdle;    // gearbox will take a shift request right now
    };

    void setInput( float throttle,
                   float brake,
                   float steer,
                   float driverBrake = -1.0f );
    void tick( float deltaTime );

    DrivingGearbox getDrivingGearbox() const;
    void setCurrentGear( int32_t gear );
    void requestUpshift();
    void requestDownshift();
    void setClutchOverride( float engagement );

    void resetToTransform( const Matrix4x4f_t &transform );

    void OnStep( const JPH::PhysicsStepListenerContext &inContext ) override;

    Matrix4x4f_t getChassisWorldMatrix() const;
    float getWheelRestCenterDrop( uint32_t i ) const {
        if ( i >= static_cast<uint32_t>( vehicle.NumWheels ) ) {
            return 0.0f;
        }
        return -wheelAttach[i].y() + suspRestLength[i];
    }
    uint32_t getNumWheels() const;
    WheelReadout getWheelReadout( uint32_t index ) const;
    float getChassisSpeed() const;
    float getEngineAngularVelocity() const;
    int32_t getCurrentGear() const;
    float getInputBrake() const { return inputBrake; }
    float getInputThrottle() const { return inputThrottle; }
    float getDriverBrake() const { return inputDriverBrake; }
    float getLateralG() const;
    float getLongitudinalG() const;

private:
    bool castWheel( JPH::PhysicsSystem &system,
                    const JPH::Body &chassis,
                    int wheelIndex,
                    float deltaTime );

    float
    filterSteering( float rawTarget, float deltaTime, float speed, float beta );
    void applyAntiRollBars();

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
    float suspDamping[PhysicsVehicleState::kMaxWheels];    // N/(m/s) (bump)
    float
        suspDamperRebound[PhysicsVehicleState::kMaxWheels]; // N/(m/s) (rebound)
    float suspDamperLP[PhysicsVehicleState::kMaxWheels]; // damper vel LP state
    float
        suspStaticLength[PhysicsVehicleState::kMaxWheels]; // ride-height len, m
    float suspCornerLoad[PhysicsVehicleState::kMaxWheels]; // static load, N
    float suspDroopStop[PhysicsVehicleState::kMaxWheels];  // static+preload, m
    float arbStiffness[2]; // anti-roll bar rate per axle (front, rear), N/m
    float steerLockRad;
    float wheelBase = 2.5f; // axle-to-axle distance, m (speed-sensitive steer)
    float prevSuspLength[PhysicsVehicleState::kMaxWheels]; // last step's length
                                                           // (damper velocity)
    uint32_t wheelSurface[PhysicsVehicleState::kMaxWheels] =
        {}; // raycast contact surface class

    // Driver input.
    float inputThrottle;
    float inputBrake;
    float inputDriverBrake;
    float inputSteer;
    float filteredSteer; // rate-limited steering state
    float steerCommand;  // shaped, speed-scaled steer (drives the wheel angle)

    float engineOmega; // rad/s, for the tacho / CarBodyState::Omega
    int32_t currentGear;
    float finalDriveRatio;

    // Aerodynamics (chassis downforce/drag, applied per physics step).
    float aeroDragArea; // 0.5 * A * Cx_drag
    float aeroDownArea; // 0.5 * A * Cz_lift
    float aeroDensity;  // air density (mode-gated: 1.7 simu / 1.2 arcade)

    // G-meter accel tracking
    float prevWorldVelocity[3];
    float localAccelLong;
    float localAccelLat;
};

extern bool gbSimuModeEnabled;
