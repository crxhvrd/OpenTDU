#pragma once

#include "camera_game.h"

class PhysicsVehicle;

enum class eReplayRoadShot : int32_t {
    RRS_Zoom = 0,
    RRS_Large = 1,
    RRS_Follow = 2,
    RRS_Fixed = 3,
    RRS_Helico = 4,
    RRS_Car = 5
};

enum class eReplayRoadCarShot : int32_t {
    RRCS_Wheel = 0,
    RRCS_Offset = 1,
    RRCS_Fixed = 2
};

// Replay.ini (RPL_*)
extern int32_t gRPLRatioCamOutCamOnCar;
extern int32_t gRPLRatioCamCarOutInGame;
extern float gRPLMinTimeToSwap;
extern float gRPLMaxTimeToSwap;
extern float gRPLSwapLength;
extern float gRPLMinHeight;
extern float gRPLMaxHeight;
extern float gRPLMaxHeightModeLarge;
extern float gRPLMaxWidthFromCenterOfRoad;
extern float gRPLSpeedRatio;
extern Eigen::Vector4f gRPLCarWheelOffset;
extern Eigen::Vector4f gRPLCarOffsetPos;
extern Eigen::Vector4f gRPLCarOffsetTRG;
extern float gRPLFOVNormalMin;
extern float gRPLFOVNormalMax;
extern float gRPLFOVZoomK1;
extern float gRPLFOVZoomK2;
extern float gRPLFOVZoomMin;
extern float gRPLFOVZoomMax;
extern float gRPLFOVLargeMin;
extern float gRPLFOVLargeMax;
extern float gRPLFOVCar;
extern float gRPLDOFCamNormalMin;
extern float gRPLDOFCamNormalMax;
extern float gRPLDOFCamZoomMin;
extern float gRPLDOFCamZoomMax;
extern float gRPLDOFCamLargeMin;
extern float gRPLDOFCamLargeMax;
extern float gRPLDOFCamFixedMin;
extern float gRPLDOFCamFixedMax;
extern Eigen::Vector4f gRPLSpringMinPos;
extern Eigen::Vector4f gRPLSpringMaxPos;
extern Eigen::Vector4f gRPLSpringRate;
extern Eigen::Vector4f gRPLSpringDamping;
extern Eigen::Vector4f gRPLSpringForceScale;
extern Eigen::Vector4f gRPLSpringForceDeadZone;
extern Eigen::Vector4f gRPLSpringForceClamping;
extern float gRPLSpringTimescale;
extern float gRPLHelicoMaxAccelLongitudinal;
extern float gRPLHelicoMaxAccelLateral;
extern float gRPLHelicoMaxSpeedLongitudinal;
extern float gRPLHelicoMaxSpeedLateral;
extern float gRPLDampingMotion;
extern float gRPLHelicoHeight;
extern float gRPLHelicoFOV;
extern Eigen::Vector4f gRPLHelicoAim;

class CameraReplayRoad : public CameraGame {
public:
    inline void setShot( const eReplayRoadShot shot,
                         const eReplayRoadCarShot carShot ) {
        // FUN_008e4230 (inlined)
        this->shot = shot;
        this->carShot = carShot;
        bResetShot = true;
    }

    inline bool isSwapRequested() const { return bSwapRequested; }

public:
    CameraReplayRoad();
    virtual ~CameraReplayRoad();

    void initialize() override;
    bool tick( float deltaTime, bool param_1, bool param_2 ) override;
    void destroy() override;

private:
    void resetShot( const Matrix4x4f_t &carMatrix, PhysicsVehicle *pVehicle );
    void resetFollowShot( const Matrix4x4f_t &carMatrix,
                          PhysicsVehicle *pVehicle );
    void resetHelicoShot( const Matrix4x4f_t &carMatrix,
                          PhysicsVehicle *pVehicle );
    void resetCarShot( PhysicsVehicle *pVehicle );
    void resetRoadSideShot( const Matrix4x4f_t &carMatrix );

    void updateFollowShot( const float deltaTime, const float carDistance );
    void updateHelicoShot( const Matrix4x4f_t &carMatrix,
                           const float deltaTime );
    void updateCarSpring( PhysicsVehicle *pVehicle, const float deltaTime );

    Eigen::Vector4f computeTarget( const Matrix4x4f_t &carMatrix,
                                   const float deltaTime );
    float computeFOV( const Matrix4x4f_t &carMatrix,
                      const float carDistance,
                      const float deltaTime,
                      float &outNearPlane,
                      float &outFarPlane );
    void updateDepthOfField( const float deltaTime );

    inline bool isCarShotSpringed() const {
        return shot == eReplayRoadShot::RRS_Car &&
               carShot >= eReplayRoadCarShot::RRCS_Wheel &&
               carShot <= eReplayRoadCarShot::RRCS_Fixed;
    }

private:
    eReplayRoadShot shot;
    eReplayRoadCarShot carShot;
    bool bSwapRequested;
    bool bResetShot;
    Eigen::Vector4f shotPosition;
    Eigen::Vector4f shotTarget;
    Eigen::Vector4f shotVelocity;
    float shotSpeed;
    float roadSide;
    bool bDepthOfField;

    Eigen::Vector4f springPosition;
    Eigen::Vector4f springVelocity;
    Eigen::Vector4f springUnused;

    float helicoYaw;
    Eigen::Vector4f helicoForward;
    Eigen::Vector4f helicoSide;
    float helicoFOVTarget;
    float helicoFOV;
    float helicoFOVTimer;
    float helicoOrbitAngle;
    float helicoOrbitAngleTarget;
    float helicoOrbitRadius;
    float helicoOrbitRadiusTarget;
    float helicoOrbitTimer;
    float helicoAimAngle;
    float helicoAimAngleTarget;
    float helicoAimRadius;
    float helicoAimRadiusTarget;
    float helicoAimTimer;
};
