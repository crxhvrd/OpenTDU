#pragma once

#include "camera_game.h"

class PhysicsCharacter;

bool IsFirstPersonInputCaptured();

class CameraFirstPerson : public CameraGame {
public:
    static constexpr int32_t kCameraType = 0x100;

public:
    bool tick( float deltaTime, bool param_1, bool param_2 ) override;
    void destroy() override;

    inline void setCharacter( PhysicsCharacter *pCharacter ) {
        pCharacterController = pCharacter;
    }
    inline float getEyeHeight() const { return eyeHeight; }

    inline void setNoClip( bool bEnabled ) { bNoClip = bEnabled; }
    inline bool isNoClip() const { return bNoClip; }
    inline void setSpawnPosition( const Eigen::Vector4f &position ) {
        spawnPosition = position;
    }

public:
    CameraFirstPerson();
    virtual ~CameraFirstPerson();

    void setViewDirection( const Eigen::Vector3f &direction );

private:
    Eigen::Vector3f getForwardVector() const;

private:
    PhysicsCharacter *pCharacterController;

    float yaw;   // radians, 0 = looking down -Z
    float pitch; // radians, positive = looking up

    float eyeHeight;
    float walkSpeed;
    float runSpeed;
    float lookSensitivity;

    Eigen::Vector4f spawnPosition;
    bool bNoClip;
};
