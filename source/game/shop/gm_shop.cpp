#include "shared.h"
#include "gm_shop.h"

#include "tdu_instance.h"

#include "core/format.h"
#include "core/time.h"
#include "debug/mng_debug_hud.h"
#include "database/gs_database.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "flash/mng_fade.h"
#include "game/game_cheats.h"
#include "game/mng_music.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/entity/entity_pos_cam.h"
#include "game/scenaric/entity/entity_shop_data.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "game/vehicle/vehicle_base.h"
#include "game/cinematic/cin_res_collection.h"
#include "game/scenaric/entity/entity_pnj.h"
#include "game/test_car/gm_test_car.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_car_reserve.h"
#include "flash/mng_flash_localize.h"
#include "player_data/achievements_data.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "world/mng_ocean.h"
#include "world/weather/gs_weather.h"

static constexpr int32_t kInteriorCameraMode = 7;
static constexpr int32_t kShowcaseCameraMode = 6;
static constexpr float kShowcaseCameraNearPlane = 0.18f;  // DAT_00f41ba0
static constexpr float kShowcaseCameraFarPlane = 5000.0f; // DAT_00f41b54
static constexpr uint32_t kDefaultRandomSeed = 12345u;
static constexpr float kCarFadeDuration = 0.5f; // FLOAT_00f41674
static constexpr int32_t kNumCheatCars = 8;
static constexpr const char *kSpecsPanelClip = "/buy/data";
static constexpr const char *kMenuParamClip = "/buy/param";
static constexpr const char *kMenuMessageClip = "/buy/message";
// Plays /:AfterValid once its "valid" animation is over
static constexpr const char *kMenuSelectionClip = "/buy/param/sur";
static constexpr float kValidateTimeout = 3.0f;
static constexpr int32_t kSpecsFirstPersonInactive = -2;
static constexpr uint32_t kMessageDB = 11;
static constexpr uint64_t kShopMessageHashcode = 0x525494;
static constexpr int32_t kBDDMenus = 0;
static constexpr int32_t kBDDAchievement =
    int32_t( AchievementsData::kAchievementDatabaseIndex );

static int32_t GetCarRewardType( const int32_t carID,
                                 uint64_t &outMustAwardHashcode ) {
    // FUN_00724840
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const DBEntryList &achievementList =
        gameDB.Databases[AchievementsData::kAchievementDatabaseIndex];
    const uint32_t numEntries =
        gameDB.pNumItems[AchievementsData::kAchievementDatabaseIndex];

    for ( uint32_t i = 0u; i < numEntries; i++ ) {
        const DBEntryAchievement &entry = gameDB.DBAchivement[i];
        if ( achievementList.getHashcodeForString( entry.Reward.pAsString ) !=
                 AchievementsData::kCarRewardHashcode ||
             entry.RewardData != carID ) {
            continue;
        }

        outMustAwardHashcode = achievementList.getHashcodeForString(
            entry.FailedExplanation.pAsString );

        const AchievementsData *pAchievements =
            gpPlayerData->getLoadedSave()->getAchievements();
        if ( pAchievements->isRewardLocked(
                 AchievementsData::kCarRewardHashcode, carID ) ) {
            return 1;
        }
        return pAchievements->isRewardCarBought( uint32_t( carID ) ) ? 2 : 0;
    }

    return 0;
}

static inline SpotMap *GetCurrentSpot() {
    return gpScenaric->getSpotList().getWorldSpotChecked(
        gpScenaric->getCurrentSpotIndex() );
}

static inline DBEntryCarShop *GetCurrentCarShop() {
    SpotMap *pSpot = GetCurrentSpot();
    return ( pSpot != nullptr )
               ? gpDatabase->getCarShopBySpotName( pSpot->Hashcode )
               : nullptr;
}

static inline bool IsCheatShop() {
    return gpMngDebugHUD != nullptr && gbInstallCheatCars;
}

static inline uint32_t NextRandom( uint32_t &seed ) {
    seed = seed * 0x7ff8a3edu + 0x2aa01d31u;
    return seed;
}

// /:IsCarInStock values for classic cars.
static constexpr uint32_t kCarNotInStock = 0u;
static constexpr uint32_t kCarReserved = 1u;
static constexpr uint32_t kCarInStock = 2u;
static constexpr int32_t kNumPriceVariations = 2;
static constexpr float kTestDriveDuration = 120.0f;   // DAT_00f41a18
static constexpr float kRandToUnit = 1.0f / 32767.0f; // DAT_00f41714

static uint32_t RandomIntInRange( const int32_t minValue,
                                  const int32_t maxValue ) {
    // FUN_0042e6f0
    return uint32_t( float( minValue ) + float( maxValue - minValue ) *
                                             float( rand() ) * kRandToUnit );
}

static const char *GetShopLabel() {
    // FUN_00724700
    SpotMap *pSpot = GetCurrentSpot();
    if ( pSpot == nullptr ) {
        return "car_dealer";
    }

    switch ( pSpot->Type ) {
    case ST_AfterMarket:
        return "after_mkt";
    case ST_Unknown8:
        switch ( pSpot->SubType ) {
        case 0:
            return "car_wash";
        case 1:
        case 9:
            return "car_paint";
        case 2:
        case 3:
            return "car_rental";
        default:
            return "";
        }
    case ST_CarDealer:
    case ST_BikeDealer:
        return "car_dealer";
    default:
        return "";
    }
}

static void SetShowcaseControlEnabled( const bool bEnabled ) {
    // FUN_0042e580
    if ( gpActiveGMScene == nullptr ) {
        return;
    }

    gpActiveGMScene->getShowcaseCarControl().setEnabled( bEnabled );

    CameraGame *pShowcaseCamera = gpCamera->findCameraWithMode(
        kShowcaseCameraMode, gpCamera->getBoundCarState() );
    pShowcaseCamera->setfield_0x26c( bEnabled );
}

GMShop::GMShop()
    : GameModeWorldBase(),
      carIDs(),
      carPositionGroups(),
      mngReflection(),
      mngLogoDisplay(),
      mngColorPalette(),
      sceneShop(),
      previousCarIndex( 0 ),
      selectedCarID( 0 ),
      bShopFlag1( true ),
      selectedCarDesc(),
      hoveredCarDesc(),
      carSpecs(),
      firstPerson(),
      parkedCarBodies(),
      specsHoveredCar( kSpecsFirstPersonInactive ),
      pSpecsRestoreCar( nullptr ),
      bSpecsShowHoveredCar( false ),
      pendingValidateTime( 0.0f ),
      pShopLogic( nullptr ),
      pBuyCinematic( nullptr ),
      bank(),
      randomSeed( kDefaultRandomSeed ),
      bCarShop( false ) {
    // FUN_007258f0
    for ( int32_t i = 0; i < ShopLogic::kNumCarSlots; i++ ) {
        carPositionGroups[i] = 1;
    }

    registerManager( &mngReflection );
    registerManager( &mngLogoDisplay );
    registerManager( &mngColorPalette );
}

GMShop::~GMShop() {
    // FUN_0042e620
}

void GMShop::createShopLogic() {
    // FUN_00724900
    SpotMap *pSpot = gpScenaric->getSpotList().getWorldSpotChecked(
        gpScenaric->getCurrentSpotIndex() );
    if ( pSpot == nullptr ) {
        pShopLogic = new ShopLogicDealer();
        return;
    }

    switch ( pSpot->Type ) {
    case ST_CarDealer:
    case ST_BikeDealer:
        pShopLogic = new ShopLogicDealer();
        bCarShop = true;
        break;
    case ST_AfterMarket:
        pShopLogic = new ShopLogicTuning();
        bCarShop = true;
        break;
    case ST_Unknown8:
        switch ( pSpot->SubType ) {
        case 1:
        case 9:
            pShopLogic = new ShopLogicPaint();
            bCarShop = false;
            break;
        case 2:
        case 3:
            pShopLogic = new ShopLogicRental();
            bCarShop = false;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

void GMShop::initialize() {
    // FUN_00726d60
    GameModeWorldBase::initialize();

    if ( gpMngOcean != nullptr ) {
        gbOceanEnabled = true;
    }

    mngWorld.setDisableStreamingOnCar( true );

    pShopLogic = nullptr;
    createShopLogic();
    OTDU_ASSERT( pShopLogic != nullptr );

    pShopLogic->setOwner( this );
    pShopLogic->fillCarList( carIDs );

    const eGameMode activeGM = gpTestDriveInstance->getActiveGameMode();
    if ( activeGM > GM_Invalid && activeGM < GM_Count ) {
        gpWeather->setCurrentWeatherDesc(
            gpWeather->getWeatherDescName( activeGM ) );
    }

    sceneShop.setOwner( this );
    sceneShop.loadResources( pShopLogic->getType() == ShopLogic::SLT_Rental );
    sceneShop.getContent()->setModeActive( true );
    sceneShop.getCarPark().setEnabled( true );
    sceneShop.setupCamera();

    mngShadow.setCastersEnabled( true );
    mngShadow.setReceiversEnabled( true );
    mngShadow.setDisableVehicleShadows( true );

    VehicleBase::bApplyWheelAttenuation = false;
    gpMngMusic->setPlayFlags( 1 );

    randomSeed = static_cast<uint32_t>( MonotonicToMs( GetTimeMonotonic() ) );
}

void GMShop::reset() {
    // FUN_00724f90
    GameModeScene::reset();

    OTDU_UNIMPLEMENTED; // TODO: ONLINE (lobby network listener)

    SpotMap *pSpot = gpScenaric->getSpotList().getWorldSpotChecked(
        gpScenaric->getCurrentSpotIndex() );

    FlashPlayer *pFlashPlayer = gpFlash->getFlashPlayer( "SHOPS" );
    if ( pFlashPlayer != nullptr ) {
        pFlashPlayer->setVariableValueNonLinked( "/:Title", "Error" );

        DBEntryCarShop *pCarShop =
            ( pSpot != nullptr )
                ? gpDatabase->getCarShopBySpotName( pSpot->Hashcode )
                : nullptr;
        if ( pCarShop != nullptr ) {
            bShopFlag1 = ( ( pCarShop->Flags >> 1 ) & 1 ) != 0;
            pFlashPlayer->setVariableValue( "/:Title",
                                            pCarShop->Label.pAsString );
        }
    }

    CameraGame *pShowcaseCamera = gpCamera->findCameraWithMode(
        kShowcaseCameraMode, gpCamera->getBoundCarState() );
    pShowcaseCamera->setNearPlane( kShowcaseCameraNearPlane );
    pShowcaseCamera->setFarPlane( kShowcaseCameraFarPlane );
}

void GMShop::tick( float deltaTime, float totalTime ) {
    // FUN_00726a90
    if ( !pShopLogic->isReady( carIDs ) ) {
        return;
    }

    GameModeScene::tick( deltaTime, totalTime );

    sceneShop.tick( deltaTime );

    SampleFrameRate();

    if ( sceneShop.getContent()->getState() == SceneInterior::SIS_Ready ) {
        onSceneReady();
    }

    pShopLogic->tick( deltaTime, totalTime );

    GatherParkedCarBodies( sceneShop.getCarPark(), parkedCarBodies );
    firstPerson.setExtraCollisionBodies( parkedCarBodies );
    firstPerson.updateHoveredCar( sceneShop.getCarPark() );
    firstPerson.tick( sceneShop.getContent(), pCinematicLoop );

    if ( firstPerson.getClickedCar() != -1 ) {
        selectFirstPersonCar( firstPerson.getClickedCar() );
    }
    updateFirstPersonSpecs();

    if ( pendingValidateTime > 0.0f ) {
        pendingValidateTime -= deltaTime;
        validateFirstPersonCar();
    }
}

void GMShop::selectFirstPersonCar( const int32_t slot ) {
    FlashPlayer *pFlashPlayer = gpFlash->getFlashPlayer( "SHOPS" );
    if ( pFlashPlayer == nullptr ) {
        return;
    }

    int32_t carIndex = -1;
    for ( int32_t i = 0; i < getNumShopCars(); i++ ) {
        if ( getShopCarSlot( i ) == slot ) {
            carIndex = i;
            break;
        }
    }
    if ( carIndex == -1 ) {
        return;
    }

    // The buy screen is part of the regular menu
    firstPerson.leave();
    updateFirstPersonSpecs();

    // Browse to the car (as the menu arrows do)
    pFlashPlayer->setVariableValue( "/:num_car", carIndex + 1 );
    FlashMessage browseMessage( "chca" );
    onMessage( &browseMessage );

    // Then validate it once the menu accepts input again
    pendingValidateTime = kValidateTimeout;
}

void GMShop::validateFirstPersonCar() {
    // Same as the menu select button
    FlashPlayer *pFlashPlayer = gpFlash->getFlashPlayer( "SHOPS" );
    if ( pFlashPlayer == nullptr ||
         pFlashPlayer->getVariableAsInt( "/:lockChca" ) != 0 ) {
        return;
    }

    const int32_t rewardType =
        pFlashPlayer->getVariableAsInt( "/:reward_type" );
    if ( rewardType == 0 ) {
        if ( pFlashPlayer->getVariableAsInt( "/:lockInnerView" ) != 0 ) {
            return;
        }

        pendingValidateTime = 0.0f;
        pFlashPlayer->setVariableValueNonLinked( "/:AfterValid", "car_action" );
        FlashClip selection;
        if ( pFlashPlayer->getClipPlayer( kMenuSelectionClip, &selection ) ) {
            selection.goToLabelAndPlay( "valid", true );
        }
        return;
    }

    pendingValidateTime = 0.0f;

    FlashClip param;
    FlashClip message;
    if ( ( rewardType == 1 || rewardType == 2 ) &&
         pFlashPlayer->getClipPlayer( kMenuParamClip, &param ) &&
         pFlashPlayer->getClipPlayer( kMenuMessageClip, &message ) ) {
        param.goToLabelAndPlay( "wait", false );
        message.goToLabelAndPlay( rewardType == 1 ? "award" : "bought", true );
    }
}

void GMShop::updateFirstPersonSpecs() {
    // In first person mode the specs panel only shows the hovered car
    const int32_t hoveredCar = firstPerson.isActive()
                                   ? firstPerson.getHoveredCar()
                                   : kSpecsFirstPersonInactive;
    if ( hoveredCar == specsHoveredCar ) {
        return;
    }
    specsHoveredCar = hoveredCar;

    if ( hoveredCar >= 0 ) {
        if ( !bSpecsShowHoveredCar ) {
            pSpecsRestoreCar = carSpecs.getCar();
            bSpecsShowHoveredCar = true;
        }

        const SceneCarPark::Entry &entry =
            sceneShop.getCarPark().getEntry( hoveredCar );
        hoveredCarDesc.clear();
        hoveredCarDesc.setup( uint32_t( entry.ConfigID ), entry.PaintIndex,
                              entry.RimID, entry.InteriorID, 0u );
        carSpecs.watch( &hoveredCarDesc );
        ApplyCarSpecsGauges( gpFlash->getFlashPlayer( "SHOPS" ),
                             kSpecsPanelClip );
    } else if ( bSpecsShowHoveredCar ) {
        carSpecs.watch( pSpecsRestoreCar );
        ApplyCarSpecsGauges( gpFlash->getFlashPlayer( "SHOPS" ),
                             kSpecsPanelClip );
        pSpecsRestoreCar = nullptr;
        bSpecsShowHoveredCar = false;
    }

    if ( hoveredCar >= 0 ) {
        firstPerson.showMoviePanel( "SHOPS", kSpecsPanelClip );
    } else {
        firstPerson.hideMoviePanel();
    }
}

void GMShop::draw( float deltaTime, float totalTime ) {
    // FUN_00724b00
    sceneShop.draw();
}

void GMShop::pause() {
    // FUN_00724e40
    firstPerson.leave();

    GameModeScene::pause();

    pShopLogic->pause();

    OTDU_UNIMPLEMENTED; // TODO: ONLINE (lobby network listener)
}

void GMShop::destroy() {
    // FUN_007250f0
    firstPerson.leave();

    GameModeWorldBase::destroy();

    mngWorld.setDisableStreamingOnCar( false );

    delete pShopLogic;
    pShopLogic = nullptr;

    // FUN_008421f0 (Havok body refresh; NOP for Jolt)

    VehicleBase::bApplyWheelAttenuation = true;

    mngShadow.setReceiversEnabled( false );

    sceneShop.suspend();
    sceneShop.getContent()->setModeActive( false );
    sceneShop.getCarPark().setEnabled( false );
    sceneShop.terminate();

    carSpecs.detach();

    gpWeather->setCurrentWeatherDesc(
        gpWeather->getWeatherDescName( GM_BootMenu ) );

    const ScenaricBank::eBankState bankState = bank.getState();
    if ( bankState != ScenaricBank::BBS_Unused &&
         bankState != ScenaricBank::BBS_LoadFailed ) {
        gpScenaric->unloadBank( &bank );
    }
}

bool GMShop::onMessage( FlashMessage *pMessage ) {
    // FUN_00726ba0
    FlashPlayer *pFlashPlayer = gpFlash->getFlashPlayer( "SHOPS" );
    if ( pShopLogic->onMessage( pMessage, pFlashPlayer ) ) {
        return true;
    }

    switch ( *pMessage ) {
    case FlashMessage( "quin" ):
        SetShowcaseControlEnabled( false );
        return true;

    case FlashMessage( "quou" ):
        SetShowcaseControlEnabled( true );
        return true;

    case FlashMessage( "caou" ): // car interior out
        sceneShop.leaveCar();
        return true;

    case FlashMessage( "cain" ): // car interior in
        sceneShop.enterCar();
        return true;

    case FlashMessage( "bpof" ):
    case FlashMessage( "bpon" ):
        return true;

    case FlashMessage( "chca" ): { // change car
        char value[32];
        if ( pFlashPlayer != nullptr &&
             pFlashPlayer->getVariableAsString( "/:num_car", value ) ) {
            selectCar( atoi( value ) - 1 );
        }
        return true;
    }

    case FlashMessage( "gofr" ): // go freeride
        // FUN_0042e5d0
        sceneShop.getCarPark().releaseCar( previousCarIndex );
        return false;

    case FlashMessage( "shin" ): // showcase in
        sceneShop.enterShowcase();
        return true;

    case FlashMessage( "shou" ): // showcase out
        sceneShop.leaveShowcase();
        return true;

    case FlashMessage( "rdmo" ):
        return true;

    case FlashMessage( "tstk" ): // test drive
        startTestDrive();
        return true;

    default:
        return false;
    }
}

bool GMShop::executeAction( int32_t param_2, void *param_3, void *pData ) {
    // FUN_00725aa0
    EntityCinematic *pCine = static_cast<EntityCinematic *>( param_3 );

    pShopLogic->onCinematicEvent( param_2, pCine, pData );

    if ( GameModeScene::executeAction( param_2, param_3, pData ) ) {
        return true;
    }

    switch ( param_2 ) {
    case CE_Requested:
    case CE_Started:
        return true;

    case CE_Ended: {
        if ( pCine == pBuyCinematic || pCinematicIntroI != nullptr ) {
            return true;
        }

        if ( pCinematicLoop == nullptr &&
             pShopLogic->getType() != ShopLogic::SLT_Paint ) {
            gpCamera->setCameraMode( kInteriorCameraMode, 0, 1.0f );
        }

        if ( gpScenaric->getIntroTutorial().isModalHintVisible() ) {
            bScenarioTutorialMustReactivateFlash = true;
            return true;
        }

        MoviePlayer *pHud = gpFlash->getMoviePlayer( 1, 0 );
        if ( pHud != nullptr ) {
            pHud->IsMoviePaused &= ~2;
        }
        return true;
    }

    case CE_StartEndFade:
        if ( pCine == pBuyCinematic ) {
            requestExit();
            gpTestDriveInstance->setNextGameMode( GM_FreeRide );
        }
        return true;

    default:
        return false;
    }
}

int32_t GMShop::getNumShopCars() const {
    // FUN_00724c20
    if ( IsCheatShop() ) {
        return kNumCheatCars;
    }

    if ( GetCurrentSpot() == nullptr ) {
        return 0;
    }

    DBEntryCarShop *pCarShop = GetCurrentCarShop();
    if ( pCarShop == nullptr ) {
        return 0;
    }

    int32_t numCars = 0;
    for ( int32_t i = 0; i < DBEntryCarShop::kNumSlots; i++ ) {
        if ( gpDatabase->isCarShopSlotAvailable( pCarShop, i ) &&
             sceneShop.getCarPark().isEntryUsed( i ) ) {
            numCars++;
        }
    }
    return numCars;
}

int32_t GMShop::getShopCarSlot( int32_t carIndex ) const {
    // FUN_00724b80
    if ( IsCheatShop() ) {
        return carIndex;
    }

    DBEntryCarShop *pCarShop = GetCurrentCarShop();
    if ( pCarShop == nullptr ) {
        return 0;
    }

    for ( int32_t i = 0; i < DBEntryCarShop::kNumSlots; i++ ) {
        if ( !gpDatabase->isCarShopSlotAvailable( pCarShop, i ) ||
             !sceneShop.getCarPark().isEntryUsed( i ) ) {
            continue;
        }

        if ( carIndex == 0 ) {
            return i;
        }
        carIndex--;
    }
    return 0;
}

bool GMShop::isShopSlotAvailable( const int32_t slot ) const {
    // FUN_00724b10
    if ( IsCheatShop() ) {
        return slot < kNumCheatCars;
    }

    return gpDatabase->isCarShopSlotAvailable( GetCurrentCarShop(), slot );
}

void GMShop::startCarCameraPath( const int32_t slot ) {
    SceneInterior *pContent = sceneShop.getContent();

    Path *pPath = pContent->getPositionPath( slot );
    if ( pPath != nullptr ) {
        pContent->startCameraPath( pPath, false, true );
        return;
    }

    const int32_t lastSlot = sceneShop.getCarPark().getLastAvailableShopSlot();
    const int32_t nextSlot = ( slot + 1 > lastSlot ) ? 0 : slot + 1;
    pPath = pContent->getPositionPath( nextSlot );
    if ( pPath != nullptr ) {
        pContent->startCameraPath( pPath, true, true );
    }
}

void GMShop::onSceneReady() {
    // FUN_00726630
    SceneInterior *pContent = sceneShop.getContent();
    EntityShopData *pShop = pContent->getShopData();
    const int32_t numPositions =
        ( pShop != nullptr ) ? pShop->getNumPositions() : 0;

    for ( int32_t i = 0; i < ShopLogic::kNumCarSlots; i++ ) {
        EntityPosCam *pPosition =
            ( i < numPositions )
                ? static_cast<EntityPosCam *>( pShop->getPosition( i ) )
                : nullptr;
        carPositionGroups[i] =
            ( pPosition != nullptr ) ? pPosition->getMode() : 1;
    }

    installCinematics( pContent->getSourceSpot(), &pContent->getBank() );

    ScenaricBank &interiorBank = pContent->getBank();
    pBuyCinematic = static_cast<EntityCinematic *>(
        interiorBank.findEntity( eEntityType::ET_CINE, "C_BUY_CAR_I" ) );
    if ( pBuyCinematic == nullptr ) {
        pBuyCinematic = static_cast<EntityCinematic *>(
            interiorBank.findEntity( eEntityType::ET_CINE, "C_BUY_PART" ) );
    }
    if ( pBuyCinematic != nullptr ) {
        pBuyCinematic->setStopWhenSkipped( false );
    }

    if ( pCinematicIntroI != nullptr ) {
        gpCamera->setCameraMode( 10, 0, 1.0f );
    } else {
        Path *pCameraPath =
            ( pShop != nullptr ) ? pShop->getCameraPath() : nullptr;
        if ( pCameraPath != nullptr ) {
            pContent->startCameraPath( pCameraPath, false, false );
            gpCamera->setCameraMode( kInteriorCameraMode, 0, 1.0f );
        } else {
            pContent->focusCamera( 0 );
            sceneShop.setSelectedCar( 0 );
        }
    }

    previousCarIndex = getShopCarSlot( 0 );

    FlashPlayer *pFlashPlayer = gpFlash->getFlashPlayer( "SHOPS" );
    if ( pFlashPlayer != nullptr ) {
        carSpecs.pMovie = pFlashPlayer;

        pFlashPlayer->showFrame( GetShopLabel(), "/", true );

        pShopLogic->setFlashPlayer( pFlashPlayer );
        pShopLogic->onFlashReady();

        pFlashPlayer->setVariableValueNonLinked( "/:num_car", "1" );
        pFlashPlayer->setVariableValue( "/:nb_cars", getNumShopCars() );

        const char *pMessage =
            gpDatabase->getStringByHashcode( kMessageDB, kShopMessageHashcode );
        pFlashPlayer->setVariableValueNonLinked(
            "/:Message", ( pMessage != nullptr ) ? pMessage : "" );
    }

    populateCarPark();
    selectCar( 0 );

    if ( bCarShop ) {
        pContent->resume();
    }
}

void GMShop::populateCarPark() {
    // FUN_00725cb0
    SceneCarPark &carPark = sceneShop.getCarPark();

    const int32_t numUsedEntries = carPark.getNumUsedEntries();
    for ( int32_t i = 0; i < numUsedEntries; i++ ) {
        const int32_t slot = carPark.getUsedEntryIndex( i );
        const int32_t carID = carIDs[ClampSlot( slot )];

        // Cars in the classic stock keep the configuration rolled by the stock.
        if ( pShopLogic->usesClassicCarStock() &&
             gpPlayerData->getLoadedSave()->getStock()->isAvailable(
                 uint32_t( carID ) ) ) {
            continue;
        }

        int32_t paintIndex = 0;
        if ( isShopSlotAvailable( slot ) ) {
            carPark.setPaint( slot, 0 );
        } else if ( carID != -1 ) {
            const int32_t numColors = gpDatabase->getNumColors( carID );
            if ( numColors > 1 ) {
                paintIndex = int32_t( NextRandom( randomSeed ) %
                                      uint32_t( numColors - 1 ) ) +
                             1;
                carPark.setPaint( slot, paintIndex );
            }
        }

        DBEntryCarPhysicsData *pCarEntry = gpDatabase->getCarDBEntry( carID );
        const int32_t numRims = gpDatabase->getNumRims( carID );

        int32_t rimID = 0;
        if ( isShopSlotAvailable( slot ) ) {
            const DBEntryRim *pDefaultRim =
                ( pCarEntry != nullptr )
                    ? static_cast<const DBEntryRim *>(
                          pCarEntry->DefaultRims.pAsRawPointer )
                    : nullptr;
            for ( int32_t rim = 0; rim < numRims; rim++ ) {
                const DBEntryRim *pRim = gpDatabase->getRim( pCarEntry, rim );
                if ( pRim != nullptr && pDefaultRim != nullptr &&
                     strcmp( pRim->RimName.pAsString,
                             pDefaultRim->RimName.pAsString ) == 0 ) {
                    rimID = rim;
                    break;
                }
            }
        } else if ( numRims > 0 ) {
            rimID = int32_t( NextRandom( randomSeed ) % uint32_t( numRims ) );
        }
        carPark.setRim( slot, rimID );

        if ( slot < 0 ) {
            continue;
        }

        SceneCarPark::Entry &entry = carPark.getEntry( slot );
        if ( isShopSlotAvailable( slot ) ) {
            entry.InteriorID = 0;
            continue;
        }

        const int32_t numInteriors = gpDatabase->getNumInteriors(
            gpDatabase->getColorByIndex( pCarEntry, paintIndex ) );
        if ( numInteriors > 0 ) {
            entry.InteriorID =
                int32_t( NextRandom( randomSeed ) % uint32_t( numInteriors ) );
        }
    }

    pShopLogic->onCarParkPopulated();

    if ( !carPark.isLoaded() ) {
        if ( carPark.getNumEntries() != 0 ) {
            carPark.streamAround( 0 );
            carPark.loadBank();
        }
        carPark.setLoaded( true );
    }
}

void GMShop::selectCar( const int32_t carIndex ) {
    // FUN_00726110
    FlashPlayer *pFlashPlayer = gpFlash->getFlashPlayer( "SHOPS" );
    if ( pFlashPlayer == nullptr || carIndex < 0 ) {
        return;
    }

    if ( carIndex >= getNumShopCars() ) {
        if ( carIndex != 0 ) {
            return;
        }

        char price[32];
        FormatMoneyString( price, 0 );
        pFlashPlayer->setVariableValueNonLinked( "/:current_car",
                                                 "No car in shop..." );
        pFlashPlayer->setVariableValueNonLinked( "/:current_price", price );
        pFlashPlayer->setVariableValueNonLinked( "/:TotalPrice", price );
        return;
    }

    const int32_t slot = getShopCarSlot( carIndex );
    if ( pShopLogic->usesClassicCarStock() ) {
        const uint32_t carID = uint32_t( carIDs[ClampSlot( slot )] );
        SavedCarReserve *pStock = gpPlayerData->getLoadedSave()->getStock();

        uint32_t stockState = kCarNotInStock;
        if ( pStock->isAvailable( carID ) ) {
            stockState = kCarInStock;
        } else if ( pStock->isReserved( carID ) ) {
            stockState = kCarReserved;
        }
        pFlashPlayer->setVariableValue( "/:IsCarInStock", stockState );
        pFlashPlayer->setVariableValue(
            "/:Price_var", RandomIntInRange( 0, kNumPriceVariations ) );
    }

    SceneCarPark &carPark = sceneShop.getCarPark();
    if ( sceneShop.isCameraMoving() ) {
        sceneShop.focusCar( slot );
        startCarCameraPath( slot );
        carPark.streamAround( slot );

        sceneShop.setLureTimer( 0.0f );
        sceneShop.setSkipCineTimer( kCarFadeDuration, pFlashPlayer );
    } else if ( slot != previousCarIndex ) {
        if ( carPositionGroups[ClampSlot( slot )] ==
             carPositionGroups[ClampSlot( previousCarIndex )] ) {
            sceneShop.moveToCar( previousCarIndex, slot );
        } else {
            pFlashPlayer->setVariableValue( "/:lockChca", 1 );
            gpMngFade->startTransition(
                ( gpMngFade->getTransitionFlags() & ~0xcu ) | 1u,
                kCarFadeDuration, &GMShop::OnCarFadeOut,
                reinterpret_cast<void *>( intptr_t( slot ) ), this );
        }
    } else {
        sceneShop.focusCar( slot );
        startCarCameraPath( slot );
        carPark.streamAround( slot );
    }

    carPark.releaseCar( previousCarIndex );
    previousCarIndex = slot;
    selectedCarID = carIDs[ClampSlot( slot )];
    carPark.openInnerView( slot );

    const SceneCarPark::Entry &entry = carPark.getEntry( slot );
    selectedCarDesc.clear();
    selectedCarDesc.setup( uint32_t( selectedCarID ), entry.PaintIndex,
                           entry.RimID, entry.InteriorID, 0u );
    carSpecs.watch( &selectedCarDesc );

    if ( !bShopFlag1 ) {
        DBEntryCarPhysicsData *pCarEntry =
            gpDatabase->getCarDBEntry( selectedCarID );
        if ( pCarEntry != nullptr && pCarEntry->Brand != nullptr ) {
            pFlashPlayer->setVariableValue(
                "/:Title", pCarEntry->Brand->BrandID.pAsString );
        }
    }

    updatePrices();
    pShopLogic->onCarSelected( carIndex );

    uint64_t mustAwardHashcode = 0ull;
    const int32_t rewardType =
        GetCarRewardType( selectedCarID, mustAwardHashcode );
    pFlashPlayer->setVariableValue( "/:reward_type", rewardType );
    if ( gpMngFlashLocalize != nullptr && rewardType == 1 ) {
        gpMngFlashLocalize->setDatabaseContext( kBDDAchievement );
        gpMngFlashLocalize->setVarLocalized( pFlashPlayer, "/:MustAwardText",
                                             mustAwardHashcode );
        gpMngFlashLocalize->setDatabaseContext( kBDDMenus );
    }
}

void GMShop::updatePrices() {
    // FUN_00724cf0
    FlashPlayer *pFlashPlayer = gpFlash->getFlashPlayer( "SHOPS" );
    if ( pFlashPlayer == nullptr ) {
        return;
    }

    int32_t paintIndex = -1;
    int32_t interiorIndex = -1;
    switch ( pShopLogic->getBuyOption() ) {
    case 1:
    case 2:
        break;
    case 3:
        paintIndex = selectedCarDesc.paintIndex;
        break;
    default:
        paintIndex = selectedCarDesc.paintIndex;
        interiorIndex = selectedCarDesc.interiorID;
        break;
    }

    char priceText[32];
    const int32_t totalPrice = int32_t( gpDatabase->getCarPrice(
        selectedCarID, paintIndex, selectedCarDesc.rimID, interiorIndex ) );
    if ( totalPrice < 1 ) {
        strcpy( priceText, "--" );
    } else {
        FormatMoneyString( priceText, totalPrice );
    }
    pFlashPlayer->setVariableValueNonLinked( "/:TotalPrice", priceText );

    const int32_t basePrice =
        int32_t( gpDatabase->getCarPrice( selectedCarID, -1, -1, -1 ) );
    if ( basePrice < 1 ) {
        strcpy( priceText, "--" );
    } else {
        FormatMoneyString( priceText, basePrice );
    }
    pFlashPlayer->setVariableValueNonLinked( "/:current_price", priceText );
}

static inline int32_t ReadOptionItem( FlashPlayer *pFlashPlayer,
                                      const int32_t maxItem ) {
    const int32_t item = pFlashPlayer->getVariableAsInt( "/:num_item" ) - 1;
    if ( item < 0 ) {
        return 0;
    }
    return ( item > maxItem ) ? maxItem : item;
}

static inline void SetOptionPrice( FlashPlayer *pFlashPlayer,
                                   const char *pOptionPriceVar,
                                   const int32_t price ) {
    char priceText[32];
    FormatMoneyString( priceText, price );
    pFlashPlayer->setVariableValueNonLinked( pOptionPriceVar, priceText );
    pFlashPlayer->setVariableValueNonLinked( "/:cur_op_price", priceText );
}

void GMShop::commitRims() {
    // FUN_00725610
    FlashPlayer *pFlashPlayer = gpFlash->getFlashPlayer( "SHOPS" );
    if ( pFlashPlayer == nullptr ) {
        return;
    }

    const int32_t rimID =
        ReadOptionItem( pFlashPlayer, gpDatabase->getNumRims( selectedCarID ) );
    if ( rimID != selectedCarDesc.rimID ) {
        selectedCarDesc.rimID = rimID;
        selectedCarDesc.notify( 0u );
    }

    DBEntryRim *pRim = gpDatabase->getRim( selectedCarID, rimID );
    if ( pRim != nullptr ) {
        pFlashPlayer->setVariableValue( "/:Option_name",
                                        pRim->FriendlyName.pAsString );
        pFlashPlayer->setVariableValue( "/:wheels",
                                        pRim->FriendlyName.pAsString );
        SetOptionPrice( pFlashPlayer, "/buy/somme/op_price_1/:op_price",
                        pRim->Price );
    }

    sceneShop.getCarPark().setRim( previousCarIndex, rimID );
    updatePrices();
}

void GMShop::commitPaint() {
    // FUN_007252d0
    FlashPlayer *pFlashPlayer = gpFlash->getFlashPlayer( "SHOPS" );
    if ( pFlashPlayer == nullptr ) {
        return;
    }

    const int32_t paintIndex = ReadOptionItem(
        pFlashPlayer, gpDatabase->getNumColors( selectedCarID ) );
    if ( paintIndex != selectedCarDesc.paintIndex ||
         selectedCarDesc.paintType != ePaintType::PT_Default ) {
        selectedCarDesc.paintType = ePaintType::PT_Default;
        selectedCarDesc.paintIndex = paintIndex;
        selectedCarDesc.notify( 0u );
    }

    SceneCarPark &carPark = sceneShop.getCarPark();
    SceneCarPark::Entry &entry = carPark.getEntry( previousCarIndex );

    VehicleBase *pVehicle = carPark.getVehicle( previousCarIndex );
    if ( pVehicle != nullptr ) {
        pVehicle->applyPaint( uint32_t( selectedCarID ), ePaintType::PT_Default,
                              paintIndex, entry.RimID, entry.InteriorID,
                              nullptr, nullptr, nullptr, nullptr );
    }

    DBEntryCarColor *pColor =
        gpDatabase->getColorByIndex( selectedCarID, paintIndex );
    if ( pColor != nullptr && pColor->ColorName.pAsString != nullptr ) {
        pFlashPlayer->setVariableValue( "/:Option_name",
                                        pColor->ColorName.pAsString );
        pFlashPlayer->setVariableValue( "/:paint",
                                        pColor->ColorName.pAsString );
        SetOptionPrice( pFlashPlayer, "/buy/somme/op_price_2/:op_price",
                        pColor->Price );
    }

    if ( entry.PaintIndex != paintIndex ) {
        carPark.setPaint( previousCarIndex, paintIndex );
        if ( previousCarIndex >= 0 ) {
            entry.InteriorID = 0;
            carPark.applyPaint( previousCarIndex );
        }
    }

    updatePrices();
}

void GMShop::commitInterior() {
    // FUN_00725750
    FlashPlayer *pFlashPlayer = gpFlash->getFlashPlayer( "SHOPS" );
    if ( pFlashPlayer == nullptr ) {
        return;
    }

    SceneCarPark &carPark = sceneShop.getCarPark();
    DBEntryCarColor *pColor = gpDatabase->getColorByIndex(
        selectedCarID, carPark.getEntry( previousCarIndex ).PaintIndex );

    const int32_t interiorIndex =
        ReadOptionItem( pFlashPlayer, gpDatabase->getNumInteriors( pColor ) );
    if ( interiorIndex != selectedCarDesc.interiorID ) {
        selectedCarDesc.interiorID = interiorIndex;
        selectedCarDesc.notify( 0u );
    }

    if ( previousCarIndex >= 0 ) {
        carPark.getEntry( previousCarIndex ).InteriorID = interiorIndex;
        carPark.applyPaint( previousCarIndex );
    }

    DBEntryInterior *pInterior =
        gpDatabase->getInteriorByIndex( pColor, interiorIndex );
    if ( pInterior != nullptr ) {
        pFlashPlayer->setVariableValueNonLinked( "/:Option_name", " " );
        pFlashPlayer->setVariableValue( "/:material",
                                        pInterior->InteriorName.pAsString );
        SetOptionPrice( pFlashPlayer, "/buy/somme/op_price_3/:op_price",
                        pInterior->Price );
    }

    updatePrices();
}

void GMShop::OnCarFadeOut( void *pData, void *pSource ) {
    // FUN_00725bd0
    GMShop *pShop = static_cast<GMShop *>( pData );
    if ( pShop != nullptr ) {
        const int32_t slot =
            static_cast<int32_t>( reinterpret_cast<intptr_t>( pSource ) );
        pShop->sceneShop.focusCar( slot );
        pShop->startCarCameraPath( slot );
        pShop->sceneShop.getCarPark().streamAround( slot );
    }

    gpMngFade->startTransition( ( gpMngFade->getTransitionFlags() & ~8u ) | 5u,
                                kCarFadeDuration, &GMShop::OnCarFadeIn );
}

void GMShop::OnCarFadeIn( void *pData, void *pSource ) {
    // FUN_00724a90
    FlashPlayer *pFlashPlayer = gpFlash->getFlashPlayer( "SHOPS" );
    if ( pFlashPlayer != nullptr ) {
        pFlashPlayer->setVariableValue( "/:lockChca", 0 );
    }
}

void GMShop::startTestDrive() {
    // FUN_00724e60
    SceneCarPark &carPark = sceneShop.getCarPark();
    const SceneCarPark::Entry &entry = carPark.getEntry( previousCarIndex );
    GMTestCar::SetTestCar( selectedCarID, entry.PaintIndex, entry.RimID,
                           entry.InteriorID );
    GMTestCar::SetTestDuration( kTestDriveDuration );

    EntityPnj *pHostess =
        static_cast<EntityPnj *>( sceneShop.getContent()->getBank().findEntity(
            eEntityType::ET_CPNJ, "Hostess" ) );
    ReadAvatarConfigFromPNJ( GMTestCar::GetPassengerConfiguration(),
                             ( pHostess != nullptr ) ? pHostess->getPnjName()
                                                     : "shop_lamborghini" );

    carPark.releaseCar( previousCarIndex );

    requestExit();
    gpTestDriveInstance->setNextGameMode( GM_TestCar );
}
