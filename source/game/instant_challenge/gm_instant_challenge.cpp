#include "shared.h"
#include "gm_instant_challenge.h"

#include "ai/ai_config.h"
#include "ai/ai_road_map.h"
#include "ai/ai_road_shape.h"
#include "config/gs_config.h"
#include "core/maths_helper.h"
#include "flash/gs_flash.h"
#include "flash/modal_message_box.h"
#include "flash/movie_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/scenaric/entity/entity_check_point.h"
#include "input/input_device.h"
#include "player_data/achievements_data.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_road_discovery.h"
#include "render/render_scene.h"
#include "flash/implementation/flash_resource.h"
#include "filesystem/gs_file.h"
#include "flash/implementation/flash_player.h"
#include "game/game_cheats.h"
#include "game/game_mechanism.h"
#include "game/mng_hud.h"
#include "flash/mng_fade.h"
#include "imgui.h"
#include "game/map/gm_map.h"
#include "game/scenaric/mng_actor.h"
#include "database/gs_database.h"
#include "world/mng_sector_name.h"
#include "game/mng_music.h"
#include "game/scenaric/gs_scenaric.h"
#include "player_data/driver.h"
#include "player_data/saved_extra_challenges.h"
#include "game/freeride/mng_fr_meeting_ic.h"
#include "system/gs_pc_options.h"
#include "world/gs_world_streaming.h"
#include "game/scenaric/spots.h"
#include "game/scenaric/ige_file.h"
#include "game/vehicle/car_state.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/camera/camera_gps.h"
#include "render/camera/gs_camera.h"
#include "render/gs_render.h"
#include "tdu_instance.h"
#include "world/gs_world.h"
#include "world/mng_world.h"
#include "world/streaming_manager.h"

static constexpr float kEditorLODScale = 0.02f; // DAT_00fab8f4
static constexpr uint64_t kSavingPopupHashcode = 0x37ba8e2;
static constexpr uint64_t kWrongVehiclePromptHashcode = 0x3c3ae62;
static constexpr float kCursorCenter = 0.5f;            // FLOAT_00f41674
static constexpr float kTestScoreToSpeedScale = 0.001f; // DAT_00f4167c
static constexpr float kMetersPerSecondToKmh = 3.6f;    // DAT_00f41768
static constexpr float kSpeedParameterScale = 100.0f;   // DAT_00f41708
static constexpr int32_t kGPSCameraMode = 3;
static constexpr int32_t kMaxNumCheckpoints = 48;
static constexpr int32_t kMaxNumSpeedCheckpoints = 8;
static constexpr int32_t kCheckpointStart = 1;
static constexpr int32_t kCheckpointFinish = 2;
static constexpr int32_t kCheckpointRegular = 3;
static constexpr int32_t kCheckpointRadar = 4;
static constexpr int32_t kMaxNumRadars = 8;
static constexpr float kICMinRouteLength = 1.607f; // DAT_00f41964
static constexpr float kICMaxRouteLength = 5.347f; // DAT_00f41960
static constexpr float kCursorDeadEndDistance = 15.0f;
static constexpr float kCursorMaxRoadDistanceSq = 10000.0f; // DAT_00f416c4
static constexpr float kNoCheckpointDistanceSq = 1.0e9f;    // DAT_00f419ac
static constexpr float kNearCheckpointScreenSq = 0.0049f;   // DAT_00f42160
static constexpr float kHoverCheckpointScreenSq = 0.0016f;  // DAT_00f4215c
static constexpr float kMaxCameraPitch = 1.0471976f;        // DAT_00f417dc
static constexpr float kCameraRotateSpeed = 1.5707964f;     // DAT_00f418a4
static constexpr float kTwoPi = 6.2831855f;                 // DAT_00f416f8
static constexpr float kStartGridCarOffset = 5.0f;          // DAT_00f41750
static constexpr uint32_t kGameModeTypeDBIndex = 0xb;
static constexpr uint64_t kSoloTypeHashcode = 0x36c6aa9;
static constexpr uint64_t kMultiTypeHashcode = 0x37bace9;
static char gGMESelectedFileName[9] = {};                   // DAT_010e7ce4
static constexpr float kQuitFocusMoveDistance = 10.0f;      // FLOAT_00f4166c
static Matrix4x4f_t gGMEReturnFrame = Matrix4x4f_t::Zero(); // DAT_01697af0
static constexpr float kICSyncDelay = 7.0f;                 // DAT_00f41844
static constexpr float kICStartDeadEndDistance = 80.0f;
static int32_t gICRouteDirty = 0; // DAT_010e7cf0
static constexpr int32_t kCursorClipScale = 75;
static constexpr int32_t kMaxNumMapIcons = 50;
static constexpr int32_t kMaxNumMultiStarts = 4;
static constexpr float kStartArrowDistance = 10.0f; // FLOAT_00f4166c
static constexpr int32_t kStartArrowFrameBase = 2;  // DAT_00fab8f8
static constexpr float kStartArrowFramesPerRadian = -15.756339f; // DAT_00f41fac
static constexpr float k169HeightScale = 1.3333f;                // DAT_00f41954
static constexpr float k169HeightOffset = 0.16667f;              // DAT_00f41950
static constexpr float kCameraPanSpeed = 2142.857f;              // DAT_00f41ec8
static constexpr float kGhostCursorMaxDistance = 100.0f;         // DAT_00f41708
static constexpr float kGhostCursorScaleSlope = -50.0f;          // DAT_00f41cb8
static constexpr float kGhostCursorMaxScale = 75.0f;             // DAT_00f41830
static constexpr float kGhostCursorMinScale = 25.0f;             // DAT_00f41834
static constexpr float kMapPanDeadZone = 0.3f;

static constexpr FlashMessage kMsgCursorOff = "mcof";
static constexpr FlashMessage kMsgCursorOn = "mcon";
static constexpr FlashMessage kMsgEmin = "emin";
static constexpr FlashMessage kMsgDeleteCheckpoint = "delc";
static constexpr FlashMessage kMsgKeyboard = "clav";
static constexpr FlashMessage kMsgGMDescription = "degm";
static constexpr FlashMessage kMsgDeletePath = "delp";
static constexpr FlashMessage kMsgEditChallenge = "edch";
static constexpr FlashMessage kMsgCheckpoint = "chkp";
static constexpr FlashMessage kMsgCheckpointInsert = "chki";
static constexpr FlashMessage kMsgCanp = "canp";
static constexpr FlashMessage kMsgMapB = "mapb";
static constexpr FlashMessage kMsgMapL = "mapl";
static constexpr FlashMessage kMsgLoop = "loop";
static constexpr FlashMessage kMsgIcsp = "icsp";
static constexpr FlashMessage kMsgInsp = "insp";
static constexpr FlashMessage kMsgICOk = "icok";
static constexpr FlashMessage kMsgGoFreeRide = "gofr";
static constexpr FlashMessage kMsgGoto = "goto";
static constexpr FlashMessage kMsgGoChallenge = "goch";
static constexpr FlashMessage kMsgEmou = "emou";
static constexpr FlashMessage kMsgErch = "erch";
static constexpr FlashMessage kMsgFinish = "fish";
static constexpr FlashMessage kMsgMovp = "movp";
static constexpr FlashMessage kMsgNextName10 = "ne10";
static constexpr FlashMessage kMsgNextName = "nena";
static constexpr FlashMessage kMsgNextDescription = "nede";
static constexpr FlashMessage kMsgPreviousDescription = "prde";
static constexpr FlashMessage kMsgPreviousName = "prna";
static constexpr FlashMessage kMsgPreviousName10 = "pr10";
static constexpr FlashMessage kMsgPrva = "prva";
static constexpr FlashMessage kMsgNeva = "neva";
static constexpr FlashMessage kMsgNewChallenge = "nech";
static constexpr FlashMessage kMsgPauseIn = "pain";
static constexpr FlashMessage kMsgPauseOut = "paou";
static constexpr FlashMessage kMsgQuit = "quit";
static constexpr FlashMessage kMsgRestart = "rest";
static constexpr FlashMessage kMsgPlca = "plca";
static constexpr FlashMessage kMsgReverse = "sens";
static constexpr FlashMessage kMsgQueryModified = "qugm";
static constexpr FlashMessage kMsgSave = "save";
static constexpr FlashMessage kMsgSbdd = "sbdd";
static constexpr FlashMessage kMsgZoomDefault = "zoed";
static constexpr FlashMessage kMsgSetp = "setp";
static constexpr FlashMessage kMsgTest = "test";
static constexpr FlashMessage kMsgZoomIn = "zoin";
static constexpr FlashMessage kMsgZoomOut = "zoou";

static bool IsBookmarkMessage( const FlashMessage message ) {
    return message == FlashMessage( "boou" ) ||
           message == FlashMessage( "afbo" ) ||
           message == FlashMessage( "boin" ) ||
           message == FlashMessage( "bona" ) ||
           message == FlashMessage( "newb" );
}

static void SetSpotStreamEnabled( const bool bEnabled ) {
    SpotStream &spotStream = gpWorld->getStreamingManager()->spotStream;
    spotStream.bStream = bEnabled;
    spotStream.bDraw = bEnabled;
}

static int32_t GetCheckpointType( const EntityCheckPoint *pCheckpoint ) {
    return int32_t( pCheckpoint->getLEDData()->Type );
}

static bool IsCheckpoint( const Entity *pEntity ) {
    return pEntity != nullptr && pEntity->getType() == eEntityType::ET_CHPT;
}

static FlashPlayer *GetHudPlayer() {
    return ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;
}

// Flash movie coordinates to relative screen coordinates.
static void FlashRectToViewport( FlashPlayer *pPlayer,
                                 float &x,
                                 float &y,
                                 float &aspectRatio,
                                 float &height,
                                 float &width ) {
    // FUN_00781820
    const VMFRect &frameSize =
        pPlayer->getBoundFlashResource()->getHeader()->FrameSize;
    const float movieWidth = float( int32_t( frameSize.XMax ) / 20 );
    const float movieHeight = float( int32_t( frameSize.YMax ) / 20 );
    aspectRatio = width / height;
    x /= movieWidth;
    y /= movieHeight;
    width /= movieWidth;
    height /= movieHeight;
    if ( gpConfig->b169Ratio ) {
        y = y * k169HeightScale - k169HeightOffset;
        height *= k169HeightScale;
        aspectRatio *= k169HeightScale;
    }
}

// Position of the index-th "SPOT" entity of a bank; kind 'a' matches any
// spot, otherwise name[7] must match.
static char FindSpotPosition( const ScenaricBank *pBank,
                              const int32_t index,
                              const char kind,
                              Eigen::Vector4f &outPosition ) {
    // FUN_00803ca0
    int32_t numFound = 0;
    for ( int32_t i = 0; i < pBank->getEntityCount(); i++ ) {
        const Entity *pEntity = pBank->getEntity( i );
        const char *pName = pEntity->getInstanceName().c_str();
        if ( strncmp( pName, "SPOT", 4 ) != 0 ) {
            continue;
        }
        if ( kind == 'a' || kind == pName[7] ) {
            numFound++;
        }
        if ( numFound == index + 1 ) {
            const SerializedTransform &transform =
                pEntity->getDesc()->Transform;
            outPosition =
                Eigen::Vector4f( transform.PositionX, transform.PositionY,
                                 transform.PositionZ, 1.0f );
            return pName[7];
        }
    }
    return '\0';
}

static float ReadMapPanAction( DeviceAction *pAction ) {
    if ( pAction == nullptr || pAction->NumAddedChannels == 0 ) {
        return 0.0f;
    }
    return pAction->FUN_00434bc0( kMapPanDeadZone, 1.0f );
}

static PhysicsVehicle *GetLocalPlayerPhysicsVehicle() {
    CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
    return ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;
}

GMInstantChallenge::GMInstantChallenge()
    : GameModeWorld( 5 ),
      missionDescription(),
      mngCursor(),
      mngPreview(),
      mngGMEFlshI(),
      mngTracRoad(),
      mngBookmark(),
      mngCamGPS(),
      mngMapViewer(),
      field_0x1115b0(),
      checkpointBank(),
      pGPSCamera( nullptr ),
      pCheckpointTemplate( nullptr ),
      pLastCheckpoint( nullptr ),
      numWaypoints( 0 ),
      editorState( 0 ),
      bEditorInputEnabled( false ),
      bGhostCursorVisible( false ),
      bToolActive( false ),
      bCursorVisible( true ),
      bTestLevelLoaded( false ),
      bZoomPending( false ),
      field_0x114d90( 0 ),
      field_0x114d94( 0 ),
      bSaveRequested( false ),
      bSaving( false ),
      hoverFlags( 0 ),
      pHoveredCheckpoint( nullptr ),
      pSelectedCheckpoint( nullptr ),
      cursorLocation(),
      pCursorProfile( nullptr ),
      hoveredLeg( -1 ),
      bChallengeValid( false ),
      field_0x114e9c{},
      field_0x114ebc{},
      field_0x114ec0{},
      bKeyPitchDown( false ),
      bKeyPitchUp( false ),
      bKeyRotateLeft( false ),
      bKeyRotateRight( false ),
      bEdgeScrollEnabled( false ),
      bCursorAtEdge( false ),
      deroulFrame{},
      parameterList(),
      challengeList(),
      pEditorPlayer( nullptr ),
      editTool( kToolPlaceCheckpoint ),
      moveMode( 0 ),
      routeLength( 0.0f ),
      parameterCategory( 0 ),
      bFinishPlaced( false ),
      bFinishRequested( false ),
      gmDescription{},
      exitFrame( Matrix4x4f_t::Identity() ),
      nextGameMode( GM_Invalid ),
      pICRoute( nullptr ),
      bICRouteReady( false ),
      bOpponentIsBot( false ),
      field_0x11521c( 0 ),
      icSyncTimer( 0.0f ),
      field_0x115224( 1 ) {
    // FUN_0078ec00
    OTDU_UNIMPLEMENTED; // TODO: ONLINE
    parameterList.setOwner( this );
    challengeList.setOwner( this );

    registerManager( &mngCursor );
    registerManager( &mngPreview );
    registerManager( &mngGMEFlshI );
    registerManager( &mngTracRoad );
    registerManager( &mngBookmark );
    registerManager( &mngCamGPS );
    registerManager( &mngMapViewer );
    mngMapViewer.setSyncSpotsFocus( false );
    mngMapViewer.setState( MngMapViewer::MS_Active );

    mngCamGPS.setDrawEarth( false );
    mngBookmark.setMngFlash( &mngFlash );
    setTransitionFlags( 0x21 );
    mngBookmark.setGME( true );
    mngFlash.setWantsAsyncInstall( true );
    mngGMEFlshI.setOwner( this );
    mngTracRoad.setOwner( this );

    gameMechType = 5;
    gameMechID = 0xc0c0;

    gpCamera->setUseBlur( true );
}

GMInstantChallenge::~GMInstantChallenge() {
    // FUN_0078ea80
    gpCamera->setUseBlur( false );
}

EventParameters *GMInstantChallenge::getMissionDescription() {
    // FUN_0042c640
    return &missionDescription;
}

void GMInstantChallenge::initialize() {
    // FUN_007900b0
    GameModeWorld::initialize();

    gpScenaric->loadPackBank( "Scenarics/Packs", "pack_cp_virtual",
                              &checkpointBank );

    Viewport *pBackViewport = gpRender->getRenderPass<RP_Back>().pViewport;
    pBackViewport->ClearColor.R = 0xa0;
    pBackViewport->ClearColor.G = 0xbc;
    pBackViewport->ClearColor.B = 0xde;
    pBackViewport->ClearColor.A = 0xff;

    gpScenaric->setCheckpointBank( &checkpointBank );
    gpMngMusic->setPlayFlags( 1 );
    gAIConfig.bEnableBots = true;
}

void GMInstantChallenge::reset() {
    // FUN_0078fbe0
    gpFlash->setLevelMoviesFlag( 1, 0, 1, true );
    bSaveRequested = false;
    gpPhysics->setPlayerSlowDownForced( true );
    GameModeWorld::reset();
    gpFlash->getMouse()->field12_0x30 = false;
    parkLocalPlayerCar();
    bEditorInputEnabled = false;

    missionDescription.getGameTypeParameter().setValue( 0, false );
    missionDescription.getSpotTypeParameter().setValue(
        EventParameters::EST_Solo, false );
    mngCursor.setViewport( gpRender->getRenderPass<RP_Near>().pViewport );
    bICRouteReady = false;

    Eigen::Vector4f startPosition = Eigen::Vector4f::Zero();
    if ( !gpScenaric->isInstantChallenge() ) {
        startPosition = gpScenaric->getGMEStartPosition();
        bCursorVisible = false;
    } else {
        const ICEChallenger &opponent = gpScenaric->getICEChallengers()[0];
        bOpponentIsBot = opponent.bBot;
        bCursorVisible = true;
        createGameMech( opponent.bBot ? 0xc0c0 : 0xc0c1, 3 );
        missionDescription.getGameTypeParameter().setValue(
            EventParameters::EGT_Race, false );
        missionDescription.getSpotTypeParameter().setValue(
            int32_t( gameMechID ), false );
        initInstantChallengeRoute();
        if ( pICRoute != nullptr ) {
            startPosition = pICRoute->StartPosition;
        }
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (CP2PGameService vtbl +0x10,
                            // GM +0x103230 observer)
    }

    if ( startPosition.x() == 0.0f && startPosition.y() == 0.0f &&
         startPosition.z() == 0.0f ) {
        FindSpotPosition( gpScenaric->getWorldScenaricBank(), 0, 'a',
                          startPosition );
    }

    pGPSCamera = static_cast<CameraGPS *>( gpCamera->findCameraWithMode(
        kGPSCameraMode, gpCamera->getBoundCarState() ) );
    if ( gpTestDriveInstance->getPreviousGameMode() != GM_FreeRide ) {
        pGPSCamera->startZoom( pGPSCamera->getZoom(), 5 );
    }

    mngMapViewer.setFocus( startPosition );
    gpCamera->requestCameraMode( kGPSCameraMode, 0.0f );
    gpWorld->setStreamingPosition( &startPosition );
    gpWorld->getStreamingManager()->setUnderwaterTilesRefined( true );
    editorState = kEditorStateEditing;

    if ( gpScenaric->getTestIGEBuffer() != nullptr ) {
        restoreTestedChallenge();
    }

    setFireFrame( pEditorPlayer );
    refreshEditorState();

    SetSpotStreamEnabled( false );
    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( true );
    }

    PhysicsVehicle *pPhysicsVehicle = GetLocalPlayerPhysicsVehicle();
    if ( pPhysicsVehicle != nullptr ) {
        if ( gpTestDriveInstance->getPreviousGameMode() == GM_FreeRide ) {
            gGMEReturnFrame = pPhysicsVehicle->getChassisWorldMatrix();
        }
        PhysicsLock physLock;
        pPhysicsVehicle->setPostponedPause( 1 );
    }
}

void GMInstantChallenge::tick( float deltaTime, float totalTime ) {
    // FUN_0078f870
    GameModeWorld::tick( deltaTime, totalTime );

    if ( pEditorPlayer != nullptr ) {
        pEditorPlayer->setVariableValue( "/:IsTestPossible",
                                         int32_t( bChallengeValid ) );
        setFireFrame( pEditorPlayer );
    }

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->resetHeightmapLODCoefs( 7 );
    pStreaming->scaleHeightmapLODCoefs( 7, kEditorLODScale );

    if ( gpScenaric->isInstantChallenge() && totalTime > 0.0f &&
         !gpScenaric->getICEChallengers()[0].bBot ) {
        FUN_00788c80( deltaTime );
    }

    clearEditorKeys();
    updateCameraControl( deltaTime );
    if ( editorState == kEditorStateEditing ) {
        updateCursorHover();
    }
    updateCursorClips();

    if ( gpScenaric->isInstantChallenge() && bICRouteReady ) {
        updateInstantChallengeRoute();
    }

    if ( gbGMEDesigner ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: DEBUG (FUN_00457290 at 10,10)
    }

    if ( bZoomPending && pGPSCamera->zoomIn() ) {
        bZoomPending = false;
    }

    if ( bSaveRequested ) {
        bSaveRequested = false;
        requestSave();
    }

    updateSave();

    if ( nextGameMode != GM_Invalid ) {
        updatePendingExit();
    }
}

void GMInstantChallenge::draw( float /*deltaTime*/, float /*totalTime*/ ) {
    // FUN_0078d3f0
    if ( pGameMech != nullptr ) {
        pGameMech->draw();
    }
    mngGMEFlshI.drawInterface();
}

void GMInstantChallenge::pause() {
    // FUN_0078f2e0
    gpFlash->setLevelMoviesFlag( 1, 0, 1, true );

    PhysicsVehicle *pPhysicsVehicle = GetLocalPlayerPhysicsVehicle();
    if ( pPhysicsVehicle != nullptr &&
         gpTestDriveInstance->getActiveGameMode() != GM_Mission ) {
        PhysicsLock physLock;
        pPhysicsVehicle->setPostponedPause( 2 );
    }

    GameModeWorld::pause();
    restoreLocalPlayerCarControl();

    SetSpotStreamEnabled( true );
    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( false );
    }
    gpRender->updateViewportDimScalars( 0.0f, 0.0f, 1.0f, 1.0f );
    gpPhysics->setPlayerParkedForced( false );
    gpPhysics->setPlayerSlowDownForced( false );
    gpFlash->setUseStick( true );
    mngFlash.removeWidget( &challengeList );
    mngFlash.removeWidget( &parameterList );

    if ( gpScenaric->isInstantChallenge() ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (CP2PGameService vtbl +0x10)
        gpScenaric->setInstantChallenge( false );
        gpPCOptions->setMainMenuAllowed( true );
    }

    if ( gpWorldStreaming->isStreamingActive() ) {
        gpWorldStreaming->setStreamCenter( nullptr );
    }
    gpWorld->getStreamingManager()->resetHeightmapLODCoefs( 7 );
    gpWorld->getStreamingManager()->setUnderwaterTilesRefined( false );

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bNeedStreamUpdate = true;
    pStreaming->sectorManager.bDrawSector = false;
    pStreaming->areaGrid.bNeedStreamUpdate = true;
    pStreaming->areaGrid.bDrawArea = false;
    pStreaming->setActiveRenderLayers( 1 );
    pStreaming->sectorManager.bStreamSector = true;
    gpWorldStreaming->updateStreaming();
    pStreaming->areaGrid.bStreamArea = true;

    if ( pGPSCamera != nullptr ) {
        pGPSCamera->reset( false );
    }
}

void GMInstantChallenge::destroy() {
    // FUN_0078f280
    gAIConfig.bEnableBots = false;
    gpScenaric->releaseIGEBuffer();
    gpScenaric->setCheckpointBank( nullptr );

    Viewport *pBackViewport = gpRender->getRenderPass<RP_Back>().pViewport;
    pBackViewport->ClearColor.R = 0x00;
    pBackViewport->ClearColor.G = 0x00;
    pBackViewport->ClearColor.B = 0x00;
    pBackViewport->ClearColor.A = 0xff;

    gpScenaric->unloadBank( &checkpointBank );
    GameModeWorld::destroy();
}

void GMInstantChallenge::drawDebug( float /*deltaTime*/ ) {
    bool bShowPreview = !mngPreview.isHidden();
    if ( ImGui::Checkbox( "Show cut checkpoint preview", &bShowPreview ) ) {
        if ( bShowPreview ) {
            // The movie writes the thumbnail rect (GMEDITOR DoAction #43)
            // but never sends `mapb` again afterwards, so apply it here.
            FlashPlayer *pHudPlayer = GetHudPlayer();
            if ( pHudPlayer != nullptr ) {
                float x = 0.0f;
                float y = 0.0f;
                float width = 0.0f;
                float height = 0.0f;
                pHudPlayer->getVariableAsFloat( "/:ViewPort_x", x );
                pHudPlayer->getVariableAsFloat( "/:ViewPort_y", y );
                pHudPlayer->getVariableAsFloat( "/:ViewPort_width", width );
                pHudPlayer->getVariableAsFloat( "/:ViewPort_height", height );
                if ( width == 0.0f || height == 0.0f ) {
                    x = 983.0f;
                    y = 330.5f;
                    width = 256.0f;
                    height = 197.0f;
                }
                float aspectRatio = width / height;
                FlashRectToViewport( pHudPlayer, x, y, aspectRatio, height,
                                     width );
                mngPreview.setViewportRect( x, y, width, height, aspectRatio );
            }
            mngPreview.show();
        } else {
            mngPreview.hide();
        }
    }
}

struct GameModeInfo {
    const char *pName;
    uint64_t Hashcode;
    int32_t Index;
};

static const GameModeInfo kGameModesInfos[] = {
    // DAT_00f68bf8
    {"timeAttack", 0x340be70, 0},
    {"speed",      0x35000b0, 1},
    {"driving",    0x35f42f0, 2},
    {"race",       0x36e8530, 3},
};

static int32_t GetGameModeInfoIndex( const char *pName ) {
    // FUN_009afa70
    for ( const GameModeInfo &info : kGameModesInfos ) {
        if ( strcasecmp( pName, info.pName ) == 0 ) {
            return info.Index;
        }
    }
    return -1;
}

static int32_t GetGameModeInfoIndexOrZero( const char *pName ) {
    // FUN_009afc00
    const int32_t index = GetGameModeInfoIndex( pName );
    return ( index == -1 ) ? 0 : index;
}

static uint64_t GetGameModeInfoHashcode( const char *pName ) {
    // FUN_009afb10
    for ( const GameModeInfo &info : kGameModesInfos ) {
        if ( strcasecmp( pName, info.pName ) == 0 ) {
            return info.Hashcode;
        }
    }
    return 0;
}

static int32_t GetSpotTypeFromName( const char *pName ) {
    // FUN_009afc20
    switch ( FlashMessage( pName ) ) {
    case FlashMessage( "club" ):
        return 0xc0c2;
    case FlashMessage( "mult" ):
        return EventParameters::EST_Multi;
    default:
        return EventParameters::EST_Solo;
    }
}

static const GameModeInfo kGameModesGSInfos[] = {
    // DAT_00f68d48
    {"timeAttack",  0x341cbb9, 1 },
    {"speed",       0x3510df9, 0 },
    {"speedTrap",   0x3605039, 2 },
    {"cats",        0x36f9279, 0 },
    {"driving",     0x37ed4b9, 3 },
    {"drivingRace", 0x38e16f9, 0 },
    {"teamRace",    0x39d5939, 0 },
    {"hotSpots",    0x3ac9b79, 8 },
    {"pursuit",     0x3bbddb9, 0 },
    {"ctf",         0x3341019, 11},
    {"special",     0x3435259, 0 },
    {"race",        0x3529499, 5 },
};

template <size_t N>
static const char *FindGameModeName( const GameModeInfo ( &infos )[N],
                                     const int32_t index ) {
    // FUN_009af930
    for ( const GameModeInfo &info : infos ) {
        if ( info.Index == index ) {
            return info.pName;
        }
    }
    return nullptr;
}

static const char *GetGameModeInfoName( const int32_t index ) {
    return FindGameModeName( kGameModesInfos, index );
}

const char *GMInstantChallenge::GetGameModeName( const int32_t index ) {
    return GetGameModeInfoName( index );
}

const char *GMInstantChallenge::GetGameModeGSName( const int32_t index ) {
    return FindGameModeName( kGameModesGSInfos, index );
}

void GMInstantChallenge::SetSelectedFileName( const char *pFileName ) {
    strncpy( gGMESelectedFileName, pFileName, 8 );
}

static char gGMETestMenu[0x20] = {};      // DAT_010e7c9c
static char gGMETestZoomLevel[0x20] = {}; // DAT_010e7cbc
static int32_t gGMETestStep = 0;          // DAT_010e7cdc

void GMInstantChallenge::onResourceStreamed() {
    // FUN_0078def0
    GameModeWorld::onResourceStreamed();
    pEditorPlayer = GetHudPlayer();
    pGPSCamera->updateFlashZoom( pEditorPlayer );
    mngBookmark.setFlashPlayer( pEditorPlayer );
    setupEditorFlash();
}

void GMInstantChallenge::setupEditorFlash() {
    // FUN_0078ae60
    if ( pEditorPlayer == nullptr ) {
        return;
    }

    GameParameterDBString &databaseName = missionDescription.getDatabaseName();
    pEditorPlayer->setVariableValue( "/:ChallengeName",
                                     ( int32_t( databaseName.getValue() ) < 0 )
                                         ? missionDescription.getNameBuffer()
                                         : databaseName.getDisplayText() );
    GameParameterDBString &databaseDescription =
        missionDescription.getDatabaseDescription();
    pEditorPlayer->setVariableValue(
        "/:ChallengeDescription",
        ( int32_t( databaseDescription.getValue() ) < 0 )
            ? missionDescription.getDescriptionBuffer()
            : databaseDescription.getDisplayText() );

    if ( gpScenaric->isInstantChallenge() ) {
        setupInstantChallengeFlash();
    }

    if ( bTestLevelLoaded ) {
        bTestLevelLoaded = false;
        const int32_t gameType = missionDescription.getGameType();
        pEditorPlayer->setVariableValue(
            "/:WhatGM",
            ( gameType < 8 ) ? GetGameModeInfoName( gameType ) : nullptr );
        switch ( missionDescription.getSpotType() ) {
        case EventParameters::EST_Solo:
            pEditorPlayer->setVariableValue( "/:WhatGMType", "solo" );
            break;
        case EventParameters::EST_Multi:
            pEditorPlayer->setVariableValue( "/:WhatGMType", "multi" );
            break;
        case 0xc0c2:
            pEditorPlayer->setVariableValue( "/:WhatGMType", "club" );
            break;
        default:
            break;
        }
        pEditorPlayer->setVariableValue( "/:GMEMenu", gGMETestMenu );
        pEditorPlayer->setVariableValue( "/:Step", gGMETestStep );
        pEditorPlayer->setVariableValue( "/:ZoomLevel", gGMETestZoomLevel );
        pEditorPlayer->showFrame( "back_test", "/", true );
    }

    updateEditorFlashVars();
    pEditorPlayer->setVariableValue(
        "/:IsFinishLine", int32_t( missionDescription.getGameType() !=
                                   EventParameters::EGT_Speed ) );

    parameterList.setPlayer( pEditorPlayer, "/array" );
    parameterList.setEditorPlayer( pEditorPlayer );
    mngFlash.addWidget( "para", &parameterList );
    challengeList.setPlayer( pEditorPlayer, "/array_list" );
    challengeList.setEditorPlayer( pEditorPlayer );
    mngFlash.addWidget( "loch", &challengeList );
}

bool GMInstantChallenge::restart() {
    // FUN_0078c1b0
    FlashPlayer *pHudPlayer = GetHudPlayer();
    if ( pHudPlayer != nullptr ) {
        pHudPlayer->goToFrameAndPlay( 0, "/", false );
    }
    gpCamera->requestCameraMode( gpCamera->getActiveCamera()->getType(), 1.0f );
    return true;
}

bool GMInstantChallenge::onMessage( FlashMessage *pMessage,
                                    FlashPlayer *pPlayer ) {
    // FUN_0078b120
    const FlashMessage message = *pMessage;

    if ( message == kMsgCursorOff ) {
        mngCursor.setCursorOff( true );
        return true;
    }
    if ( message == kMsgCursorOn ) {
        mngCursor.setCursorOff( false );
        return true;
    }
    if ( message == kMsgEmin ) {
        onEditorMouseIn();
        return true;
    }
    if ( message == kMsgDeleteCheckpoint ) {
        deleteSelectedChallenge();
        return true;
    }
    if ( message == kMsgKeyboard ) {
        char keyboard[0x20] = {};
        pPlayer->getVariableAsString( "/:VariableClavier", keyboard );
        if ( int32_t(
                 missionDescription.getDatabaseDescription().getValue() ) >=
             0 ) {
            return true;
        }
        if ( keyboard[0] == 'd' ) {
            pPlayer->getVariableAsString(
                "/:TxtInput", missionDescription.getDescriptionBuffer() );
        } else if ( keyboard[0] == 'n' ) {
            pPlayer->getVariableAsString( "/:TxtInput",
                                          missionDescription.getNameBuffer() );
        }
        return true;
    }
    if ( message == kMsgGMDescription ) {
        char gmType[0x20] = {};
        pPlayer->getVariableAsString( "/:WhatGMType", gmType );
        const char *pTypeText = gpDatabase->getStringByHashcode(
            kGameModeTypeDBIndex, ( gmType[0] == 'c' || gmType[0] == 'm' )
                                      ? kMultiTypeHashcode
                                      : kSoloTypeHashcode );
        char gmName[0x20] = {};
        pPlayer->getVariableAsString( "/:WhatGM", gmName );
        const uint64_t nameHashcode = GetGameModeInfoHashcode( gmName );
        const char *pModeText =
            ( nameHashcode != 0 )
                ? gpDatabase->getStringByHashcode( 0, nameHashcode )
                : nullptr;
        snprintf( gmDescription, sizeof( gmDescription ), "%s %s", pModeText,
                  pTypeText );
        pPlayer->setVariableValue( "/:m_TextDescription", gmDescription );
        pPlayer->setVariableValue(
            "/:IsGMLocked",
            ( GetGameModeInfoIndexOrZero( gmName ) == 2 ) ? "2" : "1" );
        return true;
    }
    if ( message == kMsgDeletePath ) {
        onDeletePoint();
        return true;
    }
    if ( message == kMsgEditChallenge ) {
        pPlayer->setVariableValue( "/:IsOverwriteGME", "1" );
        pPlayer->setVariableValue( "/:IsMultiTab", "2" );
        loadSelectedChallenge();
        gpScenaric->setTestedLevel( missionDescription.getTest().getValue() !=
                                    0 );
        gpScenaric->setModifiedIGE( false );
        setFireFrame( pEditorPlayer );
        return true;
    }
    if ( IsBookmarkMessage( message ) ) {
        return mngBookmark.onMessage( pMessage );
    }
    if ( message == kMsgCanp ) {
        onCancelToStart();
        bCursorVisible = true;
        return true;
    }
    if ( message == kMsgCheckpoint || message == kMsgCheckpointInsert ) {
        bFinishRequested = false;
        if ( isCheckpointLimitReached() && editTool == kToolPlaceCheckpoint ) {
            pPlayer->setVariableValue( "/:IsSpotOK", "2" );
            return true;
        }
        if ( canPlaceAtCursor( false ) &&
             ( !bFinishPlaced || editTool == kToolMoveCheckpoint ||
               message == kMsgCheckpointInsert ) ) {
            pPlayer->setVariableValue( "/:IsSpotOK", "1" );
            return true;
        }
        pPlayer->setVariableValue( "/:IsSpotOK", "0" );
        return true;
    }
    if ( message == kMsgFinish ) {
        bFinishRequested = true;
        const bool bSpotOK = canPlaceAtCursor( false ) && numWaypoints > 0 &&
                             editTool == kToolPlaceCheckpoint;
        pPlayer->setVariableValue( "/:IsSpotOK", bSpotOK ? "1" : "0" );
        return true;
    }
    if ( message == kMsgMapB || message == kMsgMapL ) {
        onMapLayout();
        return true;
    }
    if ( message == kMsgLoop ) {
        onToggleLoop();
        return true;
    }
    if ( message == kMsgIcsp ) {
        onInstantChallengeSetPoint();
        return true;
    }
    if ( message == kMsgInsp ) {
        onInsertPoint();
        return true;
    }
    if ( message == kMsgICOk ) {
        if ( gpScenaric->isInstantChallenge() ) {
            leaveICESession();
        }
        return true;
    }
    if ( message == kMsgGoFreeRide ) {
        if ( gpScenaric->isInstantChallenge() ) {
            leaveICESession();
        }
        quitEditor( GM_FreeRide, gGMEReturnFrame );
        return true;
    }
    if ( message == kMsgGoto ) {
        return true;
    }
    if ( message == kMsgGoChallenge ) {
        if ( gpScenaric->isInstantChallenge() ) {
            finalizeRoute();
            launchInstantChallenge();
            return true;
        }
        if ( bChallengeValid ) {
            finalizeRoute();
            startTestDrive();
        }
        return true;
    }
    if ( message == kMsgEmou ) {
        onEditorMouseOut();
        return true;
    }
    if ( message == kMsgErch ) {
        onEraseChallenge();
        return true;
    }
    if ( message == kMsgMovp ) {
        onMovePoint();
        return true;
    }
    if ( message == kMsgNextName10 ) {
        linkTextInput( missionDescription.getDatabaseName().incrementBy( 10 ) );
        return true;
    }
    if ( message == kMsgNextName ) {
        linkTextInput( missionDescription.getDatabaseName().incrementBy( 1 ) );
        return true;
    }
    if ( message == kMsgNextDescription ) {
        linkTextInput(
            missionDescription.getDatabaseDescription().incrementBy( 1 ) );
        return true;
    }
    if ( message == kMsgPreviousDescription ) {
        linkTextInput(
            missionDescription.getDatabaseDescription().decrementBy( 1 ) );
        return true;
    }
    if ( message == kMsgPreviousName ) {
        linkTextInput( missionDescription.getDatabaseName().decrementBy( 1 ) );
        return true;
    }
    if ( message == kMsgPreviousName10 ) {
        linkTextInput( missionDescription.getDatabaseName().decrementBy( 10 ) );
        return true;
    }
    if ( message == kMsgPrva || message == kMsgNeva ) {
        return true;
    }
    if ( message == kMsgNewChallenge ) {
        pPlayer->setVariableValue( "/:IsOverwriteGME", "0" );
        pPlayer->setVariableValue( "/:IsMultiTab", "2" );
        pPlayer->setVariableValue( "/:GME_Help", " " );
        onNewChallenge();
        gpScenaric->setTestedLevel( false );
        gpScenaric->setModifiedIGE( false );
        if ( pEditorPlayer != nullptr ) {
            pEditorPlayer->showFrame( "red", "infos/fire", false );
        }
        return true;
    }
    if ( message == kMsgPauseIn ) {
        bPaused = true;
        return true;
    }
    if ( message == kMsgPauseOut ) {
        bPaused = false;
        char whereTo[0x20] = {};
        pPlayer->getVariableAsString( "/:WhereTo", whereTo );
        const FlashMessage destination( whereTo );
        if ( destination == kMsgQuit ) {
            quitEditor( GM_FreeRide, gGMEReturnFrame );
            return true;
        }
        if ( destination == kMsgRestart ) {
            return restart();
        }
        return true;
    }
    if ( message == kMsgPlca ) {
        onFrameRoute();
        bCursorVisible = false;
        return true;
    }
    if ( message == kMsgReverse ) {
        EntityCheckPoint *pStart = findStartCheckpoint();
        if ( pStart == nullptr ) {
            return true;
        }
        markEditorModified();
        flipCheckpoint( pStart );
        placeStartGridCar( findStartGridCar(), pStart );
        return true;
    }
    if ( message == kMsgRestart ) {
        return restart();
    }
    if ( message == kMsgQueryModified ) {
        pEditorPlayer->setVariableValue(
            "/:IsModified", gpScenaric->isModifiedIGE() ? "1" : "0" );
        return true;
    }
    if ( message == kMsgSave ) {
        missionDescription.getTest().setValue(
            int32_t( gpScenaric->isTestedLevel() ), false );
        const bool bSaved = onSave();
        pPlayer->setVariableValue( "/:SaveMessage",
                                   bSaved ? "SAVE SUCCESSFUL" : "SAVE FAILED" );
        pPlayer->setVariableValue( "/:IsSaveOver", "1" );
        return true;
    }
    if ( message == kMsgSbdd ) {
        pEditorPlayer->setVariableValue( "/:IsDesigner",
                                         gbGMEDesigner ? "1" : "0" );
        return false;
    }
    if ( message == kMsgZoomDefault ) {
        onCursorZoneLayout();
        return true;
    }
    if ( message == kMsgSetp ) {
        onSetPoint();
        return true;
    }
    if ( message == kMsgTest ) {
        if ( !bChallengeValid ) {
            return true;
        }
        if ( !isCurrentCarAllowed() ) {
            TestDrive::DisplayModalPrompt( false, kWrongVehiclePromptHashcode,
                                           0, nullptr, nullptr, nullptr, 0,
                                           nullptr, true, false, MPT_Warning,
                                           gpFlashMessageBox );
            return true;
        }
        finalizeRoute();
        startTestDrive();
        return true;
    }
    if ( message == kMsgZoomIn ) {
        onZoomIn();
        return true;
    }
    if ( message == kMsgZoomOut ) {
        onZoomOut();
        return true;
    }

    return false;
}

void GMInstantChallenge::linkTextInput( const char *pText ) {
    // FUN_0078b120 (inlined)
    pEditorPlayer->setVariableValue( "/:TxtInput", pText );
}

void GMInstantChallenge::setFireFrame( FlashPlayer *pPlayer ) {
    // FUN_0078b120 (inlined)
    if ( pPlayer == nullptr ) {
        return;
    }
    pPlayer->showFrame( gpScenaric->isTestedLevel() ? "green" : "red",
                        "infos/fire", false );
}

int32_t GMInstantChallenge::countParameters( const int32_t category ) {
    // FUN_0078bf90
    int32_t count = 0;
    for ( GameParameter *pParameter =
              missionDescription.getParameterList().getHead();
          pParameter != nullptr; pParameter = pParameter->getNext() ) {
        const bool bAvailable = missionDescription.isParameterAvailable(
            pParameter, parameterCategory );
        if ( category == pParameter->getCategory() && bAvailable ) {
            count++;
        }
    }
    return count;
}

GameParameter *GMInstantChallenge::findParameter( const int32_t index ) {
    // FUN_0078bf40
    int32_t count = 0;
    for ( GameParameter *pParameter =
              missionDescription.getParameterList().getHead();
          pParameter != nullptr; pParameter = pParameter->getNext() ) {
        if ( missionDescription.isParameterAvailable( pParameter,
                                                      parameterCategory ) &&
             pParameter->getCategory() == parameterCategory ) {
            if ( count == index ) {
                return pParameter;
            }
            count++;
        }
    }
    return nullptr;
}

bool GMInstantChallenge::isLocalPlayerRouteOwner() const {
    // FUN_0078bcd0
    if ( pICRoute != nullptr && !bOpponentIsBot ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (CP2PGameService vtbl +0x3c
                                 // local player index vs OwnerPlayerIndex)
        return true;
    }
    return true;
}

void GMInstantChallenge::clearEditorKeys() {
    // FUN_0078bc00
    memset( field_0x114e9c, 0, sizeof( field_0x114e9c ) );
    memset( field_0x114ebc, 0, sizeof( field_0x114ebc ) );
    bKeyPitchDown = false;
    bKeyPitchUp = false;
    bKeyRotateLeft = false;
    bKeyRotateRight = false;
}

bool GMInstantChallenge::canPlaceAtCursor( const bool bCheckRouteLength ) {
    // FUN_0078bdf0
    const AiRoadMap::Road *pRoad = cursorLocation.It.pRoad;
    if ( pRoad == nullptr ) {
        return false;
    }
    if ( pRoad->isOncomingOvertakeForbidden() ) {
        return false;
    }

    const int32_t zone = GetRoadZone( pRoad->ID );
    if ( gpScenaric->isInstantChallenge() ) {
        if ( zone != 0 ) {
            return false;
        }
    } else {
        const AchievementsData *pAchievements =
            gpPlayerData->getLoadedSave()->getAchievements();
        if ( zone == 1 && !pAchievements->isSimulationModeUnlocked() ) {
            return false;
        }
        if ( zone == 2 && !pAchievements->isReward0x5912a0Unlocked() ) {
            return false;
        }
    }

    const bool bToolReady =
        ( pCheckpointTemplate != nullptr &&
          editTool == kToolPlaceCheckpoint ) ||
        ( editTool == kToolMoveCheckpoint && pSelectedCheckpoint != nullptr );
    if ( !bToolReady || ( hoverFlags & kHoverOnRoad ) == 0 ) {
        return false;
    }
    if ( ( hoverFlags & kHoverNearCheckpoint ) != 0 &&
         pSelectedCheckpoint != pHoveredCheckpoint ) {
        return false;
    }

    const bool bRouteLengthOK =
        kICMinRouteLength <= routeLength && routeLength <= kICMaxRouteLength;
    return !gpScenaric->isInstantChallenge() || bRouteLengthOK ||
           !bCheckRouteLength;
}

bool GMInstantChallenge::isCheckpointLimitReached() {
    // FUN_0078bef0
    const int32_t gameType = missionDescription.getGameType();
    if ( numWaypoints > kMaxNumCheckpoints &&
         gameType != EventParameters::EGT_Speed ) {
        return true;
    }
    return numWaypoints > kMaxNumSpeedCheckpoints &&
           gameType == EventParameters::EGT_Speed;
}

static bool SnapAwayFromDeadEnd( AiRoadLocation &location,
                                 const float minDistance ) {
    // FUN_0078d430
    AiRoadMap::Road *pRoad = location.It.pRoad;
    if ( pRoad == nullptr ) {
        return false;
    }

    const AiRoadMap::Junction *pAhead =
        location.It.bWay ? pRoad->pJunctionEnd : pRoad->pJunctionBegin;
    if ( pAhead == nullptr ) {
        const float distance =
            ComputeRoadDistance( pRoad->getShape(), false, location.It ) -
            minDistance;
        if ( distance < 0.0f ) {
            location.It.advanceByDistance( distance, false );
            location.It.computeFrame( location.Frame, -1 );
            location.T = location.It.T;
            return true;
        }
    }

    const AiRoadMap::Junction *pBehind =
        location.It.bWay ? pRoad->pJunctionBegin : pRoad->pJunctionEnd;
    if ( pBehind == nullptr ) {
        const float distance =
            ComputeRoadDistance( pRoad->getShape(), true, location.It ) -
            minDistance;
        if ( distance < 0.0f ) {
            location.It.advanceByDistance( 0.0f - distance, false );
            location.It.computeFrame( location.Frame, -1 );
            location.T = location.It.T;
            return true;
        }
    }
    return false;
}

static bool GetScreenCoords( Eigen::Vector4f &outRelative,
                             const Eigen::Vector4f &position ) {
    // FUN_007819f0
    const Frustum &frustum =
        gpRender->getRenderPass<RP_Near>().pScene->getFrustumWrite();
    Eigen::Vector4f projected = TDUMat4x4MulVec(
        gpCamera->getActiveCamera()->getWorldToCam(), position );
    projected = TDUMat4x4MulVec( frustum.getProjectionMatrix(), projected );
    projected /= projected.w();

    outRelative.x() = ( projected.x() + 1.0f ) * 0.5f;
    outRelative.y() = ( 1.0f - projected.y() ) * 0.5f;
    outRelative.z() = projected.z();
    outRelative.w() = 1.0f;
    return -1.0f <= projected.x() && projected.x() <= 1.0f &&
           -1.0f <= projected.y() && projected.y() <= 1.0f;
}

void GMInstantChallenge::updateCursorHover() {
    // FUN_0078e3c0
    hoverFlags = 0;
    hoveredLeg = -1;

    float distanceSq = 0.0f;
    ProjectOnRoad( mngCursor.getWorldPosition(), &pCursorProfile, &distanceSq,
                   true, &cursorLocation );
    SnapAwayFromDeadEnd( cursorLocation, kCursorDeadEndDistance );

    const Eigen::Vector4f cursorPosition =
        cursorLocation.Frame.row( 3 ).transpose();
    mngCursor.setGroundPosition( cursorPosition );
    if ( distanceSq >= kCursorMaxRoadDistanceSq ) {
        return;
    }

    hoverFlags |= kHoverOnRoad;
    hoveredLeg = mngTracRoad.findLeg( cursorLocation.It );
    if ( hoveredLeg != -1 ) {
        hoverFlags |= kHoverOnRoute;
    }

    EntityCheckPoint *pNearest = nullptr;
    float nearestDistanceSq = kNoCheckpointDistanceSq;
    for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
        Entity *pActor = mngActor.getActor( i );
        if ( pActor->getType() != eEntityType::ET_CHPT ) {
            continue;
        }
        const Eigen::Vector4f delta =
            cursorPosition - pActor->getModelMatrix().row( 3 ).transpose();
        const float actorDistanceSq =
            delta.x() * delta.x() + delta.z() * delta.z();
        if ( actorDistanceSq < nearestDistanceSq ) {
            pNearest = static_cast<EntityCheckPoint *>( pActor );
            nearestDistanceSq = actorDistanceSq;
        }
    }
    if ( pNearest == nullptr ) {
        return;
    }

    Eigen::Vector4f checkpointScreen;
    GetScreenCoords( checkpointScreen,
                     pNearest->getModelMatrix().row( 3 ).transpose() );
    Eigen::Vector4f groundScreen;
    GetScreenCoords( groundScreen, cursorPosition );
    checkpointScreen.y() = 1.0f - checkpointScreen.y();

    const Eigen::Vector4f &cursorScreen = mngCursor.getScreenPosition();
    const float cursorDX = checkpointScreen.x() - cursorScreen.x();
    const float cursorDY = checkpointScreen.y() - cursorScreen.y();
    const float groundDX = checkpointScreen.x() - groundScreen.x();
    const float groundDY = checkpointScreen.y() - ( 1.0f - groundScreen.y() );
    if ( groundDX * groundDX + groundDY * groundDY < kNearCheckpointScreenSq ) {
        hoverFlags |= kHoverNearCheckpoint;
    }
    if ( cursorDX * cursorDX + cursorDY * cursorDY <
         kHoverCheckpointScreenSq ) {
        hoverFlags |= kHoverCheckpoint;
        pHoveredCheckpoint = pNearest;
    }
}

void GMInstantChallenge::updateCameraControl( const float deltaTime ) {
    // FUN_0078c5c0
    if ( gpScenaric->isInstantChallenge() && !isLocalPlayerRouteOwner() ) {
        return;
    }
    if ( !bEditorInputEnabled ) {
        return;
    }

    if ( !gpScenaric->isInstantChallenge() ) {
        const float pitchInput =
            ( bKeyPitchUp ? 1.0f : 0.0f ) - ( bKeyPitchDown ? 1.0f : 0.0f );
        float pitch = std::clamp( pitchInput * deltaTime * 0.5f +
                                      pGPSCamera->getPitchOffset(),
                                  0.0f, kMaxCameraPitch );
        if ( editorState == 0 ) {
            pitch = 0.0f;
        }

        float yaw = pGPSCamera->getYawOffset();
        if ( bKeyRotateLeft ) {
            yaw -= deltaTime * kCameraRotateSpeed;
            if ( yaw < -kTwoPi ) {
                yaw += kTwoPi;
            }
        } else if ( bKeyRotateRight ) {
            yaw += deltaTime * kCameraRotateSpeed;
            if ( kTwoPi < yaw ) {
                yaw -= kTwoPi;
            }
        }
        pGPSCamera->setPitchOffset( pitch );
        pGPSCamera->setYawOffset( yaw );
    }

    const Eigen::Vector4f &gpsFocus = pGPSCamera->getFocus();
    float focusX = gpsFocus.x();
    float focusZ = gpsFocus.z();
    PlayerConfiguration *pPlayerConfig = gpConfig->pPlayerConfig;
    if ( gpConfig->NbMaxPlayers > 0 && pPlayerConfig != nullptr ) {
        float panX = ReadMapPanAction( pPlayerConfig->pMapRight ) -
                     ReadMapPanAction( pPlayerConfig->pMapLeft );
        float panZ = ReadMapPanAction( pPlayerConfig->pMapDown ) -
                     ReadMapPanAction( pPlayerConfig->pMapUp );
        if ( mngCursor.isKeyLeftDown() ) {
            panX -= 1.0f;
        }
        if ( mngCursor.isKeyRightDown() ) {
            panX += 1.0f;
        }
        if ( mngCursor.isKeyDownDown() ) {
            panZ += 1.0f;
        }
        if ( mngCursor.isKeyUpDown() ) {
            panZ -= 1.0f;
        }
        panX *= kCameraPanSpeed;
        panZ *= kCameraPanSpeed;

        if ( panX != 0.0f || panZ != 0.0f ) {
            const Eigen::Vector4f pan =
                TDUMat4x4MulVec( MakeRotationMatY( pGPSCamera->getYawOffset() ),
                                 Eigen::Vector4f( panX, 0.0f, panZ, 1.0f ) );
            focusX += pGPSCamera->getZoomFactor() * pan.x() * deltaTime;
            focusZ += pGPSCamera->getZoomFactor() * pan.z() * deltaTime;
        }
    }

    if ( pGPSCamera->isInTransition() ) {
        return;
    }

    CameraGPS *pCamera = static_cast<CameraGPS *>( gpCamera->findCameraWithMode(
        kGPSCameraMode, gpCamera->getBoundCarState() ) );
    const Eigen::Vector4f &bounds =
        ( pCamera->getZoom() == CameraGPS::GZL_Close )
            ? gpMapView->CloseBounds
            : gpMapView->DefaultBounds;
    focusX = std::max( focusX, bounds.x() );
    focusZ = std::max( focusZ, bounds.y() );
    focusX = std::min( focusX, bounds.z() );
    focusZ = std::min( focusZ, bounds.w() );

    if ( pGPSCamera->getZoom() != CameraGPS::GZL_Medium ||
         !gpScenaric->isInstantChallenge() ) {
        mngMapViewer.setFocus(
            Eigen::Vector4f( focusX, gpsFocus.y(), focusZ, gpsFocus.w() ) );
    }
}

void GMInstantChallenge::updateCursorClips() {
    // FUN_0078a370
    if ( pEditorPlayer == nullptr ) {
        return;
    }

    FlashClip ghostClip;
    FlashClip cursorClip;
    FlashClip deroulClip;
    FlashClip moveClip;
    FlashClip pointClip;
    const bool bGhost =
        pEditorPlayer->getClipPlayer( "/curseur_ghost", &ghostClip );
    const bool bCursor =
        pEditorPlayer->getClipPlayer( "/curseur", &cursorClip );
    const bool bDeroul = pEditorPlayer->getClipPlayer( "/deroul", &deroulClip );
    const bool bMove =
        pEditorPlayer->getClipPlayer( "/curseur/curseur_move", &moveClip );
    const bool bPoint =
        pEditorPlayer->getClipPlayer( "/curseur/point", &pointClip );

    const char *pDeroulFrame = "default";
    if ( !bCursorVisible ) {
        bGhostCursorVisible = false;
        if ( bCursor ) {
            cursorClip.setProperty( eFlashProperty::FP_Visible, false );
        }
    } else {
        updateCursorFrames( bCursor, bPoint, bGhost,
                            bMove ? &moveClip : nullptr, pDeroulFrame );
        if ( bGhost ) {
            bGhostCursorVisible = updateGhostCursorScale();
        }
    }

    if ( !bGhostCursorVisible && bGhost ) {
        ghostClip.setProperty( eFlashProperty::FP_Visible, false );
    }

    FlashClip loopClip;
    if ( pEditorPlayer->getClipPlayer( "deroul/checkbox_loop", &loopClip ) ) {
        pEditorPlayer->goToFrameAndPlay(
            int32_t( missionDescription.getLoop().getValue() != 0 ),
            "deroul/checkbox_loop", false );
    }

    if ( bDeroul ) {
        if ( isPaused() ) {
            pDeroulFrame = "default";
        }
        if ( strcmp( pDeroulFrame, deroulFrame ) != 0 ) {
            pEditorPlayer->showFrame( pDeroulFrame, "/deroul",
                                      strcmp( pDeroulFrame, "default" ) != 0 );
            strncpy( deroulFrame, pDeroulFrame, sizeof( deroulFrame ) - 1 );
        }
    }
}

void GMInstantChallenge::updateCursorFrames( const bool bCursor,
                                             const bool bPoint,
                                             const bool bGhost,
                                             FlashClip *pMoveClip,
                                             const char *&pDeroulFrame ) {
    // FUN_0078a370 (inlined)
    if ( editorState == 0 || editorState == 1 ) {
        if ( bCursor ) {
            pEditorPlayer->showFrame( "target", "/curseur", false );
        }
        return;
    }
    if ( editorState != kEditorStateEditing ) {
        return;
    }

    bool bPointOn = false;
    if ( editTool < kToolPlaceCheckpoint || editTool > kToolMoveCheckpoint ) {
        bPointOn = ( hoverFlags & kHoverCheckpoint ) == 0;
    } else if ( canPlaceAtCursor( true ) ) {
        bPointOn = !bFinishPlaced || editTool == kToolMoveCheckpoint;
    }
    const char *pPointFrame = bPointOn ? "on" : "off";

    const bool bStartChallenge =
        missionDescription.getStartChallenge().getValue() != 0;
    if ( editTool != kToolMoveCheckpoint && ( editTool < 5 || editTool > 6 ) ) {
        if ( ( hoverFlags & kHoverCheckpoint ) == 0 ) {
            if ( ( hoverFlags & kHoverOnRoute ) != 0 &&
                 ( hoverFlags & kHoverNearCheckpoint ) == 0 ) {
                pDeroulFrame = ( !bFinishPlaced &&
                                 missionDescription.getLoop().getValue() == 0 )
                                   ? "insert"
                                   : "insert_alone";
            }
        } else if ( !IsCheckpoint( pHoveredCheckpoint ) ||
                    pHoveredCheckpoint->getIndex() == numWaypoints ||
                    !pGameMech->hasRaceHUD() || !bFinishPlaced ) {
            if ( IsCheckpoint( pHoveredCheckpoint ) &&
                 GetCheckpointType( pHoveredCheckpoint ) == kCheckpointStart &&
                 !bStartChallenge &&
                 missionDescription.getGameType() ==
                     EventParameters::EGT_Speed ) {
                pDeroulFrame = "rollover_speed";
            } else if ( bStartChallenge ) {
                pDeroulFrame = "rollover";
            }
        } else {
            pDeroulFrame =
                ( numWaypoints == 2 ) ? "rollover_start_2" : "rollover_start";
        }
    }

    const char *pCursorFrame = "checkpoint";
    const char *pGhostFrame = "checkpoint";
    switch ( editTool ) {
    case kToolNone:
        pCursorFrame = "empty";
        break;
    case kToolPlaceCheckpoint:
    case kToolMoveCheckpoint: {
        int32_t type = 0;
        if ( editTool == kToolPlaceCheckpoint ) {
            if ( pHoveredCheckpoint == nullptr ||
                 ( hoverFlags & kHoverCheckpoint ) == 0 ) {
                if ( pCheckpointTemplate != nullptr ) {
                    type = GetCheckpointType( pCheckpointTemplate );
                }
            } else {
                type = GetCheckpointType( pHoveredCheckpoint );
            }
        } else {
            type = GetCheckpointType( pSelectedCheckpoint );
        }

        switch ( type ) {
        case kCheckpointStart:
            pCursorFrame = "start";
            break;
        case kCheckpointFinish:
            pCursorFrame = "finish";
            break;
        case kCheckpointRegular:
            pCursorFrame = "checkpoint";
            break;
        case kCheckpointRadar:
            if ( bStartChallenge ) {
                pCursorFrame = "radar";
                pGhostFrame = "radar";
                break;
            }
            pCursorFrame = "empty";
            pGhostFrame = "empty";
            break;
        default:
            pCursorFrame = "empty";
            pGhostFrame = "empty";
            break;
        }

        if ( pSelectedCheckpoint != nullptr &&
             pSelectedCheckpoint == pLastCheckpoint &&
             GetCheckpointType( pSelectedCheckpoint ) == kCheckpointRegular &&
             bFinishPlaced ) {
            pCursorFrame = "finish";
        }
        break;
    }
    case 3:
    case 5:
        pCursorFrame = "zone_center";
        break;
    case 4:
    case 6:
        pCursorFrame = "radius";
        break;
    default:
        break;
    }

    if ( bCursor ) {
        pEditorPlayer->showFrame( pCursorFrame, "/curseur", false );
    }
    if ( bPoint ) {
        pEditorPlayer->showFrame( pPointFrame, "/curseur/point", false );
    }
    if ( bGhost ) {
        pEditorPlayer->showFrame( pGhostFrame, "/curseur_ghost", false );
    }
    if ( pMoveClip != nullptr ) {
        pMoveClip->setProperty( eFlashProperty::FP_Visible,
                                editTool == kToolMoveCheckpoint );
    }
}

bool GMInstantChallenge::updateGhostCursorScale() {
    // FUN_0078a370 (inlined)
    if ( editorState != kEditorStateEditing ) {
        return false;
    }

    Eigen::Vector4f delta = cursorLocation.Frame.row( 3 ).transpose() -
                            mngCursor.getWorldPosition();
    delta.y() = 0.0f;
    const float distance = delta.norm();
    if ( distance > kGhostCursorMaxDistance ) {
        return false;
    }

    const float scale = std::clamp(
        distance * kGhostCursorScaleSlope * 0.01f + kGhostCursorMaxScale,
        kGhostCursorMinScale, kGhostCursorMaxScale );
    FlashClip iconClip;
    if ( pEditorPlayer->getClipPlayer( "/curseur_ghost/icone", &iconClip ) ) {
        iconClip.setProperty( eFlashProperty::FP_ScaleX, scale );
        iconClip.setProperty( eFlashProperty::FP_ScaleY, scale );
    }
    return true;
}

void GMInstantChallenge::updatePendingExit() {
    // FUN_0078cc90
    const Eigen::Vector4f exitPosition = exitFrame.row( 3 ).transpose();
    if ( !gpWorld->isStreamedAround( &exitPosition, true, true ) ) {
        return;
    }

    const int32_t numMovies = gpFlash->getNumMovies( 1 );
    for ( int32_t movieIndex = 0; movieIndex < numMovies; movieIndex++ ) {
        MoviePlayer *pMoviePlayer = gpFlash->getMoviePlayer( 1, movieIndex );
        if ( pMoviePlayer != nullptr ) {
            pMoviePlayer->IsMoviePaused &= ~2;
        }
    }
    gpActiveGameMode->requestExit();
    gpTestDriveInstance->setNextGameMode( nextGameMode );
}

void GMInstantChallenge::refreshEditorState() {
    // FUN_0078d600 / FUN_0078d210 / FUN_0078fbe0 (inlined)
    findLastCheckpoint();
    if ( pGameMech != nullptr ) {
        pGameMech->gmeUpdatePlacement( this );
        updateChallengeValidity();
    }
    updateEditorFlashVars();
}

void GMInstantChallenge::findLastCheckpoint() {
    // FUN_0078bd80
    pLastCheckpoint = nullptr;
    for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
        Entity *pActor = mngActor.getActor( i );
        if ( !IsCheckpoint( pActor ) ) {
            continue;
        }
        EntityCheckPoint *pCheckpoint =
            static_cast<EntityCheckPoint *>( pActor );
        if ( pLastCheckpoint == nullptr ||
             pLastCheckpoint->getIndex() < pCheckpoint->getIndex() ) {
            pLastCheckpoint = pCheckpoint;
        }
    }
}

void GMInstantChallenge::updateChallengeValidity() {
    // FUN_0078cbc0
    if ( pEditorPlayer == nullptr ) {
        return;
    }
    const bool bWasValid = bChallengeValid;
    bChallengeValid = pGameMech->isGMEChallengeValid( this );
    pEditorPlayer->setVariableValue( "/:IsChaValid",
                                     bChallengeValid ? "1" : "0" );
    if ( !bWasValid && bChallengeValid ) {
        pEditorPlayer->showFrame( "green", "valid", true );
    }
}

void GMInstantChallenge::updateRouteLength() {
    // FUN_0078cc30
    routeLength =
        ComputeEventRouteLength( getMissionDescription(), &mngTracRoad ) *
        0.001f;
    char lengthText[0x20];
    FormatRouteDistance( routeLength, lengthText );
    pEditorPlayer->setVariableValueNonLinked( "/:Length", lengthText );
}

void GMInstantChallenge::updateEditorFlashVars() {
    // FUN_0078cf20
    if ( pEditorPlayer == nullptr ) {
        return;
    }

    const bool bFinishLinePosable =
        numWaypoints != 0 && missionDescription.getLoop().getValue() == 0;
    pEditorPlayer->setVariableValue( "/:IsFinishLinePosable",
                                     int32_t( bFinishLinePosable ) );
    pEditorPlayer->setVariableValue( "/:GME_Help", " " );
    pEditorPlayer->setVariableValue( "/:NbCheckpoints", numWaypoints );
    pEditorPlayer->setVariableValue( "/:NbRadars",
                                     std::max( numWaypoints - 1, 0 ) );
    updateRouteLength();

    const int32_t startChallenge =
        int32_t( missionDescription.getStartChallenge().getValue() );
    pEditorPlayer->setVariableValue( "/:IsRadar", startChallenge );

    const char *pInfos = "check";
    if ( pGameMech != nullptr &&
         pGameMech->getMechType() == EventParameters::EGT_Speed ) {
        pInfos = ( startChallenge != 0 ) ? "radar" : "none";
    }
    pEditorPlayer->setVariableValue( "/:WhatInfos", pInfos );
}

void GMInstantChallenge::formatPointName(
    char *pOutName, const EntityCheckPoint *pCheckpoint ) {
    // FUN_00789390
    switch ( GetCheckpointType( pCheckpoint ) ) {
    case kCheckpointStart:
        strcpy( pOutName, "p_start" );
        return;
    case kCheckpointFinish:
        strcpy( pOutName, "p_finish" );
        return;
    case kCheckpointRadar:
        sprintf( pOutName, "p_radar_%02d", pCheckpoint->getIndex() - 1 );
        return;
    default:
        if ( pCheckpoint == pLastCheckpoint && bFinishPlaced ) {
            strcpy( pOutName, "p_finish" );
            return;
        }
        sprintf( pOutName, "p_check_%02d", pCheckpoint->getIndex() );
        return;
    }
}

void GMInstantChallenge::removeWaypoint( const int32_t index ) {
    // FUN_0078d100
    markEditorModified();
    if ( index < numWaypoints ) {
        for ( int32_t i = index; i < numWaypoints - 1; i++ ) {
            waypoints[i] = waypoints[i + 1];
        }
        numWaypoints--;
    }
    mngTracRoad.solveLegs( true );
}

void GMInstantChallenge::clearToStartCheckpoint() {
    // FUN_0078d500
    while ( numWaypoints > 1 ) {
        removeWaypoint( 1 );
    }

    for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
        Entity *pActor = mngActor.getActor( i );
        if ( IsCheckpoint( pActor ) &&
             GetCheckpointType( static_cast<EntityCheckPoint *>( pActor ) ) !=
                 kCheckpointStart ) {
            MngActor::Uninstanciate( &mngActor, pActor );
            i--;
        }
    }
}

void GMInstantChallenge::onEditorMouseIn() {
    // FUN_0078d600
    gpFlash->setUseStick( false );
    if ( missionDescription.getStartChallenge().getValue() == 0 &&
         missionDescription.getGameType() == EventParameters::EGT_Speed ) {
        clearToStartCheckpoint();
    }
    bEditorInputEnabled = true;
    bCursorVisible = true;
    bChallengeValid = false;
    deroulFrame[0] = '\0';
    refreshEditorState();
}

void GMInstantChallenge::onEditorMouseOut() {
    // FUN_0078c140
    gpFlash->setUseStick( true );
    mngPreview.hide();
    bEditorInputEnabled = false;
    bCursorVisible = false;
}

void GMInstantChallenge::onMovePoint() {
    // FUN_0078d210
    if ( ( hoverFlags & kHoverCheckpoint ) != 0 ) {
        pEditorPlayer->setVariableValue( "/:IsFinishLine", 0 );
        pSelectedCheckpoint = pHoveredCheckpoint;
        formatPointName( selectedPointName, pSelectedCheckpoint );
        moveMode = 1;
    }
    refreshEditorState();
}

void GMInstantChallenge::onZoomOut() {
    // FUN_0078d280
    pGPSCamera->zoomOut();
    pGPSCamera->updateFlashZoom( pEditorPlayer );
}

void GMInstantChallenge::onZoomIn() {
    // FUN_0078d2a0
    if ( !pGPSCamera->zoomIn() ) {
        return;
    }
    mngMapViewer.setFocus( mngCursor.getGroundPosition() );
    pGPSCamera->updateFlashZoom( pEditorPlayer );
}

static Entity *FindCheckpointTemplate( const ScenaricBank *pBank,
                                       const int32_t type,
                                       const bool bBigCheckpoint ) {
    // FUN_00804530
    Entity *pFound = nullptr;
    for ( int32_t i = 0; i < pBank->getEntityCount(); i++ ) {
        Entity *pEntity = pBank->getEntity( i );
        if ( !IsCheckpoint( pEntity ) ) {
            continue;
        }
        const SerializedCheckPoint *pData =
            static_cast<EntityCheckPoint *>( pEntity )->getLEDData();
        if ( int32_t( pData->Type ) != type ) {
            continue;
        }
        pFound = pEntity;
        if ( ( pData->BigCheckpoint != 0 ) == bBigCheckpoint ) {
            return pEntity;
        }
    }
    return pFound;
}

bool GameMechanism::isGMEChallengeValid( GMInstantChallenge *pEditor ) const {
    // FUN_007315e0
    EventParameters *pDescription = pEditor->getMissionDescription();
    if ( pDescription->getGameType() == EventParameters::EGT_Speed ) {
        if ( pDescription->getStartChallenge().getValue() != 0 ) {
            return pEditor->numWaypoints > 1;
        }
        return pEditor->numWaypoints > 0;
    }
    if ( pEditor->numWaypoints < 2 ) {
        return false;
    }
    return pEditor->bFinishPlaced || pDescription->getLoop().getValue() != 0;
}

void GameMechanism::gmeUpdatePlacement( GMInstantChallenge *pEditor ) {
    // FUN_00733130
    auto selectTool = [pEditor]() {
        if ( pEditor->editTool == GMInstantChallenge::kToolMoveCheckpoint ) {
            pEditor->selectedPointName[0] = '\0';
        }
        pEditor->editTool = ( pEditor->moveMode == 1 )
                                ? GMInstantChallenge::kToolMoveCheckpoint
                                : GMInstantChallenge::kToolPlaceCheckpoint;
        pEditor->bToolActive = true;
    };

    int32_t type = kCheckpointStart;
    if ( pEditor->pLastCheckpoint != nullptr ) {
        if ( hasRaceHUD() ) {
            type = kCheckpointRegular;
        } else {
            bool bRadarAvailable = false;
            if ( mechType == EventParameters::EGT_Speed ) {
                int32_t numRadars = 0;
                MngActor &mngActor = pEditor->mngActor;
                for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
                    Entity *pActor = mngActor.getActor( i );
                    if ( IsCheckpoint( pActor ) &&
                         GetCheckpointType( static_cast<EntityCheckPoint *>(
                             pActor ) ) == kCheckpointRadar ) {
                        numRadars++;
                    }
                }
                bRadarAvailable = numRadars < kMaxNumRadars &&
                                  pEditor->getMissionDescription()
                                          ->getStartChallenge()
                                          .getValue() != 0;
            }

            if ( !bRadarAvailable ) {
                pEditor->pCheckpointTemplate = nullptr;
                pEditor->mngPreview.clearEntity();
                FlashPlayer *pHudPlayer = ( gpMngHud != nullptr )
                                              ? gpMngHud->getHudPlayer()
                                              : nullptr;
                if ( pHudPlayer != nullptr ) {
                    pHudPlayer->setVariableValue( "/:PointType", "" );
                }
                if ( pEditor->editTool ==
                     GMInstantChallenge::kToolMoveCheckpoint ) {
                    pEditor->selectedPointName[0] = '\0';
                }
                pEditor->editTool = GMInstantChallenge::kToolNone;
                pEditor->bToolActive = false;
                selectTool();
                return;
            }
            type = kCheckpointRadar;
        }
    }

    pEditor->pCheckpointTemplate = static_cast<EntityCheckPoint *>(
        FindCheckpointTemplate( &pEditor->checkpointBank, type, false ) );
    pEditor->mngPreview.setEntity( pEditor->pCheckpointTemplate );
    selectTool();
    selectTool();
}

static bool IsStartGridCar( const Entity *pEntity ) {
    return pEntity != nullptr && pEntity->getType() == eEntityType::ET_CARP;
}

EntityCheckPoint *
GMInstantChallenge::findCheckpointByIndex( const int32_t index ) const {
    // FUN_00781940 / FUN_00781990 (inlined)
    for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
        Entity *pActor = mngActor.getActor( i );
        if ( IsCheckpoint( pActor ) &&
             static_cast<EntityCheckPoint *>( pActor )->getIndex() == index ) {
            return static_cast<EntityCheckPoint *>( pActor );
        }
    }
    return nullptr;
}

EntityCheckPoint *GMInstantChallenge::findStartCheckpoint() const {
    // FUN_00781990
    return findCheckpointByIndex( 1 );
}

EntityCheckPoint *GMInstantChallenge::findFinishCheckpoint() const {
    // FUN_00781940
    return findCheckpointByIndex( numWaypoints );
}

Entity *GMInstantChallenge::findStartGridCar() const {
    // FUN_0078bd10
    for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
        Entity *pActor = mngActor.getActor( i );
        if ( IsStartGridCar( pActor ) ) {
            return pActor;
        }
    }
    return nullptr;
}

void GMInstantChallenge::markEditorModified() {
    // FUN_0078d060 / FUN_0078d100 / FUN_0078d190 (inlined)
    gpScenaric->setModifiedIGE( true );
    gpScenaric->setTestedLevel( false );
    if ( pEditorPlayer != nullptr ) {
        pEditorPlayer->showFrame( "red", "infos/fire", false );
    }
}

void GMInstantChallenge::setWaypoint( const int32_t index,
                                      const AiRoadLocation &location,
                                      const bool bSolveRoute ) {
    // FUN_0078d190
    markEditorModified();
    waypoints[index] = location;
    if ( numWaypoints < index + 1 ) {
        numWaypoints = index + 1;
    }
    if ( bSolveRoute ) {
        mngTracRoad.solveLegs( true );
    }
}

void GMInstantChallenge::insertWaypoint( const int32_t index,
                                         const AiRoadLocation &location ) {
    // FUN_0078d060
    markEditorModified();
    for ( int32_t i = numWaypoints; i > index; i-- ) {
        waypoints[i] = waypoints[i - 1];
    }
    waypoints[index] = location;
    numWaypoints++;
    mngTracRoad.solveLegs( true );
}

void GMInstantChallenge::applyDefaultChallengeName(
    const EntityCheckPoint *pCheckpoint ) {
    // FUN_0078c210
    const char *pDistrict = "Ohau";
    if ( gpMngSectorName != nullptr ) {
        pDistrict = gpMngSectorName->getSectorName(
            pCheckpoint->getModelMatrix().row( 3 ).transpose() );
    }

    GameParameterDBString &databaseName = missionDescription.getDatabaseName();
    const char *pCurrentName = ( int32_t( databaseName.getValue() ) < 0 )
                                   ? missionDescription.getNameBuffer()
                                   : databaseName.getDisplayText();
    if ( strcmp( pCurrentName, "Mission Honolulu" ) == 0 ) {
        strncpy( missionDescription.getNameBuffer(), pDistrict, 0x3f );
    }
}

EntityCheckPoint *
GMInstantChallenge::instantiateCheckpoint( Entity *pTemplate,
                                           const Matrix4x4f_t *pFrame ) {
    // FUN_0078d960
    Matrix4x4f_t frame;
    if ( pFrame == nullptr ) {
        frame = cursorLocation.Frame;
    } else {
        frame = *pFrame;
        AiRoadProfile *pProfile = nullptr;
        ProjectOnRoad( frame.row( 3 ).transpose(), &pProfile, nullptr, false,
                       nullptr );
    }

    EntityCheckPoint *pCheckpoint = static_cast<EntityCheckPoint *>(
        MngActor::InstanciateEntity( &mngActor, pTemplate ) );
    pCheckpoint->propagateTransform( frame, true );
    pCheckpoint->setPrimaryInstance( true );

    if ( GetCheckpointType( pCheckpoint ) == kCheckpointStart ) {
        Entity *pCarTemplate = nullptr;
        for ( int32_t i = 0; i < checkpointBank.getEntityCount(); i++ ) {
            Entity *pEntity = checkpointBank.getEntity( i );
            if ( IsStartGridCar( pEntity ) ) {
                pCarTemplate = pEntity;
            }
        }
        if ( pCarTemplate != nullptr ) {
            Entity *pCar =
                MngActor::InstanciateEntity( &mngActor, pCarTemplate );
            placeStartGridCar( pCar, pCheckpoint );
            pCar->setPrimaryInstance( true );
        }
        applyDefaultChallengeName( pCheckpoint );
    }

    pCheckpoint->setIndex(
        ( pLastCheckpoint != nullptr ) ? pLastCheckpoint->getIndex() + 1 : 1 );
    return pCheckpoint;
}

EntityCheckPoint *
GMInstantChallenge::placeCheckpoint( Entity *pTemplate,
                                     const Matrix4x4f_t *pFrame ) {
    // FUN_0078e780
    if ( pTemplate == nullptr ) {
        return nullptr;
    }

    EntityCheckPoint *pCheckpoint = nullptr;
    if ( bFinishPlaced && bFinishRequested ) {
        pCheckpoint = findFinishCheckpoint();
        commitMove( pCheckpoint );
    } else {
        pCheckpoint = instantiateCheckpoint( pTemplate, pFrame );
        setWaypoint( pCheckpoint->getIndex() - 1, cursorLocation, true );
        orientStartTowards( pCheckpoint );
        orientCheckpoint( pCheckpoint );
    }

    if ( bFinishRequested ) {
        bFinishRequested = false;
        bFinishPlaced = true;
    }
    refreshEditorState();
    return pCheckpoint;
}

void GMInstantChallenge::commitMove( EntityCheckPoint *pCheckpoint ) {
    // FUN_0078dca0
    const int32_t index = pCheckpoint->getIndex();
    Entity *pTemplate = pCheckpoint->getParent();

    bool bKeepDirection = false;
    Eigen::Vector4f previousDirection = Eigen::Vector4f::Zero();
    if ( GetCheckpointType( static_cast<EntityCheckPoint *>( pTemplate ) ) ==
             kCheckpointStart &&
         missionDescription.getGameType() == EventParameters::EGT_Speed &&
         missionDescription.getStartChallenge().getValue() == 0 ) {
        previousDirection = pCheckpoint->getModelMatrix().row( 2 ).transpose();
        bKeepDirection = true;
    }

    if ( GetCheckpointType( pCheckpoint ) == kCheckpointStart ) {
        MngActor::Uninstanciate( &mngActor, findStartGridCar() );
    }
    MngActor::Uninstanciate( &mngActor, pCheckpoint );

    EntityCheckPoint *pMoved = instantiateCheckpoint( pTemplate, nullptr );
    pMoved->setIndex( index );
    setWaypoint( index - 1, cursorLocation, true );

    EntityCheckPoint *pSecond = findCheckpointByIndex( 2 );
    if ( pSecond != nullptr ) {
        orientStartTowards( pSecond );
    }

    if ( !bKeepDirection ) {
        orientCheckpoint( pMoved );
        return;
    }
    if ( pMoved->getModelMatrix().row( 2 ).dot(
             previousDirection.transpose() ) < 0.0f ) {
        flipCheckpoint( pMoved );
    }
}

static float ComputeHeading( const float x, const float z ) {
    // FUN_00781da0 / FUN_00782020 (inlined)
    const float heading = acosf( x );
    return ( z < 0.0f ) ? kTwoPi - heading : heading;
}

// True when the gate's back direction (-Z) points more than 90 degrees away
// from the given direction.
static bool IsGateFacingAway( const EntityCheckPoint *pGate,
                              const Eigen::Vector4f &direction ) {
    // FUN_00781da0 / FUN_00782020 (inlined)
    const Eigen::Vector4f back = NormalizeVector4(
        pGate->getModelMatrix().row( 2 ).transpose() * -1.0f );
    float delta = fmodf( ComputeHeading( direction.x(), direction.z() ) -
                             ComputeHeading( back.x(), back.z() ),
                         kTwoPi );
    if ( delta < -kPI ) {
        delta += kTwoPi;
    } else if ( kPI <= delta ) {
        delta -= kTwoPi;
    }
    return kHalfPI <= fabsf( delta );
}

void GMInstantChallenge::orientCheckpoint( EntityCheckPoint *pCheckpoint ) {
    // FUN_00781da0
    EntityCheckPoint *pPrevious =
        findCheckpointByIndex( pCheckpoint->getIndex() - 1 );
    if ( pPrevious == nullptr ) {
        return;
    }

    Eigen::Vector4f direction;
    if ( waypoints[pCheckpoint->getIndex() - 1].It.pRoad != nullptr &&
         pGameMech->hasRaceHUD() ) {
        direction = mngTracRoad.getLegPath( pPrevious->getIndex() - 1 )
                        .getEndFrame()
                        .row( 2 )
                        .transpose();
    } else {
        direction = NormalizeVector4(
            pCheckpoint->getModelMatrix().row( 3 ).transpose() -
            pPrevious->getModelMatrix().row( 3 ).transpose() );
    }

    if ( IsGateFacingAway( pCheckpoint, direction ) ) {
        flipCheckpoint( pCheckpoint );
    }
}

void GMInstantChallenge::realignStartCheckpoint(
    EntityCheckPoint *pStart, const Eigen::Vector4f &direction ) {
    // FUN_00782020
    if ( IsGateFacingAway( pStart, direction ) ) {
        flipCheckpoint( pStart );
        placeStartGridCar( findStartGridCar(), pStart );
    }
}

void GMInstantChallenge::orientStartTowards( EntityCheckPoint *pCheckpoint ) {
    // FUN_00782180
    if ( pCheckpoint->getIndex() != 2 ) {
        return;
    }
    EntityCheckPoint *pStart = findStartCheckpoint();
    if ( pStart == nullptr ) {
        return;
    }

    if ( waypoints[pStart->getIndex() - 1].It.pRoad != nullptr &&
         pGameMech->hasRaceHUD() ) {
        realignStartCheckpoint( pStart,
                                mngTracRoad.getLegPath( pStart->getIndex() - 1 )
                                    .getStartFrame()
                                    .row( 2 )
                                    .transpose() );
        return;
    }

    realignStartCheckpoint(
        pStart,
        NormalizeVector4( pCheckpoint->getModelMatrix().row( 3 ).transpose() -
                          pStart->getModelMatrix().row( 3 ).transpose() ) );
}

void GMInstantChallenge::flipCheckpoint( EntityCheckPoint *pCheckpoint ) {
    // FUN_00781ad0
    Matrix4x4f_t frame = pCheckpoint->getModelMatrix();
    const Eigen::Vector4f position = frame.row( 3 ).transpose();
    frame.row( 3 ) = Eigen::Vector4f::Zero().transpose();

    Eigen::Vector4f axis = ToDirection( frame.row( 1 ).transpose() );
    Matrix4x4f_t rotation;
    MatrixRotateZ( rotation, axis, kPI );
    frame = frame * rotation;

    const Eigen::Vector4f axisY =
        NormalizeVector4( frame.row( 1 ).transpose() );
    const Eigen::Vector4f axisZ =
        NormalizeVector4( Vector4DCross( frame.row( 0 ).transpose(), axisY ) );
    const Eigen::Vector4f axisX =
        NormalizeVector4( Vector4DCross( axisY, axisZ ) );
    frame.row( 0 ) = axisX.transpose();
    frame.row( 1 ) = axisY.transpose();
    frame.row( 2 ) = axisZ.transpose();
    frame.row( 3 ) = position.transpose();
    pCheckpoint->propagateTransform( frame, true );
}

void GMInstantChallenge::placeStartGridCar( Entity *pCar,
                                            EntityCheckPoint *pStart ) {
    // FUN_00781ce0
    if ( pCar == nullptr ) {
        return;
    }

    Matrix4x4f_t frame = pStart->getModelMatrix();
    frame.row( 3 ) += frame.row( 2 ) * kStartGridCarOffset;
    frame = MakeRotationMatY( kPI ) * frame;
    frame( 3, 1 ) = pStart->getModelMatrix()( 3, 1 ) + 1.0f;
    pCar->setTransform( frame, true );
}

void GMInstantChallenge::onSetPoint() {
    // FUN_0078e970
    if ( editTool == kToolPlaceCheckpoint ) {
        if ( bFinishRequested ) {
            pCheckpointTemplate =
                static_cast<EntityCheckPoint *>( FindCheckpointTemplate(
                    &checkpointBank, kCheckpointFinish, false ) );
        }
        placeCheckpoint( pCheckpointTemplate, nullptr );
    } else if ( editTool == kToolMoveCheckpoint ) {
        pEditorPlayer->setVariableValue(
            "/:IsFinishLine", int32_t( missionDescription.getGameType() !=
                                       EventParameters::EGT_Speed ) );
        commitMove( pSelectedCheckpoint );
        pSelectedCheckpoint = nullptr;
        moveMode = 0;
    }
    refreshEditorState();
}

void GMInstantChallenge::onInstantChallengeSetPoint() {
    // FUN_0078e830
    EntityCheckPoint *pPlaced = nullptr;
    const bool bRouteLengthOK =
        kICMinRouteLength < routeLength && routeLength < kICMaxRouteLength;
    if ( findStartCheckpoint() != nullptr &&
         ( bRouteLengthOK || gbGMEDesigner ) &&
         ( hoverFlags & kHoverOnRoad ) != 0 &&
         ( hoverFlags & kHoverNearCheckpoint ) == 0 ) {
        pPlaced = placeCheckpoint( pCheckpointTemplate, nullptr );
        if ( isLocalPlayerRouteOwner() && !bOpponentIsBot ) {
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (FUN_00788750 send the point)
        }
    }

    if ( pEditorPlayer != nullptr ) {
        if ( pPlaced == nullptr ) {
            pEditorPlayer->setVariableValue( "/:IsPointOK", "0" );
            const uint64_t warningHashcode =
                ( kICMinRouteLength < routeLength ) ? 0x35d9d7a : 0x32ffdca;
            pEditorPlayer->setVariableValue(
                "/:m_IceAvert",
                gpDatabase->getStringByHashcode( 0, warningHashcode ) );
        } else {
            pEditorPlayer->setVariableValue( "/:IsPointOK", "1" );
        }
    }

    bFinishPlaced = pPlaced != nullptr;
    refreshEditorState();
}

void GMInstantChallenge::onInsertPoint() {
    // FUN_0078dae0
    for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
        Entity *pActor = mngActor.getActor( i );
        if ( !IsCheckpoint( pActor ) ) {
            continue;
        }
        EntityCheckPoint *pCheckpoint =
            static_cast<EntityCheckPoint *>( pActor );
        if ( hoveredLeg + 1 <= pCheckpoint->getIndex() - 1 ) {
            pCheckpoint->setIndex( pCheckpoint->getIndex() + 1 );
        }
    }

    EntityCheckPoint *pInserted =
        instantiateCheckpoint( pCheckpointTemplate, nullptr );
    pInserted->setIndex( hoveredLeg + 2 );
    insertWaypoint( hoveredLeg + 1, cursorLocation );
    orientStartTowards( pInserted );
    orientCheckpoint( pInserted );
    refreshEditorState();
}

void GMInstantChallenge::onEraseChallenge() {
    // FUN_0078d580
    for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
        Entity *pActor = mngActor.getActor( i );
        if ( IsCheckpoint( pActor ) || IsStartGridCar( pActor ) ) {
            MngActor::Uninstanciate( &mngActor, pActor );
            i--;
        }
    }
    pLastCheckpoint = nullptr;
    numWaypoints = 0;
    moveMode = 0;
}

void GMInstantChallenge::onCancelToStart() {
    // FUN_0078dba0
    if ( isLocalPlayerRouteOwner() && !bOpponentIsBot && pICRoute != nullptr ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (FUN_00788750 send a reset point)
    }
    if ( !pGPSCamera->zoomIn() ) {
        bZoomPending = true;
    }
    bFinishPlaced = false;
    removeWaypoint( 1 );

    EntityCheckPoint *pSecond = findCheckpointByIndex( 2 );
    if ( pSecond != nullptr ) {
        MngActor::Uninstanciate( &mngActor, pSecond );
    }
    refreshEditorState();
}

void GMInstantChallenge::onToggleLoop() {
    // FUN_0078d2f0
    if ( pHoveredCheckpoint == nullptr || !bFinishPlaced ) {
        return;
    }

    const int32_t loopIndex =
        int32_t( missionDescription.getLoopCheckpointIndex().getValue() );
    const int32_t hoveredIndex = pHoveredCheckpoint->getIndex();
    markEditorModified();
    if ( missionDescription.getLoop().getValue() == 0 ||
         hoveredIndex != loopIndex ) {
        missionDescription.getLoop().setValue( 1, true );
        missionDescription.getLoopCheckpointIndex().setValue( hoveredIndex,
                                                              false );
    } else {
        missionDescription.getLoop().setValue( 0, true );
    }
    mngTracRoad.solveLegs( true );
    refreshEditorState();
}

void GMInstantChallenge::onFrameRoute() {
    // FUN_0078cd50
    Eigen::Vector4f startPosition = pGPSCamera->getFocus();
    Eigen::Vector4f endPosition = pGPSCamera->getFocus();
    for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
        Entity *pActor = mngActor.getActor( i );
        if ( !IsCheckpoint( pActor ) ) {
            continue;
        }
        const Eigen::Vector4f position =
            pActor->getModelMatrix().row( 3 ).transpose();
        if ( static_cast<EntityCheckPoint *>( pActor )->getIndex() == 1 ) {
            startPosition = position;
        } else {
            endPosition = position;
        }
    }
    pGPSCamera->startZoom( pGPSCamera->getZoom(), CameraGPS::GZL_Far );
    mngMapViewer.setFocus( ( startPosition + endPosition ) * 0.5f );
}

void GMInstantChallenge::onCursorZoneLayout() {
    // FUN_0078c2f0
    FlashPlayer *pHudPlayer = GetHudPlayer();
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float aspectRatio = 0.0f;
    pHudPlayer->getVariableAsFloat( "/:zoed_x", x );
    pHudPlayer->getVariableAsFloat( "/:zoed_y", y );
    pHudPlayer->getVariableAsFloat( "/:zoed_width", width );
    pHudPlayer->getVariableAsFloat( "/:zoed_height", height );
    FlashRectToViewport( pHudPlayer, x, y, aspectRatio, height, width );

    const Viewport *pViewport = gpRender->getRenderPass<RP_Near>().pViewport;
    const float left = pViewport->X + x;
    const float top = pViewport->Y + y;
    mngCursor.setMargins( Eigen::Vector4f( left, top,
                                           1.0f - ( pViewport->X + width + x ),
                                           1.0f - ( top + height ) ) );
}

void GMInstantChallenge::onMapLayout() {
    // FUN_0078c3e0
    FlashPlayer *pHudPlayer = GetHudPlayer();
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    pHudPlayer->getVariableAsFloat( "/:map_x", x );
    pHudPlayer->getVariableAsFloat( "/:map_y", y );
    pHudPlayer->getVariableAsFloat( "/:map_width", width );
    pHudPlayer->getVariableAsFloat( "/:map_height", height );
    float aspectRatio = width / height;
    FlashRectToViewport( pHudPlayer, x, y, aspectRatio, height, width );
    if ( y < 0.0f ) {
        y = 0.0f;
    }
    if ( 1.0f < height ) {
        height = 1.0f;
    }
    gpRender->updateViewportDimScalars( x, y, width, height );

    pHudPlayer->getVariableAsFloat( "/:ViewPort_x", x );
    pHudPlayer->getVariableAsFloat( "/:ViewPort_y", y );
    pHudPlayer->getVariableAsFloat( "/:ViewPort_width", width );
    pHudPlayer->getVariableAsFloat( "/:ViewPort_height", height );
    if ( width == 0.0f ) {
        mngPreview.hide();
        return;
    }

    aspectRatio = width / height;
    FlashRectToViewport( pHudPlayer, x, y, aspectRatio, height, width );
    mngPreview.setViewportRect( x, y, width, height, aspectRatio );
}

void GMInstantChallenge::convertCheckpoint( EntityCheckPoint *pCheckpoint,
                                            const int32_t type ) {
    // FUN_00782750
    Entity *pTemplate = FindCheckpointTemplate( &checkpointBank, type, false );
    const Matrix4x4f_t frame = pCheckpoint->getModelMatrix();
    const int32_t index = pCheckpoint->getIndex();
    MngActor::Uninstanciate( &mngActor, pCheckpoint );
    EntityCheckPoint *pStart = instantiateCheckpoint( pTemplate, &frame );
    pStart->setIndex( index );
}

void GMInstantChallenge::onDeletePoint() {
    // FUN_0078e620
    if ( ( hoverFlags & kHoverCheckpoint ) != 0 &&
         pHoveredCheckpoint != nullptr ) {
        const bool bDeletingStart =
            GetCheckpointType( pHoveredCheckpoint ) == kCheckpointStart;
        const bool bDeletingLast = pHoveredCheckpoint == pLastCheckpoint;
        if ( bDeletingStart ) {
            if ( bFinishPlaced && numWaypoints == 2 ) {
                return;
            }
            MngActor::Uninstanciate( &mngActor, findStartGridCar() );
        }
        if ( bDeletingLast ) {
            bFinishPlaced = false;
        }

        const int32_t deletedIndex = pHoveredCheckpoint->getIndex();
        removeWaypoint( deletedIndex - 1 );
        MngActor::Uninstanciate( &mngActor, pHoveredCheckpoint );

        EntityCheckPoint *pNewFirst = nullptr;
        for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
            Entity *pActor = mngActor.getActor( i );
            if ( !IsCheckpoint( pActor ) ) {
                continue;
            }
            EntityCheckPoint *pCheckpoint =
                static_cast<EntityCheckPoint *>( pActor );
            if ( pCheckpoint->getIndex() > deletedIndex ) {
                pCheckpoint->setIndex( pCheckpoint->getIndex() - 1 );
                if ( pCheckpoint->getIndex() == 1 ) {
                    pNewFirst = pCheckpoint;
                }
            }
        }
        if ( pNewFirst != nullptr && bDeletingStart &&
             GetCheckpointType( pNewFirst ) != kCheckpointFinish ) {
            convertCheckpoint( pNewFirst, kCheckpointStart );
        }
    }

    missionDescription.getLoop().setValue( 0, true );
    refreshEditorState();
}

static void HideMapClip( FlashPlayer *pPlayer, const char *pClipName ) {
    // FUN_00789ee0 (inlined)
    FlashClip clip;
    if ( pPlayer->getClipPlayer( pClipName, &clip ) ) {
        clip.setProperty( eFlashProperty::FP_Visible, false );
    }
}

void GMInstantChallenge::drawInterface() {
    // FUN_00782ea0
    const Eigen::Vector4f &screen = mngCursor.getScreenPosition();
    const float cursorPosition[2] = { screen.x(), 1.0f - screen.y() };
    if ( bCursorVisible ) {
        gpCamera->findCameraWithMode( kGPSCameraMode,
                                      gpCamera->getBoundCarState() );
        const int32_t cursorScale = kCursorClipScale;
        SetMapClipPosition( "/curseur", cursorPosition, nullptr, &cursorScale );
        SetMapClipPosition( "/deroul", cursorPosition, nullptr, nullptr );
        if ( bGhostCursorVisible && bToolActive ) {
            Eigen::Vector4f ghostScreen;
            GetScreenCoords( ghostScreen,
                             cursorLocation.Frame.row( 3 ).transpose() );
            const float ghostPosition[2] = { ghostScreen.x(), ghostScreen.y() };
            SetMapClipPosition( "/curseur_ghost", ghostPosition, nullptr,
                                &cursorScale );
        }
    }
    updateMapIcons();
}

void GMInstantChallenge::updateMapIcons() {
    // FUN_00789ee0
    if ( pEditorPlayer == nullptr ) {
        return;
    }

    char clipName[0x80];
    for ( int32_t i = 1; i <= kMaxNumMapIcons; i++ ) {
        sprintf( clipName, "p_check_%02d", i );
        HideMapClip( pEditorPlayer, clipName );
        if ( i - 1 < kMaxNumRadars ) {
            sprintf( clipName, "p_radar_%02d", i );
            HideMapClip( pEditorPlayer, clipName );
        }
        if ( i - 1 < kMaxNumMultiStarts ) {
            sprintf( clipName, "p_start_multi_%02d", i );
            FlashClip clip;
            if ( pEditorPlayer->getClipPlayer( clipName, &clip ) ) {
                char variableName[0x80];
                sprintf( variableName, "/p_start_multi_%02d:NumStart", i );
                pEditorPlayer->setVariableValue( variableName, i );
                clip.setProperty( eFlashProperty::FP_Visible, false );
            }
        }
        if ( i - 1 < kMaxNumRadars ) {
            sprintf( clipName, "p_camp_%02d", i );
            HideMapClip( pEditorPlayer, clipName );
        }
    }
    HideMapClip( pEditorPlayer, "p_start" );
    HideMapClip( pEditorPlayer, "p_finish" );
    HideMapClip( pEditorPlayer, "p_flag" );
    HideMapClip( pEditorPlayer, "p_zone_center" );
    HideMapClip( pEditorPlayer, "/loop_ic" );
    HideMapClip( pEditorPlayer, "sens" );
    field_0x114d94 = 0;
    field_0x114d90 = 0;

    const bool bStartChallenge =
        missionDescription.getStartChallenge().getValue() != 0;
    for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
        Entity *pActor = mngActor.getActor( i );
        if ( !IsCheckpoint( pActor ) ) {
            continue;
        }
        EntityCheckPoint *pCheckpoint =
            static_cast<EntityCheckPoint *>( pActor );
        const int32_t type = GetCheckpointType( pCheckpoint );
        if ( type == kCheckpointRadar &&
             missionDescription.getStartChallenge().getValue() != 1 ) {
            continue;
        }

        const Eigen::Vector4f position =
            pCheckpoint->getModelMatrix().row( 3 ).transpose();
        formatPointName( clipName, pCheckpoint );
        placeMapIcon( position, clipName );
        if ( missionDescription.getLoop().getValue() != 0 &&
             pCheckpoint->getIndex() ==
                 int32_t( missionDescription.getLoopCheckpointIndex()
                              .getValue() ) ) {
            placeMapIcon( position, "/loop_ic" );
        }
        if ( type == kCheckpointStart && !bStartChallenge &&
             missionDescription.getGameType() == EventParameters::EGT_Speed ) {
            placeStartArrow( pCheckpoint );
        }
    }
}

void GMInstantChallenge::placeMapIcon( const Eigen::Vector4f &position,
                                       const char *pClipName ) {
    // FUN_00789c30
    if ( pEditorPlayer == nullptr || pGPSCamera == nullptr ) {
        return;
    }
    Eigen::Vector4f screen;
    if ( !GetScreenCoords( screen, position ) ) {
        return;
    }

    const float alpha =
        ( strcmp( pClipName, selectedPointName ) == 0 ) ? 0.5f : 1.0f;
    const float screenPosition[2] = { screen.x(), screen.y() };
    SetMapClipPosition( pClipName, screenPosition, &alpha, nullptr );

    const int32_t zoom = pGPSCamera->getZoom();
    if ( zoom == CameraGPS::GZL_Far ) {
        pEditorPlayer->showFrame( "lod", pClipName, false );
    } else if ( zoom == CameraGPS::GZL_Medium ||
                zoom == CameraGPS::GZL_Close ) {
        pEditorPlayer->showFrame( "off", pClipName, false );
    } else {
        pEditorPlayer->showFrame( "lod2", pClipName, false );
    }
}

void GMInstantChallenge::placeStartArrow( const EntityCheckPoint *pStart ) {
    // FUN_00789d60
    if ( pEditorPlayer == nullptr ) {
        return;
    }
    const Eigen::Vector4f position =
        pStart->getModelMatrix().row( 3 ).transpose();
    Eigen::Vector4f startScreen;
    if ( !GetScreenCoords( startScreen, position ) ) {
        return;
    }

    const Eigen::Vector4f ahead =
        position +
        pStart->getModelMatrix().row( 2 ).transpose() * kStartArrowDistance;
    Eigen::Vector4f aheadScreen;
    GetScreenCoords( aheadScreen, ahead );
    const Eigen::Vector4f direction = NormalizeVector4(
        Eigen::Vector4f( aheadScreen.x() - startScreen.x(), 0.0f,
                         0.0f - ( aheadScreen.y() - startScreen.y() ),
                         aheadScreen.w() - startScreen.w() ) );
    const float heading = ComputeHeading( direction.x(), direction.z() );
    const int32_t arrowFrame =
        kStartArrowFrameBase -
        int32_t( ( heading + kTwoPi ) * kStartArrowFramesPerRadian );

    placeMapIcon( position, "sens" );
    FlashClip arrowClip;
    if ( pEditorPlayer->getClipPlayer( "sens/arrow", &arrowClip ) ) {
        arrowClip.goToFrame( 99 - arrowFrame % 100, false );
    }
}

void GMInstantChallenge::initInstantChallengeRoute() {
    // FUN_00789240
    gICRouteDirty = 0;
    pICRoute = &gpScenaric->getICDatas();
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_008032b0 local player index)
    pICRoute->OwnerPlayerIndex = 0;
    icSyncTimer = kICSyncDelay;
    if ( !bOpponentIsBot ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (keep alive tolerance, CP2P
                            // observer registration)
    }
    if ( !isLocalPlayerRouteOwner() ) {
        return;
    }

    ProjectOnRoad( pICRoute->StartPosition, &pCursorProfile, nullptr, true,
                   &cursorLocation );
    SnapAwayFromDeadEnd( cursorLocation, kICStartDeadEndDistance );
    pICRoute->StartPosition = cursorLocation.Frame.row( 3 ).transpose();
    if ( !bOpponentIsBot ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (FUN_00788750 send the start)
    }
    placeCheckpoint(
        FindCheckpointTemplate( &checkpointBank, kCheckpointStart, false ),
        nullptr );
}

void GMInstantChallenge::finalizeRoute() {
    // FUN_0078e080
    if ( missionDescription.getGameType() != EventParameters::EGT_Speed ) {
        convertCheckpoint( pLastCheckpoint, kCheckpointFinish );
    }
}

void GMInstantChallenge::teleportLocalPlayerCar( const Matrix4x4f_t &frame ) {
    // FUN_0078ea30
    PhysicsVehicle *pPhysicsVehicle = GetLocalPlayerPhysicsVehicle();
    PhysicsLock physLock;
    pPhysicsVehicle->resetToTransform( frame, true );
}

void GMInstantChallenge::quitEditor( const eGameMode nextMode,
                                     const Matrix4x4f_t &frame ) {
    // FUN_0078f010
    if ( gpMngFade != nullptr ) {
        gpMngFade->setRenderLogo( true );
    }
    const int32_t numMovies = gpFlash->getNumMovies( 1 );
    for ( int32_t movieIndex = 0; movieIndex < numMovies; movieIndex++ ) {
        MoviePlayer *pMoviePlayer = gpFlash->getMoviePlayer( 1, movieIndex );
        if ( pMoviePlayer != nullptr ) {
            pMoviePlayer->IsMoviePaused |= 2;
        }
    }

    nextGameMode = nextMode;
    exitFrame = frame;
    teleportLocalPlayerCar( frame );
    mngCamGPS.setForceStreaming( true );
    mngCamGPS.setForceStreamingFlush( true );
    gpMapView->bEnabled = false;
    gpFlash->setUseStick( true );
    mngPreview.hide();
    bEditorInputEnabled = false;
    bCursorVisible = false;

    const Eigen::Vector4f &focus = mngMapViewer.getFocus();
    const int32_t zoom = pGPSCamera->getZoom();
    if ( zoom != CameraGPS::GZL_Island && zoom != CameraGPS::GZL_Space ) {
        const Eigen::Vector4f delta = focus - frame.row( 3 ).transpose();
        if ( kQuitFocusMoveDistance < delta.norm() ) {
            gpMapView->startMove( frame.row( 3 ).transpose(), 1.0f );
            return;
        }
    }
    mngMapViewer.setFocus( frame.row( 3 ).transpose() );
}

void GMInstantChallenge::rebuildWaypointsFromActors() {
    // FUN_0078e0d0
    const bool bWasTested = gpScenaric->isTestedLevel();
    bFinishPlaced = false;
    for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
        Entity *pActor = mngActor.getActor( i );
        if ( IsCheckpoint( pActor ) &&
             GetCheckpointType( static_cast<EntityCheckPoint *>( pActor ) ) ==
                 kCheckpointFinish ) {
            bFinishPlaced = true;
        }
    }

    for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
        Entity *pActor = mngActor.getActor( i );
        if ( !IsCheckpoint( pActor ) ) {
            continue;
        }
        AiRoadLocation location;
        ProjectOnRoad( pActor->getModelMatrix().row( 3 ).transpose(), nullptr,
                       nullptr, true, &location );
        const int32_t index =
            static_cast<EntityCheckPoint *>( pActor )->getIndex();
        markEditorModified();
        waypoints[index - 1] = location;
        if ( numWaypoints < index ) {
            numWaypoints = index;
        }
    }
    mngTracRoad.solveLegs( true );

    if ( pEditorPlayer != nullptr ) {
        pEditorPlayer->setVariableValue(
            "/:IsFinishLine", int32_t( missionDescription.getGameType() !=
                                       EventParameters::EGT_Speed ) );
    }
    strncpy( gGMESelectedFileName, missionDescription.getFileNameBuffer(), 8 );
    refreshEditorState();
    gpScenaric->setTestedLevel( bWasTested );
}

void GMInstantChallenge::onNewChallenge() {
    // FUN_0078f760
    char gmName[0x20] = {};
    char gmType[0x20] = {};
    if ( pEditorPlayer != nullptr ) {
        pEditorPlayer->getVariableAsString( "/:WhatGM", gmName );
        pEditorPlayer->getVariableAsString( "/:WhatGMType", gmType );
    }

    int32_t gameType = GetGameModeInfoIndex( gmName );
    int32_t spotType = EventParameters::EST_Solo;
    if ( gameType == -1 ) {
        gameType = 0;
    } else if ( gameType != 0 ) {
        spotType = GetSpotTypeFromName( gmType );
    }

    createGameMech( uint32_t( spotType ), gameType );
    missionDescription.reset();
    missionDescription.getGameTypeParameter().setValue( gameType, false );
    missionDescription.getSpotTypeParameter().setValue( spotType, false );
    RefreshIGEFileList( &gIGEFileRequest, true, true );
    missionDescription.generateFileName();
    bFinishPlaced = false;
    bFinishRequested = false;
    onEraseChallenge();
    rebuildWaypointsFromActors();
    refreshEditorState();
}

void GMInstantChallenge::requestSave() {
    // FUN_0078f870 (inlined)
    finalizeRoute();
    SaveIGEFile( &gIGEFileRequest, &mngActor,
                 getMissionDescription()->getFileName() );
    gpFlash->setLevelMoviesFlag( 1, 0, 1, true );
    rebuildWaypointsFromActors();
    bSaving = true;
}

void GMInstantChallenge::updateSave() {
    // FUN_0078f870 (inlined)
    if ( !bSaving || gpFile->hasPendingFileToSave() ) {
        return;
    }
    bSaving = false;
    if ( gpFile->getLastSaveError() != SQE_Success ) {
        return;
    }

    FlashPlayer *pHudPlayer =
        ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;
    if ( pHudPlayer != nullptr ) {
        pHudPlayer->setVariableValue( "/:IsOverwriteGME", "1" );
    }
    gpScenaric->setModifiedIGE( false );
    setFireFrame( pEditorPlayer );
}

void GMInstantChallenge::leaveICESession() {
    // FUN_00788590
    for ( int32_t i = 0; i < kNumICEChallengers; i++ ) {
        if ( gpScenaric->getICEChallengers()[i].ID !=
             ICEChallenger::kNoChallenger ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (CP2PGameService vtbl
                                     // +0x18 per challenger)
        }
    }
}
void GMInstantChallenge::launchInstantChallenge() {
    // FUN_00788840
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (LAUNCHICRACE log per P2P
                             // player, session player count, keep alive
                             // tolerance)

    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    Driver *pDriver = pSave->getDriver();
    const int32_t money = int32_t( pDriver->getMoney() );
    const int32_t stake = std::min( gpScenaric->getICDatas().Stake, money );
    pSave->getExtraChallenges()->addCounter( CounterIndex( 0xf0d4 ), stake );
    pDriver->setMoneyAndNotify( int32_t( pDriver->getMoney() ) - stake );
    gpPlayerData->requestSave( 1 );

    gpScenaric->requestICIGE( &mngActor );
    gpTestDriveInstance->setNextGameMode( GM_Mission );
    requestExit();
}
void GMInstantChallenge::FUN_00788c80( const float ) {
    // FUN_00788c80
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (instant challenge P2P route sync,
                             // human opponents only)
}
bool GMInstantChallenge::deleteSelectedChallenge() {
    // FUN_0078c0d0
    const int32_t index = challengeList.getSelectedIndex();
    if ( index < 0 || index >= gIGEFileRequest.NumSortedFiles ) {
        return false;
    }
    char fileName[0x104];
    snprintf( fileName, sizeof( fileName ), "%s.%s",
              gIGEFileRequest.getSortedFile( index )->pFileName, "IGE" );
    return gpFile->deleteFile( "IGE", fileName, nullptr );
}

bool GMInstantChallenge::onSave() {
    // FUN_0078ce70
    if ( !bChallengeValid ) {
        return false;
    }
    gpFlashMessageBox->close();
    gpFlash->setLevelMoviesFlag( 1, 0, 1, false );
    if ( !gpFlashMessageBox->display( kSavingPopupHashcode, 3.0f, 0,
                                      &GMInstantChallenge::OnSavePopupShown,
                                      this, false ) ) {
        OnSavePopupShown( this );
    }
    return true;
}

void GMInstantChallenge::OnSavePopupShown( void *pUserData ) {
    // FUN_0078bd50
    GMInstantChallenge *pEditor =
        static_cast<GMInstantChallenge *>( pUserData );
    if ( pEditor == gpActiveGM &&
         gpTestDriveInstance->getActiveGameMode() == GM_GameModeEdit ) {
        pEditor->bSaveRequested = true;
    }
}

void GMInstantChallenge::restoreTestedChallenge() {
    // FUN_0078fbe0 (inlined)
    bTestLevelLoaded = true;
    ParseIGEFile( nullptr, gpScenaric->getTestIGEBuffer(), &gIGEFileRequest,
                  &mngActor, gpScenaric->getTestIGESize(), false );
    gpScenaric->releaseTestIGEBuffer();

    EventParameters *pDescription = getMissionDescription();
    createGameMech( uint32_t( pDescription->getSpotType() ),
                    pDescription->getGameType() );
    strncpy( pDescription->getFileNameBuffer(), gGMESelectedFileName, 8 );
    rebuildWaypointsFromActors();
    bChallengeValid = true;

    if ( !gpScenaric->isTestedLevel() ) {
        return;
    }
    const uint32_t testedScore = uint32_t( gpScenaric->getTestedScore() );
    switch ( pDescription->getGameType() ) {
    case EventParameters::EGT_TimeAttack:
        pDescription->getGoldTime().setValue( int32_t( testedScore ), true );
        break;
    case EventParameters::EGT_Speed:
        pDescription->getGoldSpeed().setValue(
            int32_t( float( testedScore ) * kTestScoreToSpeedScale *
                     kMetersPerSecondToKmh * kSpeedParameterScale ),
            true );
        break;
    default:
        break;
    }
}

void GMInstantChallenge::setupInstantChallengeFlash() {
    // FUN_0078aa60
    ICEChallenger player = {};
    player.bBot = false;
    if ( !bOpponentIsBot ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (CP2PGameService vtbl +0x3c
                                 // local player index)
        player.ID = ICEChallenger::kNoChallenger;
    } else {
        player.ID = kLocalICEPlayer;
    }

    const ICEChallenger *pChallengers = gpScenaric->getICEChallengers();
    int32_t numPlayers = 0;
    for ( int32_t i = 0; i < kNumICEChallengers; i++ ) {
        if ( i > 0 ) {
            player = pChallengers[i - 1];
        }

        bool bShown = false;
        if ( player.bBot ) {
            bShown = player.ID != ICEChallenger::kNoChallenger;
        } else if ( player.ID == kLocalICEPlayer ) {
            bShown = true;
        } else if ( player.ID < 0x10 ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (P2P player slot check)
        }
        if ( !bShown ) {
            continue;
        }

        numPlayers++;
        FillICEPlayerInfo( player, uint32_t( numPlayers ), pEditorPlayer );
        const bool bLocalPlayer =
            bOpponentIsBot ? ( i == 0 )
                           : ( gpScenaric->getICELocalPlayerIndex() ==
                               int32_t( player.ID ) );
        char clipPath[0x40];
        snprintf( clipPath, sizeof( clipPath ), "/players/p%d", numPlayers );
        pEditorPlayer->showFrame( bLocalPlayer ? "editor" : "default", clipPath,
                                  false );
    }

    const int32_t stake = pICRoute->Stake;
    pEditorPlayer->setVariableValue( "/:ICE_Stake", stake );
    pEditorPlayer->setVariableValue( "/:ICE_Rewards", stake * numPlayers );
    pEditorPlayer->setVariableValue( "/:Length", 0 );
    pEditorPlayer->setVariableValue( "/:nb_players", numPlayers );
    bICRouteReady = true;

    bool bHost = pICRoute == nullptr || bOpponentIsBot;
    if ( !bHost ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (CP2PGameService vtbl +0x3c
                                 // local player index == route owner)
    }
    pEditorPlayer->setVariableValueNonLinked( "/:IsHost", bHost ? "1" : "0" );
    pEditorPlayer->setVariableValueNonLinked( "/:IsConnectOK", "1" );
    bCursorVisible = true;
}

void GMInstantChallenge::updateInstantChallengeRoute() {
    // FUN_0078de40
    if ( bFinishPlaced ) {
        return;
    }

    if ( canPlaceAtCursor( false ) && numWaypoints > 0 ) {
        AiRoadLocation location;
        if ( ProjectOnRoad( mngCursor.getWorldPosition(), nullptr, nullptr,
                            false, &location ) ) {
            setWaypoint( 1, location, true );
            mngTracRoad.prefetchLegShapes( 0 );
            updateRouteLength();
            return;
        }
    }
    removeWaypoint( 1 );
    updateRouteLength();
}

bool GMInstantChallenge::isCurrentCarAllowed() {
    // FUN_0078b120 (inlined)
    EventParameters *pDescription = getMissionDescription();
    GameParameterVehicleRestriction &restriction =
        pDescription->getVehicleRestriction();
    const CarDesc *pCar = gpPlayerData->getCurrentCar();
    if ( pCar == nullptr ) {
        // NOTE: retail dereferences the current car unconditionally.
        return true;
    }
    return IsCarAllowedByRestriction(
        pCar, int32_t( pDescription->getVehicleType().getValue() ),
        int32_t( restriction.getValue() ),
        restriction.getBrand().getBrandHashcode(),
        uint32_t( restriction.getModel().getValue() ),
        int32_t( restriction.getCategory().getValue() ),
        int32_t( restriction.getCategoryFrom().getValue() ),
        int32_t( restriction.getCategoryTo().getValue() ) );
}

void GMInstantChallenge::startTestDrive() {
    // FUN_0078f1d0
    Entity *pStartCar = findStartGridCar();
    if ( pStartCar != nullptr ) {
        const Matrix4x4f_t &frame = pStartCar->getModelMatrix();
        const Eigen::Vector4f position = frame.row( 3 ).transpose();
        gpWorld->setPlayerPosition( &position );
        quitEditor( GM_Mission, frame );
    }

    if ( pEditorPlayer != nullptr ) {
        pEditorPlayer->getVariableAsString( "/:GMEMenu", gGMETestMenu );
        pEditorPlayer->getVariableAsString( "/:ZoomLevel", gGMETestZoomLevel );
        gGMETestStep = pEditorPlayer->getVariableAsInt( "/:Step" );
    }
    gpScenaric->requestTestIGE( &mngActor );
}

void GMInstantChallenge::loadSelectedChallenge() {
    // FUN_0078f530
    getMissionDescription()->reset();
    onEraseChallenge();
    findLastCheckpoint();
    if ( pGameMech != nullptr ) {
        pGameMech->gmeUpdatePlacement( this );
        updateChallengeValidity();
    }
    updateEditorFlashVars();
    strncpy( getMissionDescription()->getNameBuffer(), "Mission Honolulu",
             GameParameterString::kMaxLength );
    strncpy( getMissionDescription()->getDescriptionBuffer(), "OAHU",
             GameParameterString::kMaxLength );

    IGEFileEntry *pEntry =
        gIGEFileRequest.getSortedFile( challengeList.getSelectedIndex() );
    if ( !pEntry->bUserFile ) {
        LoadIGEFile( &gIGEFileRequest, pEntry->pFileName, &mngActor, false,
                     nullptr, false, nullptr, nullptr );
    } else {
        LoadUserIGEFile( &gIGEFileRequest, &mngActor, pEntry->pFileName,
                         nullptr, false );
    }

    EventParameters *pDescription = getMissionDescription();
    createGameMech( uint32_t( pDescription->getSpotType() ),
                    pDescription->getGameType() );
    rebuildWaypointsFromActors();

    for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
        Entity *pActor = mngActor.getActor( i );
        if ( IsCheckpoint( pActor ) &&
             GetCheckpointType( static_cast<EntityCheckPoint *>( pActor ) ) ==
                 kCheckpointStart ) {
            mngMapViewer.setFocus(
                pActor->getModelMatrix().row( 3 ).transpose() );
            break;
        }
    }

    editorState = kEditorStateEditing;
    mngCursor.setScreenPosition(
        Eigen::Vector4f( kCursorCenter, kCursorCenter, kCursorCenter, 1.0f ) );
}
