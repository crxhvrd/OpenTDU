#pragma once

#include "game/gm_world.h"

#include "game/bookmark/mng_bookmark.h"
#include "flash_list_spots.h"
#include "mng_fr_meeting_ic.h"
#include "mng_photo_mode.h"
#include "mng_map_screenshot.h"
#include "game/mng_logo_display.h"
#include "game/mng_ingame_menu.h"
#include "game/mng_pedestrians.h"
#include "game/pso_collector/mng_pso_collector.h"

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
    bool isSpotInteriorVisible() override {
        // FUN_0042df00
        return true;
    }
    bool onMessage( FlashMessage *pMessage ) override;
    bool executeAction( int32_t param_2,
                        void *param_3,
                        void *pData = nullptr ) override;

    void onPauseMenuRequested( bool bOpened ) override;
    void onMenuPauseExit( bool bRestoreState ) override;

    bool enterSpot( struct SpotMap *pSpot );
    bool enterExtraChallenge( EntityExtraChallenge *pChallenge );
    inline bool isNextIntroActive() const { return nextIntro.bIsActive; }
    inline EntityExtraChallenge *getNearestExtraChallenge() const {
        return pNearestExtraChallenge;
    }

    static void OnGPSFlyUpEnd( void *pUnused, void *pTargetGameMode );

#ifdef OTDU_USE_IMGUI
    void drawDebug( float deltaTime ) override;
#endif

private:
    void updateGPSFlyUp();
    void updateMapScreenshotProps();
    void snapLocalCarBackOnRoad();

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
    void releaseExtraChallenges();
    bool spawnConvoyCar( EntityExtraChallenge *pChallenge );
    void resolveExtraChallengeAnswer();
    bool onExtraChallengeCineMessage( EntityCinematic *pCinematic,
                                      const char *pMessage );
    static void OnExtraChallengeIntroFadeOut( void *pData, void *pSource );
    static void OnExtraChallengeCancelFadeOut( void *pData, void *pSource );
    void resolveNextIntroCine( const struct SpotSector *pSector );

    bool requestEnterActivity();
    bool enterCurrentHouseSpot();
    bool goToSelectedSpot();
    bool launchCurrentSpotInfo();
    void enterCurrentActivity();
    void acquireSpotIntroCine( struct SpotMap *pSpot );
    void startMapFlyDown();
    static void OnSpotIntroFadeOut( void *pData, void *pSource );

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
    MngPedestrians mngPedestrians;
#if OTDU_DEVBUILD
    MngPSOCollector mngPSOCollector;
#endif

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

    Entity *pExtraChallengeAvatar;
    int32_t extraChallengeKind;
    ExtraChallengeProp extraChallengeProp;
    bool bCanAcceptExtraChallenge;
    const char *pExtraChallengeRefusal;
    const char *pExtraChallengeRefusal2;
    char vehicleRestrictionText[0x100];
    Entity *pExtraChallenges[kMaxNumExtraChallenges];
    int32_t numExtraChallenges;
    EntityExtraChallenge *pNearestExtraChallenge;
    Entity *pExtraChallengeMarker;
    EntityCinematic *pExtraChallengeIntroCine;
    EntityCinematic *pExtraChallengeLoopCine;
    bool bExtraChallengeLaunched;
    struct SpotMap *pPendingSpot;
    uint32_t activityReentryTick;
};
