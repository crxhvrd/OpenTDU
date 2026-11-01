#include "shared.h"
#include "gm_lobby.h"

#include "gs_timer.h"
#include "tdu_instance.h"
#include "ai/ai_pool.h"
#include "ai/ai_road_query.h"
#include "ai/gs_ai.h"
#include "core/format.h"
#include "core/time.h"
#include "game/game_mechanism_player.h"
#include "game/map/mng_trac_road.h"
#include "game/scenaric/ige_file.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "flash/flash_callbacks_register.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/modal_message_box.h"
#include "flash/movie_player.h"
#include "game/mng_hud.h"
#include "game/mng_ingame_menu.h"
#include "game/mng_music.h"
#include "game/scenaric/entity/entity_check_point.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/local_player.h"
#include "game/scenaric/spots.h"
#include "game/vehicle/car_desc.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_data/driver.h"
#include "player_data/achievements_data.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"
#include "player_data/saved_ranking.h"
#include "player_data/saved_road_discovery.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "render/draw_list.h"
#include "render/gs_render.h"
#include "system/gs_pc_options.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/mng_ocean.h"
#include "world/mng_world.h"
#include "world/streaming_manager.h"

static constexpr FlashMessage kMsgChallengeQuit = FlashMessage( "chou" );
static constexpr FlashMessage kMsgTraining = FlashMessage( "trai" );
static constexpr FlashMessage kMsgZoomIn = FlashMessage( "zoin" );
static constexpr FlashMessage kMsgZoomOut = FlashMessage( "zoou" );
static constexpr FlashMessage kMsgMapOn = FlashMessage( "maon" );
static constexpr FlashMessage kMsgMapOff = FlashMessage( "maof" );

static constexpr float kViewFadeDuration = 0.5f; // FLOAT_00f41674
static constexpr uint32_t kNbGamesRefreshDelayMs = 5000u;
static constexpr uint32_t kPlaceCarRetryDelayMs = 500u;
static constexpr uint64_t kNoSessionPopupHashcode = 0x3a7c1f2ull;
static constexpr int32_t kNoSessionPopupUserData = 0xf;
static constexpr float kGridSpacing = 4.5f;            // DAT_00f41a54
static constexpr float kGridRowSpacing = -3.5f;        // DAT_00f41c6c
static constexpr float kGridRollingShift = -2.25f;     // DAT_00f41c70
static constexpr float kMaxTeleportDistance = 8000.0f; // DAT_00f4177c

static uint32_t GetTickCountMs() {
    return uint32_t( MonotonicToMs( GetTimeMonotonic() ) );
}

static inline Driver *GetDriver() {
    return gpPlayerData->getLoadedSave()->getDriver();
}

static void ShowLocalPlayerCar( const bool bShow ) {
    // FUN_0075c8c0
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr || pLocalCar->getVehicleInstance() == nullptr ) {
        return;
    }

    pLocalCar->getVehicleInstance()->setShouldDraw( bShow );

    EntityDriver *pDriver = pLocalCar->getDriver();
    if ( pDriver != nullptr ) {
        while ( ( pDriver->getInstanceRefCount() > 0 ) != bShow ) {
            if ( bShow ) {
                pDriver->addInstanceReference();
            } else if ( pDriver->getInstanceRefCount() > 0 ) {
                pDriver->removeInstanceReference();
            }
        }
    }

    // NOTE: Retail dereferences the physics vehicle unconditionally.
    PhysicsVehicle *pPhysicsVehicle = pLocalCar->getPhysicsVehicle();
    if ( pPhysicsVehicle != nullptr && !pPhysicsVehicle->isPaused() ) {
        PhysicsLock lock;
        pPhysicsVehicle->setPostponedPause( 1 );
    }
}

static void PauseLocalPlayerCarPhysics() {
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr ) {
        return;
    }

    PhysicsVehicle *pPhysicsVehicle = pLocalCar->getPhysicsVehicle();
    if ( pPhysicsVehicle != nullptr && pPhysicsVehicle->isActive() ) {
        PhysicsLock lock;
        pPhysicsVehicle->setPostponedPause( 1 );
    }
}

static bool IsSpotRoadUnreachable( const SpotMap *pSpot ) {
    // FUN_0088e2c0
    if ( pSpot == nullptr ) {
        return false;
    }

    SavedRoadDiscovery *pRoads =
        gpPlayerData->getLoadedSave()->getRoadDiscovery();
    if ( pSpot->Type == 8 && pSpot->SubType > 4 && pSpot->SubType < 9 ) {
        OTDU_UNIMPLEMENTED; // TODO: FUN_007ddac0 / FUN_007ddaa0
        return false;
    }

    return pRoads->isRoadLocked( pSpot->RoadID, pSpot->RoadSegment ) ||
           !IsRoadAvailable( pSpot->RoadID );
}

static Matrix4x4f_t ComputeGridSlotTransform( const Matrix4x4f_t &start,
                                              const int32_t gridSize,
                                              const int32_t slot,
                                              const bool bRolling ) {
    // FUN_0075ca10
    Matrix4x4f_t transform = start;
    if ( !bRolling ) {
        const float centerOffset =
            ( float( gridSize ) - 1.0f ) * 0.5f * kGridSpacing;
        transform.row( 3 ) =
            start.row( 0 ) * ( float( slot - 1 ) * kGridSpacing ) +
            start.row( 3 ) + start.row( 2 ) * 0.0f +
            start.row( 0 ) * centerOffset;
    } else {
        const int32_t rowIndex = gridSize - slot - 1;
        transform.row( 3 ) =
            start.row( 0 ) * ( float( rowIndex % 2 ) * kGridSpacing ) +
            start.row( 3 ) +
            start.row( 2 ) * ( float( rowIndex ) * kGridRowSpacing ) +
            start.row( 0 ) * kGridRollingShift;
    }
    return transform;
}

GMLobby::GMLobby()
    : GameModeWorldBase(),
      PopupCallback(),
      curState( LS_None ),
      prevState( LS_None ),
      nextState( LS_None ),
      stateInitTime( 0.0f ),
      pSpot( nullptr ),
      bSpectatorMode( false ),
      pPreviewCamera( nullptr ),
      routePreviewCamera( this ),
      startOrbitCamera( this ),
      checkpointFlybyCamera( this ),
      pFlashPlayer( nullptr ),
      bReturningToSession( false ),
      mngLobby(),
      mngLogoDisplay(),
      mngTracRoadMap(),
      mngCamGPS(),
      mngChat(),
      mngSpots(),
      mngMapViewer(),
      lurePictureBank(),
      pCheckpointBank( nullptr ),
      gameType( EGT_TimeAttack ),
      spotType( EST_Solo ),
      maxCheckpointIndex( 0 ),
      routeDistanceKm( 0.0f ),
      pLurePicture( nullptr ),
      pPendingLureSpot( nullptr ),
      playersViewCamera( this ),
      pPendingTeamSpot( nullptr ),
      bExitPending( false ),
      bShowNoSessionPopup( false ),
      fallbackLureBank(),
      bPlaceCarRequest( false ),
      bPlayersViewStreamed( false ),
      placeCarRequestTime( 0u ),
      nbGamesRefreshTime( 0u ),
      spotChangeTimeout( 0u ),
      bSpotChangeAllowed( false ),
      bQuickmatchJoin( false ) {
    // FUN_0075dde0
    gpScenaric->setInstantChallenge( false );
    setTransitionFlags( 0x33 );

    registerManager( &mngLogoDisplay );
    registerManager( &mngLobby );
    registerManager( &mngMapViewer );
    registerManager( &mngSpots );
    registerManager( &mngTracRoadMap );
    registerManager( &mngCamGPS );
    registerManager( &mngChat );

    mngTracRoadMap.setOwner( &mngSpots );
    mngLobby.setOwner( this );

    OTDU_UNIMPLEMENTED; // TODO: ONLINE (register the P2P game listener)
}

GMLobby::~GMLobby() {
    // FUN_0075dcd0
    OTDU_UNIMPLEMENTED; // TODO: ONLINE (unregister the P2P game listener)
}

void GMLobby::initialize() {
    // FUN_007626b0
    GameModeWorldBase::initialize();

    if ( gpMngOcean != nullptr ) {
        gbOceanEnabled = true;
    }
    gpAI->setTrafficCategories( false, 0u );
    OTDU_UNIMPLEMENTED; // TODO: ONLINE (gGSOnline +0xbf34 = 0)
    mngWorld.setStreamEnabled( true );
    gpMngMusic->setPlayFlags( 1 );

    pCheckpointBank = new ( TestDrive::AllocAligned(
        sizeof( ScenaricBank ), alignof( ScenaricBank ) ) ) ScenaricBank();
    gpScenaric->setCheckpointBank( pCheckpointBank );

    EventParameters &eventParameters = gpScenaric->getSelectedEventParameters();
    switch ( eventParameters.getSource() ) {
    case EventParameters::ES_Buffer:
    case EventParameters::ES_IGEFile:
        gpScenaric->loadPackBank( "Scenarics/Packs", "pack_cp_virtual",
                                  pCheckpointBank );
        if ( eventParameters.getSource() == EventParameters::ES_Buffer ) {
            OTDU_UNIMPLEMENTED; // TODO: GSScenaric (parse the IGE buffer at
                                // DAT_0120bde0, size DAT_0120bddc)
        } else {
            LoadIGEFile( &gIGEFileRequest, eventParameters.getRequestedName(),
                         &mngActor, false, &eventParameters, false, nullptr,
                         nullptr );
        }
        gameType = eventParameters.getGameType();
        spotType = eventParameters.getSpotType();
        break;
    case EventParameters::ES_Online:
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (instant challenge IGE buffer)
        break;
    case EventParameters::ES_CustomChallenge:
        OTDU_UNIMPLEMENTED; // TODO: SCENARIC (drive-in custom challenge IGE
                            // buffer)
        break;
    default:
        break;
    }

    resetCheckpoints();

    OTDU_UNIMPLEMENTED; // TODO: ONLINE (send the local player install)

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar != nullptr && pLocalCar->getPhysicsVehicle() != nullptr &&
         pLocalCar->getPhysicsVehicle()->isActive() ) {
        pLocalCar->getPhysicsVehicle()->setLean( 0.0f );
    }

    gpScenaric->setActiveExtraChallenge( nullptr );
    lurePictureBank.setBuffer(
        TestDrive::AllocAligned( MapScreenshotBank::kBufferSize, 0x10 ) );

    OTDU_UNIMPLEMENTED; // TODO: GSScenaric (FUN_008059f0, re-place the
                        // SCPlayer drivers)
}

void GMLobby::reset() {
    // FUN_00761380
    GameModeScene::reset();

    gpAI->setDisableCops( true );
    gpAI->setCopsActive( false );
    gpAI->getAIPool()->removeCops();
    gpMngWorld->setDisableStreamingOnCar( true );

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bNeedStreamUpdate = true;
    pStreaming->sectorManager.bDrawSector = false;
    pStreaming->areaGrid.bNeedStreamUpdate = true;
    pStreaming->areaGrid.bDrawArea = false;
    pStreaming->setActiveRenderLayers( 1 );

    pFlashPlayer = gpFlash->getFlashPlayer( "CHALLENG" );
    if ( pFlashPlayer == nullptr ) {
        return;
    }

    char currentSpotType[32] = {};
    for ( int32_t i = 0; i < gpFlash->getNumMovies( 1 ); i++ ) {
        MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, i );
        FlashPlayer *pPlayer =
            ( pMovie != nullptr && pMovie->MovieBank.isLoaded() )
                ? pMovie->MovieBank.getFlashPlayer()
                : nullptr;
        if ( pPlayer == pFlashPlayer ) {
            continue;
        }

        pMovie->IsMoviePaused |= 2;
        if ( pPlayer != nullptr ) {
            pPlayer->getVariableAsString( "/:CurrentSpotType",
                                          currentSpotType );
        }
    }
    pFlashPlayer->goToFrameAndPlay( 0, "/", true );
    pFlashPlayer->setVariableValueNonLinked( "/:CurrentSpotType",
                                             currentSpotType );

    if ( gpScenaric->getCurrentSpotIndex() != -1 ) {
        pSpot = gpScenaric->getSpotList().getWorldSpotChecked(
            gpScenaric->getCurrentSpotIndex() );
    }
    pLurePicture = nullptr;
    pPendingLureSpot = pSpot;

    uint32_t levelIndex = 0u;
    uint32_t movieIndex = 0u;
    if ( gpFlash->getFlashMovieIndex( &levelIndex, &movieIndex, "CHALLENG" ) ) {
        MoviePlayer *pMovie = gpFlash->getMoviePlayer( int32_t( levelIndex ),
                                                       int32_t( movieIndex ) );
        if ( pMovie != nullptr ) {
            pMovie->IsMoviePaused &= ~2;
        }
    }

    gpWorld->getStreamingManager()->setUnderwaterTilesRefined( false );
    mngMapViewer.setSpot( pSpot, pFlashPlayer );

    bSpectatorMode = false;
    OTDU_UNIMPLEMENTED; // TODO: ONLINE (CP2PGameService +0x26b84 = -1)
    bReturningToSession = false;
    bExitPending = false;
    bShowNoSessionPopup = false;
    bSpotChangeAllowed = true;
    spotChangeTimeout = 0u;
    nbGamesRefreshTime = GetTickCountMs();

    refreshChallengeInfos();

    if ( gpScenaric->isFromDriveIn() ) {
        pFlashPlayer->setVariableValueNonLinked( "/:IsDriveInChallenge", "1" );
        OTDU_UNIMPLEMENTED; // TODO: SCENARIC (DriveInData best score)
    }

    loadPlayerCar();

    pFlashPlayer->setVariableValue(
        "/:empty_array", gpDatabase->getStringByHashcode( 0, 0x3834a02ull ) );
    pFlashPlayer->setVariableValue( "/:FamilyLocked", 0 );
    pFlashPlayer->setVariableValue( "/:FromClub",
                                    gpScenaric->getClubChallengeKind() );

    if ( gpScenaric->getClubChallengeKind() != 0 ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (club challenge host/join setup)
    }
    pFlashPlayer->setVariableValue( "/:AskRanked", 1 );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (return to an existing session,
                             // quickmatch join)

    stateChange( LS_TrackPreview );
    ShowLocalPlayerCar( false );

    AiPool *pAIPool = gpAI->getAIPool();
    pAIPool->setTrafficPaused( false );
    pAIPool->setField0x7C( true );
    pAIPool->setField0x85( true );
    gpAI->setPauseTraffic( false );
    pFlashPlayer->setVariableValue( "/:AskRanked", 1 );

    OTDU_UNIMPLEMENTED; // TODO: ONLINE (CP2PGameService +0x3f458 = 0)
    gpScenaric->setTrainingLevel( false );

    const int32_t tutorialStep = gpScenaric->getIntroTutorial().getStep();
    const bool bIsTraining =
        gameType == EGT_Race && ( tutorialStep == kTutorialStatePending ||
                                  tutorialStep == kTutorialStateRunning );
    pFlashPlayer->setVariableValue( "/:IsTraining", bIsTraining ? 1 : 0 );
}

void GMLobby::tick( float deltaTime, float totalTime ) {
    // FUN_0075ff30
    GameModeScene::tick( deltaTime, totalTime );
    if ( bExitPending ) {
        return;
    }

    if ( spotChangeTimeout != 0u && spotChangeTimeout < GetTickCountMs() ) {
        spotChangeTimeout = 0u;
        bSpotChangeAllowed = true;
    }

    stateProcess( curState, deltaTime, SP_Update );

    if ( nbGamesRefreshTime + kNbGamesRefreshDelayMs < GetTickCountMs() ) {
        nbGamesRefreshTime = GetTickCountMs();
        updateNbGames();
    }

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar != nullptr && pLocalCar->getPhysicsVehicle() != nullptr &&
         pLocalCar->getPhysicsVehicle()->isActive() ) {
        if ( !bPlaceCarRequest &&
             placeCarRequestTime + kPlaceCarRetryDelayMs < GetTickCountMs() ) {
            placeCarRequestTime = GetTickCountMs();
            bPlaceCarRequest = true;
        }

        if ( bPlaceCarRequest &&
             gpWorld->isStreamedAround( &playersViewCamera.getSpawnPosition(),
                                        true, false ) ) {
            placeLocalPlayerCar();
            bPlaceCarRequest = false;
        }
    }

    updateLurePicture();

    if ( bShowNoSessionPopup ) {
        FlashPlayer *pMessageBoxPlayer = gpFlashMessageBox->getFlashPlayer();
        // NOTE: The PC options flag (gGSPCOptions +0x25) is assumed to be the
        // main menu one.
        if ( pMessageBoxPlayer != nullptr &&
             pMessageBoxPlayer->spriteExists( "/avert" ) &&
             gpFlashMessageBox->getDisplayState() ==
                 ModalMessageBox::MMBDS_Closed &&
             !gpPCOptions->isMainMenuOpened() ) {
            bShowNoSessionPopup = false;
            TestDrive::DisplayModalPrompt(
                false, kNoSessionPopupHashcode, 0, nullptr, this, nullptr,
                kNoSessionPopupUserData, nullptr, true, false, MPT_Warning,
                gpFlashMessageBox );
        }
    }
}

void GMLobby::draw( float /*deltaTime*/, float /*totalTime*/ ) {
    // FUN_0075f8d0
    if ( !bExitRequested &&
         gpMngMapViewer->getState() == MngMapViewer::MS_Active ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_00741d30 (map cursor update)
    }
}

void GMLobby::pause() {
    // FUN_0075f5c0
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar != nullptr && pLocalCar->getDriver() != nullptr ) {
        EntityDriver *pDriver = pLocalCar->getDriver();
        pDriver->setEnvMap( false, nullptr );
        pDriver->removeInstanceReference();
        pLocalCar->unloadDriver();
        gpScenaric->getAvatarManager().releaseAvatar( pDriver );
    }

    releasePreviewCamera();

    gpRender->setSceneViewports( 0.0f, 0.0f, 1.0f, 1.0f );
    gpMngFade->setFadeRect( Eigen::Vector2f::Zero(), 1.0f, 1.0f );
    gpMngWorld->setDisableStreamingOnCar( false );
    ShowLocalPlayerCar( true );
    gpScenaric->setLobbyMapOpened( false );
    OTDU_UNIMPLEMENTED; // TODO: GSPhysics (+0x8674 = 0)

    GameModeScene::pause();
}

void GMLobby::destroy() {
    // FUN_00762560
    gpAI->setCullFromCamera( false );

    OTDU_UNIMPLEMENTED; // TODO: ONLINE (flush the local P2P car load)

    releaseLurePictures();

    if ( gpTestDriveInstance->getActiveGameMode() != GM_Mission ) {
        const EventParameters::eSource source =
            gpScenaric->getSelectedEventParameters().getSource();
        if ( source == EventParameters::ES_Buffer ||
             source == EventParameters::ES_IGEFile ||
             source == EventParameters::ES_Online ||
             source == EventParameters::ES_CustomChallenge ) {
            gpScenaric->unloadBank( pCheckpointBank );
        }
        gpScenaric->setCheckpointBank( nullptr );
        if ( pCheckpointBank != nullptr ) {
            pCheckpointBank->ScenaricBank::~ScenaricBank();
            TestDrive::FreeAligned( pCheckpointBank );
        }
    }

    GameModeWorldBase::destroy();
}

bool GMLobby::onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) {
    // FUN_0075f340
    if ( *pMessage == kMsgChallengeQuit ) {
        gpScenaric->setFromGameInvite( false );
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (leave the P2P session)

        if ( gpScenaric->isFromDriveIn() ) {
            gpScenaric->setFromDriveIn( false );
            requestExit();
            gpTestDriveInstance->setNextGameMode( GM_DriveIn );
        } else if ( gpScenaric->getClubChallengeKind() != 0 ) {
            quitLobby();
        } else {
            quitToSpotOrFreeRide();
        }
    } else if ( *pMessage == kMsgTraining ) {
        gpScenaric->setTrainingLevel( true );
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (ChangeSessionMode(-1))
        gpTestDriveInstance->setNextGameMode( GM_Mission );
        requestExit();
    }

    if ( gpMngMapViewer == nullptr ) {
        return false;
    }

    if ( !gpMngMapViewer->onFlashMessage( pMessage ) ) {
        return false;
    }

    switch ( *pMessage ) {
    case kMsgZoomIn:
        OTDU_UNIMPLEMENTED; // TODO: FUN_00741a50 (map cursor zoom in)
        return true;
    case kMsgMapOff:
        gpScenaric->setLobbyMapOpened( false );
        return true;
    case kMsgMapOn:
        gpScenaric->setLobbyMapOpened( true );
        PauseLocalPlayerCarPhysics();
        return true;
    case kMsgZoomOut:
        OTDU_UNIMPLEMENTED; // TODO: FUN_007419c0 (map cursor zoom out)
        return true;
    default:
        return true;
    }
}

void GMLobby::onClose( ePopupAnswer /*answer*/,
                       void * /*pData*/,
                       int32_t userData ) {
    // FUN_0075bf40
    if ( userData == kNoSessionPopupUserData ) {
        quitLobby();
    }
}

void GMLobby::showPlayersView() {
    // FUN_0075c270
    mngFade.startTransition( ( mngFade.getTransitionFlags() & ~0xcu ) | 1u,
                             kViewFadeDuration, &GMLobby::OnPlayersViewFadeOut,
                             reinterpret_cast<void *>( LS_PlayersView ), this );
}

void GMLobby::showTrackPreview() {
    // FUN_0075c210
    mngFade.startTransition( ( mngFade.getTransitionFlags() & ~0xcu ) | 1u,
                             kViewFadeDuration, &GMLobby::OnTrackPreviewFadeOut,
                             reinterpret_cast<void *>( LS_TrackPreview ),
                             this );
}

void GMLobby::OnPlayersViewFadeOut( void *pData, void *pSource ) {
    // FUN_0075c1e0
    GMLobby *pLobby = static_cast<GMLobby *>( pData );
    pLobby->stateChange( int32_t( reinterpret_cast<intptr_t>( pSource ) ) );
    pLobby->mngFade.setTransitionFlags( pLobby->mngFade.getTransitionFlags() |
                                        8u );
}

void GMLobby::OnTrackPreviewFadeOut( void *pData, void *pSource ) {
    // FUN_0075c170
    GMLobby *pLobby = static_cast<GMLobby *>( pData );
    pLobby->stateChange( int32_t( reinterpret_cast<intptr_t>( pSource ) ) );
    pLobby->mngFade.startTransition(
        ( pLobby->mngFade.getTransitionFlags() & ~8u ) | 5u,
        kViewFadeDuration );
}

void GMLobby::snapStreamingToPlayerSpawn() {
    // FUN_0075f850
    const Matrix4x4f_t spawnTransform = mngActor.placePlayerVehicle( 1, false );
    Eigen::Vector4f spawnPosition = spawnTransform.row( 3 ).transpose();
    gpScenaric->getLocalPlayer().setLastPosition( spawnPosition );
    gpWorld->setPreviousTransform( spawnTransform );
    gpWorld->setStreamingPosition( &spawnPosition );
}

void GMLobby::quitLobby() {
    // FUN_0075fc20
    OTDU_UNIMPLEMENTED; // TODO: ONLINE (delete the GameSpy challenge session)
    PauseLocalPlayerCarPhysics();

    const Driver *pDriver = GetDriver();
    const uint64_t teamSpotHashcode =
        uint64_t( pDriver->TeamSpotID ) |
        ( uint64_t( pDriver->field4_0x24 ) << 32ull );
    if ( teamSpotHashcode != 0ull ) {
        Spots &spots = gpScenaric->getSpotList();
        const int32_t spotIndex = spots.getSpotMapIndex( teamSpotHashcode );
        pPendingTeamSpot = spots.getWorldSpotChecked( spotIndex );
        bExitPending = true;

        // NOTE: Retail dereferences the team spot unconditionally.
        if ( pPendingTeamSpot == nullptr ) {
            return;
        }

        Eigen::Vector4f spotPosition( pPendingTeamSpot->X, pPendingTeamSpot->Y,
                                      pPendingTeamSpot->Z, 1.0f );
        if ( gpWorld->isAreaStreamingEnabled() ) {
            gpWorld->setStreamingPositionPtr( nullptr );
            if ( gpWorldStreaming->isEnabled() ) {
                gpWorldStreaming->setStreamCenter( nullptr );
            }
        }
        gpWorld->setStreamingPosition( &spotPosition );
        if ( gpWorldStreaming->isEnabled() ) {
            gpWorldStreaming->setStreamCenter( nullptr );
            gpWorldStreaming->setStreamCenterPosition( spotPosition );
            gpWorldStreaming->updateStreaming();
        }

        StreamingManager *pStreaming = gpWorld->getStreamingManager();
        pStreaming->sectorManager.bNeedStreamUpdate = true;
        pStreaming->sectorManager.bDrawSector = false;
        pStreaming->sectorManager.bStreamSector = true;
        gpWorldStreaming->updateStreaming();

        gpScenaric->setCurrentSpotIndex( spotIndex );
        LaunchSpotLevel( pPendingTeamSpot, true );
        return;
    }

    gpScenaric->setClubChallengeKind( 0 );
    gpScenaric->setFromGameInvite( false );
    OTDU_UNIMPLEMENTED; // TODO: ONLINE (leave the P2P session)
    quitToSpotOrFreeRide();
}

void GMLobby::quitToSpotOrFreeRide() {
    // FUN_0075fc20 (inlined)
    const int32_t spotIndex = gpScenaric->getCurrentSpotIndex();
    SpotMap *pCurrentSpot =
        gpScenaric->getSpotList().getWorldSpotChecked( spotIndex );
    if ( pCurrentSpot != nullptr && IsSpotRoadUnreachable( pCurrentSpot ) ) {
        requestExit();
        gpTestDriveInstance->setNextGameMode( GM_Living );
        gpScenaric->setCurrentSpotIndex( -1 );
    } else {
        requestExit();
        gpTestDriveInstance->setNextGameMode( GM_FreeRide );
    }
}

void GMLobby::onMapSpotChanged() {
    // FUN_00760600
    OTDU_UNIMPLEMENTED; // TODO: ONLINE (FUN_007601a0, FUN_00866140, map
                        // viewer selected spot broadcast)
    stateChange( LS_PlayersView );
}

EntityCheckPoint *GMLobby::findCheckpoint( const int32_t index ) {
    // FUN_0075c460
    for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
        Entity *pActor = mngActor.getActor( i );
        if ( pActor->getType() != eEntityType::ET_CHPT ) {
            continue;
        }

        EntityCheckPoint *pCheckpoint =
            static_cast<EntityCheckPoint *>( pActor );
        if ( pCheckpoint->getIndex() == index ) {
            return pCheckpoint;
        }
    }
    return nullptr;
}

bool GMLobby::stateChange( const int32_t state ) {
    // FUN_00801470
    nextState = state;
    if ( curState != LS_None ) {
        stateProcess( curState, 0.0f, SP_Term );
    }
    prevState = curState;
    curState = state;
    stateInitTime = gpTimer->GameTotalTime;
    if ( state != LS_None ) {
        stateProcess( state, 0.0f, SP_Init );
    }
    return true;
}

bool GMLobby::stateProcess( const int32_t state,
                            const float deltaTime,
                            const eStateProcess process ) {
    // FUN_0075d8c0
    switch ( state ) {
    case LS_TrackPreview:
        if ( process == SP_Init ) {
            PauseLocalPlayerCarPhysics();
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (FreeRideMgr +0x4e18 = 0)
            ShowLocalPlayerCar( false );
            if ( pPreviewCamera != nullptr ) {
                pPreviewCamera->init();
            }
        } else if ( process == SP_Update ) {
            if ( !bSpectatorMode && pPreviewCamera != nullptr ) {
                pPreviewCamera->update( deltaTime );
            }
        } else if ( process != SP_Term ) {
            return false;
        }
        return true;

    case LS_PlayersView:
        if ( process == SP_Init ) {
            OTDU_LOG_DEBUG( "GMLobby::StateProcess(PlayersView.OnInit)\n" );
            mngFade.clearTransitionCallback();
            mngFade.setTransitionFlags( mngFade.getTransitionFlags() | 8u );
            bPlayersViewStreamed = false;
            playersViewCamera.init();
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (FreeRideMgr +0x4e18 = 0)
            ShowLocalPlayerCar( true );
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (CP2PGameService +0x26b84 = -1)
            if ( bQuickmatchJoin &&
                 pFlashPlayer->getVariableAsInt( "/:IsHostGame" ) == 0 ) {
                pFlashPlayer->setVariableValue( "/:index_player", 1 );
                pFlashPlayer->showFrame( "empty", "/lobby/b0", false );
                pFlashPlayer->showFrame( "open", "/lobby/b1", false );
                bQuickmatchJoin = false;
            }
            return true;
        }
        if ( process == SP_Update ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (redraw the local car
                                     // once the session has players)
            if ( gpMngMapViewer == nullptr ||
                 gpMngMapViewer->getState() == MngMapViewer::MS_Idle ) {
                playersViewCamera.update( deltaTime );
            }
            if ( !bPlayersViewStreamed &&
                 gpWorld->isStreamedAround( &playersViewCamera.getTarget(),
                                            true, true ) ) {
                bPlayersViewStreamed = true;
                mngFade.restartTransition(
                    ( mngFade.getTransitionFlags() & ~8u ) | 5u,
                    kViewFadeDuration );
            }
            return true;
        }
        if ( process == SP_Term ) {
            OTDU_LOG_DEBUG( "GMLobby::StateProcess(PlayersView.OnTerm)\n" );
            playersViewCamera.term();
            return true;
        }
        return false;

    case LS_SpectatorView:
        if ( process == SP_Init ) {
            OTDU_LOG_DEBUG( "GMLobby::StateProcess(SpectatorView.OnInit)\n" );
            playersViewCamera.term();
            mngFade.startTransition( ( mngFade.getTransitionFlags() & ~8u ) |
                                         5u,
                                     kViewFadeDuration );
            setSpectatorMode( true );
            ShowLocalPlayerCar( false );
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (spectator voice chat)
            if ( pFlashPlayer->getVariableAsInt( "/:IsHostGame" ) == 0 ) {
                pFlashPlayer->setVariableValue( "/:index_player", 1 );
                pFlashPlayer->showFrame( "empty", "/lobby/b0", false );
                pFlashPlayer->showFrame( "open", "/lobby/b1", false );
            }
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (FreeRideMgr +0x4e18 = 0)
            return true;
        }
        if ( process == SP_Update ) {
            return true;
        }
        if ( process == SP_Term ) {
            OTDU_LOG_DEBUG( "GMLobby::StateProcess(SpectatorView.OnTerm)\n" );
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (spectator voice chat)
            setSpectatorMode( false );
            gpMngWorld->setDisableStreamingOnCar( true );
            gpCamera->setMouseCam( false );
            return true;
        }
        return false;

    default:
        return false;
    }
}

void GMLobby::resetCheckpoints() {
    // FUN_0075c7c0
    maxCheckpointIndex = 0;
    for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
        Entity *pActor = mngActor.getActor( i );
        if ( pActor->getType() != eEntityType::ET_CHPT ) {
            continue;
        }

        EntityCheckPoint *pCheckpoint =
            static_cast<EntityCheckPoint *>( pActor );
        const int32_t index = pCheckpoint->getIndex();
        if ( index < 0 ) {
            continue;
        }

        pCheckpoint->resetEventCounters();
        if ( maxCheckpointIndex < index ) {
            maxCheckpointIndex = index;
        }
    }
    gpAI->setCullFromCamera( true );
}

bool GMLobby::loadPlayerCar() {
    // FUN_00760b70
    CarDesc *pCurrentCar = gpPlayerData->getCurrentCar();
    const int32_t carID = int32_t( pCurrentCar->getCarConfigID() );
    OTDU_LOG_DEBUG( "in LoadPlayerCar_Justine_time()" );
    OTDU_UNIMPLEMENTED; // TODO: GSBrandCar (FUN_008b5860, evict one brand car)

    LocalPlayer &localPlayer = gpScenaric->getLocalPlayer();
    CarState *pLocalCar = localPlayer.getCarState();
    if ( pLocalCar == nullptr ) {
        VehicleSpawnParams params;
        params.PhantomMode = 1;
        pLocalCar = gpCarState->spawnVehicle( carID, 0, pCurrentCar->rimID,
                                              pCurrentCar->packHashcode, params,
                                              -1, 0, VIT_Player );
        localPlayer.setCarState( pLocalCar );
        if ( gpMngInGameMenu != nullptr ) {
            gpMngInGameMenu->resetWindowsDirection();
        }

        // NOTE: Retail dereferences the new car unconditionally.
        if ( pLocalCar != nullptr && pCurrentCar->isValid() &&
             pLocalCar->getVehicleInstance() != nullptr ) {
            pLocalCar->getVehicleInstance()->applyGaragePaint( *pCurrentCar );
        }
        gpPlayerData->applyDrivingOptions();
        gpFile->flushPendingAsync();

        if ( pLocalCar == nullptr ) {
            localPlayer.setCarState( nullptr );
            return false;
        }
    }

    VehicleBaseResources *pResource = pLocalCar->getResource();
    if ( pResource != nullptr && pResource->bDriveResource ) {
        int32_t clotheSet = 0;
        DBEntryCarPhysicsData *pCarData =
            gpDatabase->getCarDBEntry( pResource->CarID );
        if ( pCarData != nullptr && gpDatabase->isBike( pCarData ) ) {
            clotheSet = 1;
        }

        AvatarManager &avatarManager = gpScenaric->getAvatarManager();
        EntityAvatar *pAvatar = avatarManager.createAvatar(
            nullptr, clotheSet, false, ~0ull, false, 1 );
        if ( pAvatar != nullptr ) {
            if ( gpScenaric->placeDriver( pLocalCar, pAvatar->getDriver() ) ) {
                pAvatar->addInstanceReference();
            } else {
                avatarManager.releaseAvatar( pAvatar );
            }
        }
    }

    gpPlayerData->applyDrivingOptions();
    gpCamera->setDrivingOptions( gpPlayerData->getCameraShake(),
                                 gpPlayerData->getRadialBlurIntensity() );
    pLocalCar->openConvertibleWindows();
    return true;
}

void GMLobby::placeLocalPlayerCar() {
    // FUN_0075f6c0
    const Matrix4x4f_t startTransform = mngActor.placePlayerVehicle( 1, false );
    OTDU_LOG_DEBUG( "PlaceLocalCar _MngActor.SetCarPos %f %f %f",
                    startTransform( 3, 0 ), startTransform( 3, 1 ),
                    startTransform( 3, 2 ) );

    // Offline the local player index is -1 (grid slot 8).
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (local P2P player index)
    const int32_t localPlayerIndex = -1;
    const Matrix4x4f_t gridTransform = ComputeGridSlotTransform(
        startTransform, kNumGridSlots, 7 - localPlayerIndex, true );

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr || pLocalCar->getPhysicsVehicle() == nullptr ||
         !pLocalCar->getPhysicsVehicle()->isActive() ) {
        return;
    }

    PhysicsVehicle *pPhysicsVehicle = pLocalCar->getPhysicsVehicle();
    OTDU_UNIMPLEMENTED; // TODO: GSPhysics (+0x8674 = 1)

    const Eigen::Vector4f streamOrigin = gpWorldStreaming->isEnabled()
                                             ? gpWorldStreaming->getPosition()
                                             : Eigen::Vector4f::Zero();
    const Eigen::Vector4f delta =
        gridTransform.row( 3 ).transpose() - streamOrigin;
    if ( delta.norm() > kMaxTeleportDistance ) {
        pPhysicsVehicle->setPostponedPause( 1 );
    }

    {
        PhysicsLock lock;
        pPhysicsVehicle->resetToTransform( gridTransform, true );
    }
    pLocalCar->resetBackOnTrack();

    OTDU_UNIMPLEMENTED; // TODO: ONLINE (SendPosToOthers)
}

bool GMLobby::refreshChallengeInfos() {
    // FUN_0075e8a0
    static constexpr uint64_t kGameModeHashcodes[4] = { 0x340be70, 0x35000b0,
                                                        0x35f42f0, 0x36e8530 };
    static constexpr uint64_t kTimeAttackRulesHashcode = 0x342c1d1;
    static constexpr uint64_t kSpeedRulesHashcode = 0x3609cf7;
    static constexpr uint64_t kSpeedNoStartRulesHashcode = 0x36fdf37;
    static constexpr uint64_t kRaceRulesHashcode = 0x3708891;
    static constexpr uint32_t kSpeedRulesDatabase = 11;

    EventParameters &eventParameters = gpScenaric->getSelectedEventParameters();
    eventParameters.setNameVariable( pFlashPlayer, "/:SpotName" );

    // NOTE: Retail always enters the lobby with a spot; the -gm 23 debug boot
    // has none.
    if ( pSpot != nullptr ) {
        if ( gpMngSectorName == nullptr ) {
            pFlashPlayer->setVariableValueNonLinked( "/:Adress", " " );
        } else {
            const char *pSectorName = gpMngSectorName->getSectorName(
                Eigen::Vector4f( pSpot->X, pSpot->Y, pSpot->Z, 0.0f ) );
            if ( pSectorName != nullptr ) {
                pFlashPlayer->setVariableValueNonLinked( "/:Adress",
                                                         pSectorName );
            }
        }
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (session challenge name and spot)

    // NOTE: Retail keys the driving rules and the cup lookup below with the
    // spot index (the high dword of the rules hashcode is uninitialized there).
    const int32_t spotIndex =
        ( pSpot != nullptr )
            ? gpScenaric->getSpotList().getSpotMapIndex( pSpot->Hashcode )
            : -1;
    eventParameters.setNameVariable( pFlashPlayer, "/:ChallengeName" );
    eventParameters.setNameVariable( pFlashPlayer, "/:Title" );

    const char *pRulesText = nullptr;
    GameParameterDBString &description =
        eventParameters.getDatabaseDescription();
    if ( description.getValue() >= 0 ) {
        pFlashPlayer->setVariableValue( "/:GameRules",
                                        description.getDisplayText() );
    } else {
        uint32_t rulesDatabase = 0;
        uint64_t rulesHashcode = 0ull;
        switch ( eventParameters.getGameType() ) {
        case EGT_TimeAttack:
            rulesHashcode = kTimeAttackRulesHashcode;
            break;
        case EGT_Speed:
            rulesDatabase = kSpeedRulesDatabase;
            rulesHashcode =
                ( eventParameters.getStartChallenge().getValue() != 0 )
                    ? kSpeedRulesHashcode
                    : kSpeedNoStartRulesHashcode;
            break;
        case EGT_Race:
            rulesHashcode = kRaceRulesHashcode;
            break;
        default:
            rulesHashcode = uint64_t( uint32_t( spotIndex ) );
            break;
        }

        if ( rulesHashcode != 0ull ) {
            pRulesText =
                gpDatabase->getStringByHashcode( rulesDatabase, rulesHashcode );
        }
        if ( pRulesText != nullptr ) {
            pFlashPlayer->setVariableValue( "/:GameRules", pRulesText );
        } else {
            pFlashPlayer->setVariableValueNonLinked( "/:GameRules", " " );
        }
    }

    char rewardText[0x20] = " ";
    const int32_t reward =
        int32_t( eventParameters.getRewardMoney().getValue() );
    if ( reward > 0 ) {
        FormatMoneyString( rewardText, reward );
    }
    pFlashPlayer->setVariableValueNonLinked( "/:GoldReward", rewardText );

    int32_t cup =
        gpPlayerData->getLoadedSave()->getRanking()->getChallengeResult(
            uint64_t( int64_t( spotIndex ) ) );
    if ( cup <= 0 ) {
        cup = 4;
    }
    pFlashPlayer->setVariableValue( "/:WhatCup", cup );

    if ( eventParameters.getGameType() != EGT_Speed ) {
        int32_t numCheckpoints = 0;
        for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
            Entity *pActor = mngActor.getActor( i );
            if ( pActor->getType() != eEntityType::ET_CHPT ) {
                continue;
            }
            numCheckpoints++;
            const EntityCheckPoint *pCheckpoint =
                static_cast<const EntityCheckPoint *>( pActor );
            const Eigen::Vector4f position =
                pCheckpoint->getModelMatrix().row( 3 ).transpose();
            ProjectOnRoad(
                position, nullptr, nullptr, false,
                mngTracRoadMap.getWaypoint( pCheckpoint->getIndex() - 1 ) );
        }

        mngTracRoadMap.setLobby( true );
        mngTracRoadMap.setNumWaypoints( numCheckpoints );
        mngTracRoadMap.solveLegs( true );
        mngTracRoadMap.setLobby( false );
        routeDistanceKm =
            ComputeEventRouteLength( &eventParameters, &mngTracRoadMap ) *
            0.001f;

        char distanceText[0x80];
        FormatRouteDistance( routeDistanceKm, distanceText );
        pFlashPlayer->setVariableValueNonLinked( "/:Distance", distanceText );
    }

    const int32_t gameTypeIndex = eventParameters.getGameType();
    const char *pGameModeText = pRulesText;
    if ( gameTypeIndex >= 0 && gameTypeIndex < 4 ) {
        pGameModeText = gpDatabase->getStringByHashcode(
            0, kGameModeHashcodes[gameTypeIndex] );
    }
    if ( pGameModeText != nullptr ) {
        pFlashPlayer->setVariableValue( "/:GameMode", pGameModeText );
    } else {
        pFlashPlayer->setVariableValueNonLinked( "/:GameMode", "" );
    }

    // Offline: the online session state (gGSOnline +0xdc/+0xe0) is never set.
    pFlashPlayer->setVariableValueNonLinked(
        "/:WhatSpotType",
        GetSpotDisplayTypeName( eventParameters.getSpotType() - EST_Solo ) );
    pFlashPlayer->setVariableValueNonLinked(
        "/:WhatSpot", ( gameTypeIndex >= 0 && gameTypeIndex < 4 )
                          ? GetSpotDisplayName( gameTypeIndex )
                          : "" );
    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    const CarDesc &currentCar =
        pSave->getGarage()->pCars[pSave->getDriver()->CurrentCar];
    pFlashPlayer->setVariableValue( "/:player_car",
                                    currentCar.getConfig()->FullName.c_str() );

    updateTrackPreview( pSpot );

    char restrictionText[0x80];
    if ( eventParameters.getSpotType() == EST_Solo ) {
        pFlashPlayer->setVariableValueNonLinked( "/:IsSolo", "1" );
        pFlashPlayer->setVariableValueNonLinked( "/:IsOnline", "0" );
        eventParameters.buildVehicleRestrictionText(
            restrictionText, sizeof( restrictionText ) - 1, false );
        eventParameters.setGroupRestrictionVars( pFlashPlayer );
        pFlashPlayer->setVariableValueNonLinked( "/:vehicle_restriction",
                                                 restrictionText );
        updateNbGames();
    } else {
        pFlashPlayer->setVariableValueNonLinked( "/:IsSolo", "0" );
        pFlashPlayer->setVariableValueNonLinked( "/:IsOnline", "1" );
        updateNbGames();
        eventParameters.buildVehicleRestrictionText(
            restrictionText, sizeof( restrictionText ) - 1, false );
        eventParameters.setGroupRestrictionVars( pFlashPlayer );
        pFlashPlayer->setVariableValueNonLinked( "/:vehicle_restriction",
                                                 restrictionText );
    }

    updateRecordsAndRewards();
    updateLevelAndCarLocks();
    return true;
}

void GMLobby::updateTrackPreview( SpotMap *pTargetSpot ) {
    // FUN_0075e650
    if ( pTargetSpot == nullptr ) {
        releasePreviewCamera();
        gpRender->setSceneViewports( 0.0f, 0.0f, 1.0f, 1.0f );
        gpMngFade->setFadeRect( Eigen::Vector2f::Zero(), 1.0f, 1.0f );
        return;
    }

    char isTrackPreview[32] = "1";
    gpCamera->findAndSetCameraMode( 0, 0, 1.0f );

    const Eigen::Vector4f spotPosition( pTargetSpot->X, pTargetSpot->Y,
                                        pTargetSpot->Z, 1.0f );
    AiRoadLocation location;
    ProjectOnRoad( spotPosition, nullptr, nullptr, false, &location );
    const Eigen::Vector4f roadPosition = location.Frame.row( 3 ).transpose();

    CameraGame *pCamera = gpCamera->getActiveCamera();
    pCamera->setTarget( Eigen::Vector4f(
        roadPosition.x() + 1.0f, roadPosition.y(), roadPosition.z(), 1.0f ) );
    pCamera->setPosition( roadPosition );
    pCamera->updateMatrix( pCamera->getWorldPositionOffset(), pCamera->getUp(),
                           pCamera->getTargetOffset() );

    releasePreviewCamera();

    switch ( gameType ) {
    case EGT_TimeAttack:
    case EGT_Race:
        pPreviewCamera = &routePreviewCamera;
        break;
    case EGT_Speed:
        pPreviewCamera =
            ( maxCheckpointIndex > 1 )
                ? static_cast<LobbyCamera *>( &checkpointFlybyCamera )
                : static_cast<LobbyCamera *>( &startOrbitCamera );
        isTrackPreview[0] = '0';
        break;
    default:
        pPreviewCamera = &startOrbitCamera;
        isTrackPreview[0] = '0';
        break;
    }

    pFlashPlayer->setVariableValueNonLinked( "/:IsTrackPreview",
                                             isTrackPreview );
}

bool GMLobby::updateNbGames() {
    // FUN_0075d240
    if ( pSpot != nullptr && spotType != EST_Solo ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (per-spot session stats)
        pFlashPlayer->setVariableValue( "/:NbGames", 0 );
        return true;
    }

    pFlashPlayer->setVariableValueNonLinked( "/:NbGames", "--" );
    return true;
}

void GMLobby::updateRecordsAndRewards() {
    // FUN_0075e1a0
    static constexpr uint64_t kNoRecordHashcode = 0x3a8ac52;
    static constexpr float kMetersPerSecondToMph = 2.2374146f;
    static constexpr float kMetersPerSecondToKmh = 3.6f;
    static constexpr float kSilverRewardRatio = 0.25f;
    static constexpr float kBronzeRewardRatio = 0.1f;

    SpotMap *pCurrentSpot = gpScenaric->getSpotList().getWorldSpotChecked(
        gpScenaric->getCurrentSpotIndex() );
    const uint64_t spotHashcode =
        ( pCurrentSpot != nullptr ) ? pCurrentSpot->Hashcode : 0ull;
    SavedRanking *pRanking = gpPlayerData->getLoadedSave()->getRanking();
    const int32_t cup = pRanking->getChallengeResult( spotHashcode );
    pFlashPlayer->setVariableValue( "/:MyCup", ( cup <= 0 ) ? 0 : cup );

    if ( spotType != EST_Solo ) {
        return;
    }

    EventParameters &eventParameters = gpScenaric->getSelectedEventParameters();
    const int32_t reward =
        int32_t( eventParameters.getRewardMoney().getValue() );
    const char *pNoRecordText =
        gpDatabase->getStringByHashcode( 0, kNoRecordHashcode );
    bool bWinnerOnly = false;
    char text[0x20];
    switch ( gameType ) {
    case EGT_Race:
        if ( cup != -1 ) {
            FormatRaceTime( text,
                            pRanking->getChallengeBestScore( spotHashcode ) );
            pFlashPlayer->setVariableValueNonLinked( "/:Player_record", text );
        } else {
            pFlashPlayer->setVariableValueNonLinked( "/:Player_record",
                                                     pNoRecordText );
        }
        pFlashPlayer->setVariableValueNonLinked( "/:TimeList_ref1", " " );
        pFlashPlayer->setVariableValueNonLinked( "/:TimeList_ref2", " " );
        pFlashPlayer->setVariableValueNonLinked( "/:TimeList_ref3", " " );
        if ( eventParameters.getNumOpponents().getValue() == 1 ||
             ( eventParameters.getLoop().getValue() >= 1 &&
               eventParameters.getNumLaps().getValue() == 0 ) ) {
            bWinnerOnly = true;
        }
        break;
    case EGT_Speed: {
        if ( cup != -1 ) {
            const CommonDB &commonDatabase = gpDatabase->getCommonDB();
            float speed = pRanking->getChallengeBestScore( spotHashcode );
            const char *pUnit = nullptr;
            if ( gpPlayerData->isUsingImperialUnits() ) {
                speed *= kMetersPerSecondToMph;
                pUnit = commonDatabase.CommonStrings[0];
            } else {
                speed *= kMetersPerSecondToKmh;
                pUnit = commonDatabase.CommonStrings[1];
            }
            snprintf( text, sizeof( text ), "%.2f %s", speed, pUnit );
            pFlashPlayer->setVariableValueNonLinked( "/:Player_record", text );
        } else {
            pFlashPlayer->setVariableValueNonLinked( "/:Player_record",
                                                     pNoRecordText );
        }
        pFlashPlayer->setVariableValueNonLinked(
            "/:TimeList_ref1",
            eventParameters.getGoldSpeed().getDisplayText() );
        pFlashPlayer->setVariableValueNonLinked(
            "/:TimeList_ref2",
            eventParameters.getSilverSpeed().getDisplayText() );
        pFlashPlayer->setVariableValueNonLinked(
            "/:TimeList_ref3",
            eventParameters.getBronzeSpeed().getDisplayText() );
    } break;
    case EGT_TimeAttack:
        FormatRaceTime(
            text, float( eventParameters.getGoldTime().getValue() ) * 0.001f );
        pFlashPlayer->setVariableValueNonLinked( "/:TimeList_ref1", text );
        FormatRaceTime( text,
                        float( eventParameters.getSilverTime().getValue() ) *
                            0.001f );
        pFlashPlayer->setVariableValueNonLinked( "/:TimeList_ref2", text );
        FormatRaceTime( text,
                        float( eventParameters.getBronzeTime().getValue() ) *
                            0.001f );
        pFlashPlayer->setVariableValueNonLinked( "/:TimeList_ref3", text );
        if ( cup != -1 ) {
            FormatRaceTime( text,
                            pRanking->getChallengeBestScore( spotHashcode ) );
            pFlashPlayer->setVariableValueNonLinked( "/:Player_record", text );
        } else {
            pFlashPlayer->setVariableValueNonLinked( "/:Player_record",
                                                     pNoRecordText );
        }
        break;
    default:
        break;
    }

    if ( reward <= 0 ) {
        pFlashPlayer->setVariableValueNonLinked( "/:Reward1", " " );
        pFlashPlayer->setVariableValueNonLinked( "/:Reward2", " " );
        pFlashPlayer->setVariableValueNonLinked( "/:Reward3", " " );
        return;
    }

    pFlashPlayer->setVariableValueNonLinked(
        "/:Reward1", FormatMoneyString( text, reward ) );
    if ( bWinnerOnly ) {
        pFlashPlayer->setVariableValueNonLinked( "/:Reward2",
                                                 FormatMoneyString( text, 0 ) );
        pFlashPlayer->setVariableValueNonLinked( "/:Reward3",
                                                 FormatMoneyString( text, 0 ) );
        return;
    }
    pFlashPlayer->setVariableValueNonLinked(
        "/:Reward2", FormatMoneyString( text, int32_t( float( reward ) *
                                                       kSilverRewardRatio ) ) );
    pFlashPlayer->setVariableValueNonLinked(
        "/:Reward3", FormatMoneyString( text, int32_t( float( reward ) *
                                                       kBronzeRewardRatio ) ) );
}

void GMLobby::updateLevelAndCarLocks() {
    // FUN_0075d3a0
    bool bLevelTooLow = false;
    bool bCurrentCarOK = false;
    bool bOtherCarOK = false;
    EventParameters &eventParameters = gpScenaric->getSelectedEventParameters();
    eventParameters.checkRequirements( &bLevelTooLow, &bCurrentCarOK,
                                       &bOtherCarOK );
    const int32_t carState = bCurrentCarOK ? 1 : ( bOtherCarOK ? 2 : 0 );

    const bool bSolo = eventParameters.getSpotType() == EST_Solo;
    if ( bSolo ) {
        const int32_t level = int32_t( eventParameters.getLevel().getValue() );
        pFlashPlayer->setVariableValue( "/:Level", ( level != 0 ) ? level : 7 );
    } else {
        pFlashPlayer->setVariableValueNonLinked( "/:Level_range", "" );
        pFlashPlayer->setVariableValue( "/:Level_min", 7 );
        pFlashPlayer->setVariableValue( "/:Level_max", 7 );
    }

    pFlashPlayer->setVariableValue( "/:IsLevelOK", bLevelTooLow ? 0 : 1 );
    pFlashPlayer->setVariableValue( "/:IsCarOK", carState );
    pFlashPlayer->setVariableValue(
        "/:IsChaLocked",
        ( ( !bCurrentCarOK || bLevelTooLow ) && bSolo ) ? 1 : 0 );
}

void GMLobby::updateLurePicture() {
    // FUN_0075ff30 (inlined)
    if ( pPendingLureSpot == nullptr ) {
        return;
    }

    if ( lurePictureBank.isReady() ) {
        pLurePicture = nullptr;
        lurePictureBank.release();
        return;
    }

    if ( lurePictureBank.isLoading() ) {
        return;
    }

    char spotName[9] = {};
    memcpy( spotName, &pPendingLureSpot->Hashcode, 8 );
    if ( !lurePictureBank.load( spotName, MapScreenshotBank::C_IGE,
                                &GMLobby::OnLurePictureLoaded, this ) ) {
        fallbackLureBank.loadImmediate();
        pLurePicture = fallbackLureBank.getMaterial( "undf" );
        if ( pLurePicture != nullptr ) {
            gFlashCallbacksRegister.add(
                "picture", &GMLobby::LurePictureDrawCallback, this );
        }
    }
    pPendingLureSpot = nullptr;
    pFlashPlayer->setVariableValue( "/:InfosLoaded", 1 );
}

void GMLobby::onLurePictureLoaded() {
    // FUN_0075feb0
    pLurePicture = lurePictureBank.getMaterial();
    if ( pLurePicture == nullptr ) {
        fallbackLureBank.loadImmediate();
        pLurePicture = fallbackLureBank.getMaterial( "undf" );
    }

    if ( pLurePicture != nullptr ) {
        gFlashCallbacksRegister.add( "picture",
                                     &GMLobby::LurePictureDrawCallback, this );
    }
}

void GMLobby::releaseLurePictures() {
    // FUN_0075d860
    gFlashCallbacksRegister.removeAllSynonyms(
        &GMLobby::LurePictureDrawCallback );
    pLurePicture = nullptr;
    fallbackLureBank.release();
    lurePictureBank.release();
    if ( lurePictureBank.getBuffer() != nullptr ) {
        TestDrive::FreeAligned( lurePictureBank.getBuffer() );
        lurePictureBank.setBuffer( nullptr );
    }
}

bool GMLobby::OnLurePictureLoaded( MapScreenshotBank * /*pBank*/,
                                   void *pUserData ) {
    // LAB_0075ff20
    if ( pUserData != nullptr ) {
        static_cast<GMLobby *>( pUserData )->onLurePictureLoaded();
    }
    return true;
}

void GMLobby::LurePictureDrawCallback( FlashRECT * /*pViewport*/,
                                       float /*depth*/,
                                       DrawList *pDrawList,
                                       void *pUserData,
                                       FlashResource *pResource,
                                       const char * /*pClipName*/ ) {
    // FUN_0075d2e0
    GMLobby *pLobby = static_cast<GMLobby *>( pUserData );
    if ( pLobby == nullptr || pLobby->pFlashPlayer == nullptr ) {
        return;
    }

    FlashClip pictureClip;
    if ( !pLobby->pFlashPlayer->getClipPlayer( "spotname/picture",
                                               &pictureClip ) ) {
        return;
    }

    FlashRECT viewport;
    pictureClip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );
    if ( pLobby->pLurePicture == nullptr ) {
        return;
    }

    pDrawList->setActiveMaterial( pLobby->pLurePicture );
    pDrawList->setDiffuse( 0, 0xffffffffu );
    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, viewport.X, viewport.Y,
                   viewport.X + viewport.Width, viewport.Y + viewport.Height,
                   pResource );
}
