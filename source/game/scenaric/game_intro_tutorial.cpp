#include "shared.h"
#include "game_intro_tutorial.h"

#include "game/game_cheats.h"
#include "game/gm_base.h"
#include "game/gs_tutorial.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_save.h"
#include "player_data/saved_garage.h"
#include "player_data/player_data_interface.h"

#include "tdu_instance.h"

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
      bModalHintVisible( false ),
      bMapForIntro( true ),
      bMustTeleport( false ),
      bInstallingCheatCars( false ) {}

GameIntroTutorial::~GameIntroTutorial() {}

void GameIntroTutorial::initialize( Spots &spotList ) {
    // FUN_007fbbe0
    pRealEstate = spotList.getSpotMap( GetIdentifier64bit( "ERE_A_6627" ) );
    pCarRental = spotList.getSpotMap( GetIdentifier64bit( "ECR_A_6703" ) );
    pSpotECD12E = spotList.getSpotMap( GetIdentifier64bit( "ECD_12E_7757" ) );
    pSpotECD8F = spotList.getSpotMap( GetIdentifier64bit( "ECD_8F_7756" ) );
    pSpotECD12B = spotList.getSpotMap( GetIdentifier64bit( "ECD_12B_7777" ) );
    pSpotIntroRace = spotList.getSpotMap( GetIdentifier64bit( "RSFE0028" ) );

    bModalHintVisible = false;
    tutorialStep = 0;
    bMapForIntro = true;
}

bool GameIntroTutorial::isActive() const {
    // FUN_00406310
    return tutorialStep != 0xd && tutorialStep != 0x0;
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

void GameIntroTutorial::beginStep( const int32_t step, const int32_t ) {
    // FUN_007fc1b0
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
        OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC (NextIntro cinematic)

        gpActiveGameMode->requestExit();
        gpTestDriveInstance->setNextGameMode( GM_FreeRide );
        return;

    default:
        OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC (tutorial steps 4..11)
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

void GameIntroTutorial::FUN_007fbd80( bool param_2 ) {
    // FUN_007fbd80
    OTDU_UNIMPLEMENTED;
}
