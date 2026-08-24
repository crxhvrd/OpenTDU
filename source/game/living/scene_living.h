#pragma once

#include "filesystem/bank.h"
#include "game/scene_interior.h"

struct SpotMap;

// House interior scene
class SceneLiving {
public:
    virtual SceneInterior *getContent() { /* FUN_0062d320 */ return &content; }

public:
    SceneLiving();
    virtual ~SceneLiving();

    virtual void initialize();
    virtual void terminate();
    virtual void tick( float deltaTime, float totalTime );
    virtual void draw();
    virtual void pause();
    void resume();
    void suspend();

private:
    void loadInteriorBank();

private:
    SceneInterior content;
    Bank interiorBank;
    ScenaricBank spotBank;
};
