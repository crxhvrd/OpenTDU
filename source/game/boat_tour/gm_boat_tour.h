#pragma once

#include "game/gm_world.h"
#include "game/scenaric/scenaric_bank.h"

#include "core/tcb_spline.h"

class EntityBoatTour;

class GMBoatTour : public GameModeWorldBase {
public:
    GMBoatTour();
    ~GMBoatTour();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void draw( float deltaTime, float totalTime ) override;
    void pause() override;
    void destroy() override;

private:
    class CameraSteadyCam *getTourCamera() const;
    void applyCameraTransform( class CameraSteadyCam *pCamera,
                               const Matrix4x4f_t &boatTransform,
                               bool bResetPitch );
    const TCBSplinePosition *getActiveSpline() const;
    Entity *getActiveBoat() const;
    bool getCameraOffset( Matrix4x4f_t &outOffset ) const;

    Entity *resolveBoatInstance( Entity *pTemplate );

private:
    ScenaricBank boatTourBank;
    EntityBoatTour *pTour;
    float currentDistanceOnPath;
    float tourSpeed;
    Entity *pBoatInstance;
};
