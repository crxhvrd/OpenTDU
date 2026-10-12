#pragma once

#include "game/gm_scene.h"

#include "ai/ai_road_query.h"

#include "world/mng_sector_name.h"
#include "world/mng_world.h"
#include "world/mng_impostor.h"
#include "world/mng_ocean.h"
#include "world/mng_road_network.h"
#include "game/mng_logo_display.h"
#include "render/mng_cam_gps.h"
#include "render/mng_font_players.h"
#include "mng_map_viewer.h"
#include "mng_trac_road.h"
#include "game/scenaric/mng_spots.h"

class CameraGame;
class CameraGPS;
class FlashPlayer;
struct Viewport;

bool ProjectToMapScreen( const Eigen::Vector4f &position,
                         CameraGame *pCamera,
                         float &outX,
                         float &outY );
bool ProjectToMapMovie( CameraGame *pCamera,
                        const Eigen::Vector4f &position,
                        float &outX,
                        float &outY );
void ScreenToWorldGround( const Eigen::Vector2f &screen,
                          Eigen::Vector4f &outPosition );
void SetMapClipPosition( const char *pClipPath,
                         const float *pPosition,
                         const float *pAlpha,
                         const int32_t *pScale,
                         const Viewport *pViewport = nullptr );

class GMMap : public GameModeScene {
public:
    inline int32_t getExitState() const { return exitState; }
    inline MngSpots &getMngSpots() { return mngSpots; }
    inline float getCursorX() const { return cursorX; }
    inline float getCursorY() const { return cursorY; }

public:
    GMMap();
    ~GMMap();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void draw( float deltaTime, float totalTime ) override;
    void pause() override;
    void destroy() override;

    void onResourceStreamed() override;
    bool onMessage( FlashMessage *pMessage ) override;
    void drawDebug( float deltaTime ) override;

    void enterFreeMode();
    bool leaveFreeModeIfActive();
    void updateSpotFilterIcons();
    int32_t computeCursorWorldFrame( Matrix4x4f_t &outFrame ) const;
    void drawMiniMapHighlight( float depth, DrawList *pDrawList );

private:
    void setActiveFilterMode( uint8_t mode );
    void leaveFreeMode( MapSpotRecord *pSelectedRecord );
    bool isSpaceViewActive() const;
    void placeLocalPlayerCar( const Eigen::Vector4f &position,
                              Matrix4x4f_t *pOutTransform,
                              const eRoadSnapMode mode,
                              const bool bShoulder );
    void goToFreeRide();
    void moveFocusTo( const Eigen::Vector4f &target );
    void goToSelectedSpot();
    void goToSpot();
    void startSpotExit( const SpotMap *pSpot );
    void goToTeleportTarget();
    void confirmTeleport();
    void setTarget();
    void forceTarget();
    void zoomIn();
    void zoomOut();
    void updateExit();
    void updateCursor( float deltaTime );
    void drawDemoDebug();
    void updatePlayerMarker();
    void updateTargetMarker();

private:
    MngFontPlayers mngFontPlayers;
    MngSpots mngSpots;
    MngWorld mngWorld;
    MngRoadNetwork mngRoadNetwork;
    MngImpostor mngImpostor;
    MngImpostorSimple mngImpostorSimple;
    MngOcean mngOcean;
    MngLogoDisplay mngLogoDisplay;
    MngTracRoadMap mngTracRoadMap;
    MngCamGPS mngCamGPS;
    MngSectorName mngSectorName;
    MngMapViewer mngMapViewer;
    DrawList demoZonesDrawList;

    bool bGhostCursorVisible;
    float cursorX;
    float cursorY;
    float field_0xfe28;
    float field_0xfe2c;
    float zoomCursorX; // (cursor when zooming in, -1 = none)
    float zoomCursorY;
    Eigen::Vector4f zoomOutCursorPosition;
    Eigen::Vector4f playerCarPosition;
    CameraGPS *pCameraGPS;
    FlashPlayer *pMapPlayer;
    int32_t field_0xfe68;
    int32_t exitState; // (2/3/5/6 = leaving the map)
    bool field_0xde500;
    bool field_0xde501;
    const SpotMap *pTargetSpot;
};

extern GMMap *gpGMMap;
