#pragma once

#include "camera_game.h"

class GameMode;

class CameraGME : public CameraGame {
public:
    CameraGME();
    virtual ~CameraGME();

    void initialize() override;
    bool tick( float deltaTime, bool param_1, bool param_2 ) override;
    void destroy() override;

private:
    void updatePan( float deltaTime );
    void updateZoom( float deltaTime );

private:
    Eigen::Vector4f direction;
    float zoomSpeed;
    float distance;
    float pitch;
    float yaw;
    float panSpeedX;
    float panSpeedZ;
    bool bAutoZoom;
    bool bField0x1a1;
    float autoPitchTimer;
    int32_t gmeMode;
    GameMode *pGameModeEditor;
};
