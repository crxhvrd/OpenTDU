#pragma once

#include "camera_game.h"

class PhysicsVehicle;

class CameraFocusObject : public CameraGame {
public:
    CameraFocusObject();
    virtual ~CameraFocusObject();

    void initialize() override;
    bool tick( float deltaTime, bool param_1, bool param_2 ) override;
    void destroy() override;

private:
    void updateFromMouse( float deltaTime );
    Matrix4x4f_t computeRotation() const;

protected:
    uint32_t mouseIndex;
    Eigen::Vector4f offset;
    Eigen::Vector4f angle;
    float distance;
    PhysicsVehicle *pObject;
};
