#pragma once

#include "game/gm_world.h"

#include "game/bookmark/mng_bookmark.h"
#include "mng_fr_meeting_ic.h"
#include "mng_photo_mode.h"
#include "mng_map_screenshot.h"
#include "game/mng_logo_display.h"
#include "game/mng_ingame_menu.h"

#include "online/mng_chat.h"
#include "render/mng_cam_gps.h"
#include "render/mng_font_players.h"

struct FlashMessage;

class GMFreeride : public GameModeWorld {
public:
    GMFreeride();
    ~GMFreeride();

    bool onMessage( FlashMessage *pMessage ) override;
    void onPauseMenuRequested( bool bOpened ) override;
    void onMenuPauseExit( bool bRestoreState ) override;

#ifdef OTDU_USE_IMGUI
    void drawDebug( float deltaTime ) override;
#endif

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
};
