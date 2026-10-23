#include "shared.h"
#include "game_mech_time.h"

#include "core/format.h"
#include "debug/ed_debug_text.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/gm_world.h"
#include "game/mission/game_mech_penalty.h"
#include "game/mng_hud.h"
#include "game/scenaric/event_parameters.h"
#include "game/scenaric/gs_scenaric.h"
#include "gs_timer.h"

static constexpr float kCentisecondsPerSecond = 100.0f;    // DAT_00f41708
static constexpr float kMillisecondsToCentiseconds = 0.1f; // FLOAT_00f41668
static constexpr float kPenaltyRatioScale = 0.1f;          // FLOAT_00f41668
static constexpr float kNoFinalTime = -1.0f;               // FLOAT_00f41664
static constexpr int32_t kResultReasonTimeOut = 1;
static constexpr int32_t kCupNone = 0;
static constexpr int32_t kCupGold = 1;
static constexpr int32_t kCupSilver = 2;
static constexpr int32_t kCupBronze = 3;

GameMechTime::GameMechTime( GameMechanism *pOwnerMech )
    : GameMechComponent( pOwnerMech ),
      elapsedTime( 0.0f ),
      startGameTime( 0.0f ),
      elapsedSystemTime( 0.0f ),
      startSystemTime( 0.0f ),
      timeLimit( 0.0f ),
      bHasTimeLimit( false ),
      bCountdownActive( false ),
      endGameTime( 0.0f ),
      raceTime( 0.0f ),
      finalTime( 0.0f ),
      timeOutTicks( kNumTimeOutTicks ) {
    // FUN_00733830 (inlined)
}

FlashPlayer *GameMechTime::getHudPlayer() const {
    return pMech->getOwner()->getHudChallenge().getHudPlayer();
}

bool GameMechTime::initialize() {
    // FUN_0072fe80
    resultReason = 0;
    result = CR_None;
    bCountdownActive = false;
    endState = -1;
    startSystemTime = 0.0f;
    startGameTime = 0.0f;
    elapsedSystemTime = 0.0f;
    elapsedTime = 0.0f;
    timeOutTicks = kNumTimeOutTicks;
    return true;
}

bool GameMechTime::setupHUD() {
    // FUN_00730590
    FlashPlayer *pPlayer = getHudPlayer();
    SetClipVisible( pPlayer, "/HUD/time", true );
    displayTime( 0.0f, false );

    if ( bHasTimeLimit ) {
        SetClipVisible( getHudPlayer(), "/HUD/time_cd", true );
        displayTime( timeLimit, true );
    }

    setTimeOutVisible( false );
    return true;
}

void GameMechTime::printDebug() {
    // FUN_0072fd70
    gEdDebugText.drawText( "TIME : State: %02d / Time:%.2f", endState,
                           double( elapsedTime ) );
}

int32_t GameMechTime::fillResults( FlashPlayer *pPlayer ) {
    // FUN_0072ffa0
    bool bHasPenalty = false;
    bool bNoTime = false;
    int32_t cup = kCupNone;

    pPlayer->setVariableValueNonLinked( "/:IsRankings", "0" );
    finalTime = raceTime;

    EventParameters *pEventParameters =
        pMech->getOwner()->getMissionDescription();
    const uint32_t goldTime =
        uint32_t( float( pEventParameters->getGoldTime().getValue() ) *
                  kMillisecondsToCentiseconds );
    const uint32_t silverTime =
        uint32_t( float( pEventParameters->getSilverTime().getValue() ) *
                  kMillisecondsToCentiseconds );
    const uint32_t bronzeTime =
        uint32_t( float( pEventParameters->getBronzeTime().getValue() ) *
                  kMillisecondsToCentiseconds );

    bool bFailed = false;
    if ( pEventParameters->getOffRoad().getValue() == 0 &&
         pEventParameters->getCollisions().getValue() == 0 ) {
        pPlayer->setVariableValueNonLinked( "/:IsPenalty", "0" );
        bFailed = result == CR_Failure;
    } else {
        bHasPenalty = true;
        const GameMechPenalty *pPenalty = static_cast<const GameMechPenalty *>(
            pMech->getComponent( GameMechanism::CS_Penalty ) );
        if ( pPenalty->getResult() == CR_Failure ) {
            pPlayer->setVariableValueNonLinked( "/:IsPenalty", "0" );
            bFailed = true;
        } else {
            const float penaltyRatio = pPenalty->getPenaltyRatio();
            pPlayer->setVariableValueNonLinked( "/:IsPenalty", "1" );
            finalTime += penaltyRatio * kPenaltyRatioScale * finalTime;
        }
    }

    if ( bFailed ) {
        pMech->fillChallengeResults( pPlayer, -1, true );
        cup = kCupNone;
        finalTime = kNoFinalTime;
        bNoTime = true;
    } else {
        const uint32_t finalCentiseconds =
            uint32_t( finalTime * kCentisecondsPerSecond );
        if ( finalCentiseconds <= goldTime ) {
            cup = kCupGold;
        } else if ( finalCentiseconds <= silverTime ) {
            cup = kCupSilver;
        } else if ( finalCentiseconds <= bronzeTime ) {
            cup = kCupBronze;
        } else {
            cup = kCupNone;
        }
        pMech->fillChallengeResults( pPlayer, ( cup == kCupNone ) ? -1 : cup,
                                     true );
    }

    pPlayer->setVariableValue( "/:WhatCup", uint32_t( cup ) );

    char goldText[32];
    char silverText[32];
    char bronzeText[32];
    char finalText[32];
    char baseText[32];
    char penaltyText[32];
    FormatCentiseconds( goldText, goldTime );
    FormatCentiseconds( silverText, silverTime );
    FormatCentiseconds( bronzeText, bronzeTime );
    const uint32_t finalCentiseconds =
        uint32_t( finalTime * kCentisecondsPerSecond );
    FormatCentiseconds( finalText, finalCentiseconds );
    pPlayer->setVariableValueNonLinked( "/:TimeList_ref1", goldText );
    pPlayer->setVariableValueNonLinked( "/:TimeList_ref2", silverText );
    pPlayer->setVariableValueNonLinked( "/:TimeList_ref3", bronzeText );

    const uint32_t baseCentiseconds =
        uint32_t( raceTime * kCentisecondsPerSecond );
    FormatCentiseconds( baseText, baseCentiseconds );
    pPlayer->setVariableValueNonLinked( "/:BaseTime", baseText );
    if ( bHasPenalty ) {
        FormatCentiseconds( penaltyText, finalCentiseconds - baseCentiseconds );
        pPlayer->setVariableValueNonLinked( "/:Penalty", penaltyText );
    }

    pPlayer->setVariableValueNonLinked( "/:FinalTime",
                                        bNoTime ? "--'--\"--" : finalText );
    pPlayer->setVariableValueNonLinked( "/:TimePlayerList_ref", " " );
    return cup;
}

bool GameMechTime::onMechStateProcess( const int32_t state,
                                       const float /*deltaTime*/,
                                       const eStateProcess process ) {
    // FUN_007303a0
    if ( startGameTime != 0.0f ) {
        elapsedTime = gpTimer->GameTotalTime - startGameTime;
    }
    if ( startSystemTime != 0.0f ) {
        elapsedSystemTime = gpTimer->SystemTotalTime - startSystemTime;
    }

    if ( state != GameMechanism::MS_Running ) {
        return false;
    }

    switch ( process ) {
    case SP_Init:
        startGameTime = gpTimer->GameTotalTime;
        startSystemTime = gpTimer->SystemTotalTime;
        elapsedSystemTime = 0.0f;
        elapsedTime = 0.0f;
        return true;

    case SP_Update: {
        displayTime( elapsedTime, false );
        if ( !bHasTimeLimit && !bCountdownActive ) {
            return false;
        }

        float remainingTime = 0.0f;
        if ( gpScenaric->isTrainingLevel() ) {
            remainingTime = timeLimit;
        } else if ( elapsedTime >= timeLimit ) {
            endState = 0;
            result = CR_Failure;
            resultReason = kResultReasonTimeOut;
            remainingTime = 0.0f;
        } else {
            remainingTime = timeLimit - elapsedTime;
            if ( float( timeOutTicks ) > remainingTime ) {
                FlashPlayer *pPlayer = getHudPlayer();
                SetClipVisible( pPlayer, "/HUD/time_out", true );
                pPlayer->showFrame( "on", "/HUD/time_out", true );
                pPlayer->setVariableValue( "/:time_out", timeOutTicks - 1 );
                timeOutTicks--;
            }
        }
        displayTime( remainingTime, true );
        return false;
    }

    case SP_Term:
        setTimeOutVisible( false );
        endGameTime = gpTimer->GameTotalTime;
        raceTime = endGameTime - startGameTime;
        displayTime( raceTime, false );
        if ( bHasTimeLimit ) {
            displayTime( 0.0f, true );
        }
        return true;

    default:
        return false;
    }
}

bool GameMechTime::startCountdown( const float duration ) {
    // FUN_0072fda0
    if ( startGameTime == 0.0f ) {
        return false;
    }

    const float elapsed = gpTimer->GameTotalTime - startGameTime;
    if ( bHasTimeLimit && duration > timeLimit - elapsed ) {
        return true;
    }

    timeOutTicks = int32_t( duration );
    timeLimit = elapsed + duration;
    bCountdownActive = true;
    return true;
}

void GameMechTime::addTimeBonus( const float bonus ) {
    // FUN_0072ff70
    if ( bCountdownActive ) {
        return;
    }

    timeLimit += bonus;
    timeOutTicks = std::min( int32_t( timeLimit ), kNumTimeOutTicks );
    setTimeOutVisible( false );
}

void GameMechTime::setTimeVisible( const bool bVisible ) {
    // FUN_0072ff00
    FlashPlayer *pPlayer = getHudPlayer();
    SetClipVisible( pPlayer, "/HUD/time", bVisible );
    SetClipVisible( pPlayer, "/HUD/time_cd", false );
}

bool GameMechTime::setTimeOutVisible( const bool bVisible ) {
    // FUN_0072fdf0
    FlashPlayer *pPlayer = getHudPlayer();
    if ( pPlayer == nullptr ) {
        return false;
    }

    FlashClip clip;
    if ( !pPlayer->getClipPlayer( "/HUD/time_out", &clip ) ) {
        return false;
    }
    clip.setProperty( eFlashProperty::FP_Visible, bVisible );
    return true;
}

void GameMechTime::displayTime( const float seconds, const bool bCountdown ) {
    // FUN_007302c0
    const int32_t totalCentiseconds =
        int32_t( seconds * kCentisecondsPerSecond );
    const int32_t cents = totalCentiseconds % 100;
    const int32_t wholeSeconds = ( ( totalCentiseconds - cents ) % 6000 ) / 100;
    const int32_t minutes = ( totalCentiseconds - wholeSeconds - cents ) / 6000;

    MngHudChallenge &hud = pMech->getOwner()->getHudChallenge();
    if ( bCountdown ) {
        hud.getCountdownMinutes().update( minutes );
        hud.getCountdownSeconds().update( wholeSeconds );
        hud.getCountdownCents().update( cents );
    } else {
        hud.getTimerMinutes().update( minutes );
        hud.getTimerSeconds().update( wholeSeconds );
        hud.getTimerCents().update( cents );
    }
}
