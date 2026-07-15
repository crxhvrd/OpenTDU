#pragma once

#include "shared.h"

struct PhysicsTire {
    static constexpr int kMaxLoadTable = 32;

    float MaxSlipRatio = 1.0f;
    float MaxSlipAngle = 1.0f;
    float FrictionEllipseThreshold = 1.0f;
    float FrictionEllipseMinFactor = 1.0f;
    int LoadTableCount = 0;
    float LoadTableMaxLoad = 1.0f;
    float PeakLat[kMaxLoadTable] = {};
    float PeakLong[kMaxLoadTable] = {};
    float Long[11] = {};
    float Lat[15] = {};
};

struct PhysicsWheelState {
    PhysicsTire Tire;

    float Radius = 0.3f;           // wheel radius (m)
    float Inertia = 1.0f;          // wheel rotational inertia
    float ReflectedInertia = 0.0f; // drivetrain inertia reflected through the
                                   // gear (driven wheel, clutch engaged)
    int SideFlag = 1;              // side; != 1 mirrors lateral/steer sign
    bool bDriven = false;          // driven wheel
    bool bSteered = false;         // steered / handbrake wheel

    bool bGroundContact = false; // has a valid ground contact this step
    Eigen::Vector4f WheelLinVel =
        Eigen::Vector4f::Zero(); // hub world linear velocity
    Eigen::Vector4f WheelPos = Eigen::Vector4f::Zero(); // hub world position
    Eigen::Vector4f SpinAxis =
        Eigen::Vector4f::Zero(); // spin-axis / body-up fallback row
    Eigen::Vector4f ContactPoint =
        Eigen::Vector4f::Zero(); // contact application point (world)
    Eigen::Vector4f ContactNormal =
        Eigen::Vector4f::Zero(); // contact normal (world)
    Eigen::Vector4f ContactLatAxis =
        Eigen::Vector4f::Zero(); // contact-frame lateral axis
    Eigen::Vector4f ContactLongAxis =
        Eigen::Vector4f::Zero(); // contact-frame longitudinal axis
    Eigen::Vector4f ForwardDir = Eigen::Vector4f::Zero(); // forward dir

    float SuspTotalForce = 0.0f; // spring+damper suspension force
    float AntiRollForce = 0.0f;  // anti-roll-bar force
    float NormalLoad = 0.0f;     // clamped contact normal force
    float SuspMaxLoad = 1.0e6f;  // max-load clamp
    float SuspMinLoad = 0.0f;    // min-load clamp
    Eigen::Vector4f SuspForce = Eigen::Vector4f::Zero();
    Eigen::Vector4f SuspForcePoint = Eigen::Vector4f::Zero();

    float GripLong = 1.0f;      // longitudinal grip mu_x
    float GripLat = 1.0f;       // lateral grip mu_y
    float BrakeGripLong = 1.0f; // longitudinal brake-grip fallback
    float BrakeGripLat = 1.0f;  // lateral brake-grip fallback
    float LoadScaleLong = 1.0f; // longitudinal tire-force load scale
    float LoadScaleDrag = 0.0f; // v^2 drag load scale
    float DynamicGrip = 1.0f;   // dynamic-grip accumulator
    float GripMult = 1.0f;      // temperature grip multiplier
    float TireTemp = 20.0f;     // tire temperature

    float DeadbandLong = 0.1f;
    float DeadbandLat = 0.1f;
    float DeadbandOmega = 0.1f;

    float LatHelpA = 0.0f;
    float LatHelpB = 0.0f;

    float AngularVel = 0.0f; // wheel angular velocity omega
    float SpinAngle = 0.0f;
    float DriveTorque = 0.0f; // applied drive/brake torque from the diff
    float BrakeTorque = 0.0f; // brake torque command
    float AngAccel = 0.0f;    // wheel angular acceleration
    float NetTorque = 0.0f;   // net wheel torque

    float TireForceLong = 0.0f;  // longitudinal tire force scalar
    float TireForceLat = 0.0f;   // lateral tire force scalar
    float DragLong = 0.0f;       // longitudinal v^2 drag
    float DragLat = 0.0f;        // lateral v^2 drag
    float ContactVelLong = 0.0f; // contact-patch longitudinal velocity
    float ContactVelLat = 0.0f;  // contact-patch lateral velocity
    float ContactVelM0 = 0.0f;
    float ContactVelM1 = 0.0f;
    float ContactVelM2 = 0.0f;
    bool bTireSlipping = false;
    float NormLatClamped = 0.0f;
    float NormLongClamped = 0.0f;
    float NormLat = 0.0f;
    float NormLong = 0.0f;

    Eigen::Vector4f WorldForceLat = Eigen::Vector4f::Zero();
    Eigen::Vector4f WorldForceLong = Eigen::Vector4f::Zero();
    Eigen::Vector4f TireForcePoint = Eigen::Vector4f::Zero();
};

enum class eEngineState {
    ES_Off = 0,     // stalled / key off (omega 0, no torque)
    ES_Crank = 1,   // starter cranking (no torque, timer)
    ES_RevUp = 2,   // fired, revving up from idle (the start blip)
    ES_Running = 3, // normal running
};

struct PhysicsEngineSim {
    static constexpr int kMaxCurve = 64;

    struct CurvePoint {
        float Omega = 0.0f;  // rad/s
        float Torque = 0.0f; // N.m
    };

    eEngineState State = eEngineState::ES_Off;
    float IgnTimer = 0.0f;      // crank/rev-up timer, s (engine[0xb0])
    bool IgnitionOn = true;     // key on (engine[0xcc]); drives auto-start
    float CrankDuration = 0.5f; // starter crank time, s (IgnitionTimeIgnite)
    float RevUpDuration = 0.4f; // start-blip time, s (IgnitionTimeRevUp)

    CurvePoint CurveMin[kMaxCurve]; // closed-throttle (engine brake)
    CurvePoint CurveMax[kMaxCurve]; // full-throttle (WOT)
    int CurveCount = 0;
    float OmegaMax = 700.0f; // redline (rad/s) = Redline_RPM * 0.10472
    float OmegaIdle = 84.0f; // idle / rev-limit ref (rad/s)
    float Inertia = 0.4f;
    float TorqueScale = 1.0f;
    float FrictionLoss = 0.0f;
    float Throttle = 0.0f;
    float Omega = 84.0f; // current engine speed (rad/s)
    float TorqueOut = 0.0f;
    float PeakTorqueOmega = 0.0f; // omega at peak torque
    float AngAccel = 0.0f;        // dOmega/dt (rad/s^2), 1-step lag
};

struct PhysicsGearboxSim {
    static constexpr int kMaxGears = 11;

    int NumGears = 3; //  DB NumGears + 2
    float Ratios[kMaxGears] =
        {}; // [0]=reverse(-4 car), [1]=neutral(0), [2..]=DB
    float FinalDrive = 4.0f;
    float TransEff = 0.9f;
    int CurrentGear = 1;

    int ShiftState = 1;    // 1 = in gear
    int ShiftSubstate = 0; // gear delta +/-
    float ShiftTimer = 0.0f;
    float UpshiftThreshold = 1.2f; // DAT_00f41dc4
    float DownshiftFactor = 0.2f;  // DAT_00f41730
    float UpshiftDelay = 0.3f;     // Physics.ini ShiftTime
    float DownshiftDelay = 0.3f;
    bool bAutomatic = true;
};

struct PhysicsClutchSim {
    float Engagement = 0.0f;         // actual engagement, driveline multiplier
    float Target = 0.0f;             // target engagement
    int State = 0;                   // 0=disengaged 1=armed 2=launch 3=locked
    float Timer = 0.0f;              // state-2 timer
    float LaunchIdleMult = 1.15f;    // DAT_00f416a8
    float LaunchRedlineMult = 0.75f; // DAT_00f416a4
    float LockThreshold = 0.9f;      // DAT_00f416a0
    float EngageThrottleHi = 10.0f;  // engage-rate at 0 throttle
    float EngageThrottleLo = 1.0f;   // engage-rate at full throttle
    float EngageRate = 3.0f;         // actual-engagement slew up (runtime-set)
    float DisengageRate = 3.0f;      // actual-engagement slew down
    bool bLocked = false;            // (stand-in path) slip vs locked
};

struct PhysicsDifferentialSim {
    int Type = 1;
    float SplitRatio = 0.5f;
    float LockCoef = 0.0f;
    float Gain = 1.0f;
};

struct PhysicsVehicleState {
    static constexpr int kMaxWheels = 4;

    int NumWheels = 4;
    PhysicsWheelState Wheels[kMaxWheels];

    Eigen::Vector4f GravityBody = Eigen::Vector4f( 0.0f, -1.0f, 0.0f, 0.0f );

    // Aerodynamics
    float AeroCoefFront = 0.0f;
    float AeroCoefRear = 0.0f;
    float AeroArea = 0.0f;

    // Powertrain
    PhysicsEngineSim Engine;
    PhysicsGearboxSim GearBox;
    PhysicsClutchSim Clutch;
    PhysicsDifferentialSim DiffFront;
    PhysicsDifferentialSim DiffRear;
    PhysicsDifferentialSim DiffCenter;
    bool bFrontDrive = false;
    bool bRearDrive = false;
    bool bCenterDrive = false;
    int CouplingMode = 0;
    float CouplingCoefE = 1.0f;
    float CouplingCoefW = 1.0f;
    float DrivelineInertia = 1.0f;
    float ReflectedWheelSpeed = 0.0f;
    float ForwardSpeed = 0.0f;
    float ReactionTorque = 0.0f;

    float BrakeTorque = 0.0f;
};

void BuildEngineCurves( PhysicsEngineSim &engine,
                        const float *pTorquePairs,
                        int numPairs,
                        float bhp,
                        float packBoost,
                        float frictionCoef );
void StepPowertrain( PhysicsVehicleState &vehicle, float throttle, float dt );
void ComputeTireForces( PhysicsWheelState &wheel,
                        const Matrix4x4f_t &vehicleMatrix );
void ComputeSuspensionForce( PhysicsWheelState &wheel );
void IntegrateWheel( PhysicsWheelState &wheel, float deltaTime );
