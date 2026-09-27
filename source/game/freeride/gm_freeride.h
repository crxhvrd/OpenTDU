#pragma once

#include "game/gm_world.h"

#include "game/bookmark/mng_bookmark.h"
#include "flash_list_spots.h"
#include "mng_fr_meeting_ic.h"
#include "mng_photo_mode.h"
#include "mng_map_screenshot.h"
#include "game/mng_logo_display.h"
#include "game/mng_ingame_menu.h"

#include "game/scenaric/gs_scenaric.h"
#include "online/mng_chat.h"
#include "render/mng_cam_gps.h"
#include "render/mng_font_players.h"

struct FlashMessage;
class Entity;
class EntityCinematic;
class EntityExtraChallenge;
class CameraGame;
class CarState;

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
    bool executeAction( int32_t param_2,
                        void *param_3,
                        void *pData = nullptr ) override;

    void onPauseMenuRequested( bool bOpened ) override;
    void onMenuPauseExit( bool bRestoreState ) override;

#ifdef OTDU_USE_IMGUI
    void drawDebug( float deltaTime ) override;
#endif

private:
    static constexpr int32_t kMaxNumExtraChallenges = 100;
    static constexpr float kEnterSpotClipDelay = 5.0f; // DAT_00f41750

    union ExtraChallengeProp {
        Entity *pGoodies;
        CarState *pConvoyCar;
    };

private:
    void gatherExtraChallenges();
    void updateNearestExtraChallenge();
    bool selectExtraChallenge( EntityExtraChallenge *pChallenge );
    Entity *spawnExtraChallengeMarker( const int32_t subType );
    void releaseExtraChallengeProp();
    void resolveNextIntroCine( const struct SpotSector *pSector );

    void populateEnterSpotPrompt( class FlashPlayer *pPlayer );
    void refreshChallengeLockState( class FlashPlayer *pPlayer );
    void markCurrentSpotDiscovered();

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

    NextIntroInfo nextIntro;
    EntityCinematic *pIntroCine;
    CameraGame *pSavedVehicleCamera;
    bool bIntroLevelLaunched;
    bool bIntroCineDone;

    Entity *pExtraChallenges[kMaxNumExtraChallenges];
    int32_t numExtraChallenges;
    int32_t extraChallengeKind;
    ExtraChallengeProp extraChallengeProp;
    EntityExtraChallenge *pNearestExtraChallenge;
    Entity *pExtraChallengeMarker;
};
