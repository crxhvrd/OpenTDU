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
        float SuspensionStaticLength;  // m, ride height
        float SuspensionDroopStop;     // m, full droop
        float LongitudinalSlip;
        float LateralSlip;
        float LongitudinalGroundSpeed; // m/s along contact-forward (0 airborne)
        float LateralGroundSpeed;      // m/s along contact-lateral (0 airborne)
        float SuspensionLoad;
        float Radius; // m
        float Width;
        bool bGrounded;
        int32_t EspState;
        float BrakeTorqueOut;
        float EspTorqueOut;
        uint32_t ContactSurface;  // phys-type code (TDUSurface)
        uint32_t ContactMaterial; // eMaterialType, MT_Default when unknown
        Eigen::Vector4f ContactPoint;
        Eigen::Vector4f ContactVelocity;
        Eigen::Vector4f UpAxis;
        Eigen::Vector4f ForwardAxis;
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

    struct DrivingGearbox {
        int32_t Gear;       // 0 = reverse, 1 = neutral, 2.. = forward gears
        int32_t NumGears;   // reverse + neutral + forward gears
        float ForwardSpeed; // m/s, signed (+ = forwards)
        bool bAutomatic;
        bool bEngineFiring;  // engine making torque (orig engine state 2 or 3)
        bool bShiftIdle;     // gearbox will take a shift request right now
        bool bEngineRunning; // engine in the running state (orig state 2)
        int32_t ShiftDirection;
    };

    enum eDrivingMode : uint8_t {
        // no override, the driver input passes through
        DM_Player = 0,
        // idle brake hold: throttle 0, brake 0.5, steer 0
        DM_HoldBrake = 1,
        // full brake, clutch out; also inhibits auto-reverse engage
        DM_Parked = 2,
        // throttle 0, brake 0.65, clutch out; same auto-reverse inhibit
        DM_SlowDown = 3,
        // replay the previous update's input record
        DM_RepeatLast = 4,
    };

    // Chronopack State
    struct Chrono {
        int32_t State = 0; // 0 off, 1 ready, 2 stop-first, 3 running
        float Timer = 0.0f;
        float StartDistance = 0.0f;
        float Odometer = 0.0f; // in metres
        float T60mph = 0.0f;
        float T100kmh = 0.0f;
        float T200kmh = 0.0f;
        float T400m = 0.0f;
        float T1000m = 0.0f;
        float TQuarterMile = 0.0f;
        float MaxDrivenSlip = 0.0f; // m/s
        float MaxOmega = 0.0f;      // rad/s
        float SlipIntegral = 0.0f;  // integral of mean driven slip (m/s) dt
        float LimiterTime = 0.0f;   // seconds at >= 0.99 * OmegaMax
    };

public:
    float getEngineThrottle() const { return vehicle.Engine.Throttle; }
    float getEngineTorqueOut() const { return vehicle.Engine.TorqueOut; }
    float getEngineOmegaMax() const { return vehicle.Engine.OmegaMax; }
    float getEngineSmoothedTorque() const {
        return vehicle.Engine.SmoothedTorque;
    }
    eEngineState getEngineState() const { return vehicle.Engine.State; }
    float getVehicleMass() const { return vehicle.Mass; }
    float getInputBrake() const { return inputBrake; }
    float getInputThrottle() const { return inputThrottle; }
    float getInputHandbrake() const { return inputHandbrake; }
    void setAutoReverseState( int32_t state ) { autoReverseState = state; }
    int32_t getAutoReverseState() const { return autoReverseState; }
    bool isActive() const { return pChassisBody != nullptr; }
    bool isSimulated() const { return bRegistered; }
    void setDrivingMode( uint8_t mode ) { drivingMode = mode; }
    uint8_t getDrivingMode() const { return drivingMode; }
    int32_t getElectronicIndex() const { return vehicle.ElectronicIndex; }
    bool areElectronicsEnabled() const { return vehicle.bElectronicsEnabled; }
    bool isEspIntervening() const { return vehicle.bEspEnabled; }
    float getEspGripDiff() const { return vehicle.EspGripDiff; }
    float getEspThreshold() const { return vehicle.EspThreshold; }
    float getEspMagFront() const { return vehicle.EspMagFront; }
    float getEspMagRear() const { return vehicle.EspMagRear; }
    Matrix4x4f_t computeDisplayMatrix() const {
        return computeDisplayMatrix( getChassisShiftedMatrix() );
    }
    const Matrix4x4f_t &getPhysicsMatrix() const { return physicsMatrix; }
    const Matrix4x4f_t &getDisplayMatrix() const { return displayMatrix; }
    const Matrix4x4f_t &getInvChassisMatrix() const { return invChassisMatrix; }
    float getBodyHeight() const { return bodyHeight; }
    float getDisplayYOffset() const { return y3DOffset; }
    float getWheelRestCenterDrop( uint32_t i ) const {
        if ( i >= static_cast<uint32_t>( vehicle.NumWheels ) ) {
            return 0.0f;
        }
        return -wheelAttach[i].y() + suspRestLength[i];
    }
    const Chrono &getChrono() const { return chrono; }
    float getForwardSpeed() const { return vehicle.ForwardSpeed; }

public:
    PhysicsVehicle();
    ~PhysicsVehicle() override;

    static SuspensionSetup
    ComputeAxleSuspension( const DBEntryCarPhysicsData &db, bool bFront );

    void initialize();
    bool create( JPH::PhysicsSystem *pSystem,
                 JPH::Body *pChassis,
                 const DBEntryCarPhysicsData &db,
                 const void *pTorqueBinary = nullptr,
                 bool bFactoryShape = false );
    void destroy();

    void setInput( float throttle, float brake, float steer );
    void setHandbrake( float handbrake );
    void setIgnition( bool bKeyOn, bool bCutEdge );
    void applyDrivingMode();
    void tick( float deltaTime );
    void setDrivingHelpLevel( int32_t level );

    DrivingGearbox getDrivingGearbox() const;
    void setCurrentGear( int32_t gear );
    void setAutomaticGearbox( bool bAutomatic );
    void requestUpshift();
    void requestDownshift();
    void setClutchOverride( float engagement );

    void resetToTransform( const Matrix4x4f_t &transform );

    void OnStep( const JPH::PhysicsStepListenerContext &inContext ) override;

    Matrix4x4f_t getChassisWorldMatrix() const;
    Matrix4x4f_t getChassisShiftedMatrix() const;
    Matrix4x4f_t computeDisplayMatrix( const Matrix4x4f_t &sourceMatrix ) const;

    void publishStepPose();
    void onWorldShifted( const Eigen::Vector3f &worldShift );
    void calibrateDisplayYOffset( float modelWheelRestY );

    uint32_t getNumWheels() const;
    WheelReadout getWheelReadout( uint32_t index ) const;

    void armChrono();
    void updateChrono( float dt );

    float getChassisSpeed() const; // Main Thread call only
    Eigen::Vector4f getChassisVelocity() const;

    float getEngineAngularVelocity() const;
    int32_t getCurrentGear() const;

    float getLateralG() const;
    float getLongitudinalG() const;
    float getVerticalG() const;

    enum eSteerCurve {
        SC_Limit = 0,     // Physics.ini SteeringLimit
        SC_Speed,         // Physics.ini SteeringSpeed
        SC_CenterSpeed,   // Physics.ini SteeringCenterSpeed
        SC_StraightSpeed, // Physics.ini SteeringStraightSpeed
        SC_CounterSpeed,  // Physics.ini SteeringCounterSpeed
        SC_Count
    };

private:
    bool castWheel( JPH::PhysicsSystem &system,
                    const JPH::Body &chassis,
                    int wheelIndex,
                    float deltaTime );

    float filterSteering( float rawTarget,
                          const JPH::Body &chassis,
                          float deltaTime );
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
    float wheelWidth[PhysicsVehicleState::kMaxWheels] = {};
    float wheelGroundGap[PhysicsVehicleState::kMaxWheels] = {};
    uint32_t wheelSurface[PhysicsVehicleState::kMaxWheels] =
        {}; // raycast contact phys-type code
    uint32_t wheelMaterial[PhysicsVehicleState::kMaxWheels] =
        {}; // raycast contact eMaterialType (MT_Default = none)

    int32_t appliedDrivingHelpLevel = -2;

    float inputThrottle;
    float inputBrake;
    float inputSteer;
    float inputHandbrake;
    uint8_t drivingMode;
    float prevInputThrottle;
    float prevInputBrake;
    float prevInputSteer;
    int32_t autoReverseState;
    float filteredSteer;
    float steerCommand; // shaped, speed-scaled steer (drives the wheel angle)

    float steerCurveX[SC_Count][3];
    float steerCurveY[SC_Count][3];
    float steerCurveScale;
    float steerNormal[2];       // Physics.ini SteeringNormal
    float steerHelpStraight[2]; // Physics.ini SteeringHelpStraight
    float steerHelpTurn[2];     // Physics.ini SteeringHelpTurn
    float steerHelpGain;        // multiplies the assist error term
    float steerHelpGain2;       // unknown
    float steerAuxTerm;         // -limit * command
    float steerAuxTermPrev;     //
    float steerYawTerm;         // negated body local yaw rate
    float steerSideslipTerm;    // yaw/sideslip blend by speed
    float steerDeadZone;        // set by the filter
    int32_t steerHelpState;     // 0 = none, 1 = straight, 3 = counter
    // Retail reads it from the player profile: 0 = pad/analog (the
    // whole assist + rate-limit tail is bypassed), != 0 = wheel/bar.
    bool bUseSteerControl;
    float steerAssistBlend;
    float engineOmega; // rad/s, for the tacho / CarBodyState::Omega
    int32_t currentGear;
    float finalDriveRatio;

    float displayRoll;
    float displayPitch;
    float displayYaw;
    float y3DOffset;
    float bodyHeight;

    Chrono chrono;

    // Pose published at the end of each physics step (see publishStepPose).
    Matrix4x4f_t physicsMatrix = Matrix4x4f_t::Identity();
    Matrix4x4f_t displayMatrix = Matrix4x4f_t::Identity();
    Matrix4x4f_t invChassisMatrix = Matrix4x4f_t::Identity();

    // Aerodynamics (chassis downforce/drag, applied per physics step).
    float aeroDragArea; // 0.5 * A * Cx_drag
    float aeroDownArea; // 0.5 * A * Cz_lift
    float aeroDensity;  // air density (mode-gated: 1.7 simu / 1.2 arcade)

    // G-meter accel tracking
    float prevWorldVelocity[3];

    // Body space acceleration
    float localAccelLong;
    float localAccelLat;
    float localAccelVert;
};

extern bool gbSimuModeEnabled;
