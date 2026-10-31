#include "shared.h"
#include "gm_freeride.h"

#include "gs_timer.h"
#include "tdu_instance.h"

#include "ai/ai_pool.h"
#include "ai/ai_road_query.h"
#include "ai/gs_ai.h"
#include "config/gs_config.h"
#include "core/time.h"
#include "input/input_device.h"
#include "physics/gs_physics.h"

#include "database/gs_database.h"
#include "database/db_brand.h"
#include "database/db_car_physics_data.h"
#include "game/game_mechanism_player.h"
#include "game/gs_tutorial.h"
#include "game/cinematic/mng_cinematics.h"
#include "game/mng_hud.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/scenaric/entity/entity_convoy.h"
#include "game/scenaric/entity/entity_extra_challenge.h"
#include "game/scenaric/entity/entity_shopping_girl.h"
#include "game/scenaric/entity/entity_spot_mesh.h"
#include "game/scenaric/entity/entity_transporter.h"
#include "game/scenaric/game_intro_tutorial.h"
#include "game/scenaric/local_player.h"
#include "game/scenaric/spots.h"
#include "game/scenaric/mng_actor.h"
#include "game/vehicle/car_desc.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "flash/mng_fade.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "flash/mng_flash_localize.h"
#include "flash/gs_flash.h"
#include "flash/movie_player.h"
#include "render/camera/camera_gps.h"
#include "render/gs_render.h"
#include "world/streaming_manager.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"
#include "player_data/saved_item_table.h"
#include "player_data/driver.h"
#include "player_data/achievements_data.h"
#include "render/camera/gs_camera.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_data/saved_road_discovery.h"
#include "render/camera/camera_vehicle.h"

static bool gbGPSFlyUpDone = false;     // DAT_010e7c90
static int32_t gGPSFlyUpTargetMode = 0; // DAT_010dfe00

static constexpr uint64_t kRewardBikeDealership = 0x5f2d20ull;
static constexpr uint64_t kRewardCarPaintDeluxe = 0x5da680ull;

static constexpr uint32_t kAchievementDBIndex = 0xfu;

static constexpr float kSpotIntroFadeDuration = 0.5f;   // FLOAT_00f41674
static constexpr float kSpotIntroFadeInDuration = 0.2f; // DAT_00f41730
static constexpr float kTransporterFareRatio = 0.1f;    // DAT_00f41668
static constexpr uint32_t kActivityReentryDelay = 5000u;
static constexpr uint64_t kRefusalNoPassengerHashcode = 0x560449;
static constexpr uint64_t kRefusalNoPassenger2Hashcode = 0x3c3ae62;
static constexpr uint64_t kRefusalRestrictionHashcode = 0x578ae9;
static constexpr uint64_t kBikeCategoryHashcode = 0x35da5b5;
static constexpr uint64_t kShoppingGirlQuestionHashcode = 0x52f709;
static constexpr uint64_t kHitchHikerQuestionHashcode = 0x9de2b9;
static constexpr uint64_t kConvoyQuestionHashcode = 0x5a9829;
static constexpr uint64_t kTransporterQuestionHashcode = 0x591189;
static constexpr uint64_t kExtraChallengeQuestionHashcode = 0x547da9;
static constexpr uint64_t kNoPassengerBrandHashcode = 0x3540681;

static Eigen::Vector4f GetWorldShift() {
    if ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() ) {
        return gpWorldStreaming->getPosition();
    }
    return Eigen::Vector4f::Zero();
}

static bool IsMapScreenshotCapturing() {
    return gpMngMapScreenshot != nullptr && gpMngMapScreenshot->isCapturing();
}

static uint32_t GetTickCountMs() {
    return uint32_t( MonotonicToMs( GetTimeMonotonic() ) );
}

GMFreeride::GMFreeride()
    : GameModeWorld( 6 ),
      mngBookmark(),
      mngFRMeetingIC(),
      mngPhotoMode(),
      mngCamGPS(),
      mngFontPlayers(),
      mngLogoDisplay(),
      mngMapScreenshot(),
      mngInGameMenu(),
      mngChat(),
      mngPedestrians(),
      flashListSpots(),
      bResumedFromPauseMenu( false ),
      bEnterSpotClipVisible( false ),
      enterSpotClipTimer( 0.0f ),
      spotBank(),
      nextIntro(),
      pIntroCine( nullptr ),
      pSavedVehicleCamera( nullptr ),
      bIntroLevelLaunched( false ),
      bIntroCineDone( false ),
      pExtraChallengeAvatar( nullptr ),
      extraChallengeKind( -1 ),
      extraChallengeProp{ nullptr },
      bCanAcceptExtraChallenge( false ),
      pExtraChallengeRefusal( nullptr ),
      pExtraChallengeRefusal2( nullptr ),
      vehicleRestrictionText{},
      pExtraChallenges{},
      numExtraChallenges( 0 ),
      pNearestExtraChallenge( nullptr ),
      pExtraChallengeMarker( nullptr ),
      pExtraChallengeIntroCine( nullptr ),
      pExtraChallengeLoopCine( nullptr ),
      bExtraChallengeLaunched( false ),
      pPendingSpot( nullptr ),
      activityReentryTick( 0u ) {
    // FUN_00793880
    if ( gpMngMapScreenshot == nullptr ) {
        gpMngMapScreenshot = &mngMapScreenshot;
    }

    registerManager( &mngBookmark );
    registerManager( &mngFRMeetingIC );
    registerManager( &mngPhotoMode );
    registerManager( &mngFontPlayers );
    registerManager( &mngLogoDisplay );

    if ( gbMapScreenshotsMngEnabled ) {
        registerManagerFirst( &mngMapScreenshot );
    }

    registerManager( &mngInGameMenu );
    registerManager( &mngCamGPS );
    registerManager( &mngChat );
    if ( ArePedestriansEnabled() ) {
        registerManagerFirst( &mngPedestrians );
    }
#if OTDU_DEVBUILD
    if ( gbPSOCollectorEnabled ) {
        registerManager( &mngPSOCollector );
    }
#endif

    mngFlash.setWantsAsyncInstall( true );
    mngBookmark.setMngFlash( &mngFlash );

    // Player-car game mechanic (overrides the GameModeWorld defaults).
    gameMechType = 4;    // player car
    gameMechID = 0xc0c1; // GMFreeRide-specific mechanic id

    // When returning from the map screen, use a fast single-tick transition.
    if ( gpTestDriveInstance != nullptr &&
         gpTestDriveInstance->getPreviousGameMode() == GM_Map &&
         ( gpScenaric == nullptr || gpScenaric->getIntroTutorial().getStep() !=
                                        kTutorialStateCarBought ) ) {
        transitionFlags = 0x11;
        numTicksPerTransition = 1;
    }
}

GMFreeride::~GMFreeride() {
    // FUN_00793750
    // Restore the game speed that was saved when the pause menu was opened.
    if ( gpTimer != nullptr ) {
        gpTimer->GameSpeed = gpTimer->PreviousGameSpeed;
    }
}

void GMFreeride::initialize() {
    // FUN_00797340
    if ( gpScenaric != nullptr ) {
        gpScenaric->getSpotList().spawnPendingSectorSpotMeshes();
    }

    if ( gpScenaric != nullptr ) {
        nextIntro = gpScenaric->getNextIntro();
        gpScenaric->getNextIntro().bIsActive = false;
    }

    if ( gpMngFontPlayers != nullptr ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (reset the 16 P2P player name
                                 // tags at CP2PGameService+0x2e0d0)
    }

    if ( nextIntro.bIsActive && nextIntro.NextGameMode == GM_FreeRide ) {
        if ( nextIntro.pSpot == nullptr ) {
            pIntroCine = gpScenaric->getIntroTutorial().loadLandingCinematic();
        } else {
            Spots &spots = gpScenaric->getSpotList();
            resolveNextIntroCine(
                spots.getSpotSector( nextIntro.pSpot->Hashcode ) );

            const Eigen::Vector4f spotPosition( nextIntro.pSpot->X,
                                                nextIntro.pSpot->Y,
                                                nextIntro.pSpot->Z, 1.0f );
            gpWorld->setPlayerPosition( &spotPosition );
            gpScenaric->getLocalPlayer().setLastPosition( spotPosition );
        }

        if ( pIntroCine == nullptr ) {
            nextIntro.bIsActive = false;
        }
    }

    if ( gpScenaric != nullptr ) {
        gpScenaric->setICEnabled( false );
    }

    if ( gpTestDriveInstance->getPreviousGameMode() != GM_Map ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: GSPUB
    }

    GameModeWorld::initialize();
}

void GMFreeride::resolveNextIntroCine( const SpotSector *pSector ) {
    // FUN_00797340 (inlined)
    pIntroCine = FindStreamedSpotCinematic( pSector, nextIntro.pCinematicName );
    if ( pIntroCine == nullptr ) {
        return;
    }

    pIntroCine->setGameModeInterface( this );
    pIntroCine->setStopWhenSkipped( true );
    pIntroCine->installMusicBanks();

    gpWorld->setAreaStreamingEnabled( false );
}

static void SetHudMoviesPaused( const bool bPaused ) {
    const int32_t numMovies = gpFlash->getNumMovies( 1 );
    for ( int32_t i = 0; i < numMovies; i++ ) {
        MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, i );
        if ( pMovie == nullptr ) {
            continue;
        }
        if ( bPaused ) {
            pMovie->IsMoviePaused |= 2;
        } else {
            pMovie->IsMoviePaused &= ~2;
        }
    }
}

static void HideLocalPlayerCar( CarState *pCarState ) {
    PhysicsVehicle *pPhysicsVehicle = pCarState->getPhysicsVehicle();
    if ( pPhysicsVehicle != nullptr ) {
        PhysicsLock physLock;
        pPhysicsVehicle->setPostponedPause( 1 );
    }

    VehicleBase *pVehicle = pCarState->getVehicleInstance();
    if ( pVehicle != nullptr ) {
        pVehicle->setShouldDraw( false );
    }

    EntityDriver *pDriver = pCarState->getDriver();
    if ( pDriver != nullptr ) {
        pDriver->removeInstanceReference();
    }
}

void GMFreeride::reset() {
    // FUN_00796830
    gpPhysics->setForceFeedbackAllowed( true );
    bIntroCineDone = false;
    activityReentryTick = 0u;
    gbGPSFlyUpDone = false;
    gGPSFlyUpTargetMode = 0;

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (local P2P player reset)

    AiPool *pAIPool = gpAI->getAIPool();
    if ( !gpScenaric->getIntroTutorial().isActive() ) {
        pAIPool->setPoliceScale( 1.0f );
        pAIPool->setForceDensity( false );
        gpAI->setDisableCops( false );
    } else {
        pAIPool->setPoliceScale( 0.0f );
        pAIPool->setForceDensity( false );
    }
    gpAI->getCopManager().setLevel( true, true );

    int32_t spawnMode = 6;
    switch ( gpTestDriveInstance->getPreviousGameMode() ) {
    case GM_BootMenu:
    case GM_Login:
        spawnMode = 8;
        break;
    case GM_Map:
    case GM_Mission:
    case GM_GameModeEdit:
    case GM_InstantChallenge:
    case GM_Estates:
        spawnMode = 7;
        break;
    default:
        spawnMode = 6;
        break;
    }
    mngActor.preparePlayerSpawnStreaming( spawnMode );
    GameModeWorld::reset();

    getPlayerGameMech()->setSpawnFrame(
        mngActor.placePlayerVehicle( spawnMode, true ) );

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( !nextIntro.bIsActive ) {
        bIntroCineDone = true;
    } else if ( pLocalCar != nullptr ) {
        HideLocalPlayerCar( pLocalCar );
    }

    if ( pLocalCar != nullptr && gpAI->getCopManager().isInfringementSaved() ) {
        gpAI->getCopManager().restoreInfringement();
    }

    pGameMech->stateChange( PlayerGameMech::MS_FR_LoadingMap );

    gpWorld->getStreamingManager()->setUnderwaterTilesRefined( false );

    if ( gpMngHud != nullptr ) {
        gpScenaric->pushObjectivesToCompass( gpMngHud );
    }

    if ( gpTestDriveInstance->getPreviousGameMode() == GM_Map ) {
        startMapFlyDown();
    } else {
        StreamingManager *pStreaming = gpWorld->getStreamingManager();
        pStreaming->sectorManager.bDrawSector = false;
        pStreaming->sectorManager.bNeedStreamUpdate = true;
        pStreaming->areaGrid.bNeedStreamUpdate = true;
        pStreaming->areaGrid.bDrawArea = false;
        pStreaming->setActiveRenderLayers( 1 );
    }

    if ( !gpScenaric->getIntroTutorial().isActive() ) {
        gatherExtraChallenges();
    }

    pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    PhysicsVehicle *pPhysicsVehicle =
        ( pLocalCar != nullptr ) ? pLocalCar->getPhysicsVehicle() : nullptr;
    if ( pPhysicsVehicle != nullptr && pPhysicsVehicle->isSimulated() ) {
        pPhysicsVehicle->setDrivingHelpLevel(
            gpPlayerData->getDrivingHelpLevel() );
    }
}

static bool FilterMapFlyMovies( const char *pMovieName ) {
    // FUN_00792c90
    if ( pMovieName != nullptr && ( strcmp( pMovieName, "HUD" ) == 0 ||
                                    strcmp( pMovieName, "GAUGES" ) == 0 ) ) {
        return gpCamera->getActiveCamera()->getType() != 3;
    }
    return true;
}

void GMFreeride::startMapFlyDown() {
    // FUN_00796830 (inlined)
    gpCamera->setMouseCam( false );
    gpCamera->setUseBlur( true );
    SetHudMoviesPaused( true );
    if ( gpMngHud != nullptr ) {
        gpMngHud->setMovieFilter( &FilterMapFlyMovies );
    }

    Viewport *pBackViewport = gpRender->getRenderPass<RP_Back>().pViewport;
    pBackViewport->ClearColor.R = 0x22;
    pBackViewport->ClearColor.G = 0x20;
    pBackViewport->ClearColor.B = 0x32;
    pBackViewport->ClearColor.A = 0xff;

    Eigen::Vector4f focus =
        gpWorld->getPreviousTransform().row( 3 ).transpose();
    CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
    PhysicsVehicle *pPhysicsVehicle =
        ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;
    if ( pPhysicsVehicle != nullptr ) {
        focus = pPhysicsVehicle->getChassisWorldMatrix().row( 3 ).transpose();
    }

    CameraGPS *pCameraGPS = static_cast<CameraGPS *>(
        gpCamera->findCameraWithMode( 3, gpCamera->getBoundCarState() ) );
    pCameraGPS->setFocus( focus );

    gpCamera->setBoundCarState( pCarState );
    if ( pCarState != nullptr ) {
        gpCamera->setTargetPhysEntity( pPhysicsVehicle );
    }
    gpFlash->getFlashSound()->playFrontEndSound( "exit_map", 0, false );

    CameraGame *pActiveCamera = gpCamera->getActiveCamera();
    if ( pActiveCamera == nullptr || pActiveCamera->getType() != 3 ) {
        return;
    }

    if ( gpActiveGM != nullptr ) {
        gpActiveGM->setBlockPauseByStart( true );
    }

    CameraGame *pVehicleCamera = gpCamera->findCameraByCurrentTransition();
    static_cast<CameraVehicle *>( pVehicleCamera )->resetTransform();

    pVehicleCamera->tick( std::numeric_limits<float>::denorm_min(), false,
                          true );

    if ( pCameraGPS != nullptr ) {
        const Eigen::Vector3f target = pVehicleCamera->getTarget();
        const Eigen::Vector3f up = pVehicleCamera->getUp();
        pCameraGPS->setFixedPose(
            *pVehicleCamera->getWorldPositionRW(),
            Eigen::Vector4f( target.x(), target.y(), target.z(), 1.0f ),
            Eigen::Vector4f( up.x(), up.y(), up.z(), 0.0f ),
            pVehicleCamera->getNearPlane(), pVehicleCamera->getFarPlane(),
            pVehicleCamera->getFOV(), pVehicleCamera->getAspectRatio() );
        pCameraGPS->requestZoom(
            pCameraGPS->getZoom(), CameraGPS::GZL_Fixed,
            &GMFreeride::OnGPSFlyUpEnd, this,
            reinterpret_cast<void *>( static_cast<intptr_t>( GM_FreeRide ) ) );
        gpCamera->setCameraMode( 3, 0, 1.0f );
    }

    mngCamGPS.setUpdateStreaming( true );
    if ( pPhysicsVehicle != nullptr ) {
        PhysicsLock physLock;
        pPhysicsVehicle->setPostponedPause( 1 );
    }
}

void GMFreeride::tick( float deltaTime, float totalTime ) {
    // FUN_00796260
    updateGPSFlyUp();

    if ( pIntroCine == nullptr ) {
        if ( nextIntro.bIsActive && nextIntro.NextGameMode == GM_FreeRide &&
             nextIntro.pSpot != nullptr ) {
            Spots &spots = gpScenaric->getSpotList();
            const SpotSector *pSector =
                spots.getSpotSector( nextIntro.pSpot->Hashcode );
            if ( pSector != nullptr ) {
                resolveNextIntroCine( pSector );

                const Eigen::Vector4f spotPosition( nextIntro.pSpot->X,
                                                    nextIntro.pSpot->Y,
                                                    nextIntro.pSpot->Z, 1.0f );
                gpWorld->setPlayerPosition( &spotPosition );
                gpScenaric->getLocalPlayer().setLastPosition( spotPosition );
            }
        }
    } else if ( ( pIntroCine->getFlags() & 0x5u ) == 0 && nextIntro.bIsActive &&
                nextIntro.NextGameMode == GM_FreeRide ) {
        if ( ( transitionFlags & 1 ) == 0 && bFlashInstalled ) {
            if ( gpMngFade != nullptr ) {
                gpMngFade->restartTransition(
                    ( gpMngFade->getTransitionFlags() & ~8u ) | 5u,
                    transitionTime * 0.001f );
            }

            CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
            if ( pLocalCar != nullptr ) {
                PhysicsVehicle *pPhysicsVehicle =
                    pLocalCar->getPhysicsVehicle();
                if ( pPhysicsVehicle != nullptr ) {
                    PhysicsLock physLock;
                    pPhysicsVehicle->setPostponedPause( 2 );
                }

                VehicleBase *pVehicle = pLocalCar->getVehicleInstance();
                if ( pVehicle != nullptr ) {
                    pVehicle->setShouldDraw( true );
                }

                EntityDriver *pDriver = pLocalCar->getDriver();
                if ( pDriver != nullptr ) {
                    pDriver->addInstanceReference();
                }
            }

            pSavedVehicleCamera = gpCamera->getActiveCamera();
            if ( nextIntro.pSpot == nullptr ) {
                pIntroCine->start( true );
            } else {
                pIntroCine->launchSpotCine( nextIntro.pSpot, 0.0f );
            }
        } else if ( gpMngFade != nullptr ) {
            gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() |
                                           8u );
        }
    }

    GameModeWorld::tick( deltaTime, totalTime );

    if ( gpCarState != nullptr ) {
        const CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
        if ( pLocalCar != nullptr ) {
            DiscoverCarRoad( pLocalCar );
        }
    }

    updateOdometerAutosave();
    updateOdometer();

    if ( !gpScenaric->getIntroTutorial().isActive() ) {
        updateNearestExtraChallenge();
    }

    updateMapScreenshotProps();
}

void GMFreeride::updateMapScreenshotProps() {
    // FUN_00796260 (inlined)
    if ( gpMngMapScreenshot == nullptr || !gbMapScreenshotsMngEnabled ) {
        return;
    }

    if ( pExtraChallengeMarker != nullptr ) {
        if ( IsMapScreenshotCapturing() ) {
            pExtraChallengeMarker->setVisible( false );
        } else if ( !pExtraChallengeMarker->isVisible() ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC
            pExtraChallengeMarker->setVisible( true );
        }
    }

    if ( pNearestExtraChallenge == nullptr ||
         pNearestExtraChallenge->getType() != eEntityType::ET_CVOY ||
         !IsMapScreenshotCapturing() ||
         extraChallengeProp.pConvoyCar == nullptr ) {
        return;
    }

    const Matrix4x4f_t goodiesMatrix =
        pNearestExtraChallenge->getGoodiesMatrix();
    PhysicsVehicle *pConvoyVehicle =
        extraChallengeProp.pConvoyCar->getPhysicsVehicle();
    {
        PhysicsLock physLock;
        pConvoyVehicle->setPostponedPause( 2 );
    }
    pConvoyVehicle->resetToTransform( goodiesMatrix, true );
    {
        PhysicsLock physLock;
        pConvoyVehicle->setPostponedPause( 1 );
    }
}

void GMFreeride::OnGPSFlyUpEnd( void * /*pUnused*/, void *pTargetGameMode ) {
    // FUN_00792c40
    gGPSFlyUpTargetMode =
        static_cast<int32_t>( reinterpret_cast<intptr_t>( pTargetGameMode ) );
    gbGPSFlyUpDone = true;
}

void GMFreeride::updateGPSFlyUp() {
    // FUN_00796260 (inlined)
    if ( !gbGPSFlyUpDone ) {
        return;
    }

    gbGPSFlyUpDone = false;
    const eGameMode targetGameMode =
        static_cast<eGameMode>( gGPSFlyUpTargetMode );
    gGPSFlyUpTargetMode = 0;

    if ( targetGameMode == GM_FreeRide ) {
        if ( gpActiveGM != nullptr ) {
            gpActiveGM->setPauseMenuLocked( false );
        }
        gpCamera->setMouseCam( true );
        gpCamera->setUseBlur( false );
        if ( gpMngHud != nullptr ) {
            gpMngHud->setMovieFilter( nullptr );
        }
        SetHudMoviesPaused( false );

        Viewport *pBackViewport = gpRender->getRenderPass<RP_Back>().pViewport;
        pBackViewport->ClearColor.R = 0x22;
        pBackViewport->ClearColor.G = 0x20;
        pBackViewport->ClearColor.B = 0x32;
        pBackViewport->ClearColor.A = 0xff;

        gpCamera->setNextCamera( gpCamera->findCameraByCurrentTransition(), 0,
                                 1.0f );

        StreamingManager *pStreaming = gpWorld->getStreamingManager();
        pStreaming->sectorManager.bDrawSector = false;
        pStreaming->sectorManager.bNeedStreamUpdate = true;
        pStreaming->areaGrid.bNeedStreamUpdate = true;
        pStreaming->areaGrid.bDrawArea = false;
        pStreaming->setActiveRenderLayers( 1 );
        pStreaming->sectorManager.bStreamSector = true;
        gpWorldStreaming->updateStreaming();
        pStreaming->areaGrid.bStreamArea = true;

        mngCamGPS.setUpdateStreaming( false );

        CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
        PhysicsVehicle *pPhysicsVehicle =
            ( pLocalCar != nullptr ) ? pLocalCar->getPhysicsVehicle() : nullptr;
        if ( pPhysicsVehicle != nullptr ) {
            PhysicsLock physLock;
            pPhysicsVehicle->setPostponedPause( 2 );
        }
        return;
    }

    if ( gpActiveGMScene != nullptr ) {
        gpActiveGMScene->restoreLocalPlayerCarControl();
    }
    snapLocalCarBackOnRoad();
    gpCamera->setUseBlur( false );

    requestExit();
    setTransitionFlags( 0 );
    gpTestDriveInstance->setNextGameMode( targetGameMode );
}

void GMFreeride::snapLocalCarBackOnRoad() {
    // FUN_00793570
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    PhysicsVehicle *pPhysicsVehicle =
        ( pLocalCar != nullptr ) ? pLocalCar->getPhysicsVehicle() : nullptr;
    if ( pPhysicsVehicle == nullptr ) {
        return;
    }

    const bool bOnGenericSurface =
        pPhysicsVehicle->isAnyWheelOnSurface( TDUSurface::Generic );
    const bool bInWater = pPhysicsVehicle->isInWater();
    if ( !MngActor::IsInSpotDetectionVolume( nullptr ) && !bOnGenericSurface &&
         !bInWater ) {
        return;
    }

    const Eigen::Vector4f position =
        pPhysicsVehicle->getChassisWorldMatrix().row( 3 ).transpose();
    Matrix4x4f_t frame;
    if ( SnapToRoadFrame( position, frame, eRoadSnapMode::RSM_KeepAwayFromEnds,
                          true, false ) == RSR_NoRoad ) {
        return;
    }

    PhysicsLock physLock;
    gpPhysics->refreshHeightmapCollision();
    pPhysicsVehicle->resetToTransform( frame, true );
}

void GMFreeride::draw( float /*deltaTime*/, float /*totalTime*/ ) {
    // FUN_007941f0
    if ( gpMngCinematics != nullptr &&
         gpMngCinematics->findFocusedPlayingCinematic() != nullptr ) {
        enterSpotClipTimer = 0.0f;
        return;
    }

    FlashPlayer *pPlayer = mngHudChallenge.getHudPlayer();

    enterSpotClipTimer +=
        ( gpTimer != nullptr ) ? gpTimer->GameDeltaTime : 0.0f;

    if ( !canEnterSpot() ||
         ( gpTimer != nullptr && gpTimer->GameSpeed <= 0.0f ) ) {
        enterSpotClipTimer = kEnterSpotClipDelay;

        if ( bEnterSpotClipVisible && pPlayer != nullptr ) {
            pPlayer->showFrame( "out", "/HUD/enter", true );
            bEnterSpotClipVisible = false;
        }
        return;
    }

    if ( bEnterSpotClipVisible ) {
        return;
    }

    FlashClip clip;
    if ( pPlayer == nullptr || !pPlayer->getClipPlayer( "/HUD/enter", &clip ) ||
         enterSpotClipTimer <= kEnterSpotClipDelay ) {
        return;
    }

    clip.setProperty( eFlashProperty::FP_Visible, true );

    pPlayer->setVariableValue( "/:WhatSpotType",
                               getPlayerGameMech()->getDisplayTypeName() );
    pPlayer->setVariableValue( "/:WhatSpot",
                               getPlayerGameMech()->getDisplayName() );

    populateEnterSpotPrompt( pPlayer );
    markCurrentSpotDiscovered();

    pPlayer->showFrame( "on", "/HUD/enter", true );
    bEnterSpotClipVisible = true;
}

void GMFreeride::populateEnterSpotPrompt( FlashPlayer *pPlayer ) {
    // FUN_00793b50
    if ( pPlayer == nullptr ) {
        return;
    }

    pPlayer->setVariableValue( "/:reward_type", 0 );
    pPlayer->setVariableValue( "/:IsChaLocked", 0 );
    pPlayer->setVariableValue( "/:IsSpotLocked", 0 );

    SpotMap *pSpot = ( pGameMech != nullptr )
                         ? getPlayerGameMech()->getCurrentSpot()
                         : nullptr;
    if ( pSpot == nullptr ) {
        refreshChallengeLockState( pPlayer );
        return;
    }

    CarDesc *pCurrentCar =
        ( gpPlayerData != nullptr ) ? gpPlayerData->getCurrentCar() : nullptr;
    DBEntryCarPhysicsData *pCurrentCarData = nullptr;
    if ( pCurrentCar != nullptr && gpDatabase != nullptr ) {
        pCurrentCarData =
            gpDatabase->getCarDBEntry( pCurrentCar->getCarConfigID() );
    }

    const bool bRentedCar = pCurrentCar != nullptr &&
                            pCurrentCar->state == CarDesc::eState::CDS_Rental;

    // Bike dealerships
    if ( pSpot->Type == 2 && IsSpotRewardLocked( kRewardBikeDealership ) ) {
        pPlayer->setVariableValue( "/:reward_type", 1 );
        pPlayer->setVariableValue( "/:SpotLockedtext",
                                   "_No idea how to unlock..." );
        SetSpotLockedTextFromReward( pPlayer, kRewardBikeDealership );
        refreshChallengeLockState( pPlayer );
        return;
    }

    if ( pSpot->Type == ST_Unknown8 ) {
        if ( pSpot->SubType == 9 &&
             IsSpotRewardLocked( kRewardCarPaintDeluxe ) ) {
            pPlayer->setVariableValue( "/:reward_type", 1 );
            pPlayer->setVariableValue( "/:SpotLockedtext",
                                       "_No idea how to unlock..." );
            SetSpotLockedTextFromReward( pPlayer, kRewardCarPaintDeluxe );
            refreshChallengeLockState( pPlayer );
            return;
        }

        if ( ( pSpot->SubType == 9 || pSpot->SubType == 1 ) && bRentedCar ) {
            pPlayer->setVariableValue( "/:IsSpotLocked", 1 );
            if ( gpMngFlashLocalize != nullptr ) {
                gpMngFlashLocalize->setVarLocalized(
                    pPlayer, "/:SpotLockedtext", 0x330bd32ull );
            }
            refreshChallengeLockState( pPlayer );
            return;
        }

        if ( pSpot->SubType == 9 && gpDatabase != nullptr &&
             gpDatabase->isBike( pCurrentCarData ) ) {
            pPlayer->setVariableValue( "/:IsSpotLocked", 1 );
            if ( gpMngFlashLocalize != nullptr ) {
                gpMngFlashLocalize->setVarLocalized(
                    pPlayer, "/:SpotLockedtext", 0x3310f3aull );
            }
            refreshChallengeLockState( pPlayer );
            return;
        }

        if ( pSpot->SubType == 9 &&
             ( pCurrentCarData == nullptr ||
               ( ( pCurrentCarData->BitField >> 6 ) & 1 ) == 0 ) ) {
            pPlayer->setVariableValue( "/:IsSpotLocked", 1 );
            if ( gpMngFlashLocalize != nullptr ) {
                gpMngFlashLocalize->setVarLocalized(
                    pPlayer, "/:SpotLockedtext", 0x338b05aull );
            }
            refreshChallengeLockState( pPlayer );
            return;
        }

        if ( pSpot->SubType == 1 && pCurrentCar != nullptr &&
             gpDatabase != nullptr &&
             gpDatabase->getNumColors( pCurrentCar->getCarConfigID() ) < 2 ) {
            pPlayer->setVariableValue( "/:IsSpotLocked", 1 );
            if ( gpMngFlashLocalize != nullptr ) {
                gpMngFlashLocalize->setVarLocalized(
                    pPlayer, "/:SpotLockedtext", 0x22d50512ull );
            }
            refreshChallengeLockState( pPlayer );
            return;
        }
    }

    // Tuning
    if ( pSpot->Type == 4 && pCurrentCar != nullptr && gpDatabase != nullptr ) {
        const int32_t carID =
            static_cast<int32_t>( pCurrentCar->getCarConfigID() );
        uint64_t lockedText = 0ull;
        if ( !gpDatabase->isCarTunable( carID ) ) {
            lockedText = 0x33729baull;
        } else if ( !gpDatabase->isCarTunableInShop( pSpot, carID ) ) {
            lockedText = 0x3b90002ull;
        } else if ( bRentedCar ) {
            lockedText = 0x3778a32ull;
        }

        if ( lockedText != 0ull ) {
            pPlayer->setVariableValue( "/:IsSpotLocked", 1 );
            if ( gpMngFlashLocalize != nullptr ) {
                gpMngFlashLocalize->setVarLocalized(
                    pPlayer, "/:SpotLockedtext", lockedText );
            }
            refreshChallengeLockState( pPlayer );
            return;
        }
    }

    // Challenge
    if ( pSpot->Type == ST_Challenge ) {
        bool bCarOK = true;
        bool bChallengeLocked = false;
        if ( !gpScenaric->getIntroTutorial().isActive() ) {
            bool bLevelTooLow = false;
            bool bHasEligibleCar = false;
            GetChallengeCarAvailability( pSpot, &bLevelTooLow,
                                         &bHasEligibleCar );

            bChallengeLocked = !bHasEligibleCar;
            if ( bChallengeLocked ) {
                bCarOK = false;
            }
            if ( bLevelTooLow ) {
                bChallengeLocked = true;
            }
        }

        pPlayer->setVariableValue( "/:IsCarOK", bCarOK ? 1 : 0 );
        pPlayer->setVariableValue( "/:IsChaLocked", bChallengeLocked ? 1 : 0 );
    }

    refreshChallengeLockState( pPlayer );
}

void GMFreeride::refreshChallengeLockState( FlashPlayer *pPlayer ) {
    // FUN_00792f30
    const EntityExtraChallenge *pChallenge =
        static_cast<const EntityExtraChallenge *>(
            ( pGameMech != nullptr )
                ? getPlayerGameMech()->getCurrentExtraChallenge()
                : nullptr );
    if ( pChallenge == nullptr ) {
        return;
    }

    bool bLocked = !pChallenge->isCarAllowed( 0, {} );
    if ( !bLocked && pChallenge->getType() == eEntityType::ET_TRAN ) {
        const EntityTransporter *pTransporter =
            static_cast<const EntityTransporter *>( pChallenge );
        if ( !pTransporter->isCarAllowed( pTransporter->getConditionType(),
                                          pTransporter->getCondition() ) ) {
            bLocked = true;
        } else {
            const uint64_t fare = static_cast<uint64_t>(
                static_cast<float>( pTransporter->getReward() ) *
                kTransporterFareRatio );
            bLocked =
                gpPlayerData->getLoadedSave()->getDriver()->getMoney() < fare;
            FlashPlayer *pHud = gpFlash->getFlashPlayer( "HUD" );
            if ( pHud != nullptr ) {
                pHud->setVariableValue( "/:NotEnoughMoney", bLocked ? 1 : 0 );
            }
        }
    }

    pPlayer->setVariableValue( "/:IsChaLocked", bLocked ? 1 : 0 );
}

void GMFreeride::markCurrentSpotDiscovered() {
    // FUN_007941c0
    SpotMap *pSpot = ( pGameMech != nullptr )
                         ? getPlayerGameMech()->getCurrentSpot()
                         : nullptr;
    if ( pSpot != nullptr ) {
        DiscoverSpotRoad( pSpot );
    }
}

void GMFreeride::pause() {
    // FUN_00794900
    gpPhysics->setForceFeedbackAllowed( false );
    if ( gpTutorial != nullptr ) {
        gpTutorial->bEnabled = false;
    }

    gpAI->setDisableBots( true );
    gpAI->setBotsActive( false );
    gpAI->getAIPool()->removeBots();
    if ( gpTestDriveInstance->getActiveGameMode() == GM_InstantChallenge ) {
        gpAI->getCopManager().saveInfringement();
    }

    if ( pIntroCine != nullptr ) {
        pIntroCine->stop( true );
    }

    releaseExtraChallengeProp();

    if ( pExtraChallengeMarker != nullptr ) {
        MngActor::Uninstanciate( &mngActor, pExtraChallengeMarker );
        pExtraChallengeMarker = nullptr;
    }

    GameModeWorld::pause();

    if ( !gpScenaric->getIntroTutorial().isActive() ) {
        releaseExtraChallenges();
    }

    gpScenaric->getAvatarManager().setUnknownBoolean( false );
}

void GMFreeride::releaseExtraChallenges() {
    // FUN_00793680
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR
    for ( int32_t i = 0; i < numExtraChallenges; i++ ) {
        if ( static_cast<EntityExtraChallenge *>( pExtraChallenges[i] )
                 ->isAvailable() ) {
            mngActor.removeActorFromList( pExtraChallenges[i] );
        }
    }
}

void GMFreeride::destroy() {
    // FUN_00794860
    GameModeWorld::destroy();

    if ( gpAI->getBotManager().isActive() &&
         gpTestDriveInstance->getActiveGameMode() != GM_Map ) {
        gpAI->setDisableBots( true );
        gpAI->setBotsActive( false );
        gpAI->getAIPool()->removeBots();
    }
    gpAI->setDisableCops( true );
    gpAI->setCopsActive( false );
    gpAI->getAIPool()->removeCops();

    if ( gpScenaric != nullptr ) {
        gpScenaric->getSpotList().despawnSectorSpotMeshes();
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: GSPUB

    const ScenaricBank::eBankState bankState = spotBank.getState();
    if ( bankState != ScenaricBank::BBS_Unused &&
         bankState != ScenaricBank::BBS_LoadFailed && gpScenaric != nullptr ) {
        gpScenaric->unloadBank( &spotBank );
    }
}

void GMFreeride::onResourceStreamed() {
    // FUN_007940f0
    GameModeWorld::onResourceStreamed();

    FlashPlayer *pPlayer = mngHudChallenge.getHudPlayer();
    if ( pPlayer == nullptr ) {
        return;
    }

    mngBookmark.setFlashPlayer( pPlayer );
    mngFRMeetingIC.setFlashPlayer( pPlayer );
    mngInGameMenu.setFlashPlayer( pPlayer );

    if ( gpMngFlashLocalize != nullptr ) {
        mngInGameMenu.refreshDrivingHelpLabel();
    }

    pPlayer->setVariableValue(
        "/:nb_houses",
        gpPlayerData->getLoadedSave()->getGarage()->getNumOwnedHouses() );

    mngFlash.addWidget( "spot", &flashListSpots );
    flashListSpots.setPlayer( pPlayer, "/menus/ss_menus/array/spots" );
    flashListSpots.setFlashPlayer( pPlayer );

    if ( gpTutorial != nullptr ) {
        gpTutorial->bEnabled = true;
    }

    mngPhotoMode.setFlashPlayer( pPlayer );
}

bool GMFreeride::executeAction( int32_t param_2, void *param_3, void *pData ) {
    // FUN_00794ff0
    if ( param_2 == CE_Started ) {
        if ( pExtraChallengeIntroCine == param_3 ||
             ( pIntroCine == param_3 && !nextIntro.bIsActive ) ) {
            gpMngFade->setRenderLogo( false );
            gpMngFade->startTransition(
                ( gpMngFade->getTransitionFlags() & ~0xau ) | 5u,
                kSpotIntroFadeInDuration );
        }
        if ( pExtraChallengeIntroCine == param_3 ) {
            if ( pExtraChallengeMarker != nullptr ) {
                pExtraChallengeMarker->setVisible( false );
            }
            CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
            if ( pLocalCar != nullptr ) {
                Matrix4x4f_t startFrame;
                Matrix4x4f_t endFrame;
                Matrix4x4f_t startFrameCopy;
                pNearestExtraChallenge->computeRoadRoute(
                    startFrame, endFrame, startFrameCopy, nullptr );
                PhysicsLock lock; // FUN_0083d900
                pLocalCar->getPhysicsVehicle()->resetToTransform(
                    startFrameCopy, true );
            }
            if ( pNearestExtraChallenge->getType() == eEntityType::ET_CVOY ) {
                spawnConvoyCar( pNearestExtraChallenge );
                // NOTE: Retail dereferences a missing convoy car.
                PhysicsVehicle *pConvoyVehicle =
                    ( extraChallengeProp.pConvoyCar != nullptr )
                        ? extraChallengeProp.pConvoyCar->getPhysicsVehicle()
                        : nullptr;
                if ( pConvoyVehicle != nullptr ) {
                    Matrix4x4f_t goodiesMatrix =
                        pNearestExtraChallenge->getGoodiesMatrix();
                    PhysicsLock lock;
                    pConvoyVehicle->resetToTransform( goodiesMatrix, true );
                    pConvoyVehicle->setPostponedPause( 1 );
                }
            }
        }
        return true;
    }

    if ( param_2 == CE_StartEndFade ) {
        if ( pIntroCine != nullptr && pIntroCine == param_3 &&
             !nextIntro.bIsActive ) {
            SpotMap *pSpot = pIntroCine->getSpotWorld();
            gpScenaric->setCurrentSpotIndex(
                gpScenaric->getSpotList().getSpotMapIndex( pSpot->Hashcode ) );
            transitionFlags |= 0x20;

            bIntroLevelLaunched = LaunchSpotLevel( pSpot, false );
            pIntroCine->setRestoreCineMix( !bIntroLevelLaunched );
            return true;
        }

        if ( pExtraChallengeIntroCine != nullptr &&
             pExtraChallengeIntroCine == param_3 && !nextIntro.bIsActive ) {
            if ( pExtraChallengeLoopCine == nullptr ) {
                pExtraChallengeIntroCine->setRestoreCineMix( false );
                if ( bCanAcceptExtraChallenge && !bExtraChallengeLaunched ) {
                    resolveExtraChallengeAnswer();
                    bExtraChallengeLaunched = true;
                }
            } else {
                pExtraChallengeLoopCine->createTDUInterface();
            }
        }
        return true;
    }

    if ( param_2 == CE_FlashMessage ) {
        return onExtraChallengeCineMessage(
            static_cast<EntityCinematic *>( param_3 ),
            static_cast<const char *>( pData ) );
    }

    if ( param_2 != CE_Ended ) {
        return false;
    }

    if ( pExtraChallengeIntroCine != nullptr &&
         pExtraChallengeIntroCine == param_3 && !nextIntro.bIsActive ) {
        pExtraChallengeIntroCine = nullptr;
        if ( pExtraChallengeLoopCine != nullptr ) {
            pExtraChallengeLoopCine->start( true );
            pExtraChallengeLoopCine->setFadeTime( 0.0f );
        } else if ( !bCanAcceptExtraChallenge ) {
            resolveExtraChallengeAnswer();
        }
        pIntroCine = nullptr;
        bIntroLevelLaunched = false;
        return true;
    }

    if ( nextIntro.bIsActive ) {
        gpScenaric->getIntroTutorial().onIntroCinematicEnded();
        nextIntro.bIsActive = false;
        bIntroLevelLaunched = false;
        bIntroCineDone = true;
        gpWorld->setAreaStreamingEnabled( true );
    }

    if ( !bIntroLevelLaunched ) {
        gpCamera->restoreCamera();

        AiPool *pAIPool = gpAI->getAIPool();
        pAIPool->setTrafficPaused( false );
        pAIPool->setField0x7C( true );
        pAIPool->setField0x85( true );
        gpAI->setPauseTraffic( false );
    }

    pIntroCine = nullptr;
    bIntroLevelLaunched = false;
    return true;
}

bool GMFreeride::onExtraChallengeCineMessage( EntityCinematic *pCinematic,
                                              const char *pMessage ) {
    // FUN_00794ff0 (CE_FlashMessage)
    if ( pCinematic->wasSkipped() && pExtraChallengeLoopCine == nullptr ) {
        return true;
    }
    const FlashMessage message( pMessage );
    if ( message == FlashMessage( "stfe" ) ) {
        gpFlash->enablePlayer( "GENERAL", ModalMessageBox::MMBDS_Closing, true,
                               1 );
        FlashPlayer *pGeneral = gpFlash->getFlashPlayer( "GENERAL" );
        if ( pMessage[4] != ':' || pMessage[5] == '\0' ) {
            return false;
        }

        const char *pKind = pMessage + 5;
        const char *pSpotType = nullptr;
        uint64_t messageHashcode = 0ull;
        bool bQuestion = false;
        if ( strncmp( pKind, "sgirl", 5 ) == 0 ) {
            pSpotType = "sgirl";
            messageHashcode = kShoppingGirlQuestionHashcode;
        } else if ( strncmp( pKind, "hiker", 5 ) == 0 ) {
            pSpotType = "hiker";
            messageHashcode = kHitchHikerQuestionHashcode;
        } else if ( strncmp( pKind, "convoy", 6 ) == 0 ) {
            pSpotType = "convoy";
            messageHashcode = kConvoyQuestionHashcode;
            bQuestion = true;
        } else if ( strncmp( pKind, "transporter", 11 ) == 0 ) {
            pSpotType = "transporter";
            messageHashcode = kTransporterQuestionHashcode;
            bQuestion = true;
        } else {
            return true;
        }

        pGeneral->setVariableValueNonLinked( "/:CurrentSpotType", pSpotType );
        FlashClip clip;
        if ( pGeneral->getClipPlayer( "/sgirl", &clip ) ) {
            clip.setProperty( eFlashProperty::FP_Visible, true );
        }
        const char *pMsgShopGirl =
            gpDatabase->getStringByHashcode( 0, messageHashcode );
        if ( pMsgShopGirl != nullptr ) {
            pGeneral->setVariableValue( "/:MsgShopGirl", pMsgShopGirl );
        }
        const char *pMsgShopGirl2 = gpDatabase->getStringByHashcode(
            0, kExtraChallengeQuestionHashcode );
        if ( pMsgShopGirl2 != nullptr ) {
            pGeneral->setVariableValue( "/:MsgShopGirl2", pMsgShopGirl2 );
        }
        if ( bQuestion ) {
            pGeneral->showFrame( "extra_q", "/msgCinematik", true );
            OTDU_UNIMPLEMENTED_ONCE; // TODO: FLASH (FUN_0099fc40 on
                                     // gGSFlash +0xf88)
        } else {
            pGeneral->showFrame( "sgirl", "/msgCinematik", true );
        }
        return true;
    }

    if ( message != FlashMessage( "sgfe" ) ) {
        return false;
    }

    gpFlash->enablePlayer( "GENERAL", ModalMessageBox::MMBDS_Closing, true, 1 );
    FlashPlayer *pGeneral = gpFlash->getFlashPlayer( "GENERAL" );
    const char *pSpotType = nullptr;
    switch ( extraChallengeKind ) {
    case PlayerGameMech::SDN_ShoppingGirl:
        pSpotType = "sgirl";
        break;
    case PlayerGameMech::SDN_Hiker:
        pSpotType = "hiker";
        break;
    case PlayerGameMech::SDN_Transporter:
        pSpotType = "transporter";
        break;
    case PlayerGameMech::SDN_Convoy:
        pSpotType = "convoy";
        break;
    default:
        break;
    }
    if ( pSpotType != nullptr ) {
        pGeneral->setVariableValueNonLinked( "/:CurrentSpotType", pSpotType );
    }
    FlashClip clip;
    if ( pGeneral->getClipPlayer( "/sgirl", &clip ) ) {
        clip.setProperty( eFlashProperty::FP_Visible, true );
    }
    if ( pExtraChallengeRefusal != nullptr ) {
        pGeneral->setVariableValue( "/:MsgShopGirl", pExtraChallengeRefusal );
    }
    if ( pExtraChallengeRefusal2 != nullptr ) {
        pGeneral->setVariableValue( "/:MsgShopGirl2", pExtraChallengeRefusal2 );
    }
    pGeneral->showFrame( "sgirl", "/msgCinematik", true );
    return true;
}

void GMFreeride::gatherExtraChallenges() {
    // FUN_00792e60
    numExtraChallenges = 0;

    if ( gpScenaric == nullptr ) {
        return;
    }

    ScenaricBank *pBank = gpScenaric->getWorldExtraChallengesBank();
    for ( int32_t i = 0; i < pBank->getEntityCount(); i++ ) {
        Entity *pEntity = pBank->getEntity( i );
        if ( pEntity == nullptr ) {
            continue;
        }

        const eEntityType type = pEntity->getType();
        if ( type != eEntityType::ET_SHOG && type != eEntityType::ET_HIHI &&
             type != eEntityType::ET_TRAN && type != eEntityType::ET_CVOY ) {
            continue;
        }

        if ( static_cast<EntityExtraChallenge *>( pEntity )->isAvailable() ) {
            mngActor.registerActor( pEntity );
        }

        if ( numExtraChallenges < kMaxNumExtraChallenges ) {
            pExtraChallenges[numExtraChallenges] = pEntity;
            numExtraChallenges++;
        }
    }
}

void GMFreeride::updateNearestExtraChallenge() {
    // FUN_007956d0
    const CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr ) {
        return;
    }

    const Eigen::Vector4f carPosition =
        pLocalCar->getBody()->WorldMatrix.row( 3 );
    const float streamSize = gpWorldStreaming->getSize();
    float nearestDistanceSq = streamSize * streamSize;
    EntityExtraChallenge *pNearest = nullptr;

    for ( int32_t i = 0; i < numExtraChallenges; i++ ) {
        EntityExtraChallenge *pChallenge =
            static_cast<EntityExtraChallenge *>( pExtraChallenges[i] );
        if ( !pChallenge->isAvailable() && !IsMapScreenshotCapturing() ) {
            continue;
        }

        const Eigen::Vector4f challengePosition =
            pChallenge->getModelMatrix().row( 3 );
        const float distanceSq =
            ( challengePosition - carPosition ).squaredNorm();
        if ( distanceSq < nearestDistanceSq ) {
            nearestDistanceSq = distanceSq;
            pNearest = pChallenge;
        }
    }

    if ( pNearest == nullptr ) {
        pNearestExtraChallenge = nullptr;
        releaseExtraChallengeProp();
        return;
    }

    if ( pNearest == pNearestExtraChallenge ) {
        return;
    }

    pNearestExtraChallenge = pNearest;
    if ( !selectExtraChallenge( pNearest ) ) {
        return;
    }

    if ( pExtraChallengeMarker != nullptr ) {
        MngActor::Uninstanciate( &mngActor, pExtraChallengeMarker );
        pExtraChallengeMarker = nullptr;
    }

    int32_t displayType = 0;
    int32_t displayName = -1;
    GetExtraChallengeDisplayIndices( pNearest, &displayType, &displayName );
    switch ( displayName ) {
    case PlayerGameMech::SDN_ShoppingGirl:
        pExtraChallengeMarker = spawnExtraChallengeMarker( 5 );
        break;
    case PlayerGameMech::SDN_Hiker:
        pExtraChallengeMarker = spawnExtraChallengeMarker( 6 );
        break;
    case PlayerGameMech::SDN_Transporter:
        pExtraChallengeMarker = spawnExtraChallengeMarker( 7 );
        break;
    case PlayerGameMech::SDN_Convoy:
        pExtraChallengeMarker = spawnExtraChallengeMarker( 8 );
        break;
    default:
        break;
    }

    if ( pExtraChallengeMarker != nullptr ) {
        Matrix4x4f_t identity = Matrix4x4f_t::Identity();
        pExtraChallengeMarker->setTransform( identity, true );

        Matrix4x4f_t markerMatrix = pNearest->getModelMatrix();
        markerMatrix.row( 3 ) = pNearest->getSpotMeshPosition();
        pExtraChallengeMarker->propagateTransform( markerMatrix, true );
    }

    const bool bHasGoodies =
        extraChallengeKind == PlayerGameMech::SDN_ShoppingGirl ||
        extraChallengeKind == PlayerGameMech::SDN_Hiker ||
        extraChallengeKind == PlayerGameMech::SDN_Transporter;
    if ( bHasGoodies && extraChallengeProp.pGoodies != nullptr ) {
        Matrix4x4f_t identity = Matrix4x4f_t::Identity();
        extraChallengeProp.pGoodies->setTransform( identity, true );

        Matrix4x4f_t goodiesMatrix = pNearest->getGoodiesMatrix();
        extraChallengeProp.pGoodies->propagateTransform( goodiesMatrix, true );
    }
}

bool GMFreeride::selectExtraChallenge( EntityExtraChallenge *pChallenge ) {
    // FUN_00794e90
    if ( pChallenge == nullptr ) {
        return false;
    }

    if ( IsMapScreenshotCapturing() ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR
    }

    releaseExtraChallengeProp();

    int32_t displayType = 0;
    GetExtraChallengeDisplayIndices( pChallenge, &displayType,
                                     &extraChallengeKind );

    if ( extraChallengeKind == PlayerGameMech::SDN_Convoy &&
         IsMapScreenshotCapturing() ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: VEHICLE
    }

    return true;
}

Entity *GMFreeride::spawnExtraChallengeMarker( const int32_t subType ) {
    // FUN_00793230
    ScenaricBank &meshBank = gpScenaric->getSpotMeshBank();
    for ( int32_t i = 0; i < meshBank.getEntityCount(); i++ ) {
        Entity *pTemplate = meshBank.getEntity( i );
        if ( pTemplate == nullptr ||
             pTemplate->getType() != eEntityType::ET_SPTM ) {
            continue;
        }

        const EntitySpotMesh *pSpotMesh =
            static_cast<const EntitySpotMesh *>( pTemplate );
        if ( pSpotMesh->getTemplateType() == ST_Unknown8 &&
             pSpotMesh->getTemplateSubType() == subType ) {
            return mngActor.instanciateActor( pTemplate );
        }
    }

    return nullptr;
}

void GMFreeride::releaseExtraChallengeProp() {
    // FUN_007932f0
    switch ( extraChallengeKind ) {
    case PlayerGameMech::SDN_ShoppingGirl:
    case PlayerGameMech::SDN_Hiker:
    case PlayerGameMech::SDN_Transporter:
        if ( extraChallengeProp.pGoodies != nullptr ) {
            MngActor::Uninstanciate( &mngActor, extraChallengeProp.pGoodies );
            extraChallengeProp.pGoodies = nullptr;
        }
        break;
    case PlayerGameMech::SDN_Convoy: {
        CarState *pConvoyCar = extraChallengeProp.pConvoyCar;
        if ( pConvoyCar != nullptr ) {
            VehicleBaseResources *pResource = pConvoyCar->getResource();
            gpCarState->releaseCarState( pConvoyCar, false );
            if ( pResource != nullptr ) {
                pResource->unload();
                gpCarState->releaseResourceEntry( pResource );
            }
            extraChallengeProp.pConvoyCar = nullptr;
        }
        break;
    }
    default:
        break;
    }

    extraChallengeKind = -1;
}

bool GMFreeride::spawnConvoyCar( EntityExtraChallenge *pChallenge ) {
    // FUN_00794a70
    if ( extraChallengeProp.pConvoyCar != nullptr ) {
        return true;
    }

    const EntityConvoy *pConvoy =
        static_cast<const EntityConvoy *>( pChallenge );
    const int32_t carID = int32_t( pConvoy->getCarID() );
    VehicleSpawnParams params;
    params.Transform = pChallenge->getGoodiesMatrix();
    params.bSetPosition = true;
    params.bCastToGround = true;
    params.bResetCar = true;
    params.PhantomMode = 1;

    VehicleBaseResources *pResource = gpCarState->loadVehicleResources(
        carID, 0, -1, ~0ull, 0, 0x10, eVehicleResourceType::VRT_Traffic );
    CarState *pConvoyCar =
        gpCarState->installVehicle( pResource, params, VIT_AI, -1 );
    // NOTE: Retail keeps the loaded resource when no car state slot is free.
    if ( pConvoyCar == nullptr && pResource != nullptr ) {
        pResource->unload();
        gpCarState->releaseResourceEntry( pResource );
    }

    VehicleBase *pVehicle =
        ( pConvoyCar != nullptr ) ? pConvoyCar->getVehicleInstance() : nullptr;
    if ( pVehicle != nullptr &&
         ( pVehicle->getType() == VehicleBase::eType::VBT_Car ||
           pVehicle->getType() == VehicleBase::eType::VBT_Bike ) ) {
        pVehicle->setPaintColorFromDB( int32_t( pConvoy->getCarColorID() ) );
        DBEntryCarColor *pColor = gpDatabase->getColorByIndex(
            carID, int32_t( pConvoy->getCarColorID() ) );
        if ( pColor != nullptr ) {
            DBEntryInterior *pInterior = gpDatabase->getInteriorByIndex(
                pColor, int32_t( pConvoy->getCarInteriorColorID() ) );
            if ( pInterior != nullptr ) {
                pVehicle->setInteriorColorsFromDB( pInterior );
            }
        }
        DBEntryRim *pRim =
            gpDatabase->getRim( carID, int32_t( pConvoy->getCarRimID() ) );
        if ( pRim != nullptr ) {
            pVehicle->setRimsFromDB( pRim );
        }
    }

    extraChallengeProp.pConvoyCar = pConvoyCar;
    return true;
}

bool GMFreeride::enterExtraChallenge( EntityExtraChallenge *pChallenge ) {
    // FUN_00795b80
    if ( pExtraChallengeIntroCine != nullptr ) {
        return false;
    }
    bExtraChallengeLaunched = false;
    pExtraChallengeRefusal = nullptr;
    pExtraChallengeRefusal2 = nullptr;

    const eEntityType type = pChallenge->getType();
    const char *pTag = nullptr;
    bool bHasLoopCine = false;
    switch ( type ) {
    case eEntityType::ET_SHOG:
        pTag = "SG";
        break;
    case eEntityType::ET_CVOY:
        pTag = "CONVOY";
        bHasLoopCine = true;
        break;
    case eEntityType::ET_HIHI:
        pTag = "HH";
        break;
    case eEntityType::ET_TRAN:
        pTag = "TRANS";
        bHasLoopCine = true;
        break;
    default:
        break;
    }

    bool bCanAccept = true;
    bool bStopWhenSkipped = false;
    bool bRestrictionRefusal = false;
    auto RefuseNoPassenger = [&]() {
        bCanAccept = false;
        pExtraChallengeRefusal =
            gpDatabase->getStringByHashcode( 0, kRefusalNoPassengerHashcode );
        pExtraChallengeRefusal2 =
            gpDatabase->getStringByHashcode( 0, kRefusalNoPassenger2Hashcode );
    };

    const DBEntryCarPhysicsData *pLocalCarEntry = gpDatabase->getCarDBEntry(
        gpScenaric->getLocalPlayer().getCarState()->getResource()->CarID );
    if ( pLocalCarEntry != nullptr &&
         ( gpDatabase->getStringByHashcode( 1, kBikeCategoryHashcode ) ==
               pLocalCarEntry->Category.pAsString ||
           gpDatabase->getStringByHashcode( 6, kNoPassengerBrandHashcode ) ==
               pLocalCarEntry->Brand->BrandID.pAsString ) &&
         type != eEntityType::ET_CVOY ) {
        RefuseNoPassenger();
    } else {
        CarDesc *pCurrentCar = gpPlayerData->getCurrentCar();
        const DBEntryCarPhysicsData *pCarEntry =
            ( pCurrentCar != nullptr ) ? gpDatabase->getCarDBEntry( int32_t(
                                             pCurrentCar->getCarConfigID() ) )
                                       : nullptr;
        const char *pNumSeats =
            ( pCarEntry != nullptr && type != eEntityType::ET_CVOY )
                ? pCarEntry->NumSeats.pAsString
                : nullptr;
        if ( pNumSeats != nullptr && pNumSeats[0] != '\0' &&
             ( pNumSeats[0] == '1' || pNumSeats[0] == '3' ) ) {
            RefuseNoPassenger();
        } else if ( type == eEntityType::ET_CVOY ) {
            bStopWhenSkipped = true;
            if ( extraChallengeProp.pConvoyCar == nullptr ) {
                extraChallengeKind = PlayerGameMech::SDN_Convoy;
            }
        } else if ( type == eEntityType::ET_TRAN ) {
            const EntityTransporter *pTransporter =
                static_cast<const EntityTransporter *>( pChallenge );
            const uint32_t fare = uint32_t( float( pTransporter->getReward() ) *
                                            kTransporterFareRatio );
            bCanAccept =
                fare < gpPlayerData->getLoadedSave()->getDriver()->getMoney();
            bStopWhenSkipped = true;
        } else if ( type == eEntityType::ET_SHOG ) {
            const EntityShoppingGirl *pShoppingGirl =
                static_cast<const EntityShoppingGirl *>( pChallenge );
            pShoppingGirl->buildVehicleRestrictionText(
                pShoppingGirl->getConditionType(),
                pShoppingGirl->getCondition(), vehicleRestrictionText,
                sizeof( vehicleRestrictionText ), true );
            pExtraChallengeRefusal2 = vehicleRestrictionText;
            bCanAccept =
                pShoppingGirl->isCarAllowed( pShoppingGirl->getConditionType(),
                                             pShoppingGirl->getCondition() );
            if ( !bCanAccept ) {
                const uint32_t conditionType =
                    pShoppingGirl->getConditionType();
                if ( conditionType == 1 || conditionType == 2 ) {
                    pExtraChallengeRefusal = gpDatabase->getStringByHashcode(
                        0, kRefusalNoPassengerHashcode );
                } else if ( conditionType == 3 ) {
                    pExtraChallengeRefusal = gpDatabase->getStringByHashcode(
                        0, kRefusalRestrictionHashcode );
                }
                bStopWhenSkipped = true;
                bRestrictionRefusal = true;
            }
        }
    }

    SavedItemTable *pChallengeStates =
        gpPlayerData->getLoadedSave()->getItemTable();
    uint16_t &state = pChallengeStates->Kinds[pChallenge->getIndex()];
    state = ( state & 0xff00u ) | SavedItemTable::ECS_Available;
    pChallengeStates->notify( 0u );

    gpScenaric->setActiveExtraChallenge( pChallenge );
    bCanAcceptExtraChallenge = bCanAccept;
    pExtraChallengeIntroCine = nullptr;
    pExtraChallengeLoopCine = nullptr;

    ScenaricBank &cineBank = gpScenaric->getExtraChallengesCineBank().getBank();
    char cineName[0x40];
    if ( !bCanAccept ) {
        snprintf( cineName, sizeof( cineName ), "C_REFUSE_%s", pTag );
        pExtraChallengeIntroCine = static_cast<EntityCinematic *>(
            cineBank.findEntity( eEntityType::ET_CINE, cineName ) );
    } else {
        snprintf( cineName, sizeof( cineName ), "C_INTRO_%s", pTag );
        pExtraChallengeIntroCine = static_cast<EntityCinematic *>(
            cineBank.findEntity( eEntityType::ET_CINE, cineName ) );
        if ( bHasLoopCine ) {
            strncat( cineName, "_L",
                     sizeof( cineName ) - strlen( cineName ) - 1 );
            pExtraChallengeLoopCine = static_cast<EntityCinematic *>(
                cineBank.findEntity( eEntityType::ET_CINE, cineName ) );
        }
    }

    if ( pExtraChallengeIntroCine == nullptr ) {
        if ( bCanAccept ) {
            gpScenaric->setActiveExtraChallenge( pChallenge );
            transitionFlags |= 0x20;
            pChallenge->launch();
        }
        gpScenaric->getAvatarManager().setUnknownBoolean( true );
        return true;
    }

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar != nullptr ) {
        gpCarState->setPoliceStateJob( pLocalCar, nullptr );
    }
    Matrix4x4f_t frame = pNearestExtraChallenge->getModelMatrix();
    gpPhysics->setPlayerSlowDownForced( true );
    pExtraChallengeIntroCine->setGameModeInterface( this );
    pExtraChallengeIntroCine->setStopWhenSkipped( bStopWhenSkipped );
    pExtraChallengeIntroCine->setAsyncActorLoad( true );
    frame.row( 3 ).head<3>() -= GetWorldShift().head<3>().transpose();
    pExtraChallengeIntroCine->setTransformOffset( frame );
    pExtraChallengeIntroCine->setRestoreCineMix( false );
    if ( ( gpMngFade->getTransitionFlags() & 1u ) == 0 ) {
        gpMngFade->startTransition(
            ( gpMngFade->getTransitionFlags() & ~0xeu ) | 1u,
            kSpotIntroFadeDuration, &GMFreeride::OnExtraChallengeIntroFadeOut,
            nullptr, this );
    }
    if ( bRestrictionRefusal ) {
        pExtraChallengeIntroCine->setFadeTime( 0.0f );
    }
    if ( pExtraChallengeLoopCine != nullptr ) {
        pExtraChallengeLoopCine->setGameModeInterface( this );
        pExtraChallengeLoopCine->setTransformOffset( frame );
        pExtraChallengeLoopCine->setStopWhenSkipped( true );
    }
    gpScenaric->getAvatarManager().setUnknownBoolean( true );
    return true;
}

void GMFreeride::OnExtraChallengeIntroFadeOut( void *pData,
                                               void * /*pSource*/ ) {
    // LAB_00795b00
    GMFreeride *pFreeride = static_cast<GMFreeride *>( pData );
    if ( pFreeride->pExtraChallengeIntroCine == nullptr ) {
        return;
    }

    gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() | 8u );
    gpMngFade->setRenderLogo( true );
    gpAI->setTrafficCategories( false, 2u );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FreeRideMgr scanning reset,
                             // ChangeSessionMode(-1))
    pFreeride->pExtraChallengeIntroCine->start( true );
}

void GMFreeride::resolveExtraChallengeAnswer() {
    // FUN_007944d0
    gpPhysics->setPlayerSlowDownForced( false );
    LocalPlayer &localPlayer = gpScenaric->getLocalPlayer();
    if ( localPlayer.getCarState() != nullptr ) {
        gpCarState->setPoliceStateJob( localPlayer.getCarState(),
                                       &localPlayer.getInfringementCheck() );
    }

    if ( !bCanAcceptExtraChallenge ) {
        gpScenaric->setActiveExtraChallenge( nullptr );
        gpCamera->restoreCamera();
        if ( pExtraChallengeMarker != nullptr ) {
            pExtraChallengeMarker->setVisible( true );
        }
        if ( pExtraChallengeAvatar != nullptr &&
             pNearestExtraChallenge != nullptr ) {
            OTDU_UNIMPLEMENTED; // TODO: AVATAR (spot avatar vtbl +0xcc, pose
                                // reset to the challenge matrix)
        }
        releaseExtraChallengeProp();
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_00737ca0)
    } else {
        gpScenaric->setActiveExtraChallenge( pNearestExtraChallenge );
        transitionFlags |= 0x30;
        pNearestExtraChallenge->launch();
    }

    bIntroLevelLaunched = false;
    AiPool *pAIPool = gpAI->getAIPool();
    pAIPool->setTrafficPaused( false );
    pAIPool->setField0x7C( true );
    pAIPool->setField0x85( true );
    gpAI->setPauseTraffic( false );
}

void GMFreeride::OnExtraChallengeCancelFadeOut( void *pData,
                                                void * /*pSource*/ ) {
    // FUN_007943c0
    GMFreeride *pFreeride = static_cast<GMFreeride *>( pData );
    if ( pFreeride->pExtraChallengeLoopCine != nullptr ) {
        pFreeride->pExtraChallengeLoopCine->stop( true );
    }
    if ( !IsMapScreenshotCapturing() ) {
        pFreeride->releaseExtraChallengeProp();
    }
    LocalPlayer &localPlayer = gpScenaric->getLocalPlayer();
    if ( localPlayer.getCarState() != nullptr ) {
        gpCarState->setPoliceStateJob( localPlayer.getCarState(),
                                       &localPlayer.getInfringementCheck() );
    }
    gpPhysics->setPlayerSlowDownForced( false );
    AiPool *pAIPool = gpAI->getAIPool();
    pAIPool->setTrafficPaused( false );
    pAIPool->setField0x7C( true );
    pAIPool->setField0x85( true );
    gpAI->setPauseTraffic( false );
    if ( pFreeride->pExtraChallengeMarker != nullptr ) {
        pFreeride->pExtraChallengeMarker->setVisible( true );
    }
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_00737ca0)
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AUDIO (bMixStateTransitionSync = true)
    MngFade &fade = pFreeride->mngFade;
    fade.startTransition( ( ( fade.getTransitionFlags() | 4u ) & ~8u ) | 1u,
                          kSpotIntroFadeDuration );
}

bool GMFreeride::requestEnterActivity() {
    // FUN_00796ef0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar != nullptr && pLocalCar->getPoliceState().isArrested() ) {
        return true;
    }

    enterCurrentActivity();

    if ( gpMngChat != nullptr ) {
        gpMngChat->closeInputPopupIfOpen();
    }
    return true;
}

void GMFreeride::enterCurrentActivity() {
    // FUN_00796e10
    if ( getPlayerGameMech()->enterCurrentActivity() ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: GSAI
    }
}

bool GMFreeride::enterSpot( SpotMap *pSpot ) {
    // FUN_00796120
    gpScenaric->setActiveExtraChallenge( nullptr );
    pPendingSpot = pSpot;

    const ScenaricBank::eBankState bankState = spotBank.getState();
    if ( bankState != ScenaricBank::BBS_Unused &&
         bankState != ScenaricBank::BBS_LoadFailed ) {
        gpScenaric->unloadBank( &spotBank );
    }

    if ( pIntroCine == nullptr ) {
        acquireSpotIntroCine( pSpot );
    }

    if ( pIntroCine == nullptr || pSpot == nullptr ) {
        gpScenaric->setCurrentSpotIndex(
            gpScenaric->getSpotList().getSpotMapIndex( pSpot->Hashcode ) );
        transitionFlags |= 0x20;
        LaunchSpotLevel( pSpot, false );
        return true;
    }

    pIntroCine->setGameModeInterface( this );
    pIntroCine->setAsyncActorLoad( true );
    pSavedVehicleCamera = gpCamera->getActiveCamera();

    PlayerConfiguration *pPlayerCfg =
        ( gpConfig != nullptr ) ? gpConfig->pPlayerConfig : nullptr;
    if ( pPlayerCfg != nullptr ) {
        ClearActionFlags( pPlayerCfg->pMenuPause );
        ClearActionFlags( pPlayerCfg->pMenuValidate );
    }

    if ( ( gpMngFade->getTransitionFlags() & 1u ) == 0 ) {
        gpMngFade->startTransition(
            ( gpMngFade->getTransitionFlags() & ~0xeu ) | 1u,
            kSpotIntroFadeDuration, &GMFreeride::OnSpotIntroFadeOut, nullptr,
            this );
    }
    return true;
}

void GMFreeride::acquireSpotIntroCine( SpotMap *pSpot ) {
    // FUN_007930e0
    if ( pIntroCine != nullptr || pSpot == nullptr || nextIntro.bIsActive ) {
        return;
    }

    const SpotSector *pSector =
        gpScenaric->getSpotList().getSpotSector( pSpot->Hashcode );
    if ( pSector == nullptr || pSpot->Type == ST_Challenge ||
         ( pSpot->Type == ST_Unknown8 && pSpot->SubType == 0 ) ) {
        return;
    }

    if ( pSpot->Type == ST_House &&
         !gpPlayerData->getLoadedSave()->getGarage()->isHouseOwned(
             pSpot->Hashcode ) ) {
        return;
    }

    ScenaricBank *pBank = nullptr;
    pIntroCine = FindStreamedSpotCinematic( pSector, "C_INTRO_E_1", &pBank );
    if ( pIntroCine == nullptr ) {
        pIntroCine = FindStreamedSpotCinematic( pSector, "C_INTRO_E", &pBank );
    }
    if ( pIntroCine == nullptr ) {
        pIntroCine = FindStreamedSpotCinematic( pSector, "c_intro_e", &pBank );
    }
    if ( pIntroCine == nullptr ) {
        return;
    }

    pIntroCine->setGameModeInterface( this );
    pIntroCine->setStopWhenSkipped( false );
}

void GMFreeride::OnSpotIntroFadeOut( void *pData, void * /*pSource*/ ) {
    // FUN_00795a70
    GMFreeride *pFreeride = static_cast<GMFreeride *>( pData );
    if ( pFreeride->pIntroCine == nullptr ||
         pFreeride->pPendingSpot == nullptr ) {
        return;
    }

    gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() | 8u );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    gpAI->setTrafficCategories( false, 2u );

    pFreeride->pIntroCine->launchSpotCine( pFreeride->pPendingSpot, 0.0f );
    pFreeride->pPendingSpot = nullptr;
}

static void SetJoinClipVisible( FlashPlayer *pPlayer, const bool bVisible ) {
    // FUN_00792ce0
    FlashClip clip;
    if ( pPlayer != nullptr && pPlayer->getClipPlayer( "/HUD/join", &clip ) ) {
        clip.setProperty( eFlashProperty::FP_Visible, bVisible );
    }
}

static void ExitToMap() {
    gpActiveGM->requestExit();
    gpActiveGM->setTransitionFlags( 0 );
    gpTestDriveInstance->setNextGameMode( GM_Map );
}

bool GMFreeride::GoHome() {
    // FUN_00794680
    const uint64_t houseHashcode =
        gpPlayerData->getLoadedSave()->getDriver()->getCurrentHouse();
    Spots &spots = gpScenaric->getSpotList();
    const int32_t houseIndex = spots.getSpotMapIndex( houseHashcode );
    gpScenaric->setCurrentSpotIndex( houseIndex );

    SpotMap *pHouse = spots.getWorldSpotChecked( houseIndex );
    if ( pHouse == nullptr ) {
        ExitToMap();
        return true;
    }

    LaunchSpotLevel( pHouse, false );
    return true;
}

bool GMFreeride::enterCurrentHouseSpot() {
    // FUN_007947c0
    SpotMap *pSpot = getPlayerGameMech()->getCurrentSpot();
    if ( pSpot == nullptr || pSpot->Type != ST_House ) {
        return false;
    }

    gpPlayerData->getLoadedSave()->getDriver()->selectCurrentHouse(
        pSpot->Hashcode );
    gpScenaric->setCurrentSpotIndex(
        gpScenaric->getSpotList().getSpotMapIndex( pSpot->Hashcode ) );
    LaunchSpotLevel( pSpot, false );
    return true;
}

bool GMFreeride::goToSelectedSpot() {
    // FUN_00794610
    SpotMap *pSpot = flashListSpots.getSelectedSpot();
    if ( pSpot == nullptr ) {
        ExitToMap();
        return true;
    }

    gpScenaric->setCurrentSpotIndex(
        gpScenaric->getSpotList().getSpotMapIndex( pSpot->Hashcode ) );
    LaunchSpotLevel( pSpot, false );
    return true;
}

bool GMFreeride::launchCurrentSpotInfo() {
    // FUN_00794820
    SpotMap *pSpot = getPlayerGameMech()->getCurrentSpot();
    if ( pSpot == nullptr ) {
        return false;
    }

    gpScenaric->setCurrentSpotIndex(
        gpScenaric->getSpotList().getSpotMapIndex( pSpot->Hashcode ) );
    LaunchSpotLevel( pSpot, true );
    return true;
}

bool GMFreeride::onMessage( FlashMessage *pMessage ) {
    // FUN_00796f40
    if ( GameModeWorld::onMessage( pMessage ) ||
         mngPhotoMode.onMessage( pMessage ) ||
         mngBookmark.onMessage( pMessage ) ) {
        return true;
    }

    if ( gpMngInGameMenu != nullptr &&
         gpMngInGameMenu->onMessage( pMessage ) ) {
        return true;
    }

    switch ( *pMessage ) {
    case FlashMessage( "inic" ):
    case FlashMessage( "goic" ):
    case FlashMessage( "icbo" ):
    case FlashMessage( "noic" ):
        return mngFRMeetingIC.onMessage( pMessage );
    case FlashMessage( "gogp" ):
        mngFlash.requestGPSTransition();
        return true;
    case FlashMessage( "gosp" ):
    case FlashMessage( "goch" ): {
        CameraGPS *pCameraGPS = static_cast<CameraGPS *>(
            gpCamera->findCameraWithMode( 3, gpCamera->getBoundCarState() ) );
        if ( pCameraGPS != nullptr && pCameraGPS->isInTransition() ) {
            return true;
        }
        if ( GetTickCountMs() < activityReentryTick ||
             gpAI->getCopManager().interceptFreerideAction( *pMessage ) ) {
            return true;
        }
        return requestEnterActivity();
    }
    case FlashMessage( "goho" ):
        if ( gpAI->getCopManager().interceptFreerideAction( *pMessage ) ) {
            return true;
        }
        return enterCurrentHouseSpot();
    case FlashMessage( "home" ):
        if ( gpAI->getCopManager().interceptFreerideAction( *pMessage ) ) {
            return true;
        }
        return GoHome();
    case FlashMessage( "gosh" ):
        if ( gpAI->getCopManager().interceptFreerideAction( *pMessage ) ) {
            return true;
        }
        return goToSelectedSpot();
    case FlashMessage( "info" ):
        if ( gpAI->getCopManager().interceptFreerideAction( *pMessage ) ) {
            return true;
        }
        return launchCurrentSpotInfo();
    case FlashMessage( "acex" ):
        if ( gpAI->getCopManager().interceptFreerideAction( *pMessage ) ) {
            return true;
        }
        resolveExtraChallengeAnswer();
        return true;
    case FlashMessage( "caex" ): {
        gpScenaric->getAvatarManager().setUnknownBoolean( false );
        activityReentryTick = GetTickCountMs() + kActivityReentryDelay;
        bCanAcceptExtraChallenge = false;
        gpScenaric->setActiveExtraChallenge( nullptr );
        if ( pExtraChallengeAvatar != nullptr &&
             pNearestExtraChallenge != nullptr ) {
            OTDU_UNIMPLEMENTED; // TODO: AVATAR (spot avatar vtbl +0xcc, pose
                                // reset to the challenge matrix)
        }
        FlashPlayer *pHudPlayer = mngHudChallenge.getHudPlayer();
        if ( pHudPlayer != nullptr ) {
            pHudPlayer->showFrame( "stop", "/HUD/enter", false );
        }
        mngFade.startTransition( ( mngFade.getTransitionFlags() & ~0xcu ) | 1u,
                                 kSpotIntroFadeDuration,
                                 &GMFreeride::OnExtraChallengeCancelFadeOut,
                                 nullptr, this );
        return true;
    }
    case FlashMessage( "acti" ):
        mngHudChallenge.updateActivityStatus();
        return true;
    case FlashMessage( "noch" ):
        SetJoinClipVisible( mngHudChallenge.getHudPlayer(), false );
        return true;
    case FlashMessage( "ssmn" ):
        if ( strcmp( reinterpret_cast<const char *>( pMessage ) + 5,
                     "activities" ) == 0 ) {
            mngHudChallenge.updateActivityLocks();
        }
        return true;
    default:
        break;
    }

    return false;
}

void GMFreeride::onPauseMenuRequested( bool bOpened ) {
    // FUN_007934f0
    GameModeWorld::onPauseMenuRequested( bOpened );

    OTDU_UNIMPLEMENTED; // TODO: ONLINE
}

void GMFreeride::onMenuPauseExit( bool bRestoreState ) {
    // FUN_00793460
    GameModeWorld::onMenuPauseExit( bRestoreState );

    bResumedFromPauseMenu = true;

    OTDU_UNIMPLEMENTED; // TODO: ONLINE
}

#ifdef OTDU_USE_IMGUI
void GMFreeride::drawDebug( float deltaTime ) {
    if ( ImGui::CollapsingHeader( "Map screenshots" ) ) {
        if ( ImGui::Checkbox( "Enabled", &gbMapScreenshotsMngEnabled ) &&
             gbMapScreenshotsMngEnabled &&
             std::find( registeredManagers.begin(), registeredManagers.end(),
                        &mngMapScreenshot ) == registeredManagers.end() ) {
            registerManagerFirst( &mngMapScreenshot );
            mngMapScreenshot.initialize( gpTestDriveInstance );
            mngMapScreenshot.reset();
        }

        if ( gbMapScreenshotsMngEnabled ) {
            mngMapScreenshot.drawDebug();
        } else {
            ImGui::TextDisabled( "Disabled" );
        }
    }

    if ( gpDatabase != nullptr ) {
        const GameDatabase &gdb = gpDatabase->getGameDB();
        const int32_t numCars = static_cast<int32_t>( gdb.pNumItems[1] );
        if ( numCars > 0 && gdb.DBCarData != nullptr ) {
            auto &carList = gpDatabase->getCarList();

            CarState *pPaintTarget =
                ( gpScenaric != nullptr )
                    ? gpScenaric->getLocalPlayer().getCarState()
                    : nullptr;
            VehicleBase *pPaintVehicle =
                ( pPaintTarget != nullptr ) ? pPaintTarget->getVehicleInstance()
                                            : nullptr;
            const bool bCanRepaint =
                ( pPaintVehicle != nullptr &&
                  pPaintTarget->getCarID() == gRequestedPlayerCarID );

            static int32_t deluxeType = 0;
            static float deluxeMain[4] = { 0.8f, 0.1f, 0.1f, 1.0f };
            static float deluxeSecond[4] = { 0.1f, 0.1f, 0.8f, 1.0f };
            static float deluxeLogo[4] = { 0.9f, 0.9f, 0.9f, 1.0f };
            static float deluxeSecond2[4] = { 0.1f, 0.6f, 0.2f, 1.0f };
            const bool bDeluxe = ( deluxeType != 0 );

            auto applySelection = [&]() {
                if ( !bCanRepaint ) {
                    return;
                }

                auto toColor = []( const float *pRGBA ) {
                    return ColorRGBA( (uint8_t)( pRGBA[0] * 255.0f ),
                                      (uint8_t)( pRGBA[1] * 255.0f ),
                                      (uint8_t)( pRGBA[2] * 255.0f ),
                                      (uint8_t)( pRGBA[3] * 255.0f ) );
                };

                const ColorRGBA mainColor = toColor( deluxeMain );
                const ColorRGBA secondColor = toColor( deluxeSecond );
                const ColorRGBA logoColor = toColor( deluxeLogo );
                const ColorRGBA secondColor2 = toColor( deluxeSecond2 );

                pPaintVehicle->applyPaint(
                    (uint32_t)gRequestedPlayerCarID, (ePaintType)deluxeType,
                    gRequestedPlayerCarColorID, gRequestedPlayerCarRimID,
                    gRequestedPlayerCarInteriorID,
                    bDeluxe ? &mainColor : nullptr,
                    bDeluxe ? &secondColor : nullptr,
                    bDeluxe && pPaintVehicle->shouldUseDeluxeTwoTones()
                        ? &logoColor
                        : nullptr,
                    bDeluxe && pPaintVehicle->shouldUseDeluxeTwoTones()
                        ? &secondColor2
                        : nullptr );
            };

            ImGui::SeparatorText( "Vehicle" );
            if ( ImGui::BeginListBox( "##DbgVehicleList" ) ) {
                for ( auto &car : carList ) {
                    const bool bSelected =
                        ( car.Hash == gRequestedPlayerCarID );

                    std::string carIDAndName = std::to_string( car.Hash );
                    carIDAndName += " - ";
                    carIDAndName += car.FullName;

                    if ( ImGui::Selectable( carIDAndName.c_str(),
                                            bSelected ) ) {
                        gRequestedPlayerCarID = car.Hash;
                        gRequestedPlayerCarColorID = 0;
                        gRequestedPlayerCarRimID = -1;
                        gRequestedPlayerCarInteriorID = 0;
                        gRequestedCarChange =
                            ( pPaintTarget == nullptr ||
                              pPaintTarget->getCarID() != car.Hash );
                    }

                    if ( bSelected )
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndListBox();
            }

            const int32_t numColors =
                gpDatabase->getNumColors( gRequestedPlayerCarID );
            if ( gRequestedPlayerCarColorID >= numColors ) {
                gRequestedPlayerCarColorID = 0;
            }

            ImGui::SeparatorText( "Paint" );
            if ( ImGui::BeginListBox( "##DbgPaintList" ) ) {
                if ( ImGui::Selectable( "Deluxe Paint",
                                        gRequestedPlayerCarColorID == -1 ) ) {
                    gRequestedPlayerCarColorID = -1;
                    deluxeType = 1;
                    applySelection();
                }
                if ( gRequestedPlayerCarColorID == -1 )
                    ImGui::SetItemDefaultFocus();

                for ( int32_t i = 0; i < numColors; i++ ) {
                    auto color =
                        gpDatabase->getColorByIndex( gRequestedPlayerCarID, i );

                    const bool bSelected = ( gRequestedPlayerCarColorID == i );

                    const char *pName = color->ColorName.pAsString;
                    if ( ImGui::Selectable( ( pName != nullptr ) ? pName
                                                                 : "<unnamed>",
                                            bSelected ) ) {
                        deluxeType = 0;
                        gRequestedPlayerCarColorID = i;

                        applySelection();
                    }

                    if ( bSelected )
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndListBox();
            }

            if ( gRequestedPlayerCarColorID == -1 ) {
                static const char *kPaintTypeNames[] = { "None", "Default",
                                                         "Metal" };

                ImGui::SeparatorText( "Deluxe paint" );
                if ( ImGui::Combo( "##DbgDeluxePaintType", &deluxeType,
                                   kPaintTypeNames,
                                   IM_ARRAYSIZE( kPaintTypeNames ) ) ) {
                    applySelection();
                }

                if ( bDeluxe ) {
                    bool bColorChanged = false;
                    bColorChanged |= ImGui::ColorEdit4( "Primary", deluxeMain );
                    bColorChanged |=
                        ImGui::ColorEdit4( "Highlights", deluxeSecond );
                    if ( pPaintVehicle->shouldUseDeluxeTwoTones() ) {
                        bColorChanged |=
                            ImGui::ColorEdit4( "Secondary", deluxeLogo );
                        bColorChanged |= ImGui::ColorEdit4(
                            "Secondary Hightlights", deluxeSecond2 );
                    }

                    if ( bColorChanged ) {
                        applySelection();
                    }
                }
            }

            DBEntryCarColor *pSelectedColor =
                ( numColors > 0 && gRequestedPlayerCarColorID >= 0 )
                    ? gpDatabase->getColorByIndex( gRequestedPlayerCarID,
                                                   gRequestedPlayerCarColorID )
                    : nullptr;

            int32_t numInteriors = 0;
            while ( gpDatabase->getInteriorByIndex(
                        pSelectedColor, numInteriors ) != nullptr ) {
                numInteriors++;
            }
            if ( gRequestedPlayerCarInteriorID >= numInteriors ) {
                gRequestedPlayerCarInteriorID = 0;
            }

            ImGui::SeparatorText( "Interior" );
            if ( numInteriors == 0 ) {
                ImGui::TextDisabled( "No interior for this paint." );
            } else if ( ImGui::BeginListBox( "##DbgInteriorList" ) ) {
                for ( int32_t i = 0; i < numInteriors; i++ ) {
                    DBEntryInterior *pInterior =
                        gpDatabase->getInteriorByIndex( pSelectedColor, i );

                    const bool bSelected =
                        ( gRequestedPlayerCarInteriorID == i );
                    const char *pName = pInterior->InteriorName.pAsString;

                    if ( ImGui::Selectable( ( pName != nullptr ) ? pName
                                                                 : "<unnamed>",
                                            bSelected ) ) {
                        gRequestedPlayerCarInteriorID = i;
                        applySelection();
                    }

                    if ( bSelected )
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndListBox();
            }

            ImGui::SeparatorText( "Rims" );
            if ( ImGui::BeginListBox( "##DbgRimList" ) ) {
                if ( ImGui::Selectable( "Default",
                                        gRequestedPlayerCarRimID == -1 ) )
                    gRequestedPlayerCarRimID = -1;
                if ( gRequestedPlayerCarRimID == -1 )
                    ImGui::SetItemDefaultFocus();

                const int32_t numRims =
                    gpDatabase->getNumRims( gRequestedPlayerCarID );
                for ( int32_t i = 0; i < numRims; i++ ) {
                    DBEntryRim *pRim =
                        gpDatabase->getRim( gRequestedPlayerCarID, i );
                    if ( pRim == nullptr ) {
                        continue;
                    }

                    const bool bSelected = ( gRequestedPlayerCarRimID == i );
                    const char *pName = pRim->FriendlyName.pAsString;

                    if ( ImGui::Selectable( ( pName != nullptr ) ? pName
                                                                 : "<unnamed>",
                                            bSelected ) ) {
                        gRequestedPlayerCarRimID = i;
                        applySelection();
                    }

                    if ( bSelected )
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndListBox();
            }
        }
    }

    ImGui::SeparatorText( "Teleport" );
    CarState *pCar = ( gpScenaric != nullptr )
                         ? gpScenaric->getLocalPlayer().getCarState()
                         : nullptr;
    if ( pCar == nullptr ) {
        ImGui::TextDisabled( "No local-player car spawned." );
    } else {
        // Absolute world position (row 3 of the body's world matrix).
        const Eigen::Vector4f cur = pCar->getBody()->WorldMatrix.row( 3 );
        ImGui::Text( "Current: (%.1f, %.1f, %.1f)", cur.x(), cur.y(), cur.z() );

        // Seed the input fields from the current position on first display.
        static float target[3] = { 0.0f, 0.0f, 0.0f };
        static bool bSeeded = false;
        if ( !bSeeded ) {
            target[0] = cur.x();
            target[1] = cur.y();
            target[2] = cur.z();
            bSeeded = true;
        }

        ImGui::SeparatorText( "Bookmarks" );
        if ( ImGui::Button( "Manoa Falls" ) ) {
            target[0] = 41464.73f;
            target[1] = 88.72f;
            target[2] = 25543.82f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Secret Island Teleporter" ) ) {
            target[0] = 63519.45f;
            target[1] = 223.50f;
            target[2] = 46305.20f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Oahu Raceway" ) ) {
            target[0] = 34569.15f;
            target[1] = 3.06f;
            target[2] = 45154.43f;
        }

        if ( ImGui::Button( "Secret Island" ) ) {
            target[0] = 64285.047f;
            target[1] = 37.758f;
            target[2] = 44133.129f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Japanese Temple" ) ) {
            target[0] = 44802.07f;
            target[1] = 88.87f;
            target[2] = 32675.04f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Observatory" ) ) {
            target[0] = 10643.63f;
            target[1] = 864.36f;
            target[2] = 23641.65f;
        }

        if ( ImGui::Button( "AI Trainer Sector" ) ) {
            target[0] = 9122.41f;
            target[1] = 0.65f;
            target[2] = 9847.77f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Tunnel" ) ) {
            target[0] = 50037.879f;
            target[1] = 356.729f;
            target[2] = 40454.828f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Diamond Head" ) ) {
            target[0] = 48442.840f;
            target[1] = 190.627f;
            target[2] = 52137.379f;
        }

        if ( ImGui::Button( "Over lighthouse car park" ) ) {
            target[0] = 548.4f;
            target[1] = 38.7f;
            target[2] = 17767.62f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Pearl City" ) ) {
            target[0] = 31846.57f;
            target[1] = 20.22f;
            target[2] = 37179.66f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "HMap LOD Bug Repro" ) ) {
            target[0] = 48413.344f;
            target[1] = 309.431f;
            target[2] = 41670.789f;
        }
        if ( ImGui::Button( "Hitchhiker" ) ) {
            target[0] = 3615.492f;
            target[1] = 2.727f;
            target[2] = 20461.934f;
        }

        ImGui::SetNextItemWidth( 300.0f );
        ImGui::DragFloat3( "World XYZ", target, 1000.0f );
        if ( ImGui::Button( "Use current" ) ) {
            target[0] = cur.x();
            target[1] = cur.y();
            target[2] = cur.z();
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Teleport" ) ) {
            gRequestedPlayerTeleportPos =
                Eigen::Vector3f( target[0], target[1], target[2] );
            gRequestedPlayerTeleport = true;
        }
    }
}
#endif
