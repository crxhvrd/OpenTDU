#pragma once

#include "ai/ai_fsm.h"
#include "ai/pid_ctrl.h"
#include "physics/vehicle/physics_vehicle.h"

class AiVehicle;
class EntityCarPlayer;

class AiVehLocomotion : public PhysicsDrivingControl {
public:
    inline bool isPhysicsDriven() const { return bPhysicsControl; }
    inline bool isPhysicsPaused() const { return bPhysicsPaused; }
    inline void setResetPhysicsOnAttach( const bool bReset ) {
        bResetPhysicsOnAttach = bReset;
    }
    inline float getTargetSpeed() const { return targetSpeed; }
    inline float getPadThrottle() const { return padThrottle; }
    inline const Eigen::Vector4f &getTargetDirection() const {
        return targetDirection;
    }

public:
    AiVehLocomotion();
    virtual ~AiVehLocomotion() = default;

    virtual void attach( AiVehicle *pOwner );
    virtual void reset( const bool bFull );
    virtual void tick( const float deltaTime, const float totalTime );
    virtual void detach();
    virtual void setPhysicsActive( const bool bActive, const bool bForce );
    virtual void setDrivenByNavigation( const bool bDriven );
    virtual void setManualInputs( const float accel,
                                  const float brake,
                                  const float handbrake,
                                  const float steer,
                                  const float lean );

protected:
    AiVehicle *pVehicle;
    float targetSpeed;
    float padThrottle; // > 0 throttle, < 0 brake
    float padHandbrake;
    float padSteering;
    bool bResetPhysicsOnAttach;
    bool bPhysicsPaused;
    bool bPhysicsControl;
    Eigen::Vector4f targetDirection;
    bool bSmoothSteering;
    float smoothSteerCoef;
    bool bDrivenByNavigation;
    float manualAccel;
    float manualBrake;
    float manualHandbrake;
    float manualSteer;
    float manualLean;
    float blockedStartTime;
    float blockedEndTime;
    Eigen::Vector4f groupPosNew;
    float initialSpeed;
    float virtualDistanceAccum;
    float timeAccum;
    int32_t numAccum;

protected:
    void updateBlockedTime( const float totalTime );
    void checkPhysicsDistance();
    void moveKinematic();
    void writeInputs( PhysicsVehicle &vehicle, const float deltaTime );
};

class AiVehLocoTraffic : public AiVehLocomotion {
public:
    AiVehLocoTraffic();

    void attach( AiVehicle *pOwner ) override;
    void reset( const bool bFull ) override;
    void tick( const float deltaTime, const float totalTime ) override;
    void detach() override;
    void forwardInput( PhysicsVehicle &vehicle, float deltaTime ) override;

private:
    float targetAngle;
    float correctThrottle;
    PIDCtrl speedControl;
    uint8_t speedPIDType;
    float currentAngle;
    PIDCtrl steerControl;
    uint8_t steerPIDType;
    float lastSteering;

private:
    void refreshPIDCoefs();
    void computeControls( PhysicsVehicle &vehicle, const float deltaTime );
};

class AiVehLocoParked : public AiVehLocomotion, private AiFsm {
public:
    enum eState : int32_t {
        PS_Inactive = 0,
        PS_Active = 1,
        PS_Parked = 2,
    };

public:
    inline void setOwnerEntity( EntityCarPlayer *pEntity ) {
        pOwnerEntity = pEntity;
    }
    inline void startParked() { goTo( PS_Parked ); }

public:
    AiVehLocoParked();

    void attach( AiVehicle *pOwner ) override;
    void reset( const bool bFull ) override;
    void tick( const float deltaTime, const float totalTime ) override;
    void detach() override;
    void forwardInput( PhysicsVehicle &vehicle, float deltaTime ) override;

private:
    EntityCarPlayer *pOwnerEntity;

private:
    bool handle( const int32_t event, const int32_t state ) override;
    float computePlayerDistanceSq() const;
};
