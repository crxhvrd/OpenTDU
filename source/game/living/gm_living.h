#pragma once

#include "game/gm_world.h"

#include "flash/entities/flash_panel.h"
#include "game/first_person_mode.h"
#include "game/mng_logo_display.h"
#include "render/mng_reflection.h"

#include "mng_collection.h"
#include "mng_rankings.h"
#include "mng_spot_picture.h"
#include "scene_living.h"
#include "trading_menu.h"

struct FlashMessage;
class FlashPlayer;

extern bool gbEnteringLivingHub;

class GMLiving : public GameModeWorldBase {
public:
    inline FlashPlayer *getFlashPlayer() const { return pFlashPlayer; }
    inline SceneLiving &getSceneLiving() { return sceneLiving; }
    inline void setSceneResumeAllowed( const bool bAllowed ) {
        bSceneResumeAllowed = bAllowed;
    }
    inline void setRadialMenuOpen( const bool bOpen ) {
        bRadialMenuOpen = bOpen;
    }

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
    bool executeAction( int32_t param_2,
                        void *param_3,
                        void *pData = nullptr ) override;
    void restartLoopCinematic();
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
    TradingMenu tradingMenu;
    MngReflection mngReflection;
    MngLogoDisplay mngLogoDisplay;
    MngCollection mngCollection;
    MngRankings mngRankings;
    MngSpotPicture mngSpotPicture;
    SceneLiving sceneLiving;
    FlashPanelNews newsWidget;
    FirstPersonMode firstPerson;
    FlashPlayer *pFlashPlayer;
    float resetTime;
    bool bSceneResumeAllowed;
    bool bHouseSpotInstalled;
    bool bRadialMenuOpen;
    int32_t pendingHouseSpotIndex; // -1 = nothing queued
};
