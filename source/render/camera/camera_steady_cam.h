#pragma once

#include "camera_game.h"

class CameraSteadyCam : public CameraGame {
public:
    inline Eigen::Vector4f *getRootPositionRW() { return &rootPosition; }
    inline void setRootPosition( const Eigen::Vector4f &position ) {
        rootPosition = position;
    }
    inline void setRootAngleX( const float value ) { rootAngleX = value; }
    inline void setRootAngleY( const float value ) { rootAngleY = value; }
    inline void setBlockPosition( const bool value ) { bBlockPosition = value; }
    inline void setPadMode( const int32_t value ) { modePad = value; }
    inline void setFPSMode( const int32_t value ) { modeFPS = value; }
    inline void setInvertMouse( const bool value ) { bInvertMouse = value; }

public:
    CameraSteadyCam();
    virtual ~CameraSteadyCam();

    void initialize() override;
    bool tick( float deltaTime, bool param_1, bool param_2 ) override;
    void destroy() override;

protected:
    void *pBody;
    Eigen::Vector4f rootPosition;
    Eigen::Vector4f rootTarget;
    uint32_t mousePort;
    float distance;
    float angleX;
    float angleY;
    float rootAngleX;
    float rootAngleY;
    float sensitivity;
    uint16_t stepRoll;
    float mouseFov;
    float speedForward;
    int32_t autoTilt;
    float autoTiltFactor;
    int32_t forceCarBehind;
    int32_t modeFPS;
    int32_t modeGravity;
    int32_t followCar;
    int32_t carIndex;
    int32_t modePad;
    int32_t modeCrouch;
    int32_t modeRun;
    bool bInvertMouse;
    bool bCollision;
    bool bBlockPosition;
};
