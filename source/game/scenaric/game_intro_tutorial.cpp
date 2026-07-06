#include "shared.h"
#include "game_intro_tutorial.h"

#include "game/scenaric/spots.h"

GameIntroTutorial::GameIntroTutorial()
    : tutorialStep( 0 ),
      pCarRental( nullptr ),
      pRealEstate( nullptr ),
      pSpotECD12E( nullptr ),
      pSpotECD8F( nullptr ),
      pSpotECD12B( nullptr ),
      pSpotIntroRace( nullptr ),
      bModalHintVisible( false ),
      bMapForIntro( true ) {}

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
    pRealEstate = nullptr;
    pCarRental = nullptr;
    bModalHintVisible = false;
    tutorialStep = 0;
    bMapForIntro = true;
}

void GameIntroTutorial::FUN_007fbd80( bool param_2 ) {
    // FUN_007fbd80
    OTDU_UNIMPLEMENTED;
}
