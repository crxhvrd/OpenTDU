#pragma once

#include "game/gm_world.h"

#include "scene_aftermarket.h"

class FlashPlayer;

class GMAfterMarket : public GameModeWorldBase {
public:
    GMAfterMarket();
    ~GMAfterMarket();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void draw( float deltaTime, float totalTime ) override {
        // FUN_007bbaf0
        // NOP
    }
    void pause() override {
        // FUN_007bbb00
        GameModeScene::pause();
    }
    void destroy() override;

    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) override {
        // FUN_007bbae0
        return false;
    }

private:
    SceneAfterMarket sceneAfterMarket;
    FlashPlayer *pFlashPlayer;
};
