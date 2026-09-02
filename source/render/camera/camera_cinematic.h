#pragma once

#include "camera_game.h"

class CinResCollection;

class CameraCinematic : public CameraGame {
public:
    inline void setCinematicInterface( CinResCollection *pInterface ) {
        pCinematicInterface = pInterface;
    }
    inline CinResCollection *getCinematicInterface() const {
        return pCinematicInterface;
    }

    inline void setNearPlane( const float value ) { nearPlane = value; }
    inline void setFarPlane( const float value ) { farPlane = value; }
    inline void setKeepGlobalPlanes( const bool value ) {
        bKeepGlobalPlanes = value;
    }

public:
    CameraCinematic();
    virtual ~CameraCinematic();

    void initialize() override;
    bool tick( float deltaTime, bool param_1, bool param_2 ) override;
    void destroy() override;

private:
    CinResCollection *pCinematicInterface;
    bool bKeepGlobalPlanes;
};
