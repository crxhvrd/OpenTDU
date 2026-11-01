#include "shared.h"
#include "gm_estates.h"

#include "tdu_instance.h"

#include "ai/ai_pool.h"
#include "ai/gs_ai.h"
#include "core/format.h"
#include "database/gs_database.h"
#include "flash/flash_callbacks_register.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/avatar/gs_avatar.h"
#include "game/mng_gps.h"
#include "game/mng_music.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/game_intro_tutorial.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "game/vehicle/car_state.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_data/achievement_checks.h"
#include "player_data/achievements_data.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_extra_challenges.h"
#include "player_data/saved_garage.h"
#include "player_data/saved_road_discovery.h"
#include "render/gs_render.h"
#include "render/render_scene.h"
#include "world/details/gs_details.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/streaming_manager.h"
#include "world/weather/gs_weather.h"

static constexpr float kFadeDuration = 0.5f;           // FLOAT_00f41674
static constexpr float kExteriorFovVsLodFactor = 1.0f; // FLOAT_00f4165c
static constexpr float kInteriorFovVsLodFactor = 0.5f; // FLOAT_00f41674
static constexpr float kReflectionOffsetScale = 0.5f;  // FLOAT_00f41674
static constexpr float kTwipsToPixels = 0.05f;         // DAT_00f41778

static constexpr int32_t kHousePurchaseStatCounter = 14;
static constexpr int32_t kHouseSaleCounter = CounterIndex( 0xf0b4 );
static constexpr int32_t kNumHousesSoldCounter = CounterIndex( 0xf128 );

static constexpr int32_t kSectorSize = 0x300;
static constexpr const char *kRemovalCarPositionName = "PosCarRemoval";

static void SetDetailEnabled( const uint32_t detailHash, const bool bEnabled ) {
    // FUN_008a7630
    TDUDetail *pDetail = gpDetails->getDetailByHash( detailHash );
    if ( pDetail != nullptr ) {
        pDetail->setEnabled( bEnabled );
    }
}

static void SetAmbientDetailsEnabled( const bool bEnabled ) {
    SetDetailEnabled( kDetailHashBirds, bEnabled );
    SetDetailEnabled( kDetailHashPapers, bEnabled );
}

static void SetReflectionSceneEnabled( const bool bEnabled ) {
    if ( gpMngReflection == nullptr ) {
        return;
    }
    RenderScene *pScene = gpMngReflection->getBoundScene();
    if ( pScene != nullptr ) {
        pScene->setEnabled( bEnabled );
    }
}

static void MoveStreamCenter( const Eigen::Vector4f &position ) {
    // FUN_007d6870
    if ( !gpWorldStreaming->isStreamingActive() ) {
        return;
    }
    gpWorldStreaming->setStreamCenter( nullptr );
    gpWorldStreaming->setStreamCenterPosition( position );
    gpWorldStreaming->updateStreaming();
}

static void StreamAround( const SpotMap *pSpot ) {
    Eigen::Vector4f position( pSpot->X, pSpot->Y, pSpot->Z, 1.0f );
    gpWorld->setStreamingPosition( &position );
}

static void SetLocalCarPostponedPause( const uint8_t request ) {
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr ) {
        return;
    }

    PhysicsVehicle *pPhysicsVehicle = pLocalCar->getPhysicsVehicle();
    if ( pPhysicsVehicle == nullptr ) {
        return;
    }

    const bool bPaused = pPhysicsVehicle->isPaused();
    if ( ( request == 1 && bPaused ) || ( request == 2 && !bPaused ) ) {
        return;
    }

    PhysicsLock lock;
    pPhysicsVehicle->setPostponedPause( request );
}

static void ExitToFreeride( GameMode *pGameMode ) {
    pGameMode->requestExit();
    gpTestDriveInstance->setNextGameMode( GM_FreeRide );
    gpPlayerData->requestSave( 2 );
}

static inline Driver *GetDriver() {
    return gpPlayerData->getLoadedSave()->getDriver();
}

static inline Garage *GetGarage() {
    return gpPlayerData->getLoadedSave()->getGarage();
}

GMEstates::GMEstates()
    : GameModeWorldBase(),
      pFlashPlayer( nullptr ),
      sceneEstates(),
      savedSpotIndex( 0 ),
      pHouseSpot( nullptr ),
      bTourTransition( false ),
      tour( HT_None ),
      bStreamToTarget( false ),
      bLaunchSellCine( false ),
      targetPosition( Eigen::Vector4f::Zero() ),
      bInfosLoadedPending( false ),
      bExitToFreeride( false ),
      bRemovalRequested( false ),
      bTourRequested( false ),
      pBuyCinematic( nullptr ),
      bInstallIntro( true ),
      pTourCinematic( nullptr ),
      sceneLiving(),
      sceneGarage(),
      houses(),
      retakeHouseHashcode( 0ull ),
      mngReflection(),
      houseList(),
      savedReflectionNormal( Eigen::Vector3f::Zero() ),
      savedReflectionOffset( 0.0f ),
      worldSizeX( 0.0f ),
      worldSizeZ( 0.0f ) {
    // FUN_0079ac60
    houseList.setOwner( this );
    setTransitionFlags( 0x27 );
    registerManager( &mngReflection );
}

GMEstates::~GMEstates() {
    // FUN_0079abe0
}

bool GMEstates::OnHousePictureLoaded( HousePictureBank * /*pBank*/,
                                      void *pUserData ) {
    // LAB_007984c0
    if ( pUserData != nullptr ) {
        static_cast<GMEstates *>( pUserData )->bInfosLoadedPending = true;
    }
    return true;
}

void GMEstates::initialize() {
    // FUN_0079b9a0
    GameModeWorldBase::initialize();

    if ( gpMngOcean != nullptr ) {
        gbOceanEnabled = true;
    }

    mngWorld.setStreamEnabled( true );
    mngWorld.setDisableStreamingOnCar( true );
    mngActor.setIsManager( false );

    gpMngMusic->setPlayFlags( 1 );
    gpFlash->getFlashSound()->playFrontEndMusic( true );

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    if ( pStreaming != nullptr ) {
        const AreaGrid &areaGrid = pStreaming->areaGrid;
        const SectorManager &sectorManager = pStreaming->sectorManager;
        worldSizeX =
            float( int32_t( areaGrid.numAreas * sectorManager.numSectors ) *
                   kSectorSize );
        worldSizeZ = float(
            int32_t( areaGrid.numSubAreas * sectorManager.numSubSectors ) *
            kSectorSize );
    }

    houses.getPictureBank().setBuffer(
        TestDrive::AllocAligned( HousePictureBank::kBufferSize, 0x10 ) );

    gpAI->setDisableBots( true );
    gpAI->setBotsActive( false );
    gpAI->getAIPool()->removeBots();
}

void GMEstates::reset() {
    // FUN_0079a880
    GameModeScene::reset();

    SetAmbientDetailsEnabled( false );

    pFlashPlayer = gpFlash->getFlashPlayer( "SHOPS" );
    if ( pFlashPlayer == nullptr ) {
        return;
    }

    pFlashPlayer->setVariableValue( "/:NoHouses", 0 );
    mngFlash.addWidget( "hous", &houseList );
    houseList.setPlayer( pFlashPlayer, "/ss_menu/array_houses" );

    savedSpotIndex = gpScenaric->getCurrentSpotIndex();
    if ( savedSpotIndex != -1 ) {
        pHouseSpot =
            gpScenaric->getSpotList().getWorldSpotChecked( savedSpotIndex );
    }

    initializeHouseList();

    if ( !houses.isRealtor() ) {
        const DBEntryHouse *pHouse = FindHouseEntry( pHouseSpot->Hashcode );
        const bool bBonusHouse =
            pHouse != nullptr &&
            strcmp( pHouse->RealtorHashcode.pAsString, "ERE_BONUS" ) == 0;
        if ( bBonusHouse ) {
            pFlashPlayer->setVariableValue( "/:SixtyFiveHouse", 1 );
            const char *pMessage =
                gpDatabase->getStringByHashcode( 0xf, 0x22b29784 );
            if ( pMessage != nullptr ) {
                pFlashPlayer->setVariableValueNonLinked( "/:msg", pMessage );
            }
        } else {
            pFlashPlayer->setVariableValue( "/:SixtyFiveHouse", 0 );
        }

        pFlashPlayer->showFrame( "house_sell", "/", true );
        StreamAround( pHouseSpot );
        gpWorld->setDirtyStreaming( true );
        SetLocalCarPostponedPause( 1 );
        bLaunchSellCine = true;
    } else {
        pFlashPlayer->showFrame( "realtor", "/", true );
        updateLocationMarker();
        loadRealtorScene( true );
        SetLocalCarPostponedPause( 1 );
    }

    if ( gpMngGPS != nullptr ) {
        gpMngGPS->announceArrival();
    }
}

void GMEstates::initializeHouseList() {
    // FUN_0079a7c0
    gFlashCallbacksRegister.removeAllSynonyms(
        &EstatesHouses::PictureDrawCallback );
    houses.getPictureBank().release();
    houses.setFlashPlayer( nullptr );

    houses.initialize( pHouseSpot );
    houses.selectHouse( &GMEstates::OnHousePictureLoaded, this, 0 );

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bDrawSector = false;
    pStreaming->sectorManager.bNeedStreamUpdate = true;
    pStreaming->areaGrid.bNeedStreamUpdate = true;
    pStreaming->areaGrid.bDrawArea = false;

    if ( houses.isRealtor() ) {
        refreshHouseList();
    }
}

void GMEstates::refreshHouseList() {
    gFlashCallbacksRegister.removeAllSynonyms(
        &FlashListHouses::HomeLureDrawCallback );
    houseList.refresh();
    pFlashPlayer->setVariableValue( "/:NoHouses",
                                    houseList.getNumHouses() == 0 ? 1 : 0 );
}

void GMEstates::loadRealtorScene( const bool bInstall ) {
    // FUN_00798740
    SceneInterior *pContent = sceneEstates.getContent();
    if ( pContent->isEntered() ) {
        return;
    }

    bInstallIntro = bInstall;
    sceneEstates.initialize();
    pContent->setModeActive( true );
}

void GMEstates::onRealtorSceneReady() {
    // FUN_00799520
    SceneInterior *pContent = sceneEstates.getContent();
    installCinematics( pContent->getSourceSpot(), &pContent->getBank() );
    if ( !bInstallIntro ) {
        pCinematicIntroI = nullptr;
    }

    pBuyCinematic = static_cast<EntityCinematic *>(
        pContent->getBank().findEntity( eEntityType::ET_CINE, "C_BUY_HOUSE" ) );
    if ( pBuyCinematic != nullptr ) {
        pBuyCinematic->setGameModeInterface( this );
        pBuyCinematic->setStopWhenSkipped( false );
    }

    StreamAround( pHouseSpot );

    if ( gpMngReflection != nullptr ) {
        savedReflectionNormal = gpMngReflection->getPlaneNormal();
        savedReflectionOffset =
            gpMngReflection->getPlaneOffset() * kReflectionOffsetScale;
    }

    sceneEstates.resume();
}

bool GMEstates::isRealtorSceneLive() {
    // FUN_0042c370
    SceneInterior *pContent = sceneEstates.getContent();
    return pContent->isEntered() &&
           pContent->getState() == SceneInterior::SIS_Live;
}

bool GMEstates::isHouseStreamed( const Eigen::Vector4f &position ) {
    // FUN_00798850
    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    SectorManager &sectorManager = pStreaming->sectorManager;

    GridPos2D sectorCell;
    GridPos2D areaCell;
    sectorManager.computeCell( position, sectorCell, areaCell );

    const Sector *pCurrentSector = sectorManager.pCurrentSector;
    if ( pCurrentSector == nullptr ||
         pCurrentSector->installedArea.posX != areaCell.posX ||
         pCurrentSector->installedArea.posY != areaCell.posY ||
         pCurrentSector->installedSector.posX != sectorCell.posX ||
         pCurrentSector->installedSector.posY != sectorCell.posY ||
         pCurrentSector->state != 1 || pStreaming->spotStream.isLoading() ) {
        return false;
    }

    SceneInterior *pLivingContent = sceneLiving.getContent();
    if ( pLivingContent->isEntered() &&
         pLivingContent->getState() != SceneInterior::SIS_Live ) {
        return false;
    }

    if ( sceneGarage.isEntered() &&
         sceneGarage.getState() != SceneInterior::SIS_Live ) {
        return false;
    }

    SceneInterior *pContent = sceneEstates.getContent();
    return !pContent->isEntered() ||
           pContent->getState() == SceneInterior::SIS_Live;
}

void GMEstates::tick( float deltaTime, float totalTime ) {
    // FUN_0079b2f0
    GameModeScene::tick( deltaTime, totalTime );

    if ( sceneEstates.getContent()->getState() == SceneInterior::SIS_Ready ) {
        onRealtorSceneReady();
    }
    sceneEstates.tick( deltaTime, totalTime );

    sceneLiving.getContent()->tick();

    sceneGarage.SceneInterior::tick();
    sceneGarage.getCarPark().tick( deltaTime );
    if ( sceneGarage.getState() == SceneInterior::SIS_Ready ) {
        sceneGarage.onBankReady();
    }

    houses.tickPendingPicture();

    if ( bInfosLoadedPending ) {
        bInfosLoadedPending = false;
        pFlashPlayer->setVariableValue( "/:InfosLoaded", 1 );
        houses.setPictureCallbacksEnabled( true );
    }

    tickHouseVisit();
    bVerifyGameFreeze = false;
}

void GMEstates::draw( float /*deltaTime*/, float /*totalTime*/ ) {
    // FUN_00798b30
    // NOP
}

void GMEstates::tickHouseVisit() {
    // FUN_0079b2f0 (inlined)
    if ( bLaunchSellCine ) {
        bLaunchSellCine = false;
        sceneLiving.initialize();
        sceneLiving.getContent()->setModeActive( true );
        launchTour( HT_Outward );

        pCinematicLoop = pTourCinematic;
        pTourCinematic = nullptr;
        if ( pCinematicLoop != nullptr ) {
            pCinematicLoop->launchSpotCine( pHouseSpot, 0.0f );
        }
        return;
    }

    if ( bTourRequested ) {
        EstatesHouses::Entry &entry = houses.getSelectedEntry();
        SpotMap *pHouse = entry.pSpot;
        gpScenaric->setCurrentSpotIndex(
            gpScenaric->getSpotList().getSpotMapIndex( pHouse->Hashcode ) );
        bTourTransition = true;
        targetPosition =
            Eigen::Vector4f( pHouse->X, pHouse->Y, pHouse->Z, 1.0f );

        if ( pCinematicLoop != nullptr ) {
            pCinematicLoop->stop( true );
        }
        gpAI->setTrafficCategories( false, 2 );
        sceneEstates.suspend();
        if ( pTourCinematic != nullptr ) {
            pTourCinematic->stop( true );
        }

        switch ( tour ) {
        case HT_Living:
            sceneLiving.initialize();
            gpRender->setFovVsLodFactor( kInteriorFovVsLodFactor );
            sceneLiving.getContent()->setModeActive( true );
            break;
        case HT_Garage:
            sceneGarage.loadBank( true );
            sceneGarage.setModeActive( true );
            sceneGarage.getCarPark().setEnabled( true );
            sceneGarage.getCarPark().setEnabled( false );
            gpRender->setFovVsLodFactor( kInteriorFovVsLodFactor );
            break;
        case HT_Removal:
            mngFade.setRenderLogo( true );
            [[fallthrough]];
        case HT_Outward:
            sceneLiving.initialize();
            gpRender->setFovVsLodFactor( kExteriorFovVsLodFactor );
            sceneLiving.getContent()->setModeActive( true );
            break;
        default:
            break;
        }

        gpWorld->setStreamingPosition( &targetPosition );
        gpWorld->setDirtyStreaming( true );
        bTourRequested = false;
        return;
    }

    if ( bTourTransition ) {
        if ( !isHouseStreamed( targetPosition ) || pTourCinematic != nullptr ) {
            mngFade.setTransitionFlags( mngFade.getTransitionFlags() | 8u );
            return;
        }

        launchTour( tour );
        if ( pTourCinematic == nullptr ) {
            if ( bExitToFreeride ) {
                bTourTransition = false;
                bExitToFreeride = false;
                ExitToFreeride( this );
                gpScenaric->getIntroTutorial().onHouseBought(
                    houses.getSelectedEntry().pSpot );
            }
            return;
        }

        bTourTransition = false;
        if ( tour != HT_Removal ) {
            pFlashPlayer->showFrame( "change", "help/title", true );
            mngFade.setRenderLogo( false );
            mngFade.startTransition(
                ( mngFade.getTransitionFlags() & ~8u ) | 5u, kFadeDuration );
            return;
        }

        SetReflectionSceneEnabled( false );
        return;
    }

    if ( bRemovalRequested ) {
        bRemovalRequested = false;
        startTour( HT_Removal );
        pFlashPlayer->setVariableValue( "/:LockFE", 1 );
        return;
    }

    if ( !bStreamToTarget ) {
        return;
    }

    const bool bStreamed = isHouseStreamed( targetPosition );
    if ( bStreamed && !isRealtorSceneLive() ) {
        return;
    }

    if ( bStreamed && ( mngFade.getTransitionFlags() & 1u ) == 0 ) {
        bStreamToTarget = false;
        pFlashPlayer->setVariableValue( "/:LockFE", 0 );
        MoveStreamCenter( targetPosition );
        mngWorld.setStreamEnabled( true );
        SetAmbientDetailsEnabled( false );

        mngFade.setRenderLogo( false );
        mngFade.startTransition( ( mngFade.getTransitionFlags() & ~8u ) | 5u,
                                 kFadeDuration );

        if ( gpMngReflection != nullptr ) {
            SetReflectionSceneEnabled( true );
            gpMngReflection->setPlanarCoords( savedReflectionNormal,
                                              savedReflectionOffset /
                                                  kReflectionOffsetScale );
        }

        StreamAround( pHouseSpot );
        sceneEstates.resume();
        if ( pCinematicLoop != nullptr ) {
            pCinematicLoop->launchSpotCine( pHouseSpot, 0.0f );
        }
        sceneEstates.getContent()->applyWeatherDescriptor();
        return;
    }

    gpWorld->setStreamingPosition( &targetPosition );
    gpWorld->setDirtyStreaming( true );
    MoveStreamCenter( targetPosition );
    mngWorld.setStreamEnabled( false );

    mngFade.setTransitionFlags( mngFade.getTransitionFlags() | 8u );
}

void GMEstates::startTour( const eHouseTour newTour ) {
    // FUN_00798970
    SetReflectionSceneEnabled( false );
    tour = newTour;
    bTourRequested = true;
    mngFade.setRenderLogo( true );
    mngFade.setTransitionFlags( mngFade.getTransitionFlags() | 8u );
}

void GMEstates::OnTourFadeOut( void *pData, void *pSource ) {
    // FUN_00798970
    static_cast<GMEstates *>( pData )->startTour(
        eHouseTour( reinterpret_cast<intptr_t>( pSource ) ) );
}

bool GMEstates::launchTour( const eHouseTour newTour ) {
    // FUN_00799ce0
    SpotMap houseSpot = *houses.getSelectedEntry().pSpot;
    Spots &spots = gpScenaric->getSpotList();
    gpScenaric->setCurrentSpotIndex(
        spots.getSpotMapIndex( houseSpot.Hashcode ) );

    bool bLoop = true;
    bool bUpdateWeather = false;
    switch ( newTour ) {
    case HT_Outward: {
        const SpotSector *pSector = spots.getSpotSector( houseSpot.Hashcode );
        if ( pSector != nullptr ) {
            pTourCinematic = FindStreamedSpotCinematic( pSector, "C_VTOUR_E" );
            SetAmbientDetailsEnabled( true );
        }
        sceneLiving.suspend();
        bUpdateWeather = pSector != nullptr;
        break;
    }
    case HT_Living:
        pTourCinematic = static_cast<EntityCinematic *>(
            sceneLiving.getContent()->getBank().findEntity(
                eEntityType::ET_CINE, "C_VTOUR_L" ) );
        sceneLiving.resume();
        break;
    case HT_Garage:
        houseSpot.Y = sceneGarage.getWorldPosition().y();
        pTourCinematic =
            static_cast<EntityCinematic *>( sceneGarage.getBank().findEntity(
                eEntityType::ET_CINE, "C_VTOUR_G" ) );
        break;
    case HT_Removal: {
        const SpotSector *pSector = spots.getSpotSector( houseSpot.Hashcode );
        if ( pSector == nullptr ) {
            break;
        }
        pTourCinematic = FindStreamedSpotCinematic( pSector, "C_REMOVAL" );
        if ( pTourCinematic != nullptr ) {
            pTourCinematic->setStopWhenSkipped( false );
        }
        bLoop = false;
        SetAmbientDetailsEnabled( true );
        mngFade.setRenderLogo( false );
        bUpdateWeather = true;
        break;
    }
    default:
        break;
    }

    if ( bUpdateWeather ) {
        const eGameMode activeGM = gpTestDriveInstance->getActiveGameMode();
        if ( activeGM > GM_Invalid && activeGM < GM_Count ) {
            gpWeather->setCurrentWeatherDesc(
                gpWeather->getWeatherDescName( activeGM ) );
        }
    }

    if ( pTourCinematic != nullptr ) {
        pTourCinematic->setGameModeInterface( this );
        pTourCinematic->launchSpotCine( &houseSpot, 0.0f );
        pTourCinematic->setLooping( bLoop );
    }

    pFlashPlayer->setVariableValue( "/:LockFE", 0 );
    return true;
}

void GMEstates::OnTourExitFadeOut( void *pData, void * /*pSource*/ ) {
    // LAB_007996b0
    GMEstates *pEstates = static_cast<GMEstates *>( pData );
    pEstates->mngFade.setTransitionFlags(
        pEstates->mngFade.getTransitionFlags() | 8u );
    if ( pEstates->pTourCinematic != nullptr ) {
        pEstates->pTourCinematic->stop( true );
        gpScenaric->setCurrentSpotIndex( pEstates->savedSpotIndex );
    }
}

void GMEstates::OnBuyCinematicFadeOut( void *pData, void * /*pSource*/ ) {
    // LAB_00799630
    GMEstates *pEstates = static_cast<GMEstates *>( pData );
    pEstates->bRemovalRequested = true;
    pEstates->pBuyCinematic->stop( true );
    pEstates->mngFade.setRenderLogo( true );
    pEstates->mngFade.setTransitionFlags(
        pEstates->mngFade.getTransitionFlags() | 8u );

    uint32_t levelIndex = 0;
    uint32_t movieIndex = 0;
    if ( gpFlash->getFlashMovieIndex( &levelIndex, &movieIndex, "SHOPS" ) ) {
        MoviePlayer *pMovie = gpFlash->getMoviePlayer( int32_t( levelIndex ),
                                                       int32_t( movieIndex ) );
        if ( pMovie != nullptr ) {
            pMovie->IsMoviePaused |= 2;
        }
    }
}

bool GMEstates::executeAction( int32_t param_2, void *param_3, void *pData ) {
    // FUN_0079ae90
    if ( GameModeScene::executeAction( param_2, param_3, pData ) ) {
        return true;
    }

    EntityCinematic *pCine = static_cast<EntityCinematic *>( param_3 );
    switch ( param_2 ) {
    case CE_Ended: {
        if ( bExitToFreeride || pTourCinematic == nullptr ||
             pTourCinematic != pCine ) {
            return true;
        }
        pTourCinematic = nullptr;

        if ( ( tour != HT_Living && tour != HT_Outward ) || !bTourTransition ) {
            SceneInterior *pLivingContent = sceneLiving.getContent();
            if ( pLivingContent->isEntered() ) {
                sceneLiving.pause();
                pLivingContent->setModeActive( false );
                sceneLiving.terminate();
            }
        }

        if ( ( tour != HT_Garage || !bTourTransition ) &&
             sceneGarage.isEntered() ) {
            sceneGarage.setModeActive( false );
            sceneGarage.getCarPark().setEnabled( false );
            sceneGarage.terminate();
        }

        if ( houses.isRealtor() ) {
            if ( bTourTransition ) {
                return true;
            }

            bStreamToTarget = true;
            tour = HT_Outward;
            targetPosition = Eigen::Vector4f( pHouseSpot->X, pHouseSpot->Y,
                                              pHouseSpot->Z, 1.0f );
            return true;
        }

        if ( bTourTransition ) {
            return true;
        }

        mngFade.startTransition( ( mngFade.getTransitionFlags() & ~8u ) | 5u,
                                 kFadeDuration );
        pCinematicLoop->launchSpotCine( pHouseSpot, 0.0f );
        pFlashPlayer->setVariableValue( "/:LockFE", 0 );

        AiPool *pAIPool = gpAI->getAIPool();
        pAIPool->setTrafficPaused( false );
        pAIPool->setField0x7C( true );
        pAIPool->setField0x85( true );
        gpAI->setPauseTraffic( false );
        return true;
    }

    case CE_Started:
        if ( tour == HT_Removal && pTourCinematic == pCine ) {
            mngFade.setRenderLogo( false );
            mngFade.startTransition(
                ( mngFade.getTransitionFlags() & ~8u ) | 5u, kFadeDuration );
            return false;
        }

        if ( pCinematicLoop != pCine || tour == HT_None ) {
            return false;
        }

        mngFade.startTransition( ( mngFade.getTransitionFlags() & ~8u ) | 5u,
                                 kFadeDuration );
        return false;

    case CE_StartEndFade:
        if ( !bExitToFreeride ) {
            return false;
        }

        if ( pBuyCinematic != nullptr && pBuyCinematic == pCine ) {
            if ( ( mngFade.getTransitionFlags() & 1u ) != 0 ) {
                return false;
            }
            mngFade.startTransition(
                ( mngFade.getTransitionFlags() & ~0xcu ) | 1u, kFadeDuration,
                &GMEstates::OnBuyCinematicFadeOut, nullptr, this );
            return false;
        }

        if ( pTourCinematic == nullptr || pTourCinematic != pCine ) {
            return false;
        }
        pTourCinematic = nullptr;

        {
            SpotMap *pHouse = houses.getSelectedEntry().pSpot;
            savedSpotIndex =
                gpScenaric->getSpotList().getSpotMapIndex( pHouse->Hashcode );
            targetPosition =
                Eigen::Vector4f( pHouse->X, pHouse->Y, pHouse->Z, 1.0f );
        }
        MoveStreamCenter( targetPosition );
        ExitToFreeride( this );
        gpScenaric->getIntroTutorial().onHouseBought(
            houses.getSelectedEntry().pSpot );
        return false;

    case CE_Begun:
        if ( pCine != pBuyCinematic || pCinematicLoop == nullptr ) {
            return false;
        }
        pCinematicLoop->stop( true );
        return false;

    default:
        return false;
    }
}

bool GMEstates::isDrawnInFakeDOF() {
    // FUN_00798780
    if ( pTourCinematic != nullptr ) {
        switch ( tour ) {
        case HT_Outward:
        case HT_Garage:
        case HT_Removal:
            return false;
        case HT_Living:
            return sceneLiving.getContent()->isDrawnInDOFPass();
        default:
            break;
        }
    }
    return sceneEstates.getContent()->isDrawnInDOFPass();
}

void GMEstates::pause() {
    // FUN_00799b20
    if ( pCinematicIntroI != nullptr ) {
        pCinematicIntroI->stop( true );
    }
    if ( pCinematicLoop != nullptr ) {
        pCinematicLoop->stop( true );
    }
    if ( pTourCinematic != nullptr ) {
        pTourCinematic->stop( true );
    }

    if ( !bExitToFreeride ) {
        gpScenaric->setCurrentSpotIndex( savedSpotIndex );
        MoveStreamCenter( Eigen::Vector4f( pHouseSpot->X, pHouseSpot->Y,
                                           pHouseSpot->Z, 1.0f ) );
    } else {
        placeRemovalCar();
    }

    mngFlash.removeWidget( &houseList );
    gFlashCallbacksRegister.removeAllSynonyms(
        &FlashListHouses::HomeLureDrawCallback );
    gFlashCallbacksRegister.removeAllSynonyms(
        &EstatesHouses::PictureDrawCallback );
    houses.getPictureBank().release();
    houses.setFlashPlayer( nullptr );

    SetLocalCarPostponedPause( 2 );

    GameModeScene::pause();
}

void GMEstates::placeRemovalCar() {
    // FUN_00798b40
    const SpotMap *pHouse = houses.getSelectedEntry().pSpot;
    if ( pHouse == nullptr ) {
        return;
    }

    const SpotSector *pSector =
        gpScenaric->getSpotList().getSpotSector( pHouse->Hashcode );
    ScenaricBank *pBank =
        ( pSector != nullptr ) ? FindStreamedSpotBank( pSector ) : nullptr;
    if ( pBank == nullptr ) {
        return;
    }

    for ( int32_t i = 0; i < pBank->getEntityCount(); i++ ) {
        Entity *pEntity = pBank->getEntity( i );
        if ( pEntity != nullptr && pEntity->getType() == eEntityType::ET_CARP &&
             pEntity->getInstanceName() == kRemovalCarPositionName ) {
            gpWorld->setPreviousTransform( pEntity->getModelMatrix() );
        }
    }
}

void GMEstates::leaveRealtorScene() {
    // FUN_007996e0
    SceneInterior *pContent = sceneEstates.getContent();
    if ( !pContent->isEntered() ) {
        return;
    }

    if ( pCinematicLoop != nullptr ) {
        pCinematicLoop->stop( true );
    }
    pCinematicLoop = nullptr;
    pBuyCinematic = nullptr;

    sceneEstates.pause();
    pContent->setModeActive( false );
    sceneEstates.terminate();
    mngFade.setRenderLogo( true );
}

void GMEstates::destroy() {
    // FUN_00799f00
    mngWorld.setDisableStreamingOnCar( false );

    SceneInterior *pLivingContent = sceneLiving.getContent();
    if ( pLivingContent->isEntered() ) {
        sceneLiving.pause();
        pLivingContent->setModeActive( false );
        sceneLiving.terminate();
    }

    if ( houses.isRealtor() ) {
        leaveRealtorScene();
    }

    void *pPictureBuffer = houses.getPictureBank().getBuffer();
    if ( pPictureBuffer != nullptr ) {
        TestDrive::FreeAligned( pPictureBuffer );
        houses.getPictureBank().setBuffer( nullptr );
    }

    GameModeWorldBase::destroy();
}

bool GMEstates::onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) {
    // FUN_0079a6c0
    switch ( *pMessage ) {
    case FlashMessage( "rltr" ):
        onRealtorReset();
        return true;
    case FlashMessage( "chho" ):
        changeHouse();
        return true;
    case FlashMessage( "buho" ):
        updateMoneyState();
        return true;
    case FlashMessage( "buyh" ):
        onBuyHouse();
        return true;
    case FlashMessage( "losh" ):
        onLocateRealtor();
        return true;
    case FlashMessage( "vtin" ):
        onTourIn();
        return true;
    case FlashMessage( "seho" ):
        onCheckSellCapacity();
        return true;
    case FlashMessage( "selh" ):
        onSellHouse();
        return true;
    case FlashMessage( "vtou" ):
        onTourOut();
        return true;
    default:
        return false;
    }
}

void GMEstates::onRealtorReset() {
    // FUN_0079a640
    retakeHouseHashcode = 0ull;
    refreshHouseList();
    pFlashPlayer->setVariableValue( "/:num_house", 1 );
    houses.selectHouse( &GMEstates::OnHousePictureLoaded, this, 0 );
    updateLocationMarker();
}

void GMEstates::changeHouse() {
    // FUN_0079a510
    char nextHouse[64];
    pFlashPlayer->getVariableAsString( "/:next_house", nextHouse );
    const int32_t houseNumber = pFlashPlayer->getVariableAsInt( "/:num_house" );
    pFlashPlayer->setVariableValue( "/:InfosLoaded", 0 );
    houses.selectHouse( &GMEstates::OnHousePictureLoaded, this,
                        houseNumber - 1 );

    const DBEntryHouse *pRetake = ( retakeHouseHashcode != 0ull )
                                      ? FindHouseEntry( retakeHouseHashcode )
                                      : nullptr;
    if ( pRetake != nullptr ) {
        const int32_t priceBuy = houses.getSelectedEntry().PriceBuy;
        const int32_t priceSell = pRetake->PriceSell;

        char priceText[32];
        FormatMoneyString( priceText, Max( priceBuy - priceSell, 0 ) );
        pFlashPlayer->setVariableValueNonLinked( "/:PriceAfterRetake",
                                                 priceText );

        int32_t gain = 0;
        if ( priceSell - priceBuy > 0 ) {
            FormatMoneyString( priceText, priceSell - priceBuy );
            pFlashPlayer->setVariableValueNonLinked( "/:PriceEarned",
                                                     priceText );
            gain = 1;
        }
        pFlashPlayer->setVariableValue( "/:Gain", gain );
    }

    updateMoneyState();
    updateLocationMarker();
}

void GMEstates::updateMoneyState() {
    // FUN_00798fa0
    uint32_t money = GetDriver()->getMoney();
    Garage *pGarage = GetGarage();

    const EstatesHouses::Entry &entry = houses.getSelectedEntry();
    int32_t freeGarageSlots = pGarage->getNumGarageSlots() -
                              pGarage->getNumCars() + entry.NumGarageSlot;
    if ( retakeHouseHashcode != 0ull ) {
        const DBEntryHouse *pRetake = FindHouseEntry( retakeHouseHashcode );
        money += uint32_t( pRetake->PriceSell );
        freeGarageSlots -= pRetake->NumGarageSlot;
    }

    int32_t enoughMoney = 0;
    if ( uint32_t( entry.PriceBuy ) <= money ) {
        if ( retakeHouseHashcode == 0ull || freeGarageSlots >= 0 ) {
            pFlashPlayer->setVariableValue( "/:EnoughMoney", 1 );
            return;
        }
        enoughMoney = 2;
    }

    pFlashPlayer->setVariableValue( "/:EnoughMoney", enoughMoney );
    pFlashPlayer->setVariableValue( "/:Gain", 2 );
}

void GMEstates::onCheckSellCapacity() {
    // FUN_00798f30
    Garage *pGarage = GetGarage();
    const DBEntryHouse *pRetake = FindHouseEntry( retakeHouseHashcode );
    const int32_t freeGarageSlots = pGarage->getNumGarageSlots() -
                                    pGarage->getNumCars() -
                                    pRetake->NumGarageSlot;
    pFlashPlayer->setVariableValue( "/:EnoughMoney",
                                    freeGarageSlots < 0 ? 0 : 1 );
}

void GMEstates::updateLocationMarker() {
    // FUN_007991c0
    const SpotMap *pHouse = houses.getSelectedEntry().pSpot;

    FlashClip pictureClip;
    if ( !pFlashPlayer->getClipPlayer( "ss_menu/location/pict",
                                       &pictureClip ) ) {
        return;
    }

    FlashRECT boundingBox;
    pictureClip.getBoundingBox( false, boundingBox );
    const float mapWidth =
        ( boundingBox.Width - boundingBox.X ) * kTwipsToPixels;
    const float mapHeight =
        ( boundingBox.Height - boundingBox.Y ) * kTwipsToPixels;

    FlashClip circleClip;
    if ( !pFlashPlayer->getClipPlayer( "ss_menu/location/circle",
                                       &circleClip ) ) {
        return;
    }

    circleClip.setProperty( eFlashProperty::FP_X,
                            ( pHouse->X / worldSizeX ) * mapWidth );
    circleClip.setProperty( eFlashProperty::FP_Y,
                            ( pHouse->Z / worldSizeZ ) * mapHeight );
}

void GMEstates::onLocateRealtor() {
    // FUN_007989b0
    const DBEntryHouse *pHouse = FindHouseEntry( pHouseSpot->Hashcode );
    if ( pHouse == nullptr ) {
        return;
    }

    const uint64_t realtorHashcode =
        GetIdentifier64bit( pHouse->RealtorHashcode.pAsString );
    Spots &spots = gpScenaric->getSpotList();
    const int32_t numSpots = int32_t( spots.getNumWorldSpots() );
    for ( int32_t i = 0; i < numSpots; i++ ) {
        SpotMap *pSpot = spots.getWorldSpot( i );
        if ( pSpot->Hashcode != realtorHashcode ) {
            continue;
        }

        ObjectiveMarker &objective = gpScenaric->getObjective( 1 );
        objective.Position =
            Eigen::Vector4f( pSpot->X, pSpot->Y, pSpot->Z, 1.0f );
        objective.Type = "finish";
        return;
    }
}

void GMEstates::onTourIn() {
    // FUN_007990a0
    char whatTour[64];
    pFlashPlayer->getVariableAsString( "/:WhatTour", whatTour );

    eHouseTour newTour = HT_Outward;
    if ( strcmp( whatTour, "outward" ) != 0 ) {
        if ( strcmp( whatTour, "garage" ) == 0 ) {
            newTour = HT_Garage;
        } else if ( strcmp( whatTour, "living" ) == 0 ) {
            newTour = HT_Living;
        }
    }

    mngFade.startTransition( ( mngFade.getTransitionFlags() & ~0xcu ) | 1u,
                             kFadeDuration, &GMEstates::OnTourFadeOut,
                             reinterpret_cast<void *>( intptr_t( newTour ) ),
                             this );
    pFlashPlayer->setVariableValue( "/:LockFE", 1 );
}

void GMEstates::onTourOut() {
    // FUN_00799980
    if ( pTourCinematic == nullptr ||
         ( pTourCinematic->getFlags() & 4u ) == 0 ) {
        return;
    }

    pFlashPlayer->setVariableValue( "/:LockFE", 1 );
    if ( pCinematicLoop != nullptr ) {
        pCinematicLoop->createTDUInterface();
    }

    mngFade.startTransition( ( mngFade.getTransitionFlags() & ~0xcu ) | 1u,
                             kFadeDuration, &GMEstates::OnTourExitFadeOut,
                             nullptr, this );
}

void GMEstates::onBuyHouse() {
    // FUN_00799fb0
    EstatesHouses::Entry &entry = houses.getSelectedEntry();
    const uint64_t houseHashcode = entry.pSpot->Hashcode;

    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    Driver *pDriver = pSave->getDriver();
    pSave->getGarage()->addHouse( houseHashcode );

    if ( retakeHouseHashcode == 0ull ) {
        if ( pDriver->getCurrentHouse() == 0ull ) {
            pDriver->selectCurrentHouse( houseHashcode );
        }
        pDriver->addExpense( entry.PriceBuy, kHousePurchaseStatCounter, true );
        pDriver->startMoneyAnimation();
    } else {
        const DBEntryHouse *pRetake = FindHouseEntry( retakeHouseHashcode );
        pSave->getExtraChallenges()->addCounter( kHouseSaleCounter,
                                                 pRetake->PriceSell );
        pDriver->setPendingMoneyOperation( pDriver->getPendingMoneyOperation() +
                                           pRetake->PriceSell );

        pDriver->addExpense( entry.PriceBuy, kHousePurchaseStatCounter, true );
        pDriver->startMoneyAnimation();

        relocateRetakeHouseCars( retakeHouseHashcode );
        if ( pDriver->getCurrentHouse() == retakeHouseHashcode ) {
            pDriver->selectCurrentHouse( houseHashcode );
        }
        refreshHouseList();
    }

    AchievementsData *pAchievements = pSave->getAchievements();
    if ( pAchievements->getField516_0x204() && pAchievements->isReady() ) {
        pAchievements->setCurrentGameMode(
            gpTestDriveInstance->getActiveGameMode() );
    }
    pAchievements->notify( ACF_Challenge | ACF_Road | ACF_Collection, false );

    bExitToFreeride = true;
    if ( pBuyCinematic == nullptr ) {
        SpotMap houseSpot = *entry.pSpot;
        Spots &spots = gpScenaric->getSpotList();
        gpScenaric->setCurrentSpotIndex(
            spots.getSpotMapIndex( houseSpot.Hashcode ) );

        bool bLoop = true;
        const SpotSector *pSector = spots.getSpotSector( houseSpot.Hashcode );
        if ( pSector != nullptr ) {
            pTourCinematic = FindStreamedSpotCinematic( pSector, "C_REMOVAL" );
            if ( pTourCinematic != nullptr ) {
                pTourCinematic->setStopWhenSkipped( false );
            }
            bLoop = false;
            SetAmbientDetailsEnabled( true );
            mngFade.setRenderLogo( false );

            const eGameMode activeGM = gpTestDriveInstance->getActiveGameMode();
            if ( activeGM > GM_Invalid && activeGM < GM_Count ) {
                gpWeather->setCurrentWeatherDesc(
                    gpWeather->getWeatherDescName( activeGM ) );
            }
        }

        if ( pTourCinematic != nullptr ) {
            pTourCinematic->setGameModeInterface( this );
            pTourCinematic->launchSpotCine( &houseSpot, 0.0f );
            pTourCinematic->setLooping( bLoop );
        }
        pFlashPlayer->setVariableValue( "/:LockFE", 0 );
    } else {
        pBuyCinematic->createTDUInterface();
        pBuyCinematic->launchSpotCine( pHouseSpot, 0.0f );
    }

    DiscoverSpotRoad( entry.pSpot );
}

void GMEstates::onSellHouse() {
    // FUN_00799750
    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    Driver *pDriver = pSave->getDriver();

    const DBEntryHouse *pRetake = FindHouseEntry( retakeHouseHashcode );
    pSave->getExtraChallenges()->addCounter( kHouseSaleCounter,
                                             pRetake->PriceSell );
    pDriver->setPendingMoneyOperation( pDriver->getPendingMoneyOperation() +
                                       pRetake->PriceSell );
    pDriver->startMoneyAnimation();

    pSave->getExtraChallenges()->addCounter( kNumHousesSoldCounter, 1 );

    relocateRetakeHouseCars( retakeHouseHashcode );

    if ( pDriver->getCurrentHouse() == retakeHouseHashcode ) {
        Garage *pGarage = pSave->getGarage();
        const DBEntryHouse *pFirstHouse = pGarage->getOwnedHouse( 0 );
        pDriver->selectCurrentHouse(
            ( pFirstHouse != nullptr )
                ? GetIdentifier64bit( pFirstHouse->SpotName.pAsString )
                : pGarage->pCars[0].getHouseHashcode() );
    }

    gpPlayerData->requestSave( 1 );

    refreshHouseList();
}

void GMEstates::relocateRetakeHouseCars( const uint64_t houseHashcode ) {
    // FUN_007992f0
    Garage *pGarage = GetGarage();
    Driver *pDriver = GetDriver();

    const int32_t numCars = pGarage->getNumCarsInHouse( houseHashcode );
    for ( int32_t i = 0; i < numCars; i++ ) {
        GarageSlot *pCar = pGarage->getCarAtIndex( i, houseHashcode, false );
        const int32_t carSlot = pGarage->getSlotIndex( pCar );

        int32_t freeSlot = pGarage->findFreeSlot( 0ull, -1 );
        while ( freeSlot != -1 &&
                pGarage->pCars[freeSlot].getHouseHashcode() == houseHashcode ) {
            freeSlot = pGarage->findFreeSlot( 0ull, freeSlot );
        }
        if ( freeSlot == -1 ) {
            OTDU_LOG_WARN( "No free garage slot to move car %d out of the "
                           "sold house\n",
                           carSlot );
            continue;
        }

        GarageSlot &destination = pGarage->pCars[freeSlot];
        destination.CarDesc::operator=( *pCar );
        destination.ebayID = pCar->ebayID;

        if ( carSlot == pDriver->CurrentCar ) {
            pDriver->CurrentCar = uint16_t( freeSlot );
            pDriver->notify( 0 );
        }
    }

    pGarage->removeHouse( retakeHouseHashcode );
}
