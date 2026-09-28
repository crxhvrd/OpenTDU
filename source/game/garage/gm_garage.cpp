#include "shared.h"
#include "gm_garage.h"

#include "tdu_instance.h"

#include "database/gs_database.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "game/mng_music.h"
#include "game/scenaric/avatar_manager.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "render/gs_render.h"
#include "world/details/gs_details.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/mng_world.h"
#include "world/streaming_manager.h"

static bool gbGarageSortRequested = false; // DAT_010e7c98

static constexpr int32_t kInnerViewDelayTicks = 10;
static constexpr float kShowcaseCameraNearPlane = 0.01f; // DAT_00f416d4
static constexpr float kResetCinematicTimer = -50.0f;    // DAT_00f41cb8

static inline Garage *GetGarage() {
    return gpPlayerData->getLoadedSave()->getGarage();
}

static inline Driver *GetDriver() {
    return gpPlayerData->getLoadedSave()->getDriver();
}

GMGarageNotifier::GMGarageNotifier( GMGarage *pOwner )
    : Notifier(), pGMGarage( pOwner ) {}

void GMGarageNotifier::onUpdate( uint32_t param ) {
    // FUN_007903b0
    if ( param == 4 ) {
        pGMGarage->exit();
    }
}

GMGarage::GMGarage()
    : GameModeScene(),
      mngReflection(),
      mngLogoDisplay(),
      mngCarAlpha(),
      garageNotifier( this ),
      pFlashPlayer( nullptr ),
      sceneGarage(),
      carList( this ),
      sortSourceSlot( -1 ),
      carSpecs(),
      selectedCar( 0 ),
      selectedSlot( 0 ),
      exitAction( EA_None ),
      innerViewDelay( -1 ) {
    // FUN_00790c50
    setTransitionFlags( 0x27 );

    registerManager( &mngReflection );
    registerManager( &mngLogoDisplay );
    registerManager( &mngCarAlpha );
}

GMGarage::~GMGarage() {
    // FUN_00790b50
}

void GMGarage::initialize() {
    // FUN_007915e0
    GameModeScene::initialize();

    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( false );
    }

    sceneGarage.loadBank( false );
    sceneGarage.setModeActive( true );
    sceneGarage.getCarPark().setEnabled( true );

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bDrawSector = true;
    pStreaming->areaGrid.bDrawArea = true;
    pStreaming->setActiveRenderLayers( 0 );

    mngFlash.addWidget( "case", &carList );

    mngShadow.setCastersEnabled( true );
    mngShadow.setReceiversEnabled( true );
    OTDU_UNIMPLEMENTED; // TODO: SHADOW

    VehicleBase::bApplyWheelAttenuation = false;
    gpMngMusic->setPlayFlags( 1 );
}

void GMGarage::reset() {
    // FUN_00790670
    GameModeScene::reset();

    if ( gpDetails != nullptr ) {
        TDUDetail *pBirds = gpDetails->getDetailByHash( kDetailHashBirds );
        if ( pBirds != nullptr ) {
            pBirds->setEnabled( false );
        }

        TDUDetail *pPapers = gpDetails->getDetailByHash( kDetailHashPapers );
        if ( pPapers != nullptr ) {
            pPapers->setEnabled( false );
        }
    }

    LocalPlayer &localPlayer = gpScenaric->getLocalPlayer();
    CarState *pLocalCar = localPlayer.getCarState();
    if ( pLocalCar != nullptr ) {
        EntityDriver *pDriver = pLocalCar->getDriver();
        if ( pDriver != nullptr ) {
            pLocalCar->unloadDriver();
            gpScenaric->getAvatarManager().releaseAvatar( pDriver );
        }

        if ( pLocalCar->getPhysicsVehicle() != nullptr ) {
            OTDU_UNIMPLEMENTED; // TODO: PHYSICS
        }

        gpCarState->unloadVehicle( pLocalCar, false, false );
        localPlayer.setCarState( nullptr );
        gpCamera->setBoundCarState( nullptr );
    }

    pFlashPlayer = gpFlash->getFlashPlayer( "HOME" );
    if ( pFlashPlayer == nullptr ) {
        return;
    }

    pFlashPlayer->setVariableValueNonLinked( "ss_menu/param:car_name",
                                             "Test Drive Unlimited" );

    sceneGarage.setupCamera();
    gpCamera->setCameraMode( 7, 0, 0.0f );

    CameraGame *pShowcaseCamera =
        gpCamera->findCameraWithMode( 6, gpCamera->getBoundCarState() );
    pShowcaseCamera->setNearPlane( kShowcaseCameraNearPlane );
    pShowcaseCamera->setFarPlane( gpRender->getDefaultFarPlane() );

    if ( gpWorldStreaming != nullptr &&
         gpWorldStreaming->isStreamingActive() ) {
        gpWorldStreaming->setStreamCenter( nullptr );
    }

    selectedCar = 0;
    selectCar( 0 );

    Garage *pGarage = GetGarage();

    char value[32];
    snprintf( value, sizeof( value ), "%d", pGarage->getNumHouseCars() );
    pFlashPlayer->setVariableValueNonLinked( "/:nb_cars", value );

    snprintf( value, sizeof( value ), "%d", selectedCar + 1 );
    pFlashPlayer->setVariableValueNonLinked( "/:num_car", value );

    pFlashPlayer->showFrame( "garage_in", "/", true );

    carList.setPlayer( pFlashPlayer, "/ss_menu/array_list" );
    carList.getNotifier().pMovie = pFlashPlayer;
    carSpecs.pMovie = pFlashPlayer;

    pGarage->subscribe( &carList.getNotifier() );
    pGarage->subscribe( &garageNotifier );

    watchCar(
        pGarage->getSlotIndex( pGarage->getHouseCarAtIndex( selectedCar ) ) );

    const GarageSlot &currentCar = pGarage->pCars[GetDriver()->CurrentCar];
    pFlashPlayer->setVariableValue( "/:player_car",
                                    currentCar.getConfig()->FullName.c_str() );
    pFlashPlayer->setVariableValue( "/:player_cur_upgrade",
                                    currentCar.getPackLevel() );
    pFlashPlayer->showFrame( "gold", "ss_menu/param/car_name", false );
    pFlashPlayer->setVariableValueNonLinked( "/:CarStatus", "gold" );

    cinematicTimer = kResetCinematicTimer;
}

void GMGarage::tick( float deltaTime, float totalTime ) {
    // FUN_00791720
    GameModeScene::tick( deltaTime, totalTime );

    gbGarageSortRequested = false;

    sceneGarage.tick();
    sceneGarage.getCarPark().tick( deltaTime );

    if ( sceneGarage.getState() == SceneInterior::SIS_Ready ) {
        sceneGarage.onBankReady();
    }

    if ( sceneGarage.getState() == SceneInterior::SIS_Ready ) {
        sceneGarage.getCarPark().openInnerView( selectedCar );
        sceneGarage.selectCar( selectedCar );
        installCinematics( sceneGarage.getSourceSpot(),
                           &sceneGarage.getBank() );
    }

    if ( innerViewDelay > 0 ) {
        innerViewDelay--;
        if ( innerViewDelay < 1 &&
             sceneGarage.isCarOnCurrentPage( selectedCar ) ) {
            sceneGarage.getCarPark().openInnerView( selectedCar );
        }
    }
}

void GMGarage::destroy() {
    // FUN_00790a40
    VehicleBase::bApplyWheelAttenuation = true;

    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( true );
    }

    mngShadow.setReceiversEnabled( false );

    sceneGarage.setModeActive( false );
    sceneGarage.getCarPark().setEnabled( false );
    sceneGarage.terminate();

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bNeedStreamUpdate = true;
    pStreaming->sectorManager.bDrawSector = false;

    pStreaming->areaGrid.bNeedStreamUpdate = true;
    pStreaming->areaGrid.bDrawArea = false;

    pStreaming->setActiveRenderLayers( 1 );

    Garage *pGarage = GetGarage();
    pGarage->unsubscribe( &carList.getNotifier() );
    pGarage->unsubscribe( &garageNotifier );
    carSpecs.detach();

    mngFlash.removeWidget( &carList );

    GameModeScene::destroy();
}

void GMGarage::watchCar( const int32_t slot ) {
    // FUN_007902f0
    Garage *pGarage = GetGarage();

    if ( slot != -1 ) {
        carSpecs.watch( &pGarage->pCars[slot] );
        return;
    }

    Driver *pDriver = GetDriver();
    if ( !pDriver->isValid() ) {
        return;
    }

    carSpecs.watch( &pGarage->pCars[pDriver->CurrentCar] );
}

void GMGarage::selectCar( const int32_t carIndex ) {
    // FUN_00790530
    Garage *pGarage = GetGarage();
    selectedSlot =
        pGarage->getSlotIndex( pGarage->getHouseCarAtIndex( carIndex ) );

    const GarageSlot &car = pGarage->pCars[selectedSlot];
    pFlashPlayer->setVariableValue( "/:player_car",
                                    car.getConfig()->FullName.c_str() );
    pFlashPlayer->setVariableValue( "/:player_cur_upgrade",
                                    car.getPackLevel() );

    DBEntryCarPhysicsData *pCarEntry =
        gpDatabase->getCarDBEntry( int32_t( car.getCarConfigID() ) );
    if ( pCarEntry != nullptr ) {
        pFlashPlayer->setVariableValueNonLinked(
            "/:player_group", pCarEntry->CarGroup.pAsString );
    }

    pFlashPlayer->showFrame( "gold", "ss_menu/param/car_name", false );
    pFlashPlayer->setVariableValueNonLinked( "/:CarStatus", "gold" );
}

void GMGarage::exit() {
    // FUN_007901a0
    switch ( exitAction ) {
    case EA_Home:
        requestExit();
        gpTestDriveInstance->setNextGameMode( GM_Home );
        pFlashPlayer->stopClip( "/" );
        break;

    case EA_Living:
        transitionFlags &= ~0x20u;
        requestExit();
        gpTestDriveInstance->setNextGameMode( GM_Living );
        pFlashPlayer->stopClip( "/" );
        break;

    case EA_FreeRide: {
        SpotMap *pSourceSpot = sceneGarage.getSourceSpot();
        if ( pSourceSpot != nullptr ) {
            gpScenaric->setNextIntro( "level\\hawai\\spots\\", nullptr,
                                      "C_OUTRO", GM_FreeRide, pSourceSpot );
        }

        pFlashPlayer->stopClip( "/" );
        requestExit();
        gpTestDriveInstance->setNextGameMode( GM_FreeRide );
        break;
    }

    case EA_Cancel:
        exitAction = EA_None;
        break;

    default:
        break;
    }
}

bool GMGarage::leave( const eExitAction action ) {
    // FUN_007903d0
    exitAction = action;
    sceneGarage.getCarPark().releaseCar( selectedCar );

    Garage *pGarage = GetGarage();
    GarageSlot &car = pGarage->pCars[selectedSlot];

    const GarageSlot &rentalSlot = pGarage->pCars[Garage::kNumSavedCarSlots];
    if ( rentalSlot.state == CarDesc::eState::CDS_Rental ) {
        if ( car.state != CarDesc::eState::CDS_Rental ) {
            OTDU_UNIMPLEMENTED; // TODO: PLAYERDATA
            gpPlayerData->selectCurrentCar( &car );
        }

        exit();
        return true;
    }

    const uint64_t selectedCarHouse = pGarage->getCarHouseHashcode( &car );
    const uint64_t currentCarHouse = pGarage->getCarHouseHashcode(
        &pGarage->pCars[GetDriver()->CurrentCar] );

    if ( currentCarHouse == GetDriver()->getCurrentHouse() ) {
        gpPlayerData->selectCurrentCar( &car );
        exit();
        return true;
    }

    OTDU_UNIMPLEMENTED; // TODO: PLAYERDATA
    (void)selectedCarHouse;

    exit();
    return false;
}

void GMGarage::refreshCarStatus( const bool bSelectedCar ) {
    // FUN_00790e10 (inlined)
    const char *pStatus = bSelectedCar ? "gold" : "normal";
    pFlashPlayer->showFrame( pStatus, "ss_menu/param/car_name", false );
    pFlashPlayer->setVariableValueNonLinked( "/:CarStatus", pStatus );
}

void GMGarage::swapCars() {
    // FUN_00790e10 (inlined)
    const int32_t targetSlot = carList.getSelectedSlot();
    if ( targetSlot == sortSourceSlot ) {
        return;
    }

    exitAction = EA_Cancel;

    Garage *pGarage = GetGarage();
    GarageSlot *pSourceCar = &pGarage->pCars[sortSourceSlot];
    GarageSlot *pTargetCar = &pGarage->pCars[targetSlot];
    const uint64_t sourceHouse = pGarage->getCarHouseHashcode( pSourceCar );
    const uint64_t targetHouse = pGarage->getCarHouseHashcode( pTargetCar );

    int32_t sourceIndex = -1;
    int32_t targetIndex = -1;
    if ( sourceHouse == GetDriver()->getCurrentHouse() ) {
        sourceIndex = pGarage->getHouseCarIndex( pSourceCar );
    }
    if ( targetHouse == GetDriver()->getCurrentHouse() ) {
        targetIndex = pGarage->getHouseCarIndex( pTargetCar );
    }

    carSpecs.detach();

    OTDU_UNIMPLEMENTED; // TODO: PLAYERDATA

    watchCar( carList.getSelectedSlot() );

    SceneCarPark &carPark = sceneGarage.getCarPark();
    if ( sourceHouse == GetDriver()->getCurrentHouse() ) {
        carPark.replaceCar( pSourceCar, sourceIndex );
    }
    if ( targetHouse == GetDriver()->getCurrentHouse() ) {
        carPark.replaceCar( pTargetCar, targetIndex );
    }

    const int32_t currentCar = GetDriver()->CurrentCar;
    if ( currentCar == sortSourceSlot ) {
        carPark.replaceCar( pSourceCar, 0 );
    } else if ( currentCar == carList.getSelectedSlot() ) {
        carPark.replaceCar( pTargetCar, 0 );
    }

    sortSourceSlot = -1;
    carList.reset();
}

bool GMGarage::onMessage( FlashMessage *pMessage ) {
    // FUN_00790e10
    Garage *pGarage = GetGarage();
    SceneCarPark &carPark = sceneGarage.getCarPark();

    switch ( *pMessage ) {
    case FlashMessage( "caou" ): // car interior out
        sceneGarage.leaveCar();
        return true;

    case FlashMessage( "cain" ): // car interior in
        sceneGarage.enterCar();
        return true;

    case FlashMessage( "bpof" ):
    case FlashMessage( "bpon" ):
        return true;

    case FlashMessage( "case" ): { // car picked in the list
        carPark.releaseCar( selectedCar );

        GarageSlot *pCar = &pGarage->pCars[carList.getSelectedSlot()];
        selectedCar = pGarage->getHouseCarIndex( pCar );

        if ( selectedCar == -1 ) {
            const int32_t slot = carList.getSelectedSlot();
            const uint64_t houseHashcode =
                pGarage->pCars[slot].getHouseHashcode();

            Driver *pDriver = GetDriver();
            pDriver->CurrentCar = uint16_t( slot );
            pDriver->notify( 0u );
            pDriver->selectCurrentHouse( houseHashcode );

            gpScenaric->setCurrentSpotIndex(
                gpScenaric->getSpotList().getSpotMapIndex( houseHashcode ) );
            requestExit();
            return true;
        }

        pFlashPlayer->setVariableValue( "/:num_car", selectedCar + 1 );
        innerViewDelay = kInnerViewDelayTicks;

        watchCar( pGarage->getSlotIndex(
            pGarage->getHouseCarAtIndex( selectedCar ) ) );
        sceneGarage.selectCarImmediate( selectedCar );

        if ( pGarage->getHouseCarAtIndex( selectedCar )->state ==
             CarDesc::eState::CDS_PutOnEbay ) {
            pFlashPlayer->setVariableValueNonLinked( "/:CarStatus", "red" );
            pFlashPlayer->showFrame( "red", "ss_menu/param/car_name", false );
            return true;
        }

        refreshCarStatus( selectedSlot == selectedCar );
        return true;
    }

    case FlashMessage( "daou" ):
        return true;

    case FlashMessage( "chda" ): // list selection changed
        watchCar( carList.getSelectedSlot() );
        return true;

    case FlashMessage( "ccar" ): { // change active car
        char numCar[32];
        if ( !pFlashPlayer->getVariableAsString( "/:num_car", numCar ) ) {
            return true;
        }

        if ( atoi( numCar ) - 1 == selectedCar ) {
            return true;
        }

        carPark.releaseCar( selectedCar );
        selectedCar = atoi( numCar ) - 1;
        innerViewDelay = kInnerViewDelayTicks;
        sceneGarage.selectCarImmediate( selectedCar );
        return true;
    }

    case FlashMessage( "chca" ): {
        watchCar( pGarage->getSlotIndex(
            pGarage->getHouseCarAtIndex( selectedCar ) ) );

        if ( pGarage->getHouseCarAtIndex( selectedCar )->state ==
             CarDesc::eState::CDS_PutOnEbay ) {
            pFlashPlayer->setVariableValueNonLinked( "/:CarStatus", "red" );
            pFlashPlayer->showFrame( "red", "ss_menu/param/car_name", false );
            return true;
        }

        refreshCarStatus( selectedSlot ==
                          pGarage->getSlotIndex(
                              pGarage->getHouseCarAtIndex( selectedCar ) ) );
        return true;
    }

    case FlashMessage( "data" ): {
        char numCar[32];
        if ( pFlashPlayer->getVariableAsString( "/:num_car", numCar ) ) {
            watchCar( atoi( numCar ) - 1 );
        }
        return true;
    }

    case FlashMessage( "liin" ): // back to the living room
        leave( EA_Living );
        return true;

    case FlashMessage( "gaou" ): // garage out
        leave( EA_Home );
        return true;

    case FlashMessage( "gofr" ): // go freeride
        return !leave( EA_FreeRide );

    case FlashMessage( "seou" ):
        return true;

    case FlashMessage( "seca" ): // select the car
        if ( !bExitRequested ) {
            selectCar( selectedCar );
        }
        return true;

    case FlashMessage( "shin" ): // car showcase in
        sceneGarage.enterShowcase();
        return true;

    case FlashMessage( "shou" ): // car showcase out
        sceneGarage.leaveShowcase();
        return true;

    case FlashMessage( "sort" ): {
        gbGarageSortRequested = true;

        const int32_t sortStep = pFlashPlayer->getVariableAsInt( "/:Sort" );
        if ( sortStep == 0 ) {
            sortSourceSlot = carList.getSelectedSlot();
        } else if ( sortStep == 1 ) {
            swapCars();
        }
        return true;
    }

    default:
        return false;
    }
}
