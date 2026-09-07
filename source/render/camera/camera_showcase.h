#pragma once

#include "camera_game.h"

#include "core/mutex.h"
#include "physics/gs_physics.h"

class CarState;

class CameraShowcase : public CameraGame {
public:
    inline void setCollisionFlags( const uint32_t value ) {
        flagsShowcase = value;
    }
    inline void setTargetCar( CarState *pCarState, void *pPhysicsVehicle ) {
        pTargetCarState = pCarState;
        pTargetPhysicsVehicle = pPhysicsVehicle;
    }
    inline void setDistanceRange( const float minValue, const float maxValue ) {
        minDistance = minValue;
        maxDistance = maxValue;
    }
    inline void setDistance( const float value ) { distanceToTarget = value; }
    inline float getDistance() const { return distanceToTarget; }
    inline float getMinDistance() const { return minDistance; }
    inline float getMaxDistance() const { return maxDistance; }
    inline void setOffset( const Eigen::Vector4f &value ) { offset = value; }
    inline void setAllowRoll( const bool value ) { bAllowRoll = value; }
    inline void setCanMoveCamera( const bool value ) { bCanMoveCamera = value; }
    inline bool canMoveCamera() const { return bCanMoveCamera; }
    inline void setInputEnabled( const bool value ) { bInputEnabled = value; }

public:
    CameraShowcase();
    virtual ~CameraShowcase();

    void initialize() override;
    bool tick( float deltaTime, bool param_1, bool param_2 ) override;
    void destroy() override;

    void lookAt( Eigen::Vector4f *param_1 );
    bool placeAt( const Eigen::Vector4f &worldPosition, bool bSnapToTarget );

private:
    friend class GSCamera;
    friend class MngScreenshots;

private:
    TestDriveMutex mutex;

    Eigen::Vector4f *pTarget;
    float rollAngle;
    Eigen::Vector4f translation;
    Eigen::Vector4f offset;
    Eigen::Vector4f angles;
    int32_t cursorType;
    float mouseInput;
    float distanceToTarget;
    float maxDistance;
    float minDistance;
    float minRotAngle;
    float maxRotAngle;
    float floorHeight;
    uint32_t flagsShowcase;
    RigidBody_t pPhysicsBoundingSphere;
    CarState *pTargetCarState;
    void *pTargetPhysicsVehicle;
    Eigen::Vector4f lastTranslation;
    Eigen::Vector4f lastAngles;
    float lastDistanceToTarget;
    Eigen::Vector4f lastPlacementPosition;

    uint8_t bInputEnabled : 1;
    uint8_t bCanMoveCamera : 1;
    uint8_t bAllowRoll : 1;

private:
    void FUN_008da540( Eigen::Vector4f &param_2, Eigen::Vector4f &param_3 );
    bool FUN_008d90e0( Eigen::Vector4f *param_2, bool param_3, float param_4 );
    void FUN_008d7fb0();
    void updateOrbitFromPosition( const Eigen::Vector4f &worldPosition,
                                  bool bWriteToPreviousSlots );
    float sweepCameraSegment( Eigen::Vector4f &out,
                              const Eigen::Vector4f &from,
                              const Eigen::Vector4f &to );
    void FUN_008d8180();
    void updateMouse( float param_1 );
    void updateMouseCursor();
    void updateAngles( float param_1 );
    float getAngleForChannel( struct InputChannel *pChannel );
    void updateTranslation( float param_1 );
    void updateRoll( float param_1 );

    void applyRotation( Matrix4x4f_t &matrix );
    void FUN_008d8750( Eigen::Vector4f &param_2 );
};
