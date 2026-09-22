#pragma once

#include "game/gm_world.h"

#include "game/first_person_mode.h"
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
    inline FlashPlayer *getFlashPlayer() const { return pFlashPlayer; }

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
    bool isDrawnInFakeDOF() override {
        // FUN_0042cf80
        return sceneLiving.getContent()->isDrawnInDOFPass();
    }

#ifdef OTDU_USE_IMGUI
    void drawDebug( float deltaTime ) override;
#endif
#ifdef OTDU_DEVBUILD
    void requestHouseSwitch( int32_t spotIndex );
#endif

private:
    void resolveHouseSpot();
    void installHouseSpot();
#ifdef OTDU_DEVBUILD
    void switchHouse( int32_t spotIndex );
#endif

private:
    MngReflection mngReflection;
    MngLogoDisplay mngLogoDisplay;
    MngCollection mngCollection;
    MngRankings mngRankings;
    SceneLiving sceneLiving;
    FirstPersonMode firstPerson;
    FlashPlayer *pFlashPlayer;
    float resetTime;
    bool bBlockSceneUpdate;
    bool bSceneResumeAllowed;
    bool bHouseSpotInstalled;
    bool bRadialMenuOpen;
    int32_t pendingHouseSpotIndex; // -1 = nothing queued
};
