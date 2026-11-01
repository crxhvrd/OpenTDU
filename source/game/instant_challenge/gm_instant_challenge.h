#pragma once

#include "game/gm_world.h"

#include "ai/ai_road_query.h"
#include "game/bookmark/mng_bookmark.h"
#include "game/map/mng_map_viewer.h"
#include "game/scenaric/event_parameters.h"
#include "game/scenaric/scenaric_bank.h"
#include "render/mng_cam_gps.h"

#include "mng_gme.h"

class CameraGPS;
class EntityCheckPoint;
struct FlashClip;
class FlashPlayer;

// Challenge editor (GME). Shared by GM_GameModeEdit (challenge creation) and
// GM_InstantChallenge
bool GetScreenCoords( Eigen::Vector4f &outRelative,
                      const Eigen::Vector4f &position );

class GMInstantChallenge : public GameModeWorld {
    friend class GameMechanism;
    friend class CameraGME;

public:
    static constexpr int32_t kMaxNumWaypoints = MngTracRoad::kMaxNumLegs;

public:
    inline AiRoadLocation &getWaypoint( const int32_t index ) {
        return waypoints[index];
    }
    inline int32_t getNumWaypoints() const { return numWaypoints; }
    inline bool isEditorInputEnabled() const { return bEditorInputEnabled; }
    inline int32_t getEditorState() const { return editorState; }
    inline bool isEdgeScrollEnabled() const { return bEdgeScrollEnabled; }
    inline void setCursorAtEdge( const bool bAtEdge ) {
        bCursorAtEdge = bAtEdge;
    }
    inline FlashPlayer *getEditorPlayer() const { return pEditorPlayer; }
    inline int32_t getParameterCategory() const { return parameterCategory; }
    inline void setParameterCategory( const int32_t category ) {
        parameterCategory = category;
    }
    bool isLocalPlayerRouteOwner() const;
    static const char *GetGameModeName( const int32_t index );
    static const char *GetGameModeGSName( const int32_t index );
    static void SetSelectedFileName( const char *pFileName );
    int32_t countParameters( const int32_t category );
    GameParameter *findParameter( const int32_t index );

public:
    GMInstantChallenge();
    ~GMInstantChallenge();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void draw( float deltaTime, float totalTime ) override;
    void pause() override;
    void destroy() override;

    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) override;
    bool restart() override;
    EventParameters *getMissionDescription() override;
    void drawDebug( float deltaTime ) override;
    void onResourceStreamed() override;

    void goToBookmark( const Eigen::Vector3f &position );
    void refreshEditorState();

public:
    static constexpr int32_t kEditorStateEditing = 2;

    // hoverFlags
    static constexpr uint32_t kHoverOnRoad = 1;
    static constexpr uint32_t kHoverCheckpoint = 2;
    static constexpr uint32_t kHoverNearCheckpoint = 4;
    static constexpr uint32_t kHoverOnRoute = 8;

    // editTool
    static constexpr int32_t kToolNone = 0;
    static constexpr int32_t kToolPlaceCheckpoint = 1;
    static constexpr int32_t kToolMoveCheckpoint = 2;

private:
private:
    void setFireFrame( FlashPlayer *pPlayer );
    void linkTextInput( const char *pText );
    void requestSave();
    void updateSave();
    void clearEditorKeys();
    bool canPlaceAtCursor( const bool bCheckRouteLength );
    bool isCheckpointLimitReached();
    void updateCameraControl( const float deltaTime );
    void updateCursorHover();
    void updateCursorClips();
    void updateCursorFrames( const bool bCursor,
                             const bool bPoint,
                             const bool bGhost,
                             FlashClip *pMoveClip,
                             const char *&pDeroulFrame );
    bool updateGhostCursorScale();
    void updatePendingExit();
    void findLastCheckpoint();
    void updateChallengeValidity();
    void updateEditorFlashVars();
    void updateRouteLength();
    void formatPointName( char *pOutName, const EntityCheckPoint *pCheckpoint );
    void removeWaypoint( const int32_t index );
    void clearToStartCheckpoint();
    void onEditorMouseIn();
    void onEditorMouseOut();
    void onMovePoint();
    void onZoomIn();
    void onZoomOut();
    EntityCheckPoint *findCheckpointByIndex( const int32_t index ) const;
    EntityCheckPoint *findStartCheckpoint() const;
    EntityCheckPoint *findFinishCheckpoint() const;
    Entity *findStartGridCar() const;
    void markEditorModified();
    void setWaypoint( const int32_t index,
                      const AiRoadLocation &location,
                      const bool bSolveRoute );
    void insertWaypoint( const int32_t index, const AiRoadLocation &location );
    void applyDefaultChallengeName( const EntityCheckPoint *pCheckpoint );
    EntityCheckPoint *instantiateCheckpoint( Entity *pTemplate,
                                             const Matrix4x4f_t *pFrame );
    EntityCheckPoint *placeCheckpoint( Entity *pTemplate,
                                       const Matrix4x4f_t *pFrame );
    void commitMove( EntityCheckPoint *pCheckpoint );
    void orientCheckpoint( EntityCheckPoint *pCheckpoint );
    void orientStartTowards( EntityCheckPoint *pCheckpoint );
    void flipCheckpoint( EntityCheckPoint *pCheckpoint );
    void realignStartCheckpoint( EntityCheckPoint *pStart,
                                 const Eigen::Vector4f &direction );
    void placeStartGridCar( Entity *pCar, EntityCheckPoint *pStart );
    void onSetPoint();
    void onInstantChallengeSetPoint();
    void onInsertPoint();
    void onEraseChallenge();
    void onCancelToStart();
    void onToggleLoop();
    void onFrameRoute();
    void onCursorZoneLayout();
    void onMapLayout();
    void convertCheckpoint( EntityCheckPoint *pCheckpoint, const int32_t type );
    void finalizeRoute();
    void teleportLocalPlayerCar( const Matrix4x4f_t &frame );
    void quitEditor( const eGameMode nextMode, const Matrix4x4f_t &frame );
    void onDeletePoint();
    void updateMapIcons();
    void setupEditorFlash();
    void placeMapIcon( const Eigen::Vector4f &position, const char *pClipName );
    void placeStartArrow( const EntityCheckPoint *pStart );

public:
    void drawInterface();

private:
    void leaveICESession();
    void launchInstantChallenge();
    void FUN_00788c80( const float deltaTime );
    void initInstantChallengeRoute();
    bool deleteSelectedChallenge();
    bool onSave();
    static void OnSavePopupShown( void *pUserData );
    void rebuildWaypointsFromActors();
    void startTestDrive();
    void setupInstantChallengeFlash();
    void updateInstantChallengeRoute();
    bool isCurrentCarAllowed();
    void restoreTestedChallenge();
    void loadSelectedChallenge();
    void onNewChallenge();

private:
    EventParameters missionDescription;         // +0x103238
    MngCursor mngCursor;                        // +0x1051e0
    MngPreview mngPreview;                      // +0x105270
    MngGMEFlshI mngGMEFlshI;                    // +0x105630
    MngTracRoadGME mngTracRoad;                 // +0x105660
    MngBookmark mngBookmark;                    // +0x10eab0
    MngCamGPS mngCamGPS;                        // +0x110460
    MngMapViewer mngMapViewer;                  // +0x111480
    ScenaricBank field_0x1115b0;                // +0x1115b0
    ScenaricBank checkpointBank;                // +0x111ea0 (pack_cp_virtual)
    CameraGPS *pGPSCamera;                      // +0x112790
    EntityCheckPoint *pCheckpointTemplate;      // +0x112794
    EntityCheckPoint *pLastCheckpoint;          // +0x112798
    AiRoadLocation waypoints[kMaxNumWaypoints]; // +0x1127a0
    int32_t numWaypoints;                       // +0x114d80
    int32_t editorState;                        // +0x114d84
    bool bEditorInputEnabled;                   // +0x114d88
    bool bGhostCursorVisible;                   // +0x114d89
    bool bToolActive;                           // +0x114d8a
    bool bCursorVisible;                        // +0x114d8b
    bool bTestLevelLoaded;                      // +0x114d8c
    bool bZoomPending;                          // +0x114d8d
    int32_t field_0x114d90;                     // +0x114d90
    int32_t field_0x114d94;                     // +0x114d94
    bool bSaveRequested;                        // +0x114d98
    bool bSaving;                               // +0x114d99
    uint32_t hoverFlags;                        // +0x114d9c
    EntityCheckPoint *pHoveredCheckpoint;       // +0x114da0
    EntityCheckPoint *pSelectedCheckpoint;      // +0x114da4
    char selectedPointName[0x88];               // +0x114da8
    AiRoadLocation cursorLocation;              // +0x114e30
    AiRoadProfile *pCursorProfile;              // +0x114e90
    int32_t hoveredLeg;                         // +0x114e94
    bool bChallengeValid;                       // +0x114e98
    float cameraPanInputs[4];                   // +0x114e9c (L, R, D, U)
    float cameraPitchInputs[2];                 // +0x114eac (down, up)
    float cameraYawInputs[2];                   // +0x114eb4 (left, right)
    bool field_0x114ebc[4];                     // +0x114ebc
    bool bKeyZoomIn;                            // +0x114ec0
    bool bKeyZoomOut;                           // +0x114ec1
    bool bKeyPitchDown;                         // +0x114ec2
    bool bKeyPitchUp;                           // +0x114ec3
    bool bKeyRotateLeft;                        // +0x114ec4
    bool bKeyRotateRight;                       // +0x114ec5
    bool bEdgeScrollEnabled;                    // +0x114ec6
    bool bCursorAtEdge;                         // +0x114ec7
    char deroulFrame[0x100];                    // +0x114ec8
    FlashListGMEParameters parameterList;       // +0x114fc8
    FlashListGMEChallenges challengeList;       // +0x11508c
    FlashPlayer *pEditorPlayer;                 // +0x11512c
    int32_t editTool;                           // +0x115130
    int32_t moveMode;                           // +0x115134
    float routeLength;                          // +0x115138
    int32_t parameterCategory;                  // +0x11513c
    bool bFinishPlaced;                         // +0x115140
    bool bFinishRequested;                      // +0x115141
    char gmDescription[0xbe];                   // +0x115142
    Matrix4x4f_t exitFrame;                     // +0x1151d0
    eGameMode nextGameMode;                     // +0x115210
    ICRoute *pICRoute;                          // +0x115214
    bool bICRouteReady;                         // +0x115218
    bool bOpponentIsBot;                        // +0x115219
    int32_t field_0x11521c;                     // +0x11521c
    float icSyncTimer;                          // +0x115220
    int32_t field_0x115224;                     // +0x115224
};
