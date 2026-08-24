#pragma once

#include "game/gm_world.h"

#include "game/mng_logo_display.h"
#include "render/mng_reflection.h"

#include "mng_collection.h"
#include "mng_rankings.h"
#include "scene_living.h"

struct FlashMessage;
class FlashPlayer;

extern bool gbEnteringLivingHub;

class GMLiving : public GameModeWorldBase {
public:
    GMLiving();
    ~GMLiving();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void draw( float deltaTime, float totalTime ) override {
        // FUN_00769590
        // NOP
    }
    void pause() override;
    void destroy() override;

    bool onMessage( FlashMessage *pMessage ) override;
    bool executeAction( int32_t param_2, void *param_3 ) override;

private:
    void resolveHouseSpot();
    void installHouseSpot();

private:
    MngReflection mngReflection;
    MngLogoDisplay mngLogoDisplay;
    MngCollection mngCollection;
    MngRankings mngRankings;
    SceneLiving sceneLiving;
    FlashPlayer *pFlashPlayer;
    float resetTime;
    bool bBlockSceneUpdate;
    bool bSceneResumeAllowed;
    bool bHouseSpotInstalled;
    bool bRadialMenuOpen;
};
