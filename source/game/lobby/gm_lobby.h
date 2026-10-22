#pragma once

#include "game/gm_world.h"

#include "core/popup_callback.h"
#include "game/map/map_screenshot_bank.h"
#include "game/map/mng_map_viewer.h"
#include "game/map/mng_trac_road.h"
#include "game/mng_logo_display.h"
#include "game/scenaric/mng_spots.h"
#include "online/mng_chat.h"
#include "render/mng_cam_gps.h"

#include "lobby_cameras.h"
#include "mng_lobby.h"

class EntityCheckPoint;
class FlashPlayer;
struct Material;

// Challenge spot lobby
class GMLobby : public GameModeWorldBase, public PopupCallback {
public:
    enum eLobbyState : int32_t {
        LS_None = -1,
        LS_TrackPreview = 0,
        LS_PlayersView = 1,
        LS_SpectatorView = 2,
    };

    enum eStateProcess : int32_t {
        SP_Init = 0xc0c0,
        SP_Update = 0xc0c1,
        SP_Term = 0xc0c2,
    };

    // Event game types
    enum eEventGameType : int32_t {
        EGT_TimeAttack = 0,
        EGT_Speed = 1,
        EGT_Driving = 2,
        EGT_Race = 3,
    };

    // Event spot types
    enum eEventSpotType : int32_t {
        EST_Solo = 0xc0c0,
        EST_Multi = 0xc0c1,
        EST_Club = 0xc0c2,
    };

public:
    inline bool isSpectatorMode() const { return bSpectatorMode; }
    inline void setSpectatorMode( const bool value ) { bSpectatorMode = value; }
    inline bool isReturningToSession() const { return bReturningToSession; }
    inline void setReturningToSession( const bool value ) {
        bReturningToSession = value;
    }
    inline MngFade &getFade() { return mngFade; }
    inline SpotMap *getSpot() const { return pSpot; }
    inline MngTracRoadMap &getTracRoadMap() { return mngTracRoadMap; }
    inline void restorePlayersViewTarget() {
        playersViewCamera.restoreTarget();
    }
    inline void releasePreviewCamera() {
        if ( pPreviewCamera != nullptr ) {
            pPreviewCamera->term();
            pPreviewCamera = nullptr;
        }
    }

public:
    GMLobby();
    ~GMLobby();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void draw( float deltaTime, float totalTime ) override;
    void pause() override;
    void destroy() override;
    bool onMessage( FlashMessage *pMessage ) override;

    void onClose( ePopupAnswer answer, void *pData, int32_t userData ) override;

    void showPlayersView();
    void showTrackPreview();
    void snapStreamingToPlayerSpawn();
    void quitLobby();
    void onMapSpotChanged();

    EntityCheckPoint *findCheckpoint( const int32_t index );
    inline int32_t getMaxCheckpointIndex() const { return maxCheckpointIndex; }
    inline MngLobby &getMngLobby() { return mngLobby; }
    void updateLevelAndCarLocks();

private:
    static constexpr int32_t kNumGridSlots = 8;

private:
    bool stateChange( const int32_t state );
    bool stateProcess( const int32_t state,
                       const float deltaTime,
                       const eStateProcess process );

    static void OnPlayersViewFadeOut( void *pData, void *pSource );
    static void OnTrackPreviewFadeOut( void *pData, void *pSource );
    static bool OnLurePictureLoaded( MapScreenshotBank *pBank,
                                     void *pUserData );
    static void LurePictureDrawCallback( FlashRECT *pViewport,
                                         float depth,
                                         DrawList *pDrawList,
                                         void *pUserData,
                                         FlashResource *pResource,
                                         const char *pClipName );

    void quitToSpotOrFreeRide();
    void resetCheckpoints();
    bool loadPlayerCar();
    void placeLocalPlayerCar();
    bool refreshChallengeInfos();
    void updateTrackPreview( SpotMap *pTargetSpot );
    bool updateNbGames();
    void updateRecordsAndRewards();
    void updateLurePicture();
    void onLurePictureLoaded();
    void releaseLurePictures();

private:
    int32_t curState;
    int32_t prevState;
    int32_t nextState;
    float stateInitTime;

    SpotMap *pSpot;
    bool bSpectatorMode;
    LobbyCamera *pPreviewCamera;
    LobbyCameraRoutePreview routePreviewCamera;
    LobbyCameraStartOrbit startOrbitCamera;
    LobbyCameraCheckpointFlyby checkpointFlybyCamera;

    FlashPlayer *pFlashPlayer;
    bool bReturningToSession;

    MngLobby mngLobby;
    MngLogoDisplay mngLogoDisplay;
    MngTracRoadMap mngTracRoadMap;
    MngCamGPS mngCamGPS;
    MngChat mngChat;
    MngSpots mngSpots;
    MngMapViewer mngMapViewer;
    MapScreenshotBank lurePictureBank;

    ScenaricBank *pCheckpointBank;
    int32_t gameType;
    int32_t spotType;
    int32_t maxCheckpointIndex;
    float routeDistanceKm;

    Material *pLurePicture;
    SpotMap *pPendingLureSpot;

    LobbyCameraPlayersView playersViewCamera;

    SpotMap *pPendingTeamSpot;
    bool bExitPending;
    bool bShowNoSessionPopup;
    SpotScreensBank fallbackLureBank;
    bool bPlaceCarRequest;
    bool bPlayersViewStreamed;
    uint32_t placeCarRequestTime;
    uint32_t nbGamesRefreshTime;
    uint32_t spotChangeTimeout;
    bool bSpotChangeAllowed;
    bool bQuickmatchJoin;
};
