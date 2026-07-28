#pragma once

#include "game/gs_base.h"

class GSPhysicsFX : public GameSystem {
public:
    const char *getName() const override { return "Service : PhysicsFX"; }

public:
    GSPhysicsFX();
    ~GSPhysicsFX();

    bool initialize( TestDriveGameInstance * ) override;
    void terminate() override;
    void tick( float totalTime, float deltaTime ) override;
    void draw( float deltaTime ) override;
};
