#pragma once

#include "shared.h"

#include "vehicle_simulation.h"

#include "physics/collision_event.h"

#include <atomic>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Constraints/Constraint.h>
#include <Jolt/Physics/PhysicsStepListener.h>

namespace JPH {
    class MutableCompoundShape;
    class Shape;
    class PhysicsSystem;
    class Body;
    class BodyInterface;
} // namespace JPH

struct DBEntryCarPhysicsData;
struct DBEntryAftermarketPack;
class PhysicsVehicle;

class PhysicsDrivingControl {
public:
    virtual ~PhysicsDrivingControl() = default;
    virtual void forwardInput( PhysicsVehicle &vehicle, float deltaTime ) = 0;
};

class PhysicsVehicle final : public JPH::PhysicsStepListener {
public:
    struct WheelReadout {
        Eigen::Vector3f LocalPosition; // suspension attach point, body space
        float AngularVelocity;         // rad/s (+ = rolling forward)
        float SteerAngle;              // rad
        float RotationAngle;           // rad
        float SuspensionLength;        // m (current, from the hard point)
        float ReleasedPosition;        // m, wheel centre Y at full droop,
                                       // body space (retail PosReleased)
        float SuspensionTravel;        // m, drawn wheel centre above
                                       // ReleasedPosition (+ = up)
        float SuspensionStaticLength;  // m, ride height
        float SuspensionDroopStop;     // m, full droop (max length)
        float SuspensionBumpStop;      // m, full compression (min length)
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
        Eigen::Vector4f ContactNormal; // ground normal at the contact (world)
        Eigen::Vector4f ContactVelocity;
        Eigen::Vector4f UpAxis;
        Eigen::Vector4f ForwardAxis;
    };

    struct SuspensionSetup {
        float minLength;  // full-bump length, m
        float maxLength;  // full-droop length = static + sag, m
        float frequency;  // ride frequency, Hz
        float damping;    // damping ratio
        float cornerLoad; // static per-wheel load, N (spring force at static)
        float
            staticLength; // suspLength at ride height (spring == cornerLoad), m
        float droopStop;  // == maxLength; full droop is the zero-force point
    };

    // Aftermarket perf pack coefficients
    struct PerfPackCoefs {
        float TopSpeed = 1.0f;
        float BHP = 1.0f;
        float Accel = 0.0f;
        float Cx = 1.0f;
        float Weight = 1.0f;
        float Suspension = 1.0f;
        float Brake = 1.0f;
        bool bInstalled = false;
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
        int32_t ShiftState;
    };

    struct DrivingOptions {
        bool bAutomatic = true;
        bool bSteerControl = false;
        bool bHShifter = false;
        bool bManualClutch = false;
        float Sensitivity = 1.0f;
        float Linearity = 0.0f;
        float RumbleMax = 0.0f;
        int32_t DrivingHelpLevel = 0;
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

    enum eAutoGearboxState : int32_t {
        AGS_Stopped = 0,     // standing still, engine idling
        AGS_EngagingReverse, // brake held at a standstill -> select reverse
        AGS_Reverse,         // reverse engaged (pedals swapped)
        AGS_MovingOff,       // pulling away forwards (waiting for 1st)
        AGS_Forward,         // driving forwards, shift logic in charge
        AGS_LineLock,        // both pedals at a standstill (declutched)
    };

    enum eChronoAccelState : int32_t {
        CAS_Off = 0,
        CAS_Ready = 1, // stopped; starts as soon as the car moves
        CAS_StopFirst = 2,
        CAS_Running = 3,
    };

    enum eChronoBrakeState : int32_t {
        CBS_Off = 0,
        CBS_WaitForSpeed = 1, // waits for the car to brake below the target
        CBS_Braking = 3,
    };

    struct Chrono {
        float StartDistance = -1.0f; // odometer when the run started
        float Timer = -1.0f;
        float T100kmh = -1.0f;
        float T200kmh = -1.0f;
        float T1000m = -1.0f;
        float T400m = -1.0f;
        float T60mph = -1.0f;
        float TQuarterMile = -1.0f;
        float Brake100kmhDistance = 0.0f; // in feet
        float Brake80mphDistance = 0.0f;
        float Brake70mphDistance = 0.0f;
        float Brake60mphDistance = 0.0f;
        eChronoBrakeState BrakeState = CBS_Off;
        eChronoAccelState AccelState = CAS_Off;
        float BrakeTargetSpeed = 100.0f; // mph
        float PreviousSpeed = 0.0f;

        float MaxDrivenSlip = 0.0f; // m/s
        float MaxOmega = 0.0f;      // rad/s
        float SlipIntegral = 0.0f;  // integral of mean driven slip (m/s) dt
        float LimiterTime = 0.0f;   // seconds at >= 0.99 * OmegaMax
    };

public:
    float getEngineThrottle() const { return vehicle.Engine.Throttle; }
    float getEngineTorqueOut() const { return vehicle.Engine.TorqueOut; }
    float getEngineOmegaMax() const { return vehicle.Engine.OmegaMax; }
    int getShiftBlockReason() const { return vehicle.GearBox.ShiftBlockReason; }
    float getWheelOverRoad() const { return vehicle.GearBox.WheelOverRoad; }
    float getEngineRevRate() const { return revRateSmoothed; }
    float getGearLeverAnticipation() const { return gearLeverAnticipation; }
    int getClutchState() const { return vehicle.Clutch.State; }
    float getClutchTarget() const { return vehicle.Clutch.Target; }
    float getClutchEngagement() const { return vehicle.Clutch.Engagement; }
    float getClutchLockThreshold() const {
        return vehicle.Clutch.LockThreshold;
    }
    float getEngineAngAccel() const { return vehicle.Engine.AngAccel; }
    bool isManualClutchEnabled() const { return bManualClutch; }
    float getUpshiftRpm() const {
        const PhysicsGearboxSim &g = vehicle.GearBox;
        return ( ( 1.0f - g.UpshiftBand ) * vehicle.Engine.PeakPowerOmega +
                 g.UpshiftBand * vehicle.Engine.OmegaMax ) *
               9.549296f;
    }
    void setUpshiftBand( const float band ) {
        vehicle.GearBox.UpshiftBand = band;
    }
    float getPeakPowerRpm() const {
        return vehicle.Engine.PeakPowerOmega * 9.549296f;
    }
    float getClutchEngageRef() const {
        return ( vehicle.Engine.PeakTorqueOmega - vehicle.Engine.OmegaIdle ) *
               0.2f;
    }
    float getEngineSmoothedTorque() const {
        return vehicle.Engine.SmoothedTorque;
    }
    eEngineState getEngineState() const { return vehicle.Engine.State; }
    void setEngineState( eEngineState state ) { vehicle.Engine.State = state; }
    float getVehicleMass() const { return vehicle.Mass; }
    float getTimeSinceHardImpact() const { return timeSinceHardImpact; }
    bool isAirborne() const;
    float getInputBrake() const { return inputBrake; }
    float getInputThrottle() const { return inputThrottle; }
    float getInputHandbrake() const { return inputHandbrake; }
    int32_t getAutoReverseState() const { return autoGearboxState; }
    void setDrivingInputScripted( bool bScripted ) {
        bDrivingInputScripted = bScripted;
    }
    void setCinematicMode( bool bCinematic ) { bCinematicMode = bCinematic; }
    bool isInCinematicMode() const { return bCinematicMode; }
    void setInputControllerAttached( bool bAttached ) {
        bInputControllerAttached = bAttached;
    }
    bool isInputControllerAttached() const { return bInputControllerAttached; }
    bool isKeyframed() const { return bKeyframed; }
    void setPhysicsPaused( bool bPaused ) { bPausePhysics = bPaused; }
    bool isPhysicsPaused() const { return bPausePhysics; }
    bool isPaused() const {
        // FUN_0040aab0
        return ( bPausePhysics || postponedPause == 1 ) && postponedPause != 2;
    }
    bool isVirtual() const { return bVirtual; }
    void setVirtualFlag( const bool bInVirtual ) { bVirtual = bInVirtual; }
    void setElectronicMode( const int32_t mode ) {
        vehicle.ElectronicMode = mode;
    }
    void setAIDriven( const bool bAIDriven ) { bIsAI = bAIDriven; }
    bool areWheelsActive() const { return bWheelsActive; }
    bool isBike() const { return bIsBike; }
    float getVirtualSpeed() const { return virtualSpeed; }
    void setVirtualSpeed( float speed ) { virtualSpeed = speed; }
    void resetAutomaticGearbox();
    void applyDrivingOptions( const DrivingOptions &options );
    void applyAIDrivingOptions();
    bool isActive() const { return pChassisBody != nullptr; }
    void setAutoReverseAllowed( const bool bAllowed ) {
        bAutoReverseAllowed = bAllowed;
    }
    const JPH::Body *getChassisBody() const { return pChassisBody; }
    bool isSimulated() const { return bRegistered; }
    void setDrivingMode( uint8_t mode ) { drivingMode = mode; }
    void setDrivingControl( PhysicsDrivingControl *pControl ) {
        pDrivingControl = pControl;
    }
    PhysicsDrivingControl *getDrivingControl() const { return pDrivingControl; }
    void setSecondaryDrivingControl( PhysicsDrivingControl *pControl ) {
        // FUN_00827cf0
        pSecondaryDrivingControl = pControl;
    }
    void setBackView( bool bValue ) { bBackView = bValue; }
    bool isBackViewRequested() const { return bBackView; }
    void setChangeView( bool bValue ) { bChangeView = bValue; }
    bool isChangeViewRequested() const { return bChangeView; }
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
    bool hasKeyframedWheelMatrices() const { return bKeyframedWheelMatrices; }
    const Matrix4x4f_t &getKeyframedForkMatrix( uint32_t i ) const {
        return keyframedForkMatrices[i];
    }
    const Matrix4x4f_t &getKeyframedDiscMatrix( uint32_t i ) const {
        return keyframedDiscMatrices[i];
    }
    float getDisplayYOffset() const { return y3DOffset; }
    float getWheelRestCenterDrop( uint32_t i ) const {
        if ( i >= static_cast<uint32_t>( vehicle.NumWheels ) ) {
            return 0.0f;
        }
        return -wheelAttach[i].y() + suspRestLength[i];
    }
    const Chrono &getChrono() const { return chrono; }
    void resetChrono();
    void armChrono();
    void armBrakeChrono( float targetSpeedMph );
    float getOdometerDistance() const { return odometerDistance; }
    int32_t getOdometerKms() const { return odometerKms; }
    int32_t getOdometerTenMeters() const { return odometerTenMeters; }
    float getForwardSpeed() const { return vehicle.ForwardSpeed; }
    bool isTrafficBody() const { return bTrafficBody; }
    void setPostponedPause( uint8_t request ) { postponedPause = request; }
    uint8_t getPostponedPause() const { return postponedPause; }
    bool isAnyWheelOnSurface( const uint32_t surface ) const {
        for ( int32_t i = 0; i < vehicle.NumWheels; i++ ) {
            if ( wheelSurface[i] == surface ) {
                return true;
            }
        }
        return false;
    }
    int32_t getPhantomMode() const { return phantomMode; }
    bool isBikeCrashRecoveryPending() const { return bBikeCrashRecovery; }
    void setBikeCrashRecovery( bool bPending ) {
        bBikeCrashRecovery = bPending;
    }
    bool hasBikeRider() const { return bBikeRiderPresent; }
    const Matrix4x4f_t &getBikeRiderMatrix() const { return bikeRiderMatrix; }
    const Eigen::Vector4f &getBikeRiderVelocity() const {
        return bikeRiderVelocity;
    }
    bool isBikeRiderOnGround() const { return bBikeRiderOnGround; }
    bool isBikeRiderMoving() const { return bBikeRiderMoving; }
    bool isBikeRiderBody( const JPH::Body &body ) const {
        return pBikeRiderBody == &body;
    }
    void notifyBikeRiderGroundContact() { bBikeRiderGroundContact = true; }
    void setBikeRiderSeatMatrix( const Matrix4x4f_t &seatMatrix ) {
        bikeRiderSeatMatrix = seatMatrix;
    }
    bool isBackOnTrackForced() const { return bForceBackOnTrack; }
    void setBackOnTrackForced( bool bForced ) { bForceBackOnTrack = bForced; }
    bool isRespawnForced() const { return bForceRespawn; }
    void setRespawnForced( bool bForced ) { bForceRespawn = bForced; }
    const Eigen::Vector3f &getBodyDimensions() const { return bodyDimensions; }

    // Detachable factory member (bumper, hood, ...) jointed to the chassis.
    struct AttachedPart {
        JPH::Body *pBody = nullptr;
        uint64_t RenderKey = 0;
    };
    void setAttachedParts( std::vector<AttachedPart> &&parts,
                           const uint64_t chassisRenderKey ) {
        attachedParts = std::move( parts );
        bodyRenderKey = chassisRenderKey;
    }
    const std::vector<AttachedPart> &getAttachedParts() const {
        return attachedParts;
    }
    uint64_t getBodyRenderKey() const { return bodyRenderKey; }
    bool hasBrokenPart() const { return bHasBrokenPart; }
    void requestPhantomMode( uint8_t mode, float duration );
    float getCameraDomeHeight( const Eigen::Vector4f &shiftedPosition,
                               float lengthFactor,
                               float widthFactor,
                               float heightFactor ) const;
    bool updatePhantomAlpha( float dt, bool bForceOpaque, float &outAlpha );
    void setBackOnTrackPlaced( bool bPlaced ) { bBackOnTrackPlaced = bPlaced; }
    void requestRollLimitRestore() { bRollLimitRestoreRequest = true; }
    bool requestCollisionInertia();
    bool isRollLimitActive() const {
        return pRollLimitConstraint != nullptr &&
               pRollLimitConstraint->GetEnabled();
    }
    // Havok group-filter layer of the chassis wheel spheres: 15 while the
    // wheels are active, 5 at rest.
    static bool IsWheelSphere( const JPH::Shape *pShape );
    uint32_t getWheelSphereLayer() const { return wheelSphereLayer.load(); }
    bool consumeBackOnTrackPlaced() {
        const bool bPlaced = bBackOnTrackPlaced;
        bBackOnTrackPlaced = false;
        return bPlaced;
    }
    float getLateralSpeed() const { return vehicle.LateralSpeed; }
    float getRumbleMax() const { return rumbleMax; }
    float getLandingImpact() const { return landingImpact; }
    float getMaxCollisionEnergy() const;
    inline float getFilteredSteer() const { return filteredSteer; }
    inline float getSteerDeadZone() const { return steerDeadZone; }
    inline bool isSteerControlUsed() const { return bUseSteerControl; }

public:
    PhysicsVehicle();
    ~PhysicsVehicle() override;

    static PerfPackCoefs
    BuildPerfPackCoefs( const DBEntryAftermarketPack *pPack );
    static SuspensionSetup
    ComputeAxleSuspension( const PhysicsConfig &config,
                           const DBEntryCarPhysicsData &db,
                           bool bFront,
                           bool bBike,
                           const PerfPackCoefs &pack );

    void initialize();
    bool create( JPH::PhysicsSystem *pSystem,
                 JPH::Body *pChassis,
                 const DBEntryCarPhysicsData &db,
                 const void *pTorqueBinary = nullptr,
                 bool bFactoryShape = false,
                 const DBEntryAftermarketPack *pPack = nullptr,
                 bool bIsAIDriven = false,
                 bool bIsNetworkDriven = false );
    void destroy();

    void setInput( float throttle, float brake, float steer );
    void setHandbrake( float handbrake );
    void setLean( float lean ) { inputLean = lean; }
    float getDisplayRoll() const { return displayRoll; }
    float getDisplayPitch() const { return displayPitch; }
    float getDisplayYaw() const { return displayYaw; }
    float getBikeHeadLean() const { return bikeHeadLean; }
    float getBikeLeanSmoothed() const { return bikeLeanSmoothed; }
    void setIgnition( bool bKeyOn, bool bCutEdge );
    void applyDrivingMode();
    void declutchForDrivingMode();
    void tick( float deltaTime );
    void setDrivingHelpLevel( int32_t level );
    void initializeElectronics();

    DrivingGearbox getDrivingGearbox() const;
    void setCurrentGear( int32_t gear );
    void setAutomaticGearbox( bool bAutomatic );
    void requestUpshift();
    void requestDownshift();
    void setClutchInput( float clutch ) { clutchPedal = clutch; }
    void setGearRequests( bool bUp, bool bDown, int32_t select ) {
        bGearUpRequest = bUp;
        bGearDownRequest = bDown;
        gearSelectRequest = select;
    }
    bool isMechanicalGearbox() const { return bMechanicalGearbox; }

    bool castToGround( const Matrix4x4f_t &source,
                       Matrix4x4f_t &outTransform ) const;
    void resetToTransform( const Matrix4x4f_t &transform,
                           bool bCastToGround = false );
    void setKeyframed( bool bInKeyframed, bool bForceMatrix );
    void moveKeyframed( const Matrix4x4f_t &worldTransform,
                        float deltaTime,
                        bool bTeleport,
                        const Matrix4x4f_t *pForkMatrices = nullptr,
                        const Matrix4x4f_t *pDiscMatrices = nullptr );
    void setVelocity( const Eigen::Vector3f &linearVelocity,
                      bool bSyncPowertrain,
                      int32_t preferredGear );
    void updateKeyframedWheels( float deltaTime );
    void resetSimulationState();
    void setVirtual( bool bInVirtual );
    void pauseSimulation();
    void resumeSimulation();
    void setChassisPose( const JPH::RMat44 &world, bool bActivate );
    void setPostponedMatrix( const Matrix4x4f_t &worldTransform );
    float computeGroundToOriginOffset() const;

    void OnStep( const JPH::PhysicsStepListenerContext &inContext ) override;
    void forwardDrivingInput( const float deltaTime );

    Matrix4x4f_t getChassisWorldMatrix() const;
    Matrix4x4f_t getChassisShiftedMatrix() const;
    Matrix4x4f_t computeDisplayMatrix( const Matrix4x4f_t &sourceMatrix ) const;

    void publishStepPose();
    void onWorldShifted( const Eigen::Vector3f &worldShift );

    uint32_t getNumWheels() const;
    WheelReadout getWheelReadout( uint32_t index ) const;
    inline const PhysicsWheelState &getWheelState( uint32_t index ) const {
        return vehicle.Wheels[index];
    }
    inline const Eigen::Vector4f &getWorldAcceleration() const {
        return worldAcceleration;
    }
    inline const Eigen::Vector4f &getLocalAcceleration() const {
        return localAcceleration;
    }
    inline const Eigen::Vector4f &getLocalAccelerationG() const {
        return localAccelerationG;
    }
    float computeWheelSteerAngle( const PhysicsWheelState &w ) const;

    void updateChrono( float dt );
    void updateChronoDiagnostics( float speed, float dt );
    void logChronoSplit( const char *pLabel, float split ) const;
    void updateOdometer( float dt );
    void updateRevRate( float dt );
    void updateGearLeverAnticipation( float dt );
    void reportStuckClutch( float dt );
    void updateTyreHeat( float dt );
    float getWheelBurn( int32_t index ) const;
    void applyImpactWheelDamage();
    bool isInWater() const;
    void updatePhantomMode( float dt );
    void applyPhantomRequest();

    void pushCollisionEvent( const CollisionEvent &event );
    void flushCollisionEvents( std::vector<CollisionEvent> &outEvents );
    float getChassisSpeed() const;
    float getReportedSpeed( const bool bPlanar = false ) const;
    Eigen::Vector4f getChassisVelocity() const;
    Eigen::Vector4f getChassisAngularVelocity() const;

    float getEngineAngularVelocity() const;
    int32_t getCurrentGear() const;
    int32_t getReportedGear() const;

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
    bool applyWheelContact( const JPH::Body &chassis,
                            int wheelIndex,
                            float deltaTime,
                            const JPH::RVec3 &attachW,
                            const JPH::RVec3 &hitPosition,
                            const JPH::Vec3 &hitNormal,
                            float hitDist,
                            uint32_t surface,
                            uint32_t material );
    void updateWheelLandingGap( int i,
                                float gap,
                                float heightStep,
                                float deltaTime );
    void resetLandingState();
    void updateLandingImpact();
    void pushLandingFXEvent();
    void setVirtualWheelContact( const JPH::Body &chassis,
                                 int wheelIndex,
                                 float deltaTime );
    void stepVirtual( float deltaTime );

    float filterSteering( float rawTarget,
                          const JPH::Body &chassis,
                          float deltaTime );
    void applyAntiRollBars();
    void updateBikeRollLimit( const JPH::Body &chassis );
    void updateBikeRider();
    void updateWheelsActive();
    void updateCollisionInertia( float deltaTime );
    void spawnBikeRider();
    void destroyBikeRider();
    void publishBikeRider();
    void updateBikeLean( float deltaTime );
    void updateBikePitch( float deltaTime );
    void resetBikeLean();
    void installWheelCollision( bool bProceduralShape );
    void updateWheelCollision();
    void updateBikeCollision();
    void applyGroundAlignment( JPH::PhysicsSystem &system,
                               const JPH::Body &chassis,
                               JPH::BodyInterface &bi,
                               float deltaTime );

    void updateGripModulation( float chassisSpeed );
    void arbitrateAutomaticGearbox( float deltaTime, float chassisSpeed );
    void logRollDiagnostics() const;

private:
    JPH::PhysicsSystem *pPhysicsSystem;
    JPH::Body *pChassisBody;
    bool bRegistered;

    PhysicsVehicleState vehicle;
    const PhysicsConfig *pConfig = &gPhysicsConfig;

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
    float prevGroundHeight[PhysicsVehicleState::kMaxWheels] =
        {}; // last step's probed ground height (slew limit)
    float wheelWidth[PhysicsVehicleState::kMaxWheels] = {};
    float wheelGroundGap[PhysicsVehicleState::kMaxWheels] = {};
    float landingGap[PhysicsVehicleState::kMaxWheels] = {};
    float landingPrevGap[PhysicsVehicleState::kMaxWheels] = {};
    float landingGapRate[PhysicsVehicleState::kMaxWheels] = {};
    float landingPeakGap[PhysicsVehicleState::kMaxWheels] = {};
    float groundHeightStep[PhysicsVehicleState::kMaxWheels] = {};
    float landingScore = 0.0f;
    float landingImpact = 0.0f;
    bool bLandingDetected = false;
    uint8_t landingWindowSteps = 0;
    uint8_t landingCooldownSteps = 0;
    float wheelRumbleTimer[PhysicsVehicleState::kMaxWheels] = {};
    float wheelRumbleAmount[PhysicsVehicleState::kMaxWheels] = {};
    float wheelTyreHeat[PhysicsVehicleState::kMaxWheels] = {};
    Eigen::Vector4f groundAlignNormal =
        Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f );
    float groundAlignRoll = 0.0f;
    float groundAlignRollPrev = 0.0f;
    float groundAlignRollRate = 0.0f;
    float groundAlignPitch = 0.0f;
    float groundAlignPitchPrev = 0.0f;
    float groundAlignPitchRate = 0.0f;
    int32_t groundAlignQueryTimer = 0;
    uint32_t wheelSurface[PhysicsVehicleState::kMaxWheels] =
        {}; // raycast contact phys-type code
    uint32_t wheelMaterial[PhysicsVehicleState::kMaxWheels] =
        {}; // raycast contact eMaterialType (MT_Default = none)

    int32_t appliedDrivingHelpLevel = -2;

    bool bLowSpeedGripState = false;
    bool bLowSpeedLongAssist = false; // FIXME
    bool bLowSpeedLatAssist = false;  // FIXME
    bool bIsBike = false;
    bool bTrafficBody = false;
    bool bRollLimitRestoreRequest = false;          // +0xd31
    bool bBackOnTrackPlaced = false;                // +0xd32
    JPH::Ref<JPH::Constraint> pRollLimitConstraint; // +0x3f1c
    uint8_t postponedPause = 0;                     // +0x8da: 1 pause, 2 resume
    bool bBikeCrashRecovery = false;                // +0xbf0
    JPH::Body *pBikeRiderBody = nullptr;            // +0x4368
    JPH::MutableCompoundShape *pChassisCompound = nullptr; // +0x4378
    JPH::Vec3 chassisCompoundCOM = JPH::Vec3::sZero();
    int32_t numBikeCollisionBoxes = 0;
    Matrix4x4f_t bikeCollisionBoxRest[3]; // +0x3fa0, +0x3fe0, +0x4020
    std::atomic<uint32_t> wheelSphereLayer{ 15 };
    bool bWheelSpheresActive = true;                    // entry+0x8c
    std::atomic<bool> bBikeRiderGroundContact{ false }; // +0xe4a
    std::atomic<bool> bCollisionInertiaRequest{ false };
    float collisionInertiaTimer = 0.0f;                     // +0xec4
    JPH::Vec3 savedInvInertiaDiagonal = JPH::Vec3::sZero(); // +0xd40
    JPH::Quat savedInertiaRotation = JPH::Quat::sIdentity();
    bool bBikeRiderPresent = false;
    bool bBikeRiderOnGround = false;
    bool bBikeRiderMoving = false;
    Matrix4x4f_t bikeRiderMatrix = Matrix4x4f_t::Identity();
    Matrix4x4f_t bikeRiderSeatMatrix = Matrix4x4f_t::Identity();
    Eigen::Vector4f bikeRiderVelocity = Eigen::Vector4f::Zero();
    bool bForceBackOnTrack = false; // +0xbf1
    bool bForceRespawn = false;     // +0xbf2
    // Phantom (ghost) chassis: 1 normal, 2 ghost, 4 ghost until clear of
    // other cars, 5 permanent ghost
    uint8_t phantomRequest = 0;
    float phantomRequestDuration = 0.0f;
    int32_t phantomMode = 1;
    float phantomTimer = 0.0f;
    float phantomDuration = 0.0f;
    bool bPhantomFadeIn = false;
    Eigen::Vector3f bodyDimensions = Eigen::Vector3f::Zero();
    std::vector<AttachedPart> attachedParts;
    uint64_t bodyRenderKey = 0;
    bool bHasBrokenPart = false;
    float rawInputThrottle;
    float rawInputBrake;
    float inputThrottle;
    float inputBrake;
    float inputSteer;
    float inputHandbrake;
    float inputLean = 0.0f; // Driving::Lean (+0xe14)
    float prevInputLean = 0.0f;
    uint8_t drivingMode;
    PhysicsDrivingControl *pDrivingControl = nullptr;
    PhysicsDrivingControl *pSecondaryDrivingControl = nullptr;
    bool bBackView = false;
    bool bChangeView = false;
    float prevInputThrottle;
    float prevInputBrake;
    float prevInputSteer;
    int32_t autoGearboxState;
    float autoGearboxThrottle;
    float autoGearboxBrake;
    float clutchPedal = 0.0f;
    bool bGearUpRequest = false;
    bool bGearDownRequest = false;
    int32_t gearSelectRequest = 1;
    bool bGearSelectBlocked = false;
    bool bMechanicalGearbox = false;
    bool bAutoReverseAllowed = true;
    bool bDrivingInputScripted;
    bool bIsAI;
    bool bIsNetwork;
    bool bCinematicMode;
    bool bInputControllerAttached;
    bool bPausePhysics;
    bool bVirtual;
    bool bWheelsActive;
    float virtualSpeed;
    bool bKeyframed;
    bool bForceKeyframeMatrix;
    Matrix4x4f_t keyframeMatrix;
    bool bKeyframedWheelMatrices = false;
    Matrix4x4f_t keyframedForkMatrices[PhysicsVehicleState::kMaxWheels];
    Matrix4x4f_t keyframedDiscMatrices[PhysicsVehicleState::kMaxWheels];
    Eigen::Vector3f prevKeyframePosition; // absolute world space
    Eigen::Vector3f keyframeVelocity = Eigen::Vector3f::Zero();
    Eigen::Vector3f prevKeyframeForward = Eigen::Vector3f::UnitZ();
    float keyframeSteer = 0.0f;
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
    float steerSensitivity;
    float rumbleMax;
    bool bHShifterEnabled;
    bool bManualClutch;
    float engineOmega; // rad/s, for the tacho / CarBodyState::Omega
    int32_t currentGear;
    float finalDriveRatio;

    float displayRoll;             // +0xe68, bike lean
    float displayPitch;            // +0xe74, bike wheelie (< 0) / stoppie (> 0)
    float displayYaw;              // +0xe7c
    float bikeLeanSmoothed = 0.0f; // +0xe6c, smoothed Driving::Lean
    float bikeHeadLean = 0.0f;     // +0xe70
    float displayPitchRate = 0.0f; // +0xe78
    bool bPitchCrossedZero = true; // +0xd2c
    int32_t pitchLandingSide = -1; // +0xd28
    float y3DOffset;
    float bodyHeight;
    float rideHeightFront;
    float rideHeightRear;
    float rideHeightMax[2] = {}; // m, front/rear
    float steerSharpness = 1.0f;

    Chrono chrono;
    float odometerDistance = 0.0f; // in metres
    int32_t odometerKms = 0;
    int32_t odometerTenMeters = 0;
    float gearLeverAnticipation = 0.0f;
    int32_t gearLeverState = 0;
    float gearLeverPrevOmega = 0.0f;

    float revRateSmoothed = 0.0f;
    float revGearTime = 0.0f;
    float revGearEntryRpm = 0.0f;
    float revGearPeakRate = 0.0f;
    int revGearTracked = -1;
    float stuckClutchTimer = 0.0f;
    float launchEntryRpm = 0.0f;
    float launchEntrySpeed = 0.0f;
    bool bStuckClutchReported = false;

    static constexpr int32_t kMaxCollisionEvents = 0x80;
    std::array<CollisionEvent, kMaxCollisionEvents> collisionEvents;
    int32_t numCollisionEvents = 0;
    float timeSinceHardImpact = 0.0f;

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

    Eigen::Vector4f worldAcceleration;
    Eigen::Vector4f localAcceleration;
    Eigen::Vector4f localAccelerationG;
};

extern bool gbSimuModeEnabled;
