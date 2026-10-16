#include "shared.h"
#include "gm_map.h"

#include "ai/ai_pool.h"
#include "ai/gs_ai.h"

#include "tdu_instance.h"
#include "imgui.h"
#include "core/maths_helper.h"
#include "flash/mng_fade.h"
#include "flash/gs_flash.h"
#include "flash/flash_callbacks_register.h"
#include "flash/implementation/ed_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "flash/modal_message_box.h"
#include "flash/movie_player.h"
#include "config/gs_config.h"
#include "game/mng_hud.h"
#include "game/vehicle/gs_brand_car.h"
#include "world/gs_aircraft.h"
#include "game/mng_music.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/local_player.h"
#include "game/scenaric/spots.h"
#include "game/vehicle/car_state.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_data/achievement_checks.h"
#include "player_data/achievements_data.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_road_discovery.h"
#include "render/camera/camera_gps.h"
#include "render/camera/gs_camera.h"
#include "render/gs_render.h"
#include "render/render_scene.h"
#include "input/gs_device.h"
#include "input/input_device.h"
#include "world/gs_boat.h"
#include "world/gs_world.h"
#include "world/streaming_manager.h"

GMMap *gpGMMap = nullptr;         // DAT_010e7d1c
bool gbMapResetGPSCamera = false; // BOOL_010e7d19

static bool gbMapDebugTeleportAnywhere = false;
static constexpr float kMapDebugNoGround = -1.0e38f;
static constexpr float kMapDebugTeleportLift = 1.0f;

static bool gbUnlockDemoRoadsAgain = false; // DAT_010e7d21
static float gDemoMinOffsetX = 1000.0f;     // DAT_00f96b70
static float gDemoMinOffsetZ = 1000.0f;     // DAT_00f96b74
static float gDemoMaxOffsetX = 1000.0f;     // DAT_00f96b78
static float gDemoMaxOffsetZ = 1000.0f;     // DAT_00f96b7c

// DAT_00fa9be8 (x, z, width, depth)
static constexpr float kDemoFordZones[2][4] = {
    {32350.0f, 39800.0f, 1650.0f, 1700.0f},
    {34800.0f, 45300.0f, 1200.0f, 225.0f },
};

static constexpr float kDemoZoneOuterHeight = 300.0f; // DAT_00f4183c
static constexpr float kDemoZoneMinRow = 7.0f;        // DAT_00f41844
static constexpr float kDemoOffsetMax = 60000.0f;     // DAT_00f41b98
static constexpr float kDemoOffsetStep = 100.0f;      // DAT_00f41708

static constexpr int32_t kGPSCameraMode = 3;
static constexpr float kMapFocusMoveMinDistance = 10.0f; // FLOAT_00f4166c
static constexpr float kMapFocusMoveDuration = 2.5f;     // DAT_00fab950
static constexpr float kMapExitHopHeight = 5000.0f;      // DAT_00fab944
static constexpr uint8_t kMapFilterFreeMode = 0x10;
static constexpr float kMapFocusDefault = 0.5f;               // FLOAT_00f41674
static constexpr float kMapSectorLODScale = 0.2f;             // DAT_00f41730
static constexpr float kMapCursorPadSpeed = 0.5f;             // DAT_00fab948
static constexpr float kMapViewHeightScale = 2.0f;            // DAT_00f416b0
static constexpr float kMapRayMiss = -99999.99f;              // DAT_00f418fc
static constexpr float kMapNoGroundHeight = 100000.0f;        // DAT_00f418e0
static constexpr float kMapNumSectorColumns = 11.0f;          // DAT_00f41b94
static constexpr float kMapNumSectorRows = 9.0f;              // DAT_00f4192c
static constexpr float kMapWidescreenYOffset = 0.33334f;      // DAT_00f41db8
static constexpr float kMapWidescreenYScale = 0.375f;         // DAT_00f41db4
static constexpr float kMapClipWidescreenYOffset = 0.16667f;  // DAT_00f41950
static constexpr float kMapClipWidescreenYScale = 0.7500188f; // DAT_00f41fa8
static constexpr float kMapClipAlphaScale = 100.0f;           // DAT_00f41708
static constexpr float kMapMarkerMinCoordinate = 0.01f;       // DAT_00f416d4

static constexpr FlashMessage kMsgFilter1 = FlashMessage( "map1" );
static constexpr FlashMessage kMsgFilter2 = FlashMessage( "map2" );
static constexpr FlashMessage kMsgFilter3 = FlashMessage( "map3" );
static constexpr FlashMessage kMsgFilter4 = FlashMessage( "map4" );
static constexpr FlashMessage kMsgGoFreeride = FlashMessage( "gofr" );
static constexpr FlashMessage kMsgFilterIn = FlashMessage( "fiin" );
static constexpr FlashMessage kMsgFilterOut = FlashMessage( "fiou" );
static constexpr FlashMessage kMsgChallengeFilter = FlashMessage( "chfi" );
static constexpr FlashMessage kMsgDcof = FlashMessage( "dcof" );
static constexpr FlashMessage kMsgDebm = FlashMessage( "debm" );
static constexpr FlashMessage kMsgFity = FlashMessage( "fity" );
static constexpr FlashMessage kMsgFreeMode = FlashMessage( "frmo" );
static constexpr FlashMessage kMsgGoIn = FlashMessage( "goin" );
static constexpr FlashMessage kMsgGoSpot = FlashMessage( "gosp" );
static constexpr FlashMessage kMsgGoTeleport = FlashMessage( "gotp" );
static constexpr FlashMessage kMsgMapOk = FlashMessage( "maok" );
static constexpr FlashMessage kMsgMnok = FlashMessage( "mnok" );
static constexpr FlashMessage kMsgStatsOff = FlashMessage( "stof" );
static constexpr FlashMessage kMsgStatsOn = FlashMessage( "ston" );
static constexpr FlashMessage kMsgNextFilter = FlashMessage( "nefi" );
static constexpr FlashMessage kMsgPrevFilter = FlashMessage( "prfi" );
static constexpr FlashMessage kMsgPlayerSession = FlashMessage( "plse" );
static constexpr FlashMessage kMsgSpotMode = FlashMessage( "spmo" );
static constexpr FlashMessage kMsgTeleportOk = FlashMessage( "tpok" );
static constexpr FlashMessage kMsgTarget = FlashMessage( "targ" );
static constexpr FlashMessage kMsgTargetOff = FlashMessage( "tgfo" );
static constexpr FlashMessage kMsgZoomIn = FlashMessage( "zoin" );
static constexpr FlashMessage kMsgZoomOut = FlashMessage( "zoou" );

static PhysicsVehicle *GetLocalPlayerPhysicsVehicle() {
    CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
    return ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;
}

static void SetLocalPlayerCarPaused( PhysicsVehicle *pPhysicsVehicle,
                                     const bool bPaused ) {
    PhysicsLock lock;
    pPhysicsVehicle->setPhysicsPaused( bPaused );
}

static void SetSpotStreamEnabled( const bool bEnabled ) {
    SpotStream &spotStream = gpWorld->getStreamingManager()->spotStream;
    spotStream.bStream = bEnabled;
    spotStream.bDraw = bEnabled;
}

static void MapHighlightCallback( FlashRECT *,
                                  float depth,
                                  DrawList *pDrawList,
                                  void *,
                                  FlashResource *,
                                  const char * ) {
    // FUN_009bffc0
    if ( gpGMMap != nullptr ) {
        gpGMMap->drawMiniMapHighlight( depth, pDrawList );
    }
}

static constexpr int32_t kNumPlayerDensityCellsX = 41;
static constexpr int32_t kNumPlayerDensityCellsY = 33;
static constexpr int32_t kNumMiniMapCaseCellsX = 11;
static constexpr int32_t kNumMiniMapCaseCellsY = 9;

static void DrawPlayerDensityGrid( const uint8_t *pGrid,
                                   const float originX,
                                   const float originY,
                                   const float cellWidth,
                                   const float cellHeight,
                                   const float depth,
                                   DrawList *pDrawList ) {
    for ( int32_t x = 0; x < kNumPlayerDensityCellsX; x++ ) {
        for ( int32_t y = 0; y < kNumPlayerDensityCellsY; y++ ) {
            const uint8_t density = pGrid[x * kNumPlayerDensityCellsY + y];
            if ( density == 0 ) {
                continue;
            }

            const float top = float( y ) * cellHeight + originY;
            const float bottom = top + cellHeight;
            const float left = float( x ) * cellWidth + originX;
            const float right = left + cellWidth;
            pDrawList->beginPrimitive( PT_TriangleStrip, 4 );
            pDrawList->setDiffuse(
                0, ColorRGBA( uint32_t( uint8_t( ( density >> 1 ) + 0x80 ) )
                                  << 24 |
                              0xff00 ) );
            pDrawList->pushVertex( left, top, depth );
            pDrawList->pushVertex( left, bottom, depth );
            pDrawList->pushVertex( right, top, depth );
            pDrawList->pushVertex( right, bottom, depth );
            pDrawList->commitPrimitive();
        }
    }
}

static bool IsObjectiveSet( const ObjectiveMarker &objective ) {
    return objective.Type != nullptr && objective.Type[0] != '\0';
}

static void SetClipVisibleFlag( FlashClip &clip, const bool bVisible ) {
    ScopedMutexLock lock = gpFlash->scopedLock();
    gpPreviousActiveFlashResource = gpActiveFlashResource;
    uint32_t &flags = clip.pSprite->getDisplayList()->getFlagsRW();
    flags = bVisible ? ( flags | 1u ) : ( flags & ~1u );
}

static float ReadMapCursorAction( DeviceAction *pAction ) {
    if ( pAction == nullptr || pAction->NumAddedChannels == 0 ) {
        return 0.0f;
    }
    return pAction->FUN_00434bc0( 0.0f, 1.0f );
}

static void MapExitHopCallback( void *pUser, CameraGPS::Pose &pose ) {
    // FUN_009beb60
    if ( pUser == nullptr || !gpMapView->isMoving() ) {
        return;
    }

    float t = gpMapView->MoveTime / gpMapView->MoveDuration;
    if ( t < 0.0f ) {
        t = 0.0f;
    } else if ( t > 1.0f ) {
        t = 1.0f;
    }

    const float hop = sinf( t * kPI ) * kMapExitHopHeight;
    pose.Position.y() += hop;
    pose.FarPlane += hop;
}

bool ProjectToMapScreen( const Eigen::Vector4f &position,
                         CameraGame *pCamera,
                         float &outX,
                         float &outY ) {
    // FUN_009bea70
    if ( pCamera == nullptr ) {
        pCamera = gpCamera->getDefaultCamera();
    }

    const Frustum &frustum =
        gpRender->getRenderPass<RP_Near>().pScene->getFrustumWrite();
    Eigen::Vector4f projected =
        TDUMat4x4MulVec( pCamera->getWorldToCam(), position );
    projected = TDUMat4x4MulVec( frustum.getProjectionMatrix(), projected );
    projected /= projected.w();

    outX = ( projected.x() + 1.0f ) * 0.5f;
    outY = ( 1.0f - projected.y() ) * 0.5f;
    return 0.0f <= outX && 0.0f <= outY && outX <= 1.0f && outY <= 1.0f;
}

static FlashPlayer *GetMapHudFlashPlayer() {
    MoviePlayer *pMoviePlayer = gpFlash->getMoviePlayer( 1, 0 );
    if ( pMoviePlayer == nullptr ||
         pMoviePlayer->MovieBank.getState() !=
             BankFlash::eBankFlashInit::BFI_Loaded ) {
        return nullptr;
    }
    return pMoviePlayer->MovieBank.getFlashPlayer();
}

bool ProjectToMapMovie( CameraGame *pCamera,
                        const Eigen::Vector4f &position,
                        float &outX,
                        float &outY ) {
    // FUN_009bed50
    const FlashResourceHeader *pHeader =
        GetMapHudFlashPlayer()->getBoundFlashResource()->getHeader();
    const float width = static_cast<float>(
        static_cast<int32_t>( pHeader->FrameSize.XMax ) / 0x14 );
    const float height = static_cast<float>(
        static_cast<int32_t>( pHeader->FrameSize.YMax ) / 0x14 );

    if ( pCamera == nullptr ) {
        pCamera = gpCamera->getDefaultCamera();
    }

    const Frustum &frustum =
        gpRender->getRenderPass<RP_Near>().pScene->getFrustumWrite();
    Eigen::Vector4f projected =
        TDUMat4x4MulVec( pCamera->getWorldToCam(), position );
    projected = TDUMat4x4MulVec( frustum.getProjectionMatrix(), projected );
    projected /= projected.w();

    float normalizedY = ( 1.0f - projected.y() ) * 0.5f;
    if ( gpConfig->b169Ratio ) {
        normalizedY = ( ( 1.0f - projected.y() ) + kMapWidescreenYOffset ) *
                      kMapWidescreenYScale;
    }
    outX = ( projected.x() + 1.0f ) * 0.5f * width;
    outY = normalizedY * height;
    return 0.0f <= outX && 0.0f <= outY && outX <= width && outY <= height;
}

void ScreenToWorldGround( const Eigen::Vector2f &screen,
                          Eigen::Vector4f &outPosition ) {
    // FUN_00782260
    CameraGame *pCamera = gpCamera->getDefaultCamera();
    const Matrix4x4f_t &camToWorld = pCamera->getCamToWorld();
    const Eigen::Vector4f right = camToWorld.row( 0 ).transpose();
    const Eigen::Vector4f up = camToWorld.row( 1 ).transpose();
    const Eigen::Vector3f eye3 = pCamera->getWorldPosition();
    const Eigen::Vector3f target3 = pCamera->getTarget();
    const Eigen::Vector4f eye( eye3.x(), eye3.y(), eye3.z(), 1.0f );
    const Eigen::Vector4f target( target3.x(), target3.y(), target3.z(), 1.0f );

    const float distance = ( target - eye ).norm();
    const float viewHeight =
        pCamera->getTanHalfFov() * distance * kMapViewHeightScale;
    const float viewWidth = pCamera->getAspectRatio() * viewHeight;
    const Eigen::Vector4f pointOnScreen =
        target + right * viewWidth * ( screen.x() - 0.5f ) +
        up * viewHeight * ( screen.y() - 0.5f );
    Eigen::Vector4f direction = pointOnScreen - eye;
    direction /= direction.norm();

    const float rayLength = distance + distance;
    const float hitDistance =
        gpPhysics->castWorldRay( eye, direction, rayLength );
    if ( hitDistance != kMapRayMiss ) {
        // FUN_00842c50
        outPosition = eye + direction * hitDistance;
        return;
    }

    const float seaScale = fabsf( eye.y() / direction.y() );
    const Eigen::Vector4f seaOffset = direction * seaScale;
    Eigen::Vector4f seaPoint = eye + seaOffset;
    seaPoint.w() = 1.0f;

    const SectorManager &sectorManager =
        gpWorld->getStreamingManager()->sectorManager;
    const float maxX = static_cast<float>( static_cast<int32_t>(
                           sectorManager.numSectors * 0x300 ) ) *
                       kMapNumSectorColumns;
    const float maxZ = static_cast<float>( static_cast<int32_t>(
                           sectorManager.numSubSectors * 0x300 ) ) *
                       kMapNumSectorRows;
    seaPoint.x() =
        ( seaPoint.x() < 0.0f ) ? 0.0f : std::min( seaPoint.x(), maxX );
    seaPoint.z() =
        ( seaPoint.z() < 0.0f ) ? 0.0f : std::min( seaPoint.z(), maxZ );

    float groundHeight = 0.0f;
    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    Sector *pSector = pStreaming->sectorManager.getSectorAtPosition( seaPoint );
    if ( pSector != nullptr && pSector->state == 1 &&
         pSector->geometry.pHeightmapInstance != nullptr ) {
        // FUN_007021b0
        groundHeight = SampleGroundHeight( pSector->geometry.pHeightmapInstance,
                                           pSector->heightmap, seaPoint.x(),
                                           seaPoint.z() );
        if ( groundHeight == kMapNoGroundHeight ) {
            Level &level = pStreaming->getLevel();
            groundHeight = SampleGroundHeight(
                level.getHeightmapGeometry().pHeightmapInstance,
                level.heightmap, seaPoint.x(), seaPoint.z() );
        }
        if ( groundHeight == kMapNoGroundHeight || groundHeight <= 0.0f ) {
            groundHeight = 0.0f;
        }
    }

    const float heightScale = fabsf( ( eye.y() - groundHeight ) / eye.y() );
    outPosition = eye + seaOffset * heightScale;
}

void SetMapClipPosition( const char *pClipPath,
                         const float *pPosition,
                         const float *pAlpha,
                         const int32_t *pScale,
                         const Viewport *pViewport ) {
    // FUN_00789a40
    if ( pViewport == nullptr ) {
        pViewport = gpRender->getRenderPass<RP_Near>().pViewport;
    }

    FlashPlayer *pPlayer = nullptr;
    switch ( gpTestDriveInstance->getActiveGameMode() ) {
    case GM_Map:
        pPlayer = gpFlash->getFlashPlayer( "MAP" );
        break;
    case GM_Club:
        pPlayer = gpFlash->getFlashPlayer( "CLUB" );
        break;
    case GM_Lobby:
        pPlayer = gpFlash->getFlashPlayer( "CHALLENG" );
        break;
    default:
        pPlayer = gpMngHud->getHudPlayer();
        break;
    }
    if ( pPlayer == nullptr ) {
        return;
    }

    FlashClip clip;
    if ( !pPlayer->getClipPlayer( pClipPath, &clip ) ) {
        return;
    }

    const FlashResourceHeader *pHeader =
        pPlayer->getBoundFlashResource()->getHeader();
    const float movieWidth = static_cast<float>(
        static_cast<int32_t>( pHeader->FrameSize.XMax ) / 0x14 );
    const float movieHeight = static_cast<float>(
        static_cast<int32_t>( pHeader->FrameSize.YMax ) / 0x14 );
    const float viewportY = pPosition[1] * pViewport->Height + pViewport->Y;
    float y = movieHeight * viewportY;
    if ( gpConfig->b169Ratio ) {
        y = ( viewportY + kMapClipWidescreenYOffset ) *
            kMapClipWidescreenYScale * movieHeight;
    }
    clip.setProperty( eFlashProperty::FP_X,
                      ( pViewport->Width * pPosition[0] + pViewport->X ) *
                          movieWidth );
    clip.setProperty( eFlashProperty::FP_Y, y );
    {
        ScopedMutexLock lock = gpFlash->scopedLock();
        gpPreviousActiveFlashResource = gpActiveFlashResource;
        clip.pSprite->getDisplayList()->getFlagsRW() |= 1;
    }
    if ( pAlpha != nullptr ) {
        clip.setProperty( eFlashProperty::FP_Alpha,
                          *pAlpha * kMapClipAlphaScale );
    }
    if ( pScale != nullptr ) {
        clip.setProperty( eFlashProperty::FP_ScaleX,
                          static_cast<float>( *pScale ) );
        clip.setProperty( eFlashProperty::FP_ScaleY,
                          static_cast<float>( *pScale ) );
    }
}

GMMap::GMMap()
    : GameModeScene(),
      mngFontPlayers(),
      mngSpots(),
      mngWorld(),
      mngRoadNetwork(),
      mngImpostor(),
      mngImpostorSimple(),
      mngOcean(),
      mngLogoDisplay(),
      mngTracRoadMap(),
      mngCamGPS(),
      mngSectorName(),
      mngMapViewer(),
      demoZonesDrawList(),
      bGhostCursorVisible( false ),
      cursorX( kMapFocusDefault ),
      cursorY( kMapFocusDefault ),
      field_0xfe28( 0.0f ),
      field_0xfe2c( 0.0f ),
      zoomCursorX( 0.0f ),
      zoomCursorY( 0.0f ),
      zoomOutCursorPosition( Eigen::Vector4f::Zero() ),
      playerCarPosition( Eigen::Vector4f::Zero() ),
      pCameraGPS( nullptr ),
      pMapPlayer( nullptr ),
      field_0xfe68( 0 ),
      exitState( 0 ),
      field_0xde500( false ),
      field_0xde501( false ),
      pTargetSpot( nullptr ) {
    // FUN_009c0340
    registerManager( &mngFontPlayers );
    registerManager( &mngSpots );
    registerManager( &mngWorld );
    registerManager( &mngRoadNetwork );
    registerManager( &mngImpostor );
    registerManager( &mngImpostorSimple );
    registerManager( &mngOcean );
    registerManager( &mngLogoDisplay );
    registerManager( &mngTracRoadMap );
    registerManager( &mngCamGPS );
    registerManager( &mngSectorName );
    registerManager( &mngMapViewer );

    bWithWorld = true;
    gpGMMap = this;
    mngMapViewer.setState( MngMapViewer::MS_Active );
    mngSpots.setOwner( this );
    mngTracRoadMap.setOwner( &mngSpots );

    mngFlash.setWantsAsyncInstall( true );
    gpCamera->setUseBlur( true );
    transitionFlags = 0x21;
    numTicksPerTransition = 0;

    gpBrandCar->setSuspended( true );
    gpAI->getAIPool()->setTrafficPaused( true );
}

GMMap::~GMMap() {
    // FUN_009bfae0
    gpAI->getAIPool()->setTrafficPaused( false );
    gpBrandCar->setSuspended( false );

    gpGMMap = nullptr;
    gpCamera->setUseBlur( false );
}

void GMMap::initialize() {
    // FUN_009c3010
    GameModeScene::initialize();

    DrawStreams streams;
    streams.Normals = 0;
    streams.Diffuse = 1;
    streams.Specular = 0;
    streams.UVMap = 1;
    streams.Tangent = 0;
    streams.Binormal = 0;
    streams.bIndex16 = 1;
    demoZonesDrawList.initialize( 4, 16, 0, true, &streams, "GMMapDemoZones",
                                  false );
}

void GMMap::reset() {
    // FUN_009c2af0
    GameModeScene::reset();

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->resetHeightmapLODCoefs( 7 );
    pStreaming->scaleHeightmapLODCoefs( 1, kMapSectorLODScale );

    pCameraGPS = static_cast<CameraGPS *>( gpCamera->findCameraWithMode(
        kGPSCameraMode, gpCamera->getBoundCarState() ) );
    if ( pCameraGPS != nullptr && gbMapResetGPSCamera ) {
        pCameraGPS->reset( true );
    }

    Eigen::Vector4f focusPosition =
        gpScenaric->getLocalPlayer().getLastPosition();
    if ( gpTestDriveInstance->getPreviousGameMode() == GM_Login ) {
        const Eigen::Vector4f defaultPosition =
            gpScenaric->getDefaultStartPosition();
        if ( defaultPosition.x() != 0.0f ) {
            focusPosition = defaultPosition;
            focusPosition.w() = 1.0f;
        }
    }
    gpScenaric->getIntroTutorial().computeMapFocus( this, &focusPosition,
                                                    false );
    mngMapViewer.setFocus( focusPosition );

    if ( gpCamera->getActiveCamera() != pCameraGPS ) {
        gpCamera->setCameraMode( kGPSCameraMode, 0, 0.0f );
    }

    SetSpotStreamEnabled( false );
    mngSpots.setIconsEnabled( true );
    mngSpots.setPadNavigationEnabled( true );
    gpMngMusic->setPlayFlags( 1 );
    gpWorld->getStreamingManager()->setUnderwaterTilesRefined( false );
    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( true );
    }
    parkLocalPlayerCar();

    PhysicsVehicle *pPhysicsVehicle = GetLocalPlayerPhysicsVehicle();
    if ( pPhysicsVehicle == nullptr ) {
        playerCarPosition = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    } else {
        playerCarPosition =
            pPhysicsVehicle->getChassisWorldMatrix().row( 3 ).transpose();
        SetLocalPlayerCarPaused( pPhysicsVehicle, true );
    }

    if ( gpMngImpostor != nullptr ) {
        gHideDistanceForUnitBSphere = 200.0f;
        gpMngImpostor->setForceTopView( true );
    }

    if ( gpMngFontPlayers != nullptr ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    }

    gpAircraft->setMapViewLOD( true );
    if ( gpBoat != nullptr ) {
        gpBoat->setEcumeEnabled( true );
    }
    gpAircraft->setMapViewMaterials( true );

    mngSectorName.bEnabled = false;
    updateSpotFilterIcons();

    gpPlayerData->getLoadedSave()->getAchievements()->notify(
        ACF_Challenge | ACF_Road | ACF_Collection, false );
}

void GMMap::tick( float deltaTime, float totalTime ) {
    // FUN_009c2980
    GameModeScene::tick( deltaTime, totalTime );

    if ( pCameraGPS != nullptr && pMapPlayer != nullptr &&
         gpMngFlashLocalize != nullptr ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    }

    field_0xde500 = ( pCameraGPS != nullptr ) && !pCameraGPS->isSpaceView();

    SampleFrameRate();

    if ( exitState != 0 ) {
        updateExit();
    }

    drawDemoDebug();
}

void GMMap::drawMiniMapHighlight( const float depth, DrawList *pDrawList ) {
    // FUN_009bfca0
    FlashClip highlightClip;
    FlashClip boundsClip;
    if ( !pMapPlayer->getClipPlayer( "/mini_map/map_hg", &highlightClip ) ||
         !pMapPlayer->getClipPlayer( "/mini_map/map_bd", &boundsClip ) ) {
        return;
    }

    const float originX =
        highlightClip.getPropertyFloat( eFlashProperty::FP_ScreenX );
    const float originY =
        highlightClip.getPropertyFloat( eFlashProperty::FP_ScreenY );
    const float cellWidth =
        ( boundsClip.getPropertyFloat( eFlashProperty::FP_ScreenX ) -
          originX ) *
        ( 1.0f / float( kNumPlayerDensityCellsX ) );
    const float cellHeight =
        ( boundsClip.getPropertyFloat( eFlashProperty::FP_ScreenY ) -
          originY ) *
        ( 1.0f / float( kNumPlayerDensityCellsY ) );

    pDrawList->setIdentityTransform();
    pDrawList->setActiveMaterial( nullptr );

    const uint8_t *pPlayerDensity = nullptr;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    if ( pPlayerDensity != nullptr ) {
        DrawPlayerDensityGrid( pPlayerDensity, originX, originY, cellWidth,
                               cellHeight, depth, pDrawList );
    }

    FlashClip caseClip;
    if ( !pMapPlayer->getClipPlayer( "/mini_map/mm_case", &caseClip ) ) {
        return;
    }

    const float boundsX =
        highlightClip.getPropertyFloat( eFlashProperty::FP_X );
    const float boundsY =
        highlightClip.getPropertyFloat( eFlashProperty::FP_Y );
    const float caseWidth =
        ( boundsClip.getPropertyFloat( eFlashProperty::FP_X ) - boundsX ) *
        ( 1.0f / float( kNumMiniMapCaseCellsX ) );
    const float caseHeight =
        ( boundsClip.getPropertyFloat( eFlashProperty::FP_Y ) - boundsY ) *
        ( 1.0f / float( kNumMiniMapCaseCellsY ) );
    const Eigen::Vector4f &focus = pCameraGPS->getFocus();
    const float caseX =
        float( int32_t( focus.x() / caseWidth ) ) * caseWidth + boundsX;
    const float caseY =
        float( int32_t( focus.z() / caseHeight ) ) * caseHeight + boundsY;
    caseClip.setProperty( eFlashProperty::FP_X, caseX );
    caseClip.setProperty( eFlashProperty::FP_Y, caseY );
}

void GMMap::updatePlayerMarker() {
    // FUN_009c0200
    FlashClip playerClip;
    if ( !pMapPlayer->getClipPlayer( "/player", &playerClip ) ) {
        return;
    }

    bool bVisible = false;
    if ( !isSpaceViewActive() ) {
        float x = 0.0f;
        float y = 0.0f;
        CameraGame *pCamera = gpCamera->findCameraWithMode(
            kGPSCameraMode, gpCamera->getBoundCarState() );
        if ( kMapMarkerMinCoordinate <= fabsf( playerCarPosition.x() ) ||
             kMapMarkerMinCoordinate <= fabsf( playerCarPosition.y() ) ) {
            bVisible = ProjectToMapMovie( pCamera, playerCarPosition, x, y );
        }
        playerClip.setProperty( eFlashProperty::FP_X, x );
        playerClip.setProperty( eFlashProperty::FP_Y, y );
    }
    SetClipVisibleFlag( playerClip, bVisible );
}

void GMMap::updateTargetMarker() {
    // FUN_009bffe0
    FlashClip targetClip;
    if ( !pMapPlayer->getClipPlayer( "/target", &targetClip ) ) {
        return;
    }

    if ( isSpaceViewActive() ) {
        SetClipVisibleFlag( targetClip, false );
        return;
    }

    const ObjectiveMarker &gpsObjective = gpScenaric->getObjective( 1 );
    const ObjectiveMarker &netPlayerObjective = gpScenaric->getObjective( 5 );
    Eigen::Vector4f targetPosition = gpsObjective.Position;
    if ( !IsObjectiveSet( gpsObjective ) ) {
        if ( !IsObjectiveSet( netPlayerObjective ) ) {
            SetClipVisibleFlag( targetClip, false );
            return;
        }
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    }

    SetClipVisibleFlag( targetClip, true );
    CameraGame *pCamera = gpCamera->findCameraWithMode(
        kGPSCameraMode, gpCamera->getBoundCarState() );
    float x = 0.0f;
    float y = 0.0f;
    if ( !ProjectToMapMovie( pCamera, targetPosition, x, y ) ) {
        targetClip.setProperty( eFlashProperty::FP_Visible, false );
        return;
    }
    targetClip.setProperty( eFlashProperty::FP_Visible, true );
    targetClip.setProperty( eFlashProperty::FP_X, x );
    targetClip.setProperty( eFlashProperty::FP_Y, y );
}

void GMMap::draw( float deltaTime, float /*totalTime*/ ) {
    // FUN_009c1de0
    if ( pMapPlayer != nullptr ) {
        updatePlayerMarker();
        updateTargetMarker();
        if ( exitState == 0 ) {
            updateCursor( deltaTime );
        }
    }

    if ( gDisplayDemoZones ) {
        gpRender->getRenderPass<RP_SunDazzle>().pScene->enqueueDynamicDrawList(
            &demoZonesDrawList );
    }
}

void GMMap::pause() {
    // FUN_009c0820
    gbMapResetGPSCamera = false;
    mngSpots.setDrawDensityMap( false );
    mngSpots.setDrawChallengeCones( false );
    gFlashCallbacksRegister.removeAllSynonyms( &MapHighlightCallback );
    gpWorld->getStreamingManager()->setUnderwaterTilesRefined( false );

    SetSpotStreamEnabled( true );
    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( false );
    }

    GameModeScene::pause();

    restoreLocalPlayerCarControl();

    PhysicsVehicle *pPhysicsVehicle = GetLocalPlayerPhysicsVehicle();

    if ( pPhysicsVehicle != nullptr ) {
        SetLocalPlayerCarPaused( pPhysicsVehicle, false );
    }

    if ( pCameraGPS != nullptr ) {
        pCameraGPS->endTransition();
    }

    if ( gpMngImpostor != nullptr ) {
        gHideDistanceForUnitBSphere = 100.0f;
        gpMngImpostor->setForceTopView( false );
    }

    gpAircraft->setMapViewLOD( false );
    if ( gpBoat != nullptr ) {
        gpBoat->setEcumeEnabled( false );
    }
    gpAircraft->setMapViewMaterials( false );
}

void GMMap::destroy() {
    // FUN_009c2f20
    GameModeScene::destroy();

    AiPool *pAIPool = gpAI->getAIPool();
    if ( gpTestDriveInstance->getActiveGameMode() != GM_FreeRide ) {
        gpAI->setDisableBots( true );
        gpAI->setBotsActive( false );
        pAIPool->removeBots();
    }
    gpAI->setDisableCops( true );
    gpAI->setCopsActive( false );
    pAIPool->removeCops();

    demoZonesDrawList.destroy();

    if ( pCameraGPS != nullptr ) {
        pCameraGPS->setPoseCallback( nullptr, nullptr );
    }
}

void GMMap::onResourceStreamed() {
    // FUN_009c13f0
    GameModeScene::onResourceStreamed();

    pMapPlayer = nullptr;
    uint32_t levelIndex = 0;
    uint32_t movieIndex = 0;
    if ( gpFlash->getFlashMovieIndex( &levelIndex, &movieIndex, "MAP" ) ) {
        MoviePlayer *pMovie = gpFlash->getMoviePlayer( levelIndex, movieIndex );
        if ( pMovie != nullptr && pMovie->MovieBank.isLoaded() ) {
            pMapPlayer = pMovie->MovieBank.getFlashPlayer();
        }
    }

    if ( pMapPlayer == nullptr ) {
        return;
    }

    pMapPlayer->setVariableValue( "/:ContactType",
                                  gpFlash->getContactTypeRW() );
    if ( pCameraGPS != nullptr ) {
        pCameraGPS->updateFlashZoom( pMapPlayer );
    }
    gFlashCallbacksRegister.add( "map_hg", &MapHighlightCallback, nullptr );
    mngSpots.setFlashPlayer();
}

bool GMMap::onMessage( FlashMessage *pMessage ) {
    // FUN_009c1e60
    OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_006bd120

    switch ( *pMessage ) {
    case kMsgFilter1:
        setActiveFilterMode( 1 );
        return true;
    case kMsgFilter2:
        setActiveFilterMode( 2 );
        return true;
    case kMsgFilter3:
        setActiveFilterMode( 4 );
        return true;
    case kMsgFilter4:
        setActiveFilterMode( 8 );
        return true;
    case kMsgGoFreeride:
        goToFreeRide();
        return true;
    case kMsgFilterIn:
        mngSpots.refreshFilterCheckboxes();
        mngSpots.setPadNavigationEnabled( false );
        field_0xde501 = true;
        return true;
    case kMsgFilterOut:
        mngSpots.setPadNavigationEnabled( mngSpots.areIconsEnabled() );
        field_0xde501 = false;
        return true;
    case kMsgChallengeFilter:
        mngSpots.applyFilterCheckboxes();
        return true;
    case kMsgDcof:
        mngSpots.setInfoCartShown( false );
        mngSpots.setInfoCartRequested( false );
        return true;
    case kMsgDebm:
        mngSpots.deleteSelectedBookmark();
        return true;
    case kMsgFity:
        leaveFreeModeIfActive();
        return true;
    case kMsgFreeMode:
        enterFreeMode();
        return true;
    case kMsgGoIn:
        goToSelectedSpot();
        return true;
    case kMsgGoSpot:
        goToSpot();
        return true;
    case kMsgGoTeleport:
        goToTeleportTarget();
        return true;
    case kMsgMapOk:
        mngSpots.setInfoCartRequested( true );
        if ( pMapPlayer != nullptr ) {
            pMapPlayer->setVariableValue(
                "/:WhatMode", ( gMapActiveFilterMode == kMapFilterFreeMode )
                                  ? "free"
                                  : "spot" );
        }
        return true;
    case kMsgStatsOff:
        if ( gpMapView != nullptr ) {
            gpMapView->bEnabled = false;
        }
        mngSpots.setPadNavigationEnabled( false );
        field_0xde501 = true;
        return true;
    case kMsgStatsOn:
        if ( gpMapView != nullptr ) {
            gpMapView->bEnabled = true;
        }
        mngSpots.setPadNavigationEnabled( true );
        field_0xde501 = false;
        return true;
    case kMsgNextFilter:
        mngSpots.cycleFilterRow( true );
        return true;
    case kMsgPrevFilter:
        mngSpots.cycleFilterRow( false );
        return true;
    case kMsgMnok:
        mngSpots.closeInfoCart();
        mngSpots.setInfoCartRequested( false );
        return true;
    case kMsgPlayerSession:
        OTDU_UNIMPLEMENTED; // TODO: ONLINE
        return true;
    case kMsgSpotMode:
        leaveFreeMode( nullptr );
        return true;
    case kMsgTeleportOk:
        confirmTeleport();
        return true;
    case kMsgTarget:
        setTarget();
        return true;
    case kMsgTargetOff:
        forceTarget();
        return true;
    case kMsgZoomIn:
        zoomIn();
        return true;
    case kMsgZoomOut:
        zoomOut();
        return true;
    default:
        return false;
    }
}

void GMMap::setActiveFilterMode( const uint8_t mode ) {
    // FUN_009c13a0
    if ( gMapActiveFilterMode == kMapFilterFreeMode ) {
        leaveFreeMode( nullptr );
    }
    mngSpots.setWasFreeMode( gMapActiveFilterMode == kMapFilterFreeMode );
    gMapActiveFilterMode = mode;
    mngSpots.refreshDensityMap();
    mngSpots.requestResort();
}

void GMMap::enterFreeMode() {
    // FUN_009c0720
    if ( gMapActiveFilterMode != kMapFilterFreeMode ) {
        gMinimapFilterMode = gMapActiveFilterMode;
    }
    gMapActiveFilterMode = kMapFilterFreeMode;

    MapSpotRecord *pSelectedRecord = mngSpots.getSelectedRecord();
    if ( pSelectedRecord == nullptr ||
         !mngSpots.isCursorOverRecord( pSelectedRecord,
                                       Eigen::Vector2f( cursorX, cursorY ) ) ) {
        if ( pSelectedRecord == nullptr ) {
            cursorX = kMapFocusDefault;
            cursorY = kMapFocusDefault;
        } else {
            ProjectToMapScreen( pSelectedRecord->Position, nullptr, cursorX,
                                cursorY );
        }
    }

    if ( pMapPlayer != nullptr ) {
        pMapPlayer->setVariableValue( "/:WhatMode", "free" );
        pMapPlayer->showFrame( "free", "/curseur", false );
    }
}

bool GMMap::leaveFreeModeIfActive() {
    // FUN_009c1380
    if ( gMapActiveFilterMode != kMapFilterFreeMode ) {
        return false;
    }
    leaveFreeMode( nullptr );
    return true;
}

void GMMap::leaveFreeMode( MapSpotRecord *pSelectedRecord ) {
    // FUN_009c1290
    mngSpots.setWasFreeMode( gMapActiveFilterMode == kMapFilterFreeMode );
    gMapActiveFilterMode = gMinimapFilterMode;
    mngSpots.setSelectedRecord( pSelectedRecord );
    mngSpots.refreshSelectionCart();

    if ( pMapPlayer == nullptr ) {
        return;
    }

    pMapPlayer->setVariableValue( "/:WhatMode", "spot" );

    static constexpr const char *kCursorFrames[CameraGPS::GZL_Count] = {
        nullptr, "5", "4", "3", "2", "1" };
    const int32_t zoom = ( pCameraGPS != nullptr ) ? pCameraGPS->getZoom()
                                                   : CameraGPS::GZL_Fixed;
    if ( zoom > CameraGPS::GZL_Fixed && zoom < CameraGPS::GZL_Count ) {
        pMapPlayer->showFrame( kCursorFrames[zoom], "/curseur", false );
    }
}

void GMMap::goToFreeRide() {
    // FUN_009bfa20
    mngSpots.setInfoCartRequested( false );

    const Eigen::Vector4f carPosition =
        gpWorld->getPreviousTransform().row( 3 ).transpose();
    gpScenaric->setWasMapExited( true );
    moveFocusTo( carPosition );

    mngCamGPS.setForceStreaming( true );
    gpMapView->bEnabled = false;

    CameraGPS *pCamera = static_cast<CameraGPS *>( gpCamera->findCameraWithMode(
        kGPSCameraMode, gpCamera->getBoundCarState() ) );
    if ( pCamera != nullptr ) {
        pCamera->setPoseCallback( &MapExitHopCallback, this );
    }
    exitState = 6;
}

void GMMap::moveFocusTo( const Eigen::Vector4f &target ) {
    // FUN_009bf930
    const int32_t zoom = pCameraGPS->getZoom();
    if ( zoom != CameraGPS::GZL_Island && zoom != CameraGPS::GZL_Space &&
         ( mngMapViewer.getFocus() - target ).norm() >
             kMapFocusMoveMinDistance ) {
        gpMapView->startMove( target, kMapFocusMoveDuration );
        mngMapViewer.setPendingFocusMove( target );
        return;
    }

    mngMapViewer.setFocus( target );
}

static Eigen::Vector4f
ProjectPositionOnRoad( const Eigen::Vector4f &position ) {
    AiRoadLocation location;
    ProjectOnRoad( position, nullptr, nullptr, false, &location );
    return location.Frame.row( 3 ).transpose();
}

void GMMap::startSpotExit( const SpotMap *pSpot ) {
    // FUN_009bfc40
    exitState = 1;
    mngCamGPS.setForceStreaming( true );
    mngCamGPS.setForceStreamingFlush( true );
    pTargetSpot = pSpot;
    gpMapView->bEnabled = false;
    if ( pSpot != nullptr ) {
        moveFocusTo( Eigen::Vector4f( pSpot->X, pSpot->Y, pSpot->Z, 1.0f ) );
    }
}

void GMMap::goToSelectedSpot() {
    // FUN_009c1e60 (inlined)
    const SpotMap *pSpot = mngSpots.getSelectedSpot();
    startSpotExit( pSpot );
    if ( pSpot != nullptr ) {
        return;
    }

    const MapSpotRecord *pSelectedRecord = mngSpots.getSelectedRecord();
    if ( pSelectedRecord == nullptr ) {
        goToFreeRide();
        return;
    }
    moveFocusTo( ProjectPositionOnRoad( pSelectedRecord->Position ) );
}

void GMMap::goToSpot() {
    // FUN_009c1e60 (inlined)
    static constexpr float kSpotApproachDistance = 10.0f;

    gpScenaric->setWasMapExited( true );
    mngCamGPS.setForceStreaming( true );
    exitState = 2;

    const MapSpotRecord *pSelectedRecord = mngSpots.getSelectedRecord();
    Eigen::Vector4f target;
    if ( pSelectedRecord == nullptr ) {
        exitState = 6;
        mngSpots.setInfoCartRequested( false );
        target = gpWorld->getPreviousTransform().row( 3 ).transpose();
    } else if ( pSelectedRecord->Info.Kind == MapSpotRecord::kKindBookmark ) {
        exitState = 5;
        target = pSelectedRecord->Position;
    } else {
        Matrix4x4f_t approachFrame;
        ComputeRoadApproachFrame( pSelectedRecord->Position, approachFrame,
                                  kSpotApproachDistance );
        target = approachFrame.row( 3 ).transpose();
    }
    moveFocusTo( target );
    gpMapView->bEnabled = false;
}

static int32_t SnapToMapRoadFrame( const Eigen::Vector4f &position,
                                   Matrix4x4f_t &outFrame,
                                   const eRoadSnapMode mode,
                                   const bool bShoulder ) {
    if ( !gbMapDebugTeleportAnywhere ) {
        return SnapToRoadFrame( position, outFrame, mode, bShoulder, false );
    }

    outFrame = Matrix4x4f_t::Identity();
    outFrame.row( 3 ) = position.transpose();

    // NOTE: Only used by free teleport (ie Debug only)
    const float groundHeight =
        gpPhysics->getGroundHeight( position.x(), position.z() );
    if ( groundHeight > kMapDebugNoGround ) {
        outFrame( 3, 1 ) = groundHeight + kMapDebugTeleportLift;
    }
    return 2;
}

void GMMap::goToTeleportTarget() {
    // FUN_009c1e60 (inlined)
    gpScenaric->setWasMapExited( true );
    Matrix4x4f_t cursorFrame = Matrix4x4f_t::Identity();
    computeCursorWorldFrame( cursorFrame );
    moveFocusTo( cursorFrame.row( 3 ).transpose() );
    mngCamGPS.setForceStreaming( true );
    exitState = 3;
    gpMapView->bEnabled = false;
}

void GMMap::confirmTeleport() {
    // FUN_009c0d10
    pMapPlayer->setVariableValue(
        "/:IsTargetPresent",
        IsObjectiveSet( gpScenaric->getObjective( 1 ) ) ? 1 : 0 );
    if ( gMapActiveFilterMode != kMapFilterFreeMode ) {
        const int32_t bHasSelection =
            ( mngSpots.getSelectedRecord() != nullptr ) ? 1 : 0;
        pMapPlayer->setVariableValue( "/:IsTargetOK", bHasSelection );
        pMapPlayer->setVariableValue( "/:IsTeleportOK", bHasSelection );
        return;
    }

    Matrix4x4f_t cursorFrame = Matrix4x4f_t::Identity();
    const int32_t roadResult = computeCursorWorldFrame( cursorFrame );
    const bool bHasObjective =
        IsObjectiveSet( gpScenaric->getObjective( 1 ) ) ||
        IsObjectiveSet( gpScenaric->getObjective( 5 ) );
    const bool bInsideMap =
        mngSpots.isInsideMapRect( cursorFrame.row( 3 ).transpose() );

    int32_t bTeleportOK = 0;
    if ( !gpScenaric->getIntroTutorial().isActive() ) {
        pMapPlayer->setVariableValue(
            "/:IsTargetOK",
            ( roadResult != 0 && ( bInsideMap || bHasObjective ) ) ? 1 : 0 );
        bTeleportOK = ( bInsideMap && roadResult == 2 ) ? 1 : 0;
    } else {
        pMapPlayer->setVariableValue( "/:IsTargetOK", 0 );
    }
    pMapPlayer->setVariableValue( "/:IsTeleportOK", bTeleportOK );
}

void GMMap::placeLocalPlayerCar( const Eigen::Vector4f &position,
                                 Matrix4x4f_t *pOutTransform,
                                 const eRoadSnapMode mode,
                                 const bool bShoulder ) {
    // FUN_009c11d0
    Matrix4x4f_t transform;
    SnapToMapRoadFrame( position, transform, mode, bShoulder );
    if ( pOutTransform != nullptr ) {
        *pOutTransform = transform;
    }

    PhysicsVehicle *pPhysicsVehicle = GetLocalPlayerPhysicsVehicle();
    if ( pPhysicsVehicle != nullptr ) {
        PhysicsLock lock;
        gpPhysics->refreshHeightmapCollision();
        pPhysicsVehicle->resetToTransform( transform, true );
        playerCarPosition = transform.row( 3 ).transpose();
    }
}

void GMMap::setTarget() {
    // FUN_009c0b40
    ObjectiveMarker &target = gpScenaric->getObjective( 1 );
    const MapSpotRecord *pSelectedRecord = mngSpots.getSelectedRecord();
    if ( !IsObjectiveSet( target ) ) {
        gpScenaric->getObjective( 5 ).Type = "";
        if ( pSelectedRecord == nullptr ) {
            Matrix4x4f_t cursorFrame = Matrix4x4f_t::Identity();
            if ( computeCursorWorldFrame( cursorFrame ) == 0 ) {
                return;
            }
            target.Position = cursorFrame.row( 3 ).transpose();
            target.Position.w() = 1.0f;
            target.Type = "finish";
            return;
        }

        target.Position = pSelectedRecord->Position;
        target.Type = "finish";
        if ( pSelectedRecord->pSpot != nullptr ) {
            gpScenaric->setObjectiveSpotHashcode(
                pSelectedRecord->pSpot->Hashcode );
        }
        return;
    }

    target.Type = "";
    gpScenaric->setObjectiveSpotHashcode( 0 );
}

void GMMap::forceTarget() {
    // FUN_009c0c50
    ObjectiveMarker &target = gpScenaric->getObjective( 1 );
    const MapSpotRecord *pSelectedRecord = mngSpots.getSelectedRecord();
    if ( pSelectedRecord == nullptr ) {
        Matrix4x4f_t cursorFrame = Matrix4x4f_t::Identity();
        computeCursorWorldFrame( cursorFrame );
        target.Position = cursorFrame.row( 3 ).transpose();
        target.Position.w() = 1.0f;
    } else {
        gpScenaric->getObjective( 5 ).Type = "";
        target.Position = pSelectedRecord->Position;
        if ( pSelectedRecord->pSpot != nullptr ) {
            target.Type = "finish";
            gpScenaric->setObjectiveSpotHashcode(
                pSelectedRecord->pSpot->Hashcode );
            return;
        }
    }
    target.Type = "finish";
}

void GMMap::zoomIn() {
    // FUN_009c0fa0
    Matrix4x4f_t cursorFrame = Matrix4x4f_t::Identity();
    if ( gMapActiveFilterMode == kMapFilterFreeMode ) {
        computeCursorWorldFrame( cursorFrame );
    }
    gpMapView->bEnabled = true;

    if ( pCameraGPS != nullptr && pCameraGPS->zoomIn() ) {
        updateSpotFilterIcons();

        if ( gMapActiveFilterMode == kMapFilterFreeMode ) {
            if ( !isSpaceViewActive() ) {
                const Eigen::Vector4f cursorPosition =
                    cursorFrame.row( 3 ).transpose();
                if ( mngSpots.isInsideMapRect( cursorPosition ) ) {
                    mngMapViewer.setFocus( cursorPosition );
                    pCameraGPS->setFocus( cursorPosition );
                    mngSpots.setFocus( cursorPosition );
                    zoomCursorX = cursorX;
                    zoomCursorY = cursorY;
                } else {
                    zoomCursorX = -1.0f;
                    zoomCursorY = -1.0f;
                }
            }
        } else {
            const MapSpotRecord *pSelectedRecord = mngSpots.getSelectedRecord();
            if ( pSelectedRecord != nullptr ) {
                mngMapViewer.setFocus( pSelectedRecord->Position );
                pCameraGPS->setFocus( pSelectedRecord->Position );
                mngSpots.setFocus( pSelectedRecord->Position );
            }
        }

        if ( pCameraGPS->getPreviousZoom() == CameraGPS::GZL_Space ) {
            gpFlash->getFlashSound()->playFrontEndMusic( true );
            gpAudio->requestMixStateResync();
        }
        field_0xfe68 = 0;
    }

    if ( pCameraGPS == nullptr ) {
        return;
    }

    if ( pMapPlayer != nullptr ) {
        pCameraGPS->updateFlashZoom( pMapPlayer );
    }

    CameraGame *pCamera =
        gpCamera->findCameraWithMode( 1, gpCamera->getBoundCarState() );
    if ( pCamera != nullptr ) {
        const Eigen::Vector4f position = *pCameraGPS->getWorldPositionRW();
        const Eigen::Vector4f target( pCameraGPS->getTarget().x(),
                                      pCameraGPS->getTarget().y(),
                                      pCameraGPS->getTarget().z(), 1.0f );
        pCamera->setPosition( position );
        pCamera->setTarget( target );
        pCamera->updateMatrix( position.head<3>(),
                               Eigen::Vector3f( 0.0f, 1.0f, 0.0f ),
                               target.head<3>() );
    }
}

void GMMap::zoomOut() {
    // FUN_009c0e70
    if ( pCameraGPS == nullptr ) {
        return;
    }

    if ( pCameraGPS->zoomOut() ) {
        updateSpotFilterIcons();

        if ( pCameraGPS->getZoom() == CameraGPS::GZL_Space ) {
            gpMapView->bEnabled = false;
            gpAudio->changeMixState( ~gpAudio->getMixFlags( "ClientRadio" ) &
                                     gpAudio->getMixState() );
            gpFlash->getFlashSound()->playFrontEndMusic( true );
        }

        if ( gMapActiveFilterMode == kMapFilterFreeMode ) {
            Matrix4x4f_t cursorFrame = Matrix4x4f_t::Identity();
            computeCursorWorldFrame( cursorFrame );
            zoomOutCursorPosition = cursorFrame.row( 3 ).transpose();
        }

        field_0xfe68 = 0;
        zoomCursorX = -1.0f;
        zoomCursorY = -1.0f;
    }

    if ( pMapPlayer != nullptr ) {
        pCameraGPS->updateFlashZoom( pMapPlayer );
    }
}

void GMMap::updateSpotFilterIcons() {
    // FUN_009c0e70 (inlined)
    mngSpots.setDrawDensityMap( false );
    mngSpots.setDrawChallengeCones( false );
    if ( pCameraGPS == nullptr ||
         pCameraGPS->getZoom() != CameraGPS::GZL_Island ) {
        return;
    }

    const uint8_t filterMode = GetEffectiveMapFilterMode();
    if ( ( filterMode & 4 ) != 0 ) {
        mngSpots.setDrawDensityMap( true );
    }
    if ( ( filterMode & 1 ) != 0 ) {
        mngSpots.setDrawChallengeCones( true );
    }
}

void GMMap::updateExit() {
    // FUN_009c2430
    if ( gpMngFade != nullptr ) {
        gpMngFade->setRenderLogo( true );
    }

    switch ( exitState ) {
    case 1: {
        if ( pTargetSpot == nullptr ) {
            const MapSpotRecord *pSelectedRecord = mngSpots.getSelectedRecord();
            if ( pSelectedRecord == nullptr ) {
                goToFreeRide();
                return;
            }
            const Eigen::Vector4f position =
                ProjectPositionOnRoad( pSelectedRecord->Position );
            if ( gpMapView->isMoving() ||
                 !gpWorld->isStreamedAround( &position, true, true ) ) {
                break;
            }
            placeLocalPlayerCar( position, nullptr,
                                 eRoadSnapMode::RSM_ProjectOnly, false );
            gpTestDriveInstance->setNextGameMode( GM_FreeRide );
            requestExit();
            break;
        }

        const Eigen::Vector4f position( pTargetSpot->X, pTargetSpot->Y,
                                        pTargetSpot->Z, 1.0f );
        if ( gpMapView->isMoving() ||
             !gpWorld->isStreamedAround( &position, true, true ) ) {
            break;
        }
        placeLocalPlayerCar( position, nullptr,
                             eRoadSnapMode::RSM_KeepAwayFromEnds, false );
        gpScenaric->setCurrentSpotIndex(
            gpScenaric->getSpotList().getSpotMapIndex(
                pTargetSpot->Hashcode ) );
        LaunchSpotLevel( const_cast<SpotMap *>( pTargetSpot ), true );
        break;
    }
    case 2: {
        static constexpr float kSpotApproachDistance = 10.0f;
        const MapSpotRecord *pSelectedRecord = mngSpots.getSelectedRecord();
        if ( pSelectedRecord == nullptr ) {
            goToFreeRide();
            break;
        }
        Matrix4x4f_t frame;
        ComputeRoadApproachFrame( pSelectedRecord->Position, frame,
                                  kSpotApproachDistance );
        const Eigen::Vector4f position = frame.row( 3 ).transpose();
        if ( gpMapView->isMoving() ||
             !gpWorld->isStreamedAround( &position, true, true ) ) {
            break;
        }
        placeLocalPlayerCar( pSelectedRecord->Position, nullptr,
                             eRoadSnapMode::RSM_BackOff, false );
        gpWorld->setPreviousTransform( frame );
        gpTestDriveInstance->setNextGameMode( GM_FreeRide );
        requestExit();
        break;
    }
    case 5: {
        const MapSpotRecord *pSelectedRecord = mngSpots.getSelectedRecord();
        if ( pSelectedRecord == nullptr ) {
            goToFreeRide();
            break;
        }
        const Eigen::Vector4f position = pSelectedRecord->Position;
        if ( gpMapView->isMoving() ||
             !gpWorld->isStreamedAround( &position, true, true ) ) {
            break;
        }

        CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
        PhysicsVehicle *pPhysicsVehicle =
            ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;
        if ( pPhysicsVehicle != nullptr ) {
            Matrix4x4f_t carTransform = Matrix4x4f_t::Identity();
            const Bookmark *pBookmark = mngSpots.getSelectedBookmark();
            if ( pBookmark == nullptr ) {
                carTransform.row( 3 ) = position.transpose();
            } else {
                const Eigen::Vector3f &forward = pBookmark->Orientation;
                Eigen::Vector4f axisZ( forward.x(), forward.y(), forward.z(),
                                       0.0f );
                axisZ.normalize();
                Eigen::Vector4f axisX( forward.z(), 0.0f, -forward.x(), 0.0f );
                axisX.normalize();
                Eigen::Vector3f axisY =
                    axisZ.head<3>().cross( axisX.head<3>() ).normalized();
                carTransform.row( 0 ) = axisX.transpose();
                carTransform.row( 1 ) =
                    Eigen::Vector4f( axisY.x(), axisY.y(), axisY.z(), 0.0f )
                        .transpose();
                carTransform.row( 2 ) = axisZ.transpose();
                carTransform.row( 3 ) =
                    Eigen::Vector4f( pBookmark->Position.x(),
                                     pBookmark->Position.y(),
                                     pBookmark->Position.z(), 1.0f )
                        .transpose();
            }

            PhysicsLock lock;
            pCarState->resetBackOnTrack();
            pPhysicsVehicle->resetToTransform( carTransform, true );
        }

        Matrix4x4f_t previousTransform = Matrix4x4f_t::Identity();
        previousTransform.row( 3 ) = position.transpose();
        gpWorld->setPreviousTransform( previousTransform );
        gpTestDriveInstance->setNextGameMode( GM_FreeRide );
        requestExit();
        break;
    }
    case 3: {
        const Eigen::Vector4f &focus = mngMapViewer.getFocus();
        if ( gpMapView->isMoving() ||
             !gpWorld->isStreamedAround( &focus, true, true ) ) {
            break;
        }

        Matrix4x4f_t carTransform;
        placeLocalPlayerCar( focus, &carTransform,
                             eRoadSnapMode::RSM_KeepAwayFromEnds, true );
        gpWorld->setPreviousTransform( carTransform );
        gpTestDriveInstance->setNextGameMode( GM_FreeRide );
        requestExit();
        break;
    }
    case 6: {
        const Matrix4x4f_t carTransform = gpWorld->getPreviousTransform();
        const Eigen::Vector4f carPosition = carTransform.row( 3 ).transpose();
        if ( pCameraGPS->isInTransition() || gpMapView->isMoving() ||
             !gpWorld->isStreamedAround( &carPosition, true, true ) ) {
            break;
        }

        CameraGPS *pCamera =
            static_cast<CameraGPS *>( gpCamera->findCameraWithMode(
                kGPSCameraMode, gpCamera->getBoundCarState() ) );
        if ( pCamera != nullptr ) {
            pCamera->setPoseCallback( nullptr, nullptr );
        }

        CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
        PhysicsVehicle *pPhysicsVehicle =
            ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;
        if ( pPhysicsVehicle != nullptr ) {
            PhysicsLock lock;
            gpPhysics->refreshHeightmapCollision();
            pPhysicsVehicle->resetToTransform( carTransform, true );
        }

        setTransitionFlags( getTransitionFlags() & ~0x20u );
        requestExit();
        gpTestDriveInstance->setNextGameMode( GM_FreeRide );
        break;
    }
    default:
        break;
    }
}

bool GMMap::isSpaceViewActive() const {
    // FUN_009be8e0
    return pCameraGPS->getZoom() == CameraGPS::GZL_Space ||
           ( pCameraGPS->getPreviousZoom() == CameraGPS::GZL_Space &&
             pCameraGPS->isInTransition() );
}

int32_t GMMap::computeCursorWorldFrame( Matrix4x4f_t &outFrame ) const {
    // FUN_009c0ab0
    if ( pCameraGPS == nullptr ) {
        return 0;
    }
    if ( pCameraGPS->getZoom() == CameraGPS::GZL_Space ||
         ( pCameraGPS->getPreviousZoom() == CameraGPS::GZL_Space &&
           pCameraGPS->isInTransition() ) ) {
        return 0;
    }

    Eigen::Vector4f cursorPosition;
    ScreenToWorldGround( Eigen::Vector2f( cursorX, 1.0f - cursorY ),
                         cursorPosition );
    return SnapToMapRoadFrame( cursorPosition, outFrame,
                               eRoadSnapMode::RSM_ProjectOnly, false );
}

void GMMap::updateCursor( const float deltaTime ) {
    // FUN_009c14a0
    if ( pMapPlayer == nullptr || bExitRequested || exitState != 0 ||
         !mngSpots.areMapRectsLatched() ) {
        return;
    }
    const FlashRECT &rect = mngSpots.getFreeModeRect();

    FlashClip ghostClip;
    FlashClip cursorClip;
    const bool bHasGhostClip =
        pMapPlayer->getClipPlayer( "/curseur_ghost", &ghostClip );
    const bool bHasCursorClip =
        pMapPlayer->getClipPlayer( "/curseur", &cursorClip );

    if ( isSpaceViewActive() || field_0xde501 ||
         gpFlashMessageBox->FUN_00403100() ) {
        if ( bHasCursorClip ) {
            cursorClip.setProperty( eFlashProperty::FP_Visible, false );
        }
        if ( bGhostCursorVisible && bHasGhostClip ) {
            bGhostCursorVisible = false;
            ghostClip.setProperty( eFlashProperty::FP_Visible, false );
        }
        return;
    }

    PlayerConfiguration *pPlayerConfig = gpConfig->pPlayerConfig;
    const bool bMenuPadPressed =
        ( pPlayerConfig->pMenuLeft->Flags & 1 ) != 0 ||
        ( pPlayerConfig->pMenuRight->Flags & 1 ) != 0 ||
        ( pPlayerConfig->pMenuUp->Flags & 1 ) != 0 ||
        ( pPlayerConfig->pMenuDown->Flags & 1 ) != 0;

    const float inputLeft =
        ReadMapCursorAction( pPlayerConfig->pMapCursorLeft );
    const float inputRight =
        ReadMapCursorAction( pPlayerConfig->pMapCursorRight );
    const float inputUp = ReadMapCursorAction( pPlayerConfig->pMapCursorUp );
    const float inputDown =
        ReadMapCursorAction( pPlayerConfig->pMapCursorDown );

    const bool bMouseMoved = gpDevice->getMouseState().FUN_00980000();
    const Eigen::Vector2f mouse( gEdFlash.Input.MouseCoordinates[0],
                                 gEdFlash.Input.MouseCoordinates[1] );
    const bool bMouseInRect =
        rect.X <= mouse.x() && mouse.x() <= rect.Width + rect.X &&
        rect.Y <= mouse.y() && mouse.y() <= rect.Height + rect.Y;

    if ( gMapActiveFilterMode != kMapFilterFreeMode && !bMenuPadPressed ) {
        if ( inputDown == 0.0f && inputUp == 0.0f && inputLeft == 0.0f &&
             inputRight == 0.0f ) {
            if ( bMouseMoved && bMouseInRect ) {
                MapSpotRecord *pHoveredRecord = nullptr;
                const bool bHovered =
                    mngSpots.findRecordUnderCursor( &mouse, &pHoveredRecord );
                const InputChannel *pButton =
                    gpDevice->getMouseState().pChannelButton2;
                if ( !bHovered && ( pButton == nullptr ||
                                    ( pButton->Flags & 0x10 ) == 0 ) ) {
                    enterFreeMode();
                } else if ( pHoveredRecord != nullptr &&
                            mngSpots.getSelectedRecord() != pHoveredRecord ) {
                    if ( gMapActiveFilterMode != kMapFilterFreeMode ) {
                        enterFreeMode();
                    }
                    leaveFreeMode( pHoveredRecord );
                }
            }
        } else {
            enterFreeMode();
        }
    }

    if ( gMapActiveFilterMode == kMapFilterFreeMode && exitState == 0 &&
         !bExitRequested && !pCameraGPS->isInTransition() ) {
        if ( gpMapView == nullptr || !gpMapView->isMoving() ) {
            if ( gpConfig->NbMaxPlayers < 1 ) {
                return;
            }

            if ( inputDown > 0.0f ) {
                cursorY = kMapCursorPadSpeed * inputDown * deltaTime + cursorY;
            }
            if ( inputUp > 0.0f ) {
                cursorY = cursorY - kMapCursorPadSpeed * inputUp * deltaTime;
            }
            if ( inputLeft > 0.0f ) {
                cursorX = cursorX - kMapCursorPadSpeed * inputLeft * deltaTime;
            }
            if ( inputRight > 0.0f ) {
                cursorX = kMapCursorPadSpeed * inputRight * deltaTime + cursorX;
            }
            if ( bMouseMoved && bMouseInRect ) {
                cursorX = mouse.x();
                cursorY = mouse.y();
            }

            float panX = 0.0f;
            if ( rect.X < cursorX ) {
                if ( rect.Width + rect.X <= cursorX ) {
                    panX = inputRight;
                }
            } else {
                panX = 0.0f - inputLeft;
            }
            float panZ = 0.0f;
            if ( rect.Y < cursorY ) {
                if ( rect.Height + rect.Y <= cursorY ) {
                    panZ = inputDown;
                }
            } else {
                panZ = 0.0f - inputUp;
            }
            gpMapView->InputX = panX;
            gpMapView->InputZ = panZ;

            cursorX = std::max( cursorX, rect.X );
            cursorY = std::max( cursorY, rect.Y );
            cursorX = std::min( cursorX, rect.Width + rect.X );
            cursorY = std::min( cursorY, rect.Height + rect.Y );
            const float cursor[2] = { cursorX, cursorY };
            SetMapClipPosition( "/curseur", cursor, nullptr, nullptr );

            Matrix4x4f_t cursorFrame = Matrix4x4f_t::Identity();
            const int32_t roadResult = computeCursorWorldFrame( cursorFrame );
            const Eigen::Vector4f cursorPosition =
                cursorFrame.row( 3 ).transpose();

            if ( bHasGhostClip && !mngSpots.isCursorOverSelection(
                                      Eigen::Vector2f( cursorX, cursorY ) ) ) {
                if ( roadResult == 0 ) {
                    if ( bGhostCursorVisible ) {
                        bGhostCursorVisible = false;
                        ghostClip.setProperty( eFlashProperty::FP_Visible,
                                               false );
                    }
                } else {
                    float ghost[2] = { 0.0f, 0.0f };
                    ProjectToMapScreen( cursorPosition, nullptr, ghost[0],
                                        ghost[1] );
                    SetMapClipPosition( "/curseur_ghost", ghost, nullptr,
                                        nullptr );
                    if ( !bGhostCursorVisible ) {
                        bGhostCursorVisible = true;
                        ghostClip.setProperty( eFlashProperty::FP_Visible,
                                               true );
                    }
                }
            }

            const bool bLockedRoadUnderCursor =
                mngSpots.isInsideMapRect( cursorPosition ) && roadResult == 2 &&
                !gpScenaric->getIntroTutorial().isActive();
            pMapPlayer->showFrame( bLockedRoadUnderCursor ? "on" : "off",
                                   "/curseur/cible", false );

            const Eigen::Vector2f cursor2D( cursorX, cursorY );
            MapSpotRecord *pHoveredRecord = nullptr;
            if ( mngSpots.findRecordUnderCursor( &cursor2D,
                                                 &pHoveredRecord ) ) {
                leaveFreeMode( pHoveredRecord );
            }
            return;
        }
    }

    if ( bGhostCursorVisible && bHasGhostClip ) {
        bGhostCursorVisible = false;
        ghostClip.setProperty( eFlashProperty::FP_Visible, false );
    }

    if ( !pCameraGPS->isInTransition() ) {
        return;
    }

    const float progress = pCameraGPS->getTransitionProgress();
    if ( zoomCursorX <= 0.0f || zoomCursorY <= 0.0f ) {
        if ( zoomOutCursorPosition.x() == 0.0f ||
             zoomOutCursorPosition.y() == 0.0f ||
             zoomOutCursorPosition.z() == 0.0f ) {
            cursorX = kMapFocusDefault;
            cursorY = kMapFocusDefault;
        } else {
            float x = 0.0f;
            float y = 0.0f;
            if ( ProjectToMapScreen( zoomOutCursorPosition, nullptr, x, y ) ) {
                cursorX = x;
                cursorY = y;
            }
        }
    } else {
        cursorX =
            ( 1.0f - progress ) * zoomCursorX + progress * kMapFocusDefault;
        cursorY =
            ( 1.0f - progress ) * zoomCursorY + progress * kMapFocusDefault;
    }
    const float cursor[2] = { cursorX, cursorY };
    SetMapClipPosition( "/curseur", cursor, nullptr, nullptr );
}

void GMMap::drawDebug( float /*deltaTime*/ ) {
    ImGui::Checkbox( "Free teleport", &gbMapDebugTeleportAnywhere );

    // FUN_0074d360
    ImGui::Checkbox( "Display Zones", &gDisplayDemoZones );
    ImGui::Checkbox( "Unlock demo roads again", &gbUnlockDemoRoadsAgain );
    ImGui::DragFloat( "Min OffSet X", &gDemoMinOffsetX, kDemoOffsetStep, 0.0f,
                      kDemoOffsetMax );
    ImGui::DragFloat( "Min OffSet Z", &gDemoMinOffsetZ, kDemoOffsetStep, 0.0f,
                      kDemoOffsetMax );
    ImGui::DragFloat( "Max OffSet X", &gDemoMaxOffsetX, kDemoOffsetStep, 0.0f,
                      kDemoOffsetMax );
    ImGui::DragFloat( "Max OffSet Z", &gDemoMaxOffsetZ, kDemoOffsetStep, 0.0f,
                      kDemoOffsetMax );
}

static void PushDemoZoneQuad( DrawList &drawList,
                              const float minX,
                              const float maxX,
                              const float minZ,
                              const float maxZ,
                              const float height,
                              const uint32_t color ) {
    const Eigen::Vector4f corners[4] = {
        Eigen::Vector4f( maxX, height, minZ, 1.0f ),
        Eigen::Vector4f( minX, height, minZ, 1.0f ),
        Eigen::Vector4f( maxX, height, maxZ, 1.0f ),
        Eigen::Vector4f( minX, height, maxZ, 1.0f ),
    };

    float screen[4][2] = {};
    for ( int32_t i = 0; i < 4; i++ ) {
        ProjectToMapScreen( corners[i], nullptr, screen[i][0], screen[i][1] );
    }

    drawList.beginPrimitive( PT_TriangleStrip, 4 );
    drawList.setActiveMaterial( gpFlash->getNullMaterial() );
    drawList.setDiffuse( 0, ColorRGBA( color ) );
    for ( int32_t i = 0; i < 4; i++ ) {
        drawList.pushVertex( screen[i][0], screen[i][1], 0.0f );
    }
    drawList.commitPrimitive();
}

void GMMap::drawDemoDebug() {
    // FUN_009bef90
    if ( gbUnlockDemoRoadsAgain ) {
        SavedRoadDiscovery *pRoads =
            gpPlayerData->getLoadedSave()->getRoadDiscovery();
        pRoads->clear();
        pRoads->unlockAllRoads();
        gbUnlockDemoRoadsAgain = false;
    }

    if ( !gDisplayDemoZones ) {
        return;
    }

    demoZonesDrawList.reset();

    const SectorManager &sectorManager =
        gpWorld->getStreamingManager()->sectorManager;
    const float mapWidth = static_cast<float>(
        static_cast<int32_t>( sectorManager.numSectors * 0x300 ) );
    const float mapDepth = static_cast<float>(
        static_cast<int32_t>( sectorManager.numSubSectors * 0x300 ) );

    PushDemoZoneQuad( demoZonesDrawList, mapWidth * kMapNumSectorRows,
                      mapWidth * kMapNumSectorColumns,
                      mapDepth * kDemoZoneMinRow, mapDepth * kMapNumSectorRows,
                      kDemoZoneOuterHeight, 0xb4ff0000 );
    PushDemoZoneQuad(
        demoZonesDrawList, mapWidth * kMapNumSectorRows + gDemoMinOffsetX,
        mapWidth * kMapNumSectorColumns - gDemoMaxOffsetX,
        mapDepth * kDemoZoneMinRow + gDemoMinOffsetZ,
        mapDepth * kMapNumSectorRows - gDemoMaxOffsetZ, 0.0f, 0xb40000ff );
    for ( const float ( &zone )[4] : kDemoFordZones ) {
        PushDemoZoneQuad( demoZonesDrawList, zone[0], zone[0] + zone[2],
                          zone[1], zone[1] + zone[3], 0.0f, 0xb40000ff );
    }
}
