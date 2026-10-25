#include "shared.h"
#include "game_intro_tutorial.h"

#include "ai/ai_road_query.h"
#include "config/gs_config.h"
#include "core/maths_helper.h"
#include "game/game_cheats.h"
#include "game/gm_base.h"
#include "game/gs_tutorial.h"
#include "game/map/gm_map.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/spots.h"
#include "flash/implementation/flash_player.h"
#include "flash/modal_message_box.h"
#include "database/gs_database.h"
#include "game/gm_scene.h"
#include "game/gm_world.h"
#include "physics/gs_physics.h"
#include "system/gs_pc_options.h"
#include "player_data/achievements_data.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_save.h"
#include "player_data/saved_road_discovery.h"
#include "player_data/saved_garage.h"
#include "player_data/player_data_interface.h"
#include "world/gs_world.h"

#include "tdu_instance.h"

static const Eigen::Vector4f kTutorialDefaultMapFocus( 48389.043f,
                                                       20.90166f,
                                                       48200.7f,
                                                       1.0f ); // DAT_00f421cc

GameIntroTutorial::GameIntroTutorial()
    : tutorialStep( 0 ),
      pCarRental( nullptr ),
      pRealEstate( nullptr ),
      pSpotECD12E( nullptr ),
      pSpotECD8F( nullptr ),
      pSpotECD12B( nullptr ),
      pSpotIntroRace( nullptr ),
      pHome( nullptr ),
      carDealer( 0 ),
      carIDRental( 0 ),
      carRentalTime( 0 ),
      landingCinematicBank(),
      bModalHintVisible( false ),
      bMapForIntro( true ),
      bMustTeleport( false ),
      bInstallingCheatCars( false ) {}

GameIntroTutorial::~GameIntroTutorial() {}

void GameIntroTutorial::initialize( Spots &spotList ) {
    // FUN_007fbbe0
    resolveSpots( spotList );

    bModalHintVisible = false;
    tutorialStep = 0;
    bMapForIntro = true;
}

void GameIntroTutorial::resolveSpots( Spots &spotList ) {
    pRealEstate = spotList.getSpotMap( GetIdentifier64bit( "ERE_A_6627" ) );
    pCarRental = spotList.getSpotMap( GetIdentifier64bit( "ECR_A_6703" ) );
    pSpotECD12E = spotList.getSpotMap( GetIdentifier64bit( "ECD_12E_7757" ) );
    pSpotECD8F = spotList.getSpotMap( GetIdentifier64bit( "ECD_8F_7756" ) );
    pSpotECD12B = spotList.getSpotMap( GetIdentifier64bit( "ECD_12B_7777" ) );
    pSpotIntroRace = spotList.getSpotMap( GetIdentifier64bit( "RSFE0028" ) );
}

bool GameIntroTutorial::isActive() const {
    // FUN_00406310
    return tutorialStep != 0xd && tutorialStep != 0x0;
}

bool GameIntroTutorial::hidesSpot( const SpotMap *pSpot ) const {
    // FUN_007fb9c0
    switch ( tutorialStep ) {
    case kTutorialStateFirstDrive:
        return pSpot != pRealEstate;
    case kTutorialStateHouseBought:
    case kTutorialStateBuyCar:
        return pSpot != pSpotECD12E && pSpot != pSpotECD8F &&
               pSpot != pSpotECD12B;
    case kTutorialStateCarBought:
        return pSpot != pSpotIntroRace;
    case kTutorialStateHome:
        return pSpot != pHome;
    default:
        return false;
    }
}

void GameIntroTutorial::reset() {
    pHome = nullptr;
    carDealer = 0;
    bModalHintVisible = false;
    tutorialStep = kTutorialStatePending;
    bMapForIntro = true;
}

void GameIntroTutorial::tick() {
    // FUN_007fcaf0
    if ( bInstallingCheatCars ) {
        IPlayerData *pSave = gpPlayerData->getLoadedSave();
        if ( pSave != nullptr &&
             static_cast<PlayerSave *>( pSave )->installCheatCars() ) {
            bInstallingCheatCars = false;
        }
    }

    if ( tutorialStep != kTutorialStatePending ) {
        if ( tutorialStep == kTutorialStateFirstDrive && bMustTeleport ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC (tutorial teleport)
        }
        return;
    }

    if ( gbScenarioTutorialEnabled ) {
        return;
    }

    IPlayerData *pLoadedSave = gpPlayerData->getLoadedSave();
    if ( pLoadedSave == nullptr ) {
        return;
    }

    Driver *pDriver = pLoadedSave->getDriver();
    const int32_t savedStep = pDriver->TutorialState;
    if ( savedStep != kTutorialStatePending ) {
        if ( savedStep == kTutorialStateFirstDrive ) {
            carIDRental = pDriver->TutorialUserParam;
        } else if ( savedStep > 7 && savedStep < 10 ) {
            carDealer = pDriver->TutorialUserParam;
        }

        setStep( savedStep );
        return;
    }

    endStep( tutorialStep );

    const int32_t previousStep = tutorialStep;
    tutorialStep = kTutorialStateIntro;

    pDriver->TutorialState = tutorialStep;
    pDriver->TutorialUserParam = 0;
    pDriver->notify( 0u );

    beginStep( tutorialStep, previousStep );
}

void GameIntroTutorial::onCarRented( const int32_t carID,
                                     const int32_t rentalTime ) {
    // FUN_007fca90
    if ( !isActive() ) {
        return;
    }

    carIDRental = carID;
    carRentalTime = rentalTime;
    setStep( kTutorialStateFirstDrive );
}

void GameIntroTutorial::onDealerGoToMap() {
    // FUN_007fc9a0
    if ( !isActive() ) {
        return;
    }

    carDealer = gpScenaric->getCurrentSpotIndex();
    setStep( kTutorialStateHouseBought );
}

void GameIntroTutorial::onHouseBought( SpotMap *pHouse ) {
    // FUN_007fca40
    if ( !isActive() ) {
        return;
    }

    pHome = pHouse;
    setStep( kTutorialStateHouseBought );
}

void GameIntroTutorial::onChallengeEntered() {
    // FUN_007fba90
    if ( isActive() && tutorialStep != kTutorialStateChallenge ) {
        setStep( kTutorialStateChallenge );
    }
}

void GameIntroTutorial::onHouseEntered() {
    // FUN_007fbe80
    if ( !isActive() || tutorialStep == kTutorialStateHomeReached ) {
        return;
    }

    setStep( kTutorialStateHomeReached );
    gpPlayerData->requestSave( 2 );
}

void GameIntroTutorial::onMissionQuit() {
    // FUN_007fc6f0
    if ( !isActive() ) {
        return;
    }

    setStep( kTutorialStateHome );
    gpPlayerData->requestSave( 2 );
}

static void UnlockTutorialAchievement() {
    AchievementsData *pAchievements =
        gpPlayerData->getLoadedSave()->getAchievements();
    if ( pAchievements->getField516_0x204() ) {
        return;
    }

    pAchievements->setField516_0x204( true );
    if ( pAchievements->isReady() ) {
        pAchievements->setCurrentGameMode(
            gpTestDriveInstance->getActiveGameMode() );
    }

    pAchievements->notify( ACF_Challenge | ACF_Road | ACF_Collection, false );
}

void GameIntroTutorial::onIntroFinished() {
    // FUN_007fc670
    UnlockTutorialAchievement();
    setStep( kTutorialStateCarRental );
}

void GameIntroTutorial::onCarBought() {
    // FUN_007fc9f0
    if ( !isActive() ) {
        return;
    }

    carDealer = gpScenaric->getCurrentSpotIndex();
    setStep( kTutorialStateCarBought );
}

void GameIntroTutorial::setStep( const int32_t step ) {
    // FUN_007fc5f0
    endStep( tutorialStep );

    const int32_t previousStep = tutorialStep;
    tutorialStep = step;

    Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();

    int32_t userParam = 0;
    if ( step == kTutorialStateFirstDrive ) {
        userParam = carIDRental;
    } else if ( step == kTutorialStateBuyCar ||
                step == kTutorialStateCarBought ) {
        userParam = carDealer;
    }

    pDriver->TutorialState = tutorialStep;
    pDriver->TutorialUserParam = userParam;
    pDriver->notify( 0u );

    beginStep( tutorialStep, previousStep );
}

static constexpr float kTutorialSpawnDistance = 10.0f;

static void SetPreviousTransformNearSpot( const SpotMap *pSpot,
                                          const bool bFaceSpot ) {
    Matrix4x4f_t frame;
    ComputeRoadApproachFrame(
        Eigen::Vector4f( pSpot->X, pSpot->Y, pSpot->Z, 1.0f ), frame,
        kTutorialSpawnDistance );
    if ( bFaceSpot ) {
        MatrixPreRotateY( frame, kPI );
    }
    gpWorld->setPreviousTransform( frame );
}

static void RestartFreeRideFromMap() {
    gpActiveGameMode->requestExit();
    gpTestDriveInstance->setNextGameMode( GM_Map );
    gpTestDriveInstance->setNextGameMode( GM_FreeRide );
}

static void SetSpotObjective( SpotMap *pSpot ) {
    gpScenaric->getSpotList().setSingleValidSpot( pSpot );
    if ( pSpot == nullptr ) {
        return;
    }

    ObjectiveMarker &objective = gpScenaric->getObjective( 2 );
    objective.Position = Eigen::Vector4f( pSpot->X, pSpot->Y, pSpot->Z, 1.0f );
    objective.Type = "finish";
}

static void LaunchTutorialSpot( SpotMap *pSpot ) {
    LaunchSpotLevel( pSpot, false );
    gpScenaric->setCurrentSpotIndex(
        gpScenaric->getSpotList().getSpotMapIndex( pSpot->Hashcode ) );
}

void GameIntroTutorial::beginStep( const int32_t step,
                                   const int32_t previousStep ) {
    // FUN_007fc1b0
    const bool bResumedFromSave = previousStep == kTutorialStatePending;

    switch ( step ) {
    case kTutorialStateIntro:
        if ( gbNewGameSkipIntro ) {
            IPlayerData *pLoadedSave = gpPlayerData->getLoadedSave();
            if ( pLoadedSave != nullptr &&
                 pLoadedSave->getGarage()->getNumOwnedHouses() == 0 ) {
                bInstallingCheatCars = !static_cast<PlayerSave *>( pLoadedSave )
                                            ->installCheatCars();
            }

            setStep( kTutorialStateRunning );
            return;
        }

        gpActiveGameMode->requestExit();
        gpTestDriveInstance->setNextGameMode( GM_Intro );
        return;

    case kTutorialStateAirplane:
        gpScenaric->setNextIntro( "Scenarics\\Cinematics\\", "tutorial_intro",
                                  "C_INTRO", GM_FreeRide, nullptr );

        gpActiveGameMode->requestExit();
        gpTestDriveInstance->setNextGameMode( GM_FreeRide );
        return;

    case kTutorialStateCarRental:
        LaunchTutorialSpot( pCarRental );
        DiscoverSpotRoad( pCarRental );
        return;

    case kTutorialStateFirstDrive:
        if ( bResumedFromSave ) {
            SetPreviousTransformNearSpot( pCarRental, true );
            carRentalTime = 600;
            gpScenaric->setCurrentSpotIndex(
                gpScenaric->getSpotList().getSpotMapIndex(
                    pCarRental->Hashcode ) );
            gpPlayerData->getLoadedSave()->getGarage()->beginRental(
                uint32_t( carIDRental ), carRentalTime, 0, 0, 0 );
            RestartFreeRideFromMap();
        }
        SetSpotObjective( pRealEstate );
        return;

    case kTutorialStateBuyHouse:
        LaunchTutorialSpot( pRealEstate );
        return;

    case kTutorialStateHouseBought:
        gbMapResetGPSCamera = true;
        gpActiveGameMode->requestExit();
        gpTestDriveInstance->setNextGameMode( GM_Map );
        return;

    case kTutorialStateBuyCar:
        if ( bResumedFromSave ) {
            carDealer = 0;
            setStep( kTutorialStateHouseBought );
        }
        return;

    case kTutorialStateCarBought:
        if ( bResumedFromSave ) {
            SpotMap *pDealer =
                gpScenaric->getSpotList().getWorldSpotChecked( carDealer );
            if ( pDealer != nullptr ) {
                SetPreviousTransformNearSpot( pDealer, false );
            }
            RestartFreeRideFromMap();
        }
        SetSpotObjective( pSpotIntroRace );
        return;

    case kTutorialStateChallenge:
        if ( bResumedFromSave ) {
            LaunchTutorialSpot( pSpotIntroRace );
        }
        return;

    case kTutorialStateHome:
        if ( bResumedFromSave ) {
            SetPreviousTransformNearSpot( pSpotIntroRace, true );
            RestartFreeRideFromMap();
        }
        SetSpotObjective( gpScenaric->getSpotList().getSpotMap(
            gpPlayerData->getLoadedSave()->getDriver()->getCurrentHouse() ) );
        return;

    default:
        return;
    }
}

void GameIntroTutorial::endStep( const int32_t step ) {
    // FUN_007fbab0
    switch ( step ) {
    case kTutorialStateFirstDrive:
    case kTutorialStateCarBought:
    case kTutorialStateHome:
        gpScenaric->getObjective( 2 ).Type = "";
        return;

    case kTutorialStateHouseBought:
        bMapForIntro = false;
        return;

    default:
        return;
    }
}

void GameIntroTutorial::sendFlashVariables( FlashPlayer *pPlayer ) {
    // FUN_007fc920
    if ( pPlayer == nullptr ) {
        return;
    }

    const bool bNoTutorial = tutorialStep == kTutorialStatePending ||
                             tutorialStep == kTutorialStateRunning;
    pPlayer->setVariableValue( "/:IsTutorial", bNoTutorial ? "0" : "1" );
    pPlayer->setVariableValue( "/:IsMapForTuto", bNoTutorial ? "1" : "0" );
    pPlayer->setVariableValue(
        "/:IsMapQuit", ( tutorialStep != kTutorialStateHouseBought ) ? 1 : 0 );
    if ( tutorialStep != kTutorialStateFirstDrive &&
         tutorialStep != kTutorialStateCarBought &&
         tutorialStep != kTutorialStateCarRental ) {
        launchCurrentStateDialog();
    }
}

void GameIntroTutorial::computeMapFocus( GMMap *pMap,
                                         Eigen::Vector4f *pOutFocus,
                                         const bool bSelectSpot ) {
    // FUN_007fc010
    if ( pOutFocus == nullptr || pMap == nullptr || !isActive() ||
         tutorialStep != kTutorialStateHouseBought ) {
        return;
    }

    SpotMap *dealers[3] = { pSpotECD12E, pSpotECD8F, pSpotECD12B };
    for ( SpotMap *pDealer : dealers ) {
        DiscoverSpotRoad( pDealer );
    }

    Spots &spotList = gpScenaric->getSpotList();
    for ( SpotMap *pDealer : dealers ) {
        if ( carDealer != spotList.getSpotMapIndex( pDealer->Hashcode ) ) {
            continue;
        }

        if ( bSelectSpot ) {
            pMap->getMngSpots().selectSpotRecord( pDealer );
        }
        *pOutFocus =
            Eigen::Vector4f( pDealer->X, pDealer->Y, pDealer->Z, 1.0f );
        return;
    }

    *pOutFocus = kTutorialDefaultMapFocus;
}

void GameIntroTutorial::onIntroCinematicEnded() {
    // FUN_007fcd40
    if ( tutorialStep == kTutorialStateAirplane ) {
        const ScenaricBank::eBankState bankState =
            landingCinematicBank.getState();
        if ( bankState != ScenaricBank::BBS_Unused &&
             bankState != ScenaricBank::BBS_LoadFailed ) {
            gpScenaric->unloadBank( &landingCinematicBank );
        }
        setStep( kTutorialStateCarRental );
        return;
    }

    if ( isActive() ) {
        launchCurrentStateDialog();
    }
}

EntityCinematic *GameIntroTutorial::loadLandingCinematic() {
    // FUN_007fcdc0
    if ( tutorialStep != kTutorialStateAirplane ) {
        return nullptr;
    }

    NextIntroInfo &nextIntro = gpScenaric->getNextIntro();
    nextIntro.bIsActive = false;

    // FUN_00806250
    std::string bankPath = gpConfig->getResRootPath();
    bankPath += nextIntro.pPath;
    const char lastChar = bankPath.empty() ? '\0' : bankPath.back();
    if ( lastChar != '\\' && lastChar != '/' ) {
        bankPath += '\\';
    }
    bankPath += nextIntro.pFilename;
    bankPath += ".bnk";

    gpScenaric->registerBank( &landingCinematicBank );
    landingCinematicBank.loadFromFile( bankPath.c_str(), nullptr, false );
    landingCinematicBank.forceLoad();

    // FUN_007dac10
    return static_cast<EntityCinematic *>( landingCinematicBank.findEntity(
        eEntityType::ET_CINE, nextIntro.pCinematicName ) );
}

void GameIntroTutorial::setModalHintVisible( const bool bVisible ) {
    // FUN_007fbd80
    if ( tutorialStep == kTutorialStateIntro ) {
        return;
    }

    bModalHintVisible = bVisible;

    bool bUpdateHudMovies = true;
    if ( gpActiveGameMode != nullptr ) {
        switch ( gpActiveGameMode->getCurrentGameMode() ) {
        case GM_FreeRide:
            bUpdateHudMovies = false;
            break;
        case GM_Shop:
        case GM_Goodies:
        case GM_DriveIn:
        case GM_Estates:
            bUpdateHudMovies =
                gpActiveGMScene != nullptr &&
                gpActiveGMScene->mustScenarioTutorialReactivateFlash();
            break;
        default:
            break;
        }
    }

    if ( bUpdateHudMovies ) {
        SetHUDMoviesPaused( bVisible );
    }

    if ( gpActiveGM != nullptr && isActive() ) {
        gpActiveGM->setBlockPauseByStart( bVisible );
    }
    gpPhysics->setPlayerSlowDownForced( bVisible );
}

void GameIntroTutorial::showFlashTutorialDialog( const char *pDialogID ) {
    // FUN_007fbef0
    if ( pDialogID == nullptr || bModalHintVisible ) {
        return;
    }

    ModalMessageBox *pMessageBox = gpFlashMessageBox;
    FlashPlayer *pPlayer = pMessageBox->getFlashPlayer();
    if ( pPlayer == nullptr || !pPlayer->spriteExists( "/avert" ) ||
         pMessageBox->getDisplayState() !=
             ModalMessageBox::eDisplayState::MMBDS_Closed ||
         gpPCOptions->isMainMenuOpened() ) {
        return;
    }

    const GameDatabase &gameDB = gpDatabase->getGameDB();
    if ( gameDB.DBTutorial == nullptr ) {
        return;
    }

    const char *pEmptyLabel = gpDatabase->getStringByHashcode(
        GSTutorial::kTutorialDatabaseIndex, 0x51fd96 );
    const uint32_t numEntries =
        gameDB.pNumItems[GSTutorial::kTutorialDatabaseIndex];
    for ( uint32_t i = 0u; i < numEntries; i++ ) {
        const DBEntryTutorial &entry = gameDB.DBTutorial[i];
        if ( strcmp( entry.TextID.pAsString, pDialogID ) != 0 ||
             entry.Libelle.pAsString == pEmptyLabel ) {
            continue;
        }

        TestDrive::DisplayModalPrompt(
            false, 0ull, GSTutorial::kTutorialDatabaseIndex, nullptr, nullptr,
            nullptr, 0, entry.Libelle.pAsString, true, false, MPT_Tutorial,
            pMessageBox );
        return;
    }
}

void GameIntroTutorial::launchCurrentStateDialog() {
    // FUN_007fc7f0
    if ( !isActive() || gpTestDriveInstance->getActiveGameMode() == GM_Login ) {
        return;
    }

    static constexpr const char *kStepDialogs[] = {
        nullptr,         nullptr,   nullptr,         nullptr,
        "tut_rental_pc", "tut_fr1", "tut_realtor",   "tut_map",
        "tut_cardeal",   "tut_fr2", "tut_challenge", "tut_fr3",
        "tut_home" };
    showFlashTutorialDialog( kStepDialogs[tutorialStep] );

    if ( tutorialStep != kTutorialStateHomeReached ) {
        return;
    }

    setStep( kTutorialStateRunning );
    gpScenaric->getSpotList().setUseOnlyValid( false );
    gpScenaric->getSpotList().updateSpotMeshValidity();
    EntityCinematic::ShowSpots( true );
    UnlockTutorialAchievement();
}
