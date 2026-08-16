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
    int SideFlag = 0;              // != 1 mirrors lateral/steer sign,
                                   // so the LEFT wheels are the mirrored ones.
    int32_t AxleIndex = 0;         // 0 = front, 1 = rear
    bool bDriven = false;          // driven wheel
    bool bSteered = false;         // steered / handbrake wheel

    bool bGroundContact = false; // has a valid ground contact this step
    Eigen::Vector4f WheelLinVel =
        Eigen::Vector4f::Zero(); // chassis velocity at the contact point
    Eigen::Vector4f AttachPoint =
        Eigen::Vector4f::Zero(); // suspension attachment (strut top), world
    Eigen::Vector4f SuspAxis = Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f );
    Eigen::Vector4f WheelUpAxis = Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f );
    Eigen::Vector4f ContactPoint =
        Eigen::Vector4f::Zero(); // contact application point (world)
    Eigen::Vector4f GroundProbePoint = Eigen::Vector4f::Zero();
    Eigen::Vector4f ContactNormal =
        Eigen::Vector4f::Zero(); // contact normal (world)
    Eigen::Vector4f ContactLatAxis =
        Eigen::Vector4f::Zero(); // contact-frame lateral axis
    Eigen::Vector4f ContactLongAxis =
        Eigen::Vector4f::Zero(); // contact-frame longitudinal axis
    Eigen::Vector4f ForwardDir = Eigen::Vector4f::Zero(); // forward dir

    float SuspTotalForce = 0.0f; // spring+damper suspension force
    float AntiRollForce = 0.0f;  // anti-roll-bar force
    float NormalLoad = 0.0f;     // clamped contact normal force (grip)
    float SuspMaxLoad = 1.0e6f;  // max-load clamp (grip)
    float SuspMinLoad = 0.0f;    // min-load clamp
    Eigen::Vector4f SuspForce = Eigen::Vector4f::Zero();
    Eigen::Vector4f SuspForcePoint = Eigen::Vector4f::Zero();

    float GripLong = 1.0f; // longitudinal grip mu_x
    float GripLat = 1.0f;  // lateral grip mu_y
    float BaseGripLong = 1.0f;
    float BaseGripLat = 1.0f;
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

    float BrakeCmdTorque = 0.0f;
    float HandbrakeCmdTorque = 0.0f;
    float BrakeTorqueOut = 0.0f; //  post-ABS brake torque
    float EspTorqueOut = 0.0f;   //  ESP correction torque

    float EspTargetOmega = 0.0f;
    int32_t EspState = 0;

    float AngularVel = 0.0f; // wheel angular velocity omega
    float SpinAngle = 0.0f;
    float DriveTorque = 0.0f; // applied drive/brake torque from the diff
    float BrakeTorque = 0.0f; // brake torque command
    float AngAccel = 0.0f;    // wheel angular acceleration
    float ReactionScale = 1.0f;
    float ReactionSlipCoef = 0.0f;
    float NetTorque = 0.0f; // net wheel torque

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
    ES_Off = 0,      // stalled / key off (omega 0, no torque)
    ES_Crank = 1,    // starter cranking (no torque, gearbox forced to neutral)
    ES_Running = 2,  // fired and running: rev limiter + clutch live here
    ES_Stalling = 3, // ignition cut / lugged: still fires, dies at 0.25*idle
};

struct PhysicsEngineSim {
    static constexpr int kMaxCurve = 64;

    struct CurvePoint {
        float Omega = 0.0f;  // rad/s
        float Torque = 0.0f; // N.m
    };

    eEngineState State = eEngineState::ES_Off;
    float IgnTimer = 0.0f;  // starter timer
    bool IgnitionOn = true; // starter held?
    bool bIgnitionOnce = false;
    float CrankDuration = 0.5f; // s

    CurvePoint CurveMin[kMaxCurve]; // closed-throttle (engine brake)
    CurvePoint CurveMax[kMaxCurve]; // full-throttle (WOT)
    int CurveCount = 0;
    float OmegaMax = 700.0f; // redline (rad/s) = Redline_RPM * 0.10472
    float OmegaIdle = 84.0f; // idle / rev-limit ref (rad/s)
    float Inertia = 0.4f;
    float TorqueScale = 1.0f;
    float FrictionLoss = 0.0f;
    float ThrottleIdle = 0.0f;
    bool bApplyIdleThrottleFloor = false;
    float Throttle = 0.0f;
    float Omega = 84.0f; // current engine speed (rad/s)
    float TorqueOut = 0.0f;
    float SmoothedTorque = 0.0f;
    float PeakTorqueOmega = 0.0f; // omega at peak torque
    float PeakPowerOmega = 0.0f;  // omega at peak power
    float AngAccel = 0.0f;        // dOmega/dt (rad/s^2), 1-step lag
    float RevLimiterCutTime = 0.25f;
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
    float UpshiftBand = 0.7f;
    float DownshiftBand = 0.5f;
    int DrivelineInertiaMode = 1; //  0=/|k| 1=/k^2 2=/sqrt(k)
    float BaseInertia = 0.15f;
    float DiffInertia = 0.01f;
    bool bAutomatic = true;
};

struct PhysicsClutchSim {
    float Engagement = 0.0f;      // actual engagement, driveline multiplier
    float Target = 0.0f;          // target engagement
    int State = 0;                // 0=disengaged 1=armed 2=launch 3=locked
    float Timer = 0.0f;           // state-2 timer
    float LaunchIdleMult = 1.15f; // DAT_00f416a8
    float LaunchPeakPowerMult =
        0.75f; // x peak-power omega, NOT redline (DAT_00f416a4)
    float LockThreshold = 0.9f;     // DAT_00f416a0
    float EngageThrottleHi = 10.0f; // engage-rate at 0 throttle
    float EngageThrottleLo = 1.0f;  // engage-rate at full throttle
    float EngageRate = 3.0f;        // actual-engagement slew up
    float DisengageRate = 3.0f;     // actual-engagement slew down
    float OverrideEngagement = -1.0f;
};

struct PhysicsDifferentialSim {
    int Type = 1;
    float SplitRatio = 0.5f;     // bias (front/rear=0.5, center=DB*0.01)
    float LockCoef = 0.0f;       // lock torque per (speedA-speedB): LSD + spool
    float Gain = 1.0f;           // node gain (open fallthrough + spool only)
    float Deadband = 0.0f;       // LSD grip diff deadband (N.m)
    float RatioThreshold = 4.0f; // LSD grip imbalance ceiling
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
    bool AnyWheelGrounded = false; // any wheel touched ground (last step)
    int CouplingMode = 0;
    float CouplingCoefE = 1.0f;
    float CouplingCoefW = 1.0f;
    float DrivelineInertia = 1.0f;
    float ReflectedWheelSpeed = 0.0f; // Ww
    float ForwardSpeed = 0.0f;
    float ReactionTorque = 0.0f;  // Tw
    float CouplingTorque = 0.0f;  // clutch impulse solve
    float ClutchEngage = 0.0f;    // clutchEngage
    float DiffInputTorque = 0.0f; // T_in fed to the driven diff node

    float BrakeTorque = 0.0f;
    float Mass = 1200.0f; // total vehicle mass (kg)
    int32_t ElectronicIndex = -1;
    bool bElectronicsEnabled = false;

    bool bEdcEnabled = true;

    float AbsMagFront = 1.0f;
    float AbsMagRear = 1.0f;
    bool bAbsEnabled = true; // rebuilt every frame

    float EspMagFront = 1.0f;
    float EspMagRear = 1.0f;
    float EspMagFiltered = 1.0f;  // the committed value, smoothed 20/s
    bool bEspIntervening = false; // latches "already intervening"
    bool bEspEnabled = false;     // rebuilt every frame
    float EspGripDiff = 0.0f;     // DAT_010d6f0c, the signed axle imbalance
    float EspThreshold = 0.0f;    // the curve value it is tested against
};

float ThrottlePosForTorque( const PhysicsEngineSim &engine,
                            float targetTorque,
                            float omega );

float EngineTorqueAt( const PhysicsEngineSim &engine,
                      float omega,
                      float throttle );

void BuildEngineCurves( PhysicsEngineSim &engine,
                        const float *pTorquePairs,
                        int numPairs,
                        float bhp,
                        float packBoost,
                        float frictionCoef );
void StepPowertrain( PhysicsVehicleState &vehicle,
                     float throttle,
                     float brake,
                     float dt );
void ComputeTireForces( PhysicsWheelState &wheel,
                        const Matrix4x4f_t &vehicleMatrix );
void ComputeSuspensionForce( PhysicsWheelState &wheel );
float ComputeWheelReactionTorque( const PhysicsWheelState &wheel,
                                  float deltaTime,
                                  float *pOutLongForce = nullptr );
void IntegrateWheel( PhysicsWheelState &wheel, float deltaTime );
