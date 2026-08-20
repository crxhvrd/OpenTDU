#pragma once

#include "game/gm_world.h"

#include "game/bookmark/mng_bookmark.h"
#include "flash_list_spots.h"
#include "mng_fr_meeting_ic.h"
#include "mng_photo_mode.h"
#include "mng_map_screenshot.h"
#include "game/mng_logo_display.h"
#include "game/mng_ingame_menu.h"

#include "online/mng_chat.h"
#include "render/mng_cam_gps.h"
#include "render/mng_font_players.h"

struct FlashMessage;
class Entity;

class GMFreeride : public GameModeWorld {
public:
    GMFreeride();
    ~GMFreeride();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void draw( float deltaTime, float totalTime ) override;
    void pause() override;
    void destroy() override;

    void onResourceStreamed() override;
    bool onMessage( FlashMessage *pMessage ) override;
    bool executeAction( int32_t param_2, void *param_3 ) override;

    void onPauseMenuRequested( bool bOpened ) override;
    void onMenuPauseExit( bool bRestoreState ) override;

#ifdef OTDU_USE_IMGUI
    void drawDebug( float deltaTime ) override;
#endif

private:
    static constexpr int32_t kMaxNumExtraChallenges = 100;

private:
    void gatherExtraChallenges();

private:
    MngBookmark mngBookmark;
    MngFRMeetingIC mngFRMeetingIC;
    MngPhotoMode mngPhotoMode;
    MngCamGPS mngCamGPS;
    MngFontPlayers mngFontPlayers;
    MngLogoDisplay mngLogoDisplay;
    MngMapScreenshot mngMapScreenshot;
    MngInGameMenu mngInGameMenu;
    MngChat mngChat;

    FlashListSpots flashListSpots;
    bool bResumedFromPauseMenu;
    bool bEnterSpotClipVisible;
    float enterSpotClipTimer;
    ScenaricBank spotBank;
    Entity *pExtraChallenges[kMaxNumExtraChallenges];
    int32_t numExtraChallenges;
};
