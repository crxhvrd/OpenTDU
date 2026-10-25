#include "shared.h"
#include "game_mech_eliminator.h"

#include "ai/ai_game_session.h"
#include "core/format.h"
#include "database/gs_database.h"
#include "debug/ed_debug_text.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/gm_world.h"
#include "game/mission/game_mech_race_hud.h"
#include "game/mission/game_mech_time.h"
#include "game/scenaric/event_parameters.h"

static constexpr float kSecondsPerCentisecond = 0.01f; // DAT_00f416d4
static constexpr float kNoFinalTime = -1.0f;           // FLOAT_00f41664
static constexpr uint64_t kNetworkPlayerNameHashcode = 0x3383742;
static constexpr const char *kNoTime = "--'--\"--"; // 0x00f37758

GameMechEliminator::GameMechEliminator( GameMechanism *pOwnerMech )
    : GameMechComponent( pOwnerMech ),
      pRaceHUD( nullptr ),
      numParticipants( 0 ),
      numRemaining( 0 ),
      entries(),
      waitTime( 0.0f ),
      numInactive( 0 ),
      numBehind( 0 ),
      nameList{},
      timeList{},
      posList{},
      namePlayerList{},
      timePlayerList{},
      posPlayerList{} {
    // FUN_009bd180
}

FlashPlayer *GameMechEliminator::getHudPlayer() const {
    return pMech->getOwner()->getHudChallenge().getHudPlayer();
}

bool GameMechEliminator::initialize() {
    // FUN_009bd160
    resultReason = 0;
    result = CR_None;
    numBehind = 0;
    numInactive = 0;
    endState = -1;
    return true;
}

bool GameMechEliminator::setupHUD() {
    // FUN_009bd280
    FlashPlayer *pPlayer = getHudPlayer();
    ShowHudClip( pPlayer, "/HUD/eliminator" );
    for ( int32_t slot = 1; slot <= kMaxNumParticipants; slot++ ) {
        char path[0x40];
        snprintf( path, sizeof( path ), "/HUD/eliminator/%d", slot );
        FlashClip clip;
        if ( !pPlayer->getClipPlayer( path, &clip ) ) {
            continue;
        }
        pPlayer->showFrame( "empty", path, false );
        snprintf( path, sizeof( path ), "/HUD/eliminator/%d:player_name",
                  slot );
        pPlayer->setVariableValueNonLinked( path, "" );
    }
    return true;
}

void GameMechEliminator::printDebug() {
    // FUN_009bd110
    gEdDebugText.drawText(
        "LapElimination : State: %02d - TimeWaiting: %.2f - DidntPass: %d(%d)",
        getResultState(), double( waitTime ), numBehind, numInactive );
}

void GameMechEliminator::updateLapCounts() {
    // FUN_009be390 (inlined)
    const int32_t numLaps = numParticipants - 1;
    pRaceHUD->setNumLaps( numLaps );
    if ( numLaps >= 0 ) {
        pRaceHUD->setNumSteps( pRaceHUD->getCheckpointsPerLap() * numLaps + 1 +
                               pRaceHUD->getLoopStartIndex() );
    } else {
        pRaceHUD->setNumSteps( 0 );
    }
    pRaceHUD->updateCountersHud();
}

bool GameMechEliminator::onMechStateProcess( const int32_t state,
                                             const float deltaTime,
                                             const eStateProcess process ) {
    // FUN_009be390
    if ( state == GameMechanism::MS_InstallingResources ) {
        if ( process == SP_Init ) {
            pRaceHUD = static_cast<GameMechRaceHUD *>(
                pMech->getComponent( GameMechanism::CS_RaceHUD ) );
            pRaceHUD->setNumLaps( -1 );
            pRaceHUD->setNumSteps( 0 );
            pRaceHUD->updateCountersHud();
        }
    } else if ( state == GameMechanism::MS_WaitingStart ) {
        if ( process == SP_Init ) {
            result = CR_None;
            pRaceHUD = static_cast<GameMechRaceHUD *>(
                pMech->getComponent( GameMechanism::CS_RaceHUD ) );
            numParticipants = 0;
            GameSession *pSession = pMech->getGameSession();
            for ( int32_t i = 0; i < kMaxNumParticipants; i++ ) {
                Entry &entry = entries[i];
                if ( pSession->getParticipant( i )->isActive() ) {
                    entry.field_0x0 = 0;
                    entry.EliminationRank = -1;
                    entry.bInRace = true;
                    entry.bParticipant = true;
                    numParticipants++;
                } else {
                    entry.bParticipant = false;
                    entry.bInRace = false;
                }
            }
            numRemaining = numParticipants;
            updateLapCounts();
        }
    } else if ( state == GameMechanism::MS_Running && process == SP_Update ) {
        updateHud();
        updateElimination( deltaTime );
    }
    return false;
}

void GameMechEliminator::updateHud() {
    // FUN_009bde20
    FlashPlayer *pPlayer = getHudPlayer();
    char path[0x40];
    int32_t slot = 1;
    for ( GameParticipant *pParticipant = pRaceHUD->getRankingHead();
          pParticipant != nullptr;
          pParticipant = pParticipant->getSessionParams().pRankNextPlayer,
                          slot++ ) {
        const char *pName = pParticipant->getName();
        snprintf( path, sizeof( path ), "/HUD/eliminator/%d", slot );
        FlashClip clip;
        if ( !pPlayer->getClipPlayer( path, &clip ) ) {
            continue;
        }
        pPlayer->showFrame( ( numRemaining < slot ) ? "off" : "on", path,
                            false );
        snprintf( path, sizeof( path ), "/HUD/eliminator/%d:player_name",
                  slot );
        if ( pName != nullptr ) {
            pPlayer->setVariableValueNonLinked( path, pName );
        }
    }
    if ( slot > kMaxNumParticipants ) {
        return;
    }

    for ( ; slot <= kMaxNumParticipants; slot++ ) {
        snprintf( path, sizeof( path ), "/HUD/eliminator/%d", slot );
        FlashClip clip;
        if ( !pPlayer->getClipPlayer( path, &clip ) ) {
            continue;
        }
        pPlayer->showFrame( "empty", path, false );
        snprintf( path, sizeof( path ), "/HUD/eliminator/%d:player_name",
                  slot );
        pPlayer->setVariableValueNonLinked( path, "" );
    }
}

void GameMechEliminator::updateElimination( const float deltaTime ) {
    // FUN_009bdc40
    GameSession *pSession = pMech->getGameSession();
    const int32_t lap = numParticipants - numRemaining + 1;
    int32_t lastIndex = -1;
    numBehind = 0;
    numInactive = 0;
    for ( int32_t i = 0; i < kMaxNumParticipants; i++ ) {
        const Entry &entry = entries[i];
        if ( !entry.bParticipant || !entry.bInRace ) {
            continue;
        }
        GameParticipant *pParticipant = pSession->getParticipant( i );
        if ( !pParticipant->isActive() ) {
            numInactive++;
            numBehind++;
            lastIndex = i;
        } else if ( pParticipant->getSessionParams().CurrentLap < lap + 1 ) {
            numBehind++;
            if ( numInactive == 0 ) {
                lastIndex = i;
            }
        }
    }

    if ( numBehind == 0 ) {
        // Everyone completed the lap: the slowest to reach its end leaves.
        const int32_t lapsDone = lap + 1;
        int32_t lapEndStep = 1;
        if ( pMech->getOwner()
                     ->getMissionDescription()
                     ->getNumLaps()
                     .getValue() <= 1 &&
             lapsDone != 0 ) {
            lapEndStep =
                ( lapsDone - 1 ) * pRaceHUD->getCheckpointsPerLap() + 1;
        }

        uint32_t slowestTime = 0u;
        for ( int32_t i = 0; i < kMaxNumParticipants; i++ ) {
            const Entry &entry = entries[i];
            if ( !entry.bParticipant || !entry.bInRace ) {
                continue;
            }
            GameParticipant *pParticipant = pSession->getParticipant( i );
            if ( !pParticipant->isActive() ) {
                continue;
            }
            const GameParticipant::SessionParams &params =
                pParticipant->getSessionParams();
            if ( params.LastCheckpoint == lapEndStep &&
                 params.LastCheckpointTime > slowestTime ) {
                slowestTime = params.LastCheckpointTime;
                lastIndex = i;
            }
        }
        eliminate( lastIndex );
        return;
    }

    if ( numBehind > numInactive + 1 || lastIndex == -1 ) {
        waitTime = 0.0f;
        return;
    }
    waitTime += deltaTime;
    if ( waitTime >= kLastPlayerWaitDuration ) {
        eliminate( lastIndex );
    }
}

void GameMechEliminator::eliminate( const int32_t index ) {
    // FUN_009bdaa0
    if ( index < 0 ) {
        return;
    }

    Entry &entry = entries[index];
    entry.bInRace = false;
    entry.EliminationRank = numRemaining;
    numRemaining--;

    GameSession *pSession = pMech->getGameSession();
    if ( pSession->getLocalParticipant() ==
         pSession->getParticipant( index ) ) {
        result = CR_Failure;
        resultReason = 1;
    } else if ( numRemaining == 1 ) {
        result = CR_Success;
        resultReason = 2;
    }

    FlashPlayer *pPlayer = getHudPlayer();
    if ( ShowHudClip( pPlayer, "HUD/msg_eliminator" ) ) {
        pPlayer->showFrame( "on", "HUD/msg_eliminator", true );
        GameParticipant *pParticipant = pSession->getParticipant( index );
        if ( pParticipant != nullptr ) {
            pPlayer->setVariableValueNonLinked( "/:temp_name",
                                                pParticipant->getName() );
        }

        char path[0x40];
        snprintf( path, sizeof( path ), "/HUD/eliminator/%d",
                  entry.EliminationRank );
        FlashClip clip;
        if ( pPlayer->getClipPlayer( path, &clip ) ) {
            pPlayer->showFrame( "off", path, false );
        }
    }
    pSession->getParticipant( index )->removeFromWorld();
}

int32_t GameMechEliminator::fillResults( FlashPlayer *pPlayer ) {
    // FUN_009bd450
    char rankText[0x20] = "??";
    posPlayerList[0] = '\0';
    timePlayerList[0] = '\0';
    namePlayerList[0] = '\0';
    posList[0] = '\0';
    timeList[0] = '\0';
    nameList[0] = '\0';

    int32_t cup = -1;
    int32_t numPlayers = 0;
    GameParticipant *pParticipant = nullptr;
    GameMechRaceHUD *pHud = static_cast<GameMechRaceHUD *>(
        pMech->getComponent( GameMechanism::CS_RaceHUD ) );
    if ( pHud != nullptr ) {
        pHud->sortRanking( false );
        pParticipant = pHud->getRankingHead();
    }
    pPlayer->setVariableValueNonLinked( "/:IsPenalty", "0" );

    GameSession *pSession = pMech->getGameSession();
    GameMechTime *pTime = pMech->getTimeComponent();
    for ( int32_t rank = 1; pParticipant != nullptr;
          pParticipant = pParticipant->getSessionParams().pRankNextPlayer,
                  rank++ ) {
        snprintf( rankText, sizeof( rankText ), "%d", rank );
        strcat( posList, rankText );
        strcat( nameList, pParticipant->getName() );
        numPlayers++;

        const bool bWinner = ( rank == 1 );
        const uint32_t raceTime =
            pParticipant->getSessionParams().LastCheckpointTime;
        if ( bWinner ) {
            const uint32_t hundredths = raceTime % 100;
            const uint32_t seconds = ( ( raceTime - hundredths ) % 6000 ) / 100;
            const uint32_t minutes =
                ( raceTime - seconds * 100 - hundredths ) / 6000;
            char timeText[0x20];
            snprintf( timeText, sizeof( timeText ), "%d'%02d\"%02d", minutes,
                      seconds, hundredths );
            strcat( timeList, timeText );
        } else {
            const char *pNetworkName = gpDatabase->getStringByHashcode(
                0, kNetworkPlayerNameHashcode );
            strcat( timeList,
                    ( pParticipant->isRemote() && pNetworkName != nullptr )
                        ? pNetworkName
                        : "--" );
        }
        pParticipant->setFinishPosition( bWinner ? 0 : -1 );
        strcat( nameList, "\n" );
        strcat( timeList, "\n" );
        strcat( posList, "\n" );

        if ( pParticipant == pSession->getLocalParticipant() ) {
            strcat( namePlayerList, pParticipant->getName() );
            if ( !bWinner ) {
                pPlayer->setVariableValueNonLinked( "/:FinalTime", kNoTime );
                pTime->setFinalTime( kNoFinalTime );
            }
            if ( cup == -1 ) {
                pMech->fillChallengeResults( pPlayer, rank, false );
                cup = ( rank <= 3 ) ? rank : 0;
                strcat( posPlayerList, rankText );
                if ( bWinner ) {
                    pTime->setFinalTime( float( raceTime ) *
                                         kSecondsPerCentisecond );
                    char timeText[0x20];
                    pPlayer->setVariableValueNonLinked(
                        "/:BaseTime",
                        FormatCentiseconds( timeText, raceTime ) );
                    pPlayer->setVariableValueNonLinked(
                        "/:FinalTime",
                        FormatCentiseconds( timeText, raceTime ) );
                }
            }
        }
        strcat( namePlayerList, "\n" );
        strcat( timePlayerList, "\n" );
        strcat( posPlayerList, "\n" );
    }

    pPlayer->setVariableValue( "/:MyCup", cup );
    pPlayer->setVariableValue( "/:WhatCup", cup );
    pPlayer->setVariableValue( "/:NamePlayerList", namePlayerList );
    pPlayer->setVariableValue( "/:TimePlayerList", timePlayerList );
    pPlayer->setVariableValue( "/:PosPlayerList", posPlayerList );
    pPlayer->setVariableValue( "/:NameList", nameList );
    pPlayer->setVariableValue( "/:TimeList", timeList );
    pPlayer->setVariableValue( "/:PosList", posList );
    pPlayer->setVariableValue( "/:nb_players", numPlayers );
    return cup;
}
