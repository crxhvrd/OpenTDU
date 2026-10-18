#pragma once

#include "filesystem/bank.h"
#include "game/scene_interior.h"

// Realtor office interior (Interior/ire_*.bnk)
class SceneEstates {
public:
    virtual SceneInterior *getContent() { /* FUN_0062d320 */ return &content; }

public:
    SceneEstates();
    virtual ~SceneEstates();

    virtual void initialize();
    virtual void terminate();
    virtual void tick( float deltaTime, float totalTime );
    virtual void draw() {
        // FUN_00774190
        // NOP
    }
    virtual void pause();
    void resume();
    void suspend();

private:
    SceneInterior content;
    Bank interiorBank;
};
