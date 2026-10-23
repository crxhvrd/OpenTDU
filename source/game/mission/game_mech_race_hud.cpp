#include "shared.h"
#include "game_mech_race_hud.h"

#include "ai/ai_game_session.h"
#include "core/format.h"
#include "database/gs_database.h"
#include "debug/ed_debug_text.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/gm_world.h"
#include "game/mission/game_mech_time.h"
#include "game/mng_hud.h"
#include "game/scenaric/entity/entity_check_point.h"
#include "game/scenaric/event_parameters.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/mng_actor.h"
#include "game/scenaric/scenaric_bank.h"
#include "game/vehicle/car_state.h"
#include "gs_timer.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_ranking.h"
#include "tdu_instance.h"

static constexpr float kCentisecondsPerSecond = 100.0f;      // DAT_00f41708
static constexpr float kSecondsPerCentisecond = 0.01f;       // DAT_00f416d4
static constexpr float kMillisecondsToSeconds = 0.001f;      // FLOAT_00f4167c
static constexpr float kTemplateSwapDelay = 3.0f;            // DAT_00f416b4
static constexpr float kMetersPerSecondToKmh = 3.6f;         // DAT_00f41768
static constexpr float kExtraChallengeMaxStartSpeed = 10.0f; // FLOAT_00f4166c
static constexpr float kNoFinalTime = -1.0f;                 // FLOAT_00f41664
static constexpr uint32_t kLedTypeStart = 1;
static constexpr uint32_t kLedTypeFinish = 2;
static constexpr uint32_t kLedTypeCheckpoint = 3;
static constexpr int32_t kMaxEventSlots = 16;
static constexpr int32_t kRaceMessageSplit = 2;
static constexpr int32_t kMsgCheckpointValidated = 5;
static constexpr uint64_t kNetworkPlayerNameHashcode = 0x3383742;
static constexpr uint64_t kRankingWinMessageHashcode = 0x394d632;
static constexpr uint64_t kRankingLoseMessageHashcode = 0x38fcd22;
static constexpr const char *kNoTime = "--'--\"--"; // 0x00f37758

// "+ m'ss"cc" / "- ss"cc" of a signed 1/100 s gap.
static char *FormatCentisecondsGap( char *pOutText, int32_t gap ) {
    // FUN_007306c0
    if ( pOutText == nullptr ) {
        return nullptr;
    }
    if ( gap < 0 ) {
        pOutText[0] = '-';
        gap = -gap;
    } else {
        pOutText[0] = '+';
    }
    pOutText[1] = ' ';
    pOutText[2] = '\0';

    const int32_t hundredths = gap % 100;
    const int32_t seconds = ( ( gap - hundredths ) % 6000 ) / 100;
    const int32_t minutes = ( gap - seconds * 100 - hundredths ) / 6000;
    if ( minutes > 0 ) {
        sprintf( pOutText + 2, "%d'%02d\"%02d", minutes, seconds, hundredths );
    } else {
        sprintf( pOutText + 2, "%d\"%02d", seconds, hundredths );
    }
    return pOutText;
}

static bool ShowHudClip( FlashPlayer *pPlayer, const char *pClipPath ) {
    FlashClip clip;
    if ( !pPlayer->getClipPlayer( pClipPath, &clip ) ) {
        return false;
    }

    ScopedMutexLock lock = gpFlash->scopedLock();
    clip.pSprite->getDisplayList()->getFlagsRW() |= 1;
    return true;
}

static void SetHudClipVisible( FlashPlayer *pPlayer, const char *pClipPath ) {
    FlashClip clip;
    if ( pPlayer->getClipPlayer( pClipPath, &clip ) ) {
        // FUN_004413d0
        clip.setProperty( eFlashProperty::FP_Visible, true );
    }
}

static float DistanceToCheckpoint( CarState *pCarState,
                                   const EntityCheckPoint *pCheckpoint ) {
    // FUN_00425a60
    Eigen::Vector4f delta =
        pCarState->getBody()->WorldMatrix.row( 3 ).transpose() -
        pCheckpoint->getModelMatrix().row( 3 ).transpose();
    delta.y() = 0.0f;
    return delta.dot( delta );
}

static FlashPlayer *GetHudPlayer() {
    return ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;
}

GameMechRaceHUD::GameMechRaceHUD( GameMechanism *pOwnerMech )
    : GameMechComponent( pOwnerMech ),
      pRecordTimes( nullptr ),
      pCurrentTimes( nullptr ),
      pEventParameters( nullptr ),
      pMngActor( nullptr ),
      pRankingHead( nullptr ),
      nameList{},
      timeList{},
      posList{},
      namePlayerList{},
      timePlayerList{},
      posPlayerList{},
      numSteps( 0 ),
      numLaps( 0 ),
      loopStartIndex( 0 ),
      checkpointsPerLap( 0 ),
      templateSwapTimer( 0.0f ),
      bShowCheckpointHud( true ),
      bShowNextCheckpointOnly( true ),
      bActive( false ) {
    // FUN_0072e260
}

GameMechRaceHUD::~GameMechRaceHUD() {
    // FUN_0042e250
}

int32_t GameMechRaceHUD::wrapStep( const int32_t step ) const {
    // FUN_0072dc30
    if ( step <= loopStartIndex + checkpointsPerLap ||
         pEventParameters->getLoop().getValue() == 0 ) {
        return step;
    }
    if ( checkpointsPerLap == 0 ) {
        return 0;
    }
    return ( step - loopStartIndex - 1 ) % checkpointsPerLap + loopStartIndex +
           1;
}

int32_t GameMechRaceHUD::stepFromIndexAndLap( const int32_t checkpointIndex,
                                              const int32_t lap ) const {
    // FUN_0072dbf0
    const int32_t loopCheckpointIndex =
        int32_t( pEventParameters->getLoopCheckpointIndex().getValue() );
    if ( checkpointIndex >= loopCheckpointIndex && lap != 0 ) {
        return ( lap - 1 ) * checkpointsPerLap + checkpointIndex;
    }
    return checkpointIndex;
}

int32_t GameMechRaceHUD::getParticipantLap( const int32_t slot ) const {
    // FUN_0072dc80
    GameParticipant *pParticipant =
        pMech->getGameSession()->getParticipant( slot );
    return ( pParticipant != nullptr )
               ? pParticipant->getSessionParams().CurrentLap
               : 0;
}

bool GameMechRaceHUD::isOnLapCheckpoint( const int32_t slot ) const {
    // FUN_0072db70
    GameParticipant *pParticipant =
        pMech->getGameSession()->getParticipant( slot );
    if ( pParticipant == nullptr ) {
        return false;
    }

    const int32_t step = pParticipant->getSessionParams().LastCheckpoint;
    const int32_t loopCheckpointIndex =
        int32_t( pEventParameters->getLoopCheckpointIndex().getValue() );
    bool bLapBoundary = false;
    if ( checkpointsPerLap != 0 && step > loopCheckpointIndex ) {
        bLapBoundary =
            ( step - loopCheckpointIndex + 1 ) % checkpointsPerLap == 0;
    }
    return step == loopCheckpointIndex - 1 || bLapBoundary;
}

EntityCheckPoint *GameMechRaceHUD::findCheckpointForStep( int32_t step ) const {
    // FUN_0072dcb0
    if ( pMech->getOwner()->getMissionDescription()->getLoop().getValue() !=
         0 ) {
        step = wrapStep( step );
    }
    for ( int32_t i = 0; i < pMngActor->getActorCount(); i++ ) {
        Entity *pActor = pMngActor->getActor( i );
        if ( pActor->getType() != eEntityType::ET_CHPT ) {
            continue;
        }
        EntityCheckPoint *pCheckpoint =
            static_cast<EntityCheckPoint *>( pActor );
        if ( pCheckpoint->getIndex() >= 0 && pCheckpoint->getIndex() == step ) {
            return pCheckpoint;
        }
    }
    return nullptr;
}

EntityCheckPoint *
GameMechRaceHUD::swapCheckpointTemplate( EntityCheckPoint *pCheckpoint,
                                         const uint32_t ledType ) {
    // FUN_0072f530
    const SerializedCheckPoint *pLEDData = pCheckpoint->getLEDData();
    if ( pLEDData->Type == ledType ) {
        return pCheckpoint;
    }

    const bool bBigCheckpoint =
        pLEDData != nullptr && pLEDData->BigCheckpoint != 0;
    Entity *pTemplate = gpScenaric->getCheckpointBank()->findCheckpointTemplate(
        ledType, bBigCheckpoint );

    Matrix4x4f_t transform = pCheckpoint->getModelMatrix();
    const int32_t index = pCheckpoint->getIndex();

    const uint32_t flags = pCheckpoint->getFlags();
    MngActor::Uninstanciate( pMngActor, pCheckpoint );

    EntityCheckPoint *pInstance = static_cast<EntityCheckPoint *>(
        pMngActor->instanciateActor( pTemplate ) );
    Matrix4x4f_t identity = Matrix4x4f_t::Identity();
    pInstance->setTransform( identity, true );
    pInstance->propagateTransform( transform, true );
    pInstance->setTransform( transform, true );
    pInstance->setIndex( index );
    pInstance->setFlags( flags );
    pInstance->setPrimaryInstance( true );
    return pInstance;
}

void GameMechRaceHUD::resetCheckpoints() {
    // FUN_0072fc00
    resultReason = 0;
    endState = -1;
    result = 0;
    pEventParameters = pMech->getOwner()->getMissionDescription();
    pMngActor = &pMech->getOwner()->getActorManager();

    if ( pRecordTimes != nullptr ) {
        TestDrive::FreeAligned( pRecordTimes );
        pRecordTimes = nullptr;
    }
    if ( pCurrentTimes != nullptr ) {
        TestDrive::FreeAligned( pCurrentTimes );
        pCurrentTimes = nullptr;
    }

    for ( int32_t i = 0; i < pMngActor->getActorCount(); i++ ) {
        Entity *pActor = pMngActor->getActor( i );
        if ( pActor->getType() != eEntityType::ET_CHPT ) {
            continue;
        }
        EntityCheckPoint *pCheckpoint =
            static_cast<EntityCheckPoint *>( pActor );
        if ( pCheckpoint->getIndex() >= 0 ) {
            pCheckpoint->resetEventCounters();
        }
    }
}

void GameMechRaceHUD::computeCheckpointCounts() {
    // FUN_0072f660
    for ( int32_t i = 0; i < pMngActor->getActorCount(); i++ ) {
        Entity *pActor = pMngActor->getActor( i );
        if ( pActor->getType() != eEntityType::ET_CHPT ) {
            continue;
        }
        EntityCheckPoint *pCheckpoint =
            static_cast<EntityCheckPoint *>( pActor );
        const int32_t index = pCheckpoint->getIndex();
        if ( index < 0 ) {
            continue;
        }
        if ( bShowNextCheckpointOnly ) {
            pCheckpoint->setVisible(
                pCheckpoint->getLEDData()->Type == kLedTypeStart || index < 2 );
        }
        if ( numSteps < index ) {
            numSteps = index;
        }
    }

    if ( pEventParameters->getLoop().getValue() == 0 ) {
        numLaps = 0;
    } else {
        numLaps = int32_t( pEventParameters->getNumLaps().getValue() );
        if ( pMech->getComponent( GameMechanism::CS_Unknown10 ) != nullptr ) {
            numLaps = 8;
        } else if ( gpScenaric->isTrainingLevel() ) {
            numLaps = 1;
        }
        if ( numLaps == 0 ) {
            numLaps = 1;
        }

        loopStartIndex =
            int32_t( pEventParameters->getLoopCheckpointIndex().getValue() ) -
            1;
        checkpointsPerLap = numSteps - loopStartIndex;
        numSteps = ( numLaps < 0 )
                       ? 0
                       : checkpointsPerLap * numLaps + loopStartIndex + 1;

        EntityCheckPoint *pLast =
            findCheckpointForStep( checkpointsPerLap + loopStartIndex );
        swapCheckpointTemplate( pLast, kLedTypeCheckpoint );
        EntityCheckPoint *pStart = findCheckpointForStep( 1 );
        pStart = swapCheckpointTemplate( pStart, kLedTypeStart );
        if ( bShowNextCheckpointOnly ) {
            pStart->setVisible( true );
        }
        templateSwapTimer = 0.0f;
    }

    if ( numSteps != 0 ) {
        const size_t bufferSize = size_t( numSteps ) * sizeof( uint32_t );
        pRecordTimes = static_cast<uint32_t *>(
            TestDrive::AllocAligned( bufferSize, alignof( uint32_t ) ) );
        memset( pRecordTimes, 0, bufferSize );
        pCurrentTimes = static_cast<uint32_t *>(
            TestDrive::AllocAligned( bufferSize, alignof( uint32_t ) ) );
        memset( pCurrentTimes, 0, bufferSize );
    }
}

bool GameMechRaceHUD::initialize() {
    // FUN_0072fb40
    resetCheckpoints();
    numSteps = 0;
    numLaps = 0;
    checkpointsPerLap = 0;
    loopStartIndex = 0;
    if ( gpActiveGameMode->getCurrentGameMode() == GM_Mission ) {
        computeCheckpointCounts();
    }
    return true;
}

void GameMechRaceHUD::reset() {
    // FUN_0072e080
    pRankingHead = nullptr;
    bActive = false;
}

bool GameMechRaceHUD::terminate() {
    // FUN_0072fbc0
    if ( pRecordTimes != nullptr ) {
        TestDrive::FreeAligned( pRecordTimes );
        pRecordTimes = nullptr;
    }
    if ( pCurrentTimes != nullptr ) {
        TestDrive::FreeAligned( pCurrentTimes );
        pCurrentTimes = nullptr;
    }
    return true;
}

void GameMechRaceHUD::printDebug() {
    // FUN_0072daa0
    gEdDebugText.drawText(
        "CHECKPOINTS : State: %02d / NbCP: %d(%d) / NbLaps: %d ",
        getResultState(), numSteps, checkpointsPerLap, numLaps );
}

void GameMechRaceHUD::UpdatePositionHud( FlashPlayer *pPlayer,
                                         const int32_t rank,
                                         const uint32_t numPlayers ) {
    // FUN_0072e2a0
    char position[0x20] = "-";
    char suffix[0x20] = " ";
    char playerCount[0x20] = "   ";
    if ( numPlayers > 0 ) {
        switch ( rank ) {
        case 1:
            strcpy( position, "1" );
            strcpy( suffix, "ST" );
            break;
        case 2:
            strcpy( position, "2" );
            strcpy( suffix, "nd" );
            break;
        case 3:
            strcpy( position, "3" );
            strcpy( suffix, "rd" );
            break;
        default:
            snprintf( position, sizeof( position ), "%d", rank );
            strcpy( suffix, "th" );
            break;
        }
        snprintf( playerCount + 1, sizeof( playerCount ) - 1, "%u",
                  numPlayers );
    }
    pPlayer->setVariableValueNonLinked( "/:Pos", position );
    pPlayer->setVariableValueNonLinked( "/:Exp", suffix );
    pPlayer->setVariableValueNonLinked( "/:NbPlayers", playerCount );
}

void GameMechRaceHUD::updateCountersHud() {
    // FUN_0072e5d0
    FlashPlayer *pPlayer = GetHudPlayer();
    GameParticipant *pLocal = pMech->getGameSession()->getLocalParticipant();
    if ( pLocal == nullptr ) {
        return;
    }

    if ( numSteps < 0 ) {
        pPlayer->setVariableValueNonLinked( "/:NbCheckpoints", "-" );
        pPlayer->setVariableValueNonLinked( "/:TotalCheckpoints", "-" );
    } else {
        pPlayer->setVariableValue( "/:NbCheckpoints",
                                   pLocal->getSessionParams().LastCheckpoint );
        pPlayer->setVariableValue( "/:TotalCheckpoints", numSteps );
    }

    if ( pEventParameters->getLoop().getValue() == 0 ) {
        return;
    }
    if ( numLaps < 0 ) {
        pPlayer->setVariableValueNonLinked( "/:NbTurns", "-" );
        pPlayer->setVariableValueNonLinked( "/:TotalTurns", "-" );
        return;
    }
    pPlayer->setVariableValue(
        "/:NbTurns",
        std::min( pLocal->getSessionParams().CurrentLap, numLaps ) );
    pPlayer->setVariableValue( "/:TotalTurns", numLaps );
}

bool GameMechRaceHUD::showCheckpointGap( const uint32_t recordTime,
                                         const uint32_t time,
                                         bool bShowGap ) {
    // FUN_0072e450
    FlashPlayer *pPlayer = GetHudPlayer();
    bool bBetter = false;
    int32_t gap = 0;
    if ( recordTime != 0 ) {
        gap = int32_t( time - recordTime );
    } else {
        bShowGap = false;
    }

    char timeText[0x20];
    char gapText[0x20];
    FormatCentiseconds( timeText, time );
    FormatCentisecondsGap( gapText, gap );
    if ( bShowGap ) {
        if ( gap > 0 ) {
            pPlayer->setVariableValueNonLinked( "/:IsCheckGreen", "0" );
        } else {
            bBetter = true;
            pPlayer->setVariableValueNonLinked( "/:IsCheckGreen", "1" );
        }
        pPlayer->setVariableValueNonLinked( "/:Gap", gapText );
    } else {
        pPlayer->setVariableValueNonLinked( "/:Gap", " " );
        pPlayer->setVariableValueNonLinked( "/:IsCheckGreen", "2" );
    }
    pPlayer->setVariableValueNonLinked( "/:CheckTime", timeText );
    pPlayer->showFrame( "chekpoint", "/HUD/checkpoints", true );
    return bBetter;
}

void GameMechRaceHUD::showBottomGap( const int32_t gap ) {
    // FUN_0072e3f0
    FlashPlayer *pPlayer = GetHudPlayer();
    char gapText[0x20];
    FormatCentisecondsGap( gapText, gap );
    pPlayer->setVariableValueNonLinked( "/:IsBottomCheckGreen", "1" );
    pPlayer->setVariableValueNonLinked( "/:Gap_bottom", gapText );
    pPlayer->showFrame( "checkpoint", "/HUD/checkpoint_bottom", true );
}

bool GameMechRaceHUD::setupHUD() {
    // FUN_0072eb00
    FlashPlayer *pPlayer = GetHudPlayer();
    ShowHudClip( pPlayer, "/HUD/time" );
    if ( bShowCheckpointHud ) {
        ShowHudClip( pPlayer, "/HUD/checkpoints" );
    }
    if ( pEventParameters->getLoop().getValue() != 0 &&
         pEventParameters->getNumLaps().getValue() != 1 ) {
        SetHudClipVisible( pPlayer, "/HUD/laps" );
    }
    if ( !gpScenaric->isTrainingLevel() &&
         pMech->getMechType() == GameMechanism::MT_Race ) {
        SetHudClipVisible( pPlayer, "/HUD/position" );
        UpdatePositionHud( pPlayer, 0, 0 );
    }
    updateCountersHud();
    pPlayer->setVariableValueNonLinked( "/:CheckTime", "" );
    pPlayer->setVariableValueNonLinked( "/:Gap", "" );
    return true;
}

void GameMechRaceHUD::updateObjectives() {
    // FUN_0072dd20
    GameSession *pSession = pMech->getGameSession();
    GameParticipant *pLocal =
        pSession->getParticipant( pSession->getLocalParticipantIndex() );
    const int32_t localStep =
        ( pLocal != nullptr ) ? pLocal->getSessionParams().LastCheckpoint : 0;
    const int32_t nextStep = wrapStep( localStep ) + 1;
    EntityCheckPoint *pNext = findCheckpointForStep( nextStep );

    for ( int32_t i = 0; i < MngHud::kMaxObjectiveMarkers; i++ ) {
        gpMngHud->setObjectiveMarkerType( i, "" );
    }
    gpScenaric->getObjective( 3 ).Type = "";
    gpScenaric->getObjective( 4 ).Type = "";
    if ( pNext == nullptr ) {
        return;
    }

    bool bShowNext = true;
    if ( numLaps != 0 ) {
        GameParticipant *pLocalLap =
            pSession->getParticipant( pSession->getLocalParticipantIndex() );
        const int32_t lap = ( pLocalLap != nullptr )
                                ? pLocalLap->getSessionParams().CurrentLap
                                : 0;
        bShowNext = lap <= numLaps;
    }

    if ( pNext->getType() == eEntityType::ET_CHPT ) {
        if ( !bShowNext ) {
            return;
        }

        const Eigen::Vector4f position =
            pNext->getModelMatrix().row( 3 ).transpose();
        const bool bGPS = pEventParameters->getGps().getValue() >= 1;
        switch ( pNext->getLEDData()->Type ) {
        case kLedTypeStart:
            if ( pMech->getMechType() != GameMechanism::MT_ExtraChallenge ) {
                gpMngHud->setObjectiveMarker( 0, position, "start" );
                if ( bGPS ) {
                    ObjectiveMarker &objective = gpScenaric->getObjective( 3 );
                    objective.Position = position;
                    objective.Type = "start";
                }
            }
            break;
        case kLedTypeFinish:
            gpMngHud->setObjectiveMarker( 0, position, "finish" );
            if ( bGPS ) {
                ObjectiveMarker &objective = gpScenaric->getObjective( 3 );
                objective.Position = position;
                objective.Type = "finish";
            }
            break;
        default:
            gpMngHud->setObjectiveMarker( 0, position, "checkpoint" );
            if ( pNext->getLEDData()->Type == kLedTypeCheckpoint && bGPS ) {
                ObjectiveMarker &objective = gpScenaric->getObjective( 3 );
                objective.Position = position;
                objective.Type = "checkpoint";
            }
            break;
        }

        if ( bGPS && pNext->getLEDData()->Type != kLedTypeFinish ) {
            EntityCheckPoint *pAfter = findCheckpointForStep( nextStep + 1 );
            if ( pAfter != nullptr &&
                 pAfter->getType() == eEntityType::ET_CHPT ) {
                const uint32_t afterType = pAfter->getLEDData()->Type;
                if ( afterType == kLedTypeFinish ||
                     afterType == kLedTypeCheckpoint ) {
                    ObjectiveMarker &objective = gpScenaric->getObjective( 4 );
                    objective.Position =
                        pAfter->getModelMatrix().row( 3 ).transpose();
                    objective.Type = ( afterType == kLedTypeFinish )
                                         ? "finish"
                                         : "checkpoint";
                }
            }
        }
    }

    if ( bShowNext && bShowNextCheckpointOnly ) {
        pNext->setVisible( true );
    }
}

int32_t GameMechRaceHUD::sortRanking( const bool bUseDistance ) {
    // FUN_0072e090
    GameSession *pSession = pMech->getGameSession();
    int32_t numInserted = 0;
    for ( int32_t slot = 0; slot < pSession->getCapacity(); slot++ ) {
        GameParticipant *pParticipant = pSession->getParticipant( slot );
        if ( pParticipant == nullptr || !pParticipant->isActive() ) {
            continue;
        }
        if ( numInserted == 0 ) {
            pParticipant->getSessionParams().pRankNextPlayer = nullptr;
            pRankingHead = pParticipant;
            numInserted++;
            continue;
        }

        EntityCheckPoint *pNextCheckpoint = nullptr;
        if ( bUseDistance &&
             pParticipant->getSessionParams().LastCheckpoint != numSteps ) {
            pNextCheckpoint = findCheckpointForStep(
                pParticipant->getSessionParams().LastCheckpoint + 1 );
        }

        GameParticipant **ppLink = &pRankingHead;
        while ( true ) {
            GameParticipant *pOther = *ppLink;
            bool bAhead = false;
            if ( pParticipant->getSessionParams().LastCheckpoint >
                 pOther->getSessionParams().LastCheckpoint ) {
                bAhead = true;
            } else if ( pParticipant->getSessionParams().LastCheckpoint ==
                        pOther->getSessionParams().LastCheckpoint ) {
                if ( pParticipant->getSessionParams().LastCheckpoint ==
                         numSteps ||
                     !bUseDistance ) {
                    bAhead =
                        pParticipant->getSessionParams().LastCheckpointTime <
                        pOther->getSessionParams().LastCheckpointTime;
                } else if ( pNextCheckpoint != nullptr &&
                            pParticipant->getCarState() != nullptr &&
                            pOther->getCarState() != nullptr ) {
                    const float distance = DistanceToCheckpoint(
                        pParticipant->getCarState(), pNextCheckpoint );
                    const float otherDistance = DistanceToCheckpoint(
                        pOther->getCarState(), pNextCheckpoint );
                    bAhead = otherDistance > distance;
                }
            }

            if ( bAhead ) {
                pParticipant->getSessionParams().pRankNextPlayer = pOther;
                *ppLink = pParticipant;
                break;
            }
            if ( pOther->getSessionParams().pRankNextPlayer == nullptr ) {
                pOther->getSessionParams().pRankNextPlayer = pParticipant;
                pParticipant->getSessionParams().pRankNextPlayer = nullptr;
                break;
            }
            ppLink = &pOther->getSessionParams().pRankNextPlayer;
        }
        numInserted++;
    }
    return pSession->getNumParticipants();
}

void GameMechRaceHUD::assignRanks() {
    // FUN_0072da80
    int32_t rank = 1;
    for ( GameParticipant *pParticipant = pRankingHead; pParticipant != nullptr;
          pParticipant = pParticipant->getSessionParams().pRankNextPlayer ) {
        pParticipant->getSessionParams().LastPosition = rank++;
    }
}

bool GameMechRaceHUD::isRivalOnSameStep( const int32_t step,
                                         GameParticipant *pParticipant,
                                         const int32_t slot ) const {
    // FUN_0072dad0
    GameSession *pSession = pMech->getGameSession();
    if ( step <= 1 || step >= numSteps ||
         slot == pSession->getLocalParticipantIndex() ) {
        return false;
    }
    if ( pRankingHead != pSession->getLocalParticipant() ||
         pParticipant != pRankingHead->getSessionParams().pRankNextPlayer ) {
        return false;
    }
    return pParticipant->getSessionParams().LastCheckpoint ==
           pSession->getLocalParticipant()->getSessionParams().LastCheckpoint;
}

bool GameMechRaceHUD::recordCheckpointTime(
    EntityCheckPoint *pCheckpoint, const CheckpointPassedParam *pParam ) {
    // FUN_0072e6c0
    GameSession *pSession = pMech->getGameSession();
    const float elapsed = pMech->getTimeComponent()->getElapsedTime();
    const uint32_t time = uint32_t( elapsed * kCentisecondsPerSecond );
    const int32_t checkpointIndex = pCheckpoint->getIndex();

    GameParticipant *pParticipant = pSession->findByCarID( pParam->CarID );
    if ( pParticipant == nullptr ) {
        return false;
    }

    const int32_t loopCheckpointIndex =
        int32_t( pEventParameters->getLoopCheckpointIndex().getValue() );
    if ( pCheckpoint->getIndex() == loopCheckpointIndex ) {
        pParticipant->getSessionParams().CurrentLap++;
    }
    const int32_t lap = pParticipant->getSessionParams().CurrentLap;
    int32_t step = checkpointIndex;
    if ( checkpointIndex >= loopCheckpointIndex && lap != 0 ) {
        step = ( lap - 1 ) * checkpointsPerLap + checkpointIndex;
    }
    pParticipant->getSessionParams().LastCheckpoint = step;
    pParticipant->getSessionParams().LastCheckpointTime = time;

    const uint32_t localCarID =
        pSession->getLocalParticipant()->getCarState()->getID();
    if ( pParam->CarID == localCarID ) {
        pCurrentTimes[step - 1] = time;
    }

    RaceSessionMessage message;
    message.Type = kRaceMessageSplit;
    message.ID = 0;
    message.Step = step;
    message.Time = elapsed;
    message.Lap = pParticipant->getSessionParams().CurrentLap;
    pSession->sendRaceMessage( message );

    if ( pRecordTimes == nullptr ) {
        return false;
    }

    if ( pParam->CarID == localCarID ) {
        if ( pCheckpoint->getLEDData()->Type != kLedTypeStart &&
             pMech->getMechType() != GameMechanism::MT_ExtraChallenge &&
             !gpScenaric->isInstantChallenge() ) {
            showCheckpointGap( pRecordTimes[step - 1], time, true );
        }
    } else if ( pMech->getMechType() == GameMechanism::MT_Race &&
                pMech->getMechID() == EventParameters::EST_Solo &&
                isRivalOnSameStep( step, pParticipant,
                                   int32_t( pParam->CarID ) ) ) {
        showBottomGap( int32_t( time - pSession->getLocalParticipant()
                                           ->getSessionParams()
                                           .LastCheckpointTime ) );
    }

    uint32_t &record = pRecordTimes[step - 1];
    if ( record == 0 || time < record ) {
        record = time;
    }
    return false;
}

bool GameMechRaceHUD::onMessage( Entity *pSender,
                                 const int32_t messageID,
                                 void *pParam ) {
    // FUN_0072e8b0
    if ( !bActive ) {
        return false;
    }

    EntityCheckPoint *pCheckpoint = static_cast<EntityCheckPoint *>( pSender );
    const CheckpointPassedParam *pPassed =
        static_cast<const CheckpointPassedParam *>( pParam );
    GameSession *pSession = pMech->getGameSession();

    if ( messageID == GameMechanism::kMsgCheckpointPassed ) {
        GameParticipant *pParticipant =
            pSession->getParticipant( pPassed->PlayerSlot );
        const int32_t currentStep =
            ( pParticipant != nullptr )
                ? pParticipant->getSessionParams().LastCheckpoint
                : 0;
        if ( pPassed->CheckpointIndex != wrapStep( currentStep + 1 ) ) {
            return true;
        }
        if ( pMech->getMechType() == GameMechanism::MT_ExtraChallenge &&
             pPassed->CheckpointIndex == 2 &&
             gpScenaric->getLocalPlayer().getCarState()->getBody()->SpeedInKmh *
                     kMetersPerSecondToKmh >
                 kExtraChallengeMaxStartSpeed ) {
            return true;
        }

        pCheckpoint->addEventReached( pPassed->PlayerSlot );
        pMech->onMessage( pCheckpoint, kMsgCheckpointValidated, pParam );
        return true;
    }

    if ( messageID != kMsgCheckpointValidated ) {
        return false;
    }

    recordCheckpointTime( pCheckpoint, pPassed );

    const uint32_t localCarID =
        pSession->getLocalParticipant()->getCarState()->getID();
    if ( pPassed->CarID == localCarID ) {
        const int32_t lap = getParticipantLap( pPassed->PlayerSlot );
        if ( stepFromIndexAndLap( pPassed->CheckpointIndex, lap ) ==
             numSteps ) {
            endState = 0;
            result = 1;
            resultReason = 2;
        }
        updateObjectives();
        updateCountersHud();
        if ( bShowNextCheckpointOnly ) {
            pCheckpoint->setVisible( false );
        }
    }

    if ( pMech->getCurrentState() == GameMechanism::MS_Running &&
         pCheckpoint->getLEDData()->Type == kLedTypeCheckpoint ) {
        FlashPlayer *pPlayer = GetHudPlayer();
        const int32_t bonusMs =
            int32_t( gpScenaric->getSelectedEventParameters()
                         .getCheckpointBonus()
                         .getValue() );
        if ( bonusMs < 1 ) {
            if ( pPlayer != nullptr ) {
                pPlayer->setVariableValue( "/:IsTimeBonus", 0 );
            }
            return true;
        }

        const float bonus = float( bonusMs ) * kMillisecondsToSeconds;
        pMech->getTimeComponent()->addTimeBonus( bonus );
        if ( pPlayer != nullptr ) {
            pPlayer->setVariableValue( "/:IsTimeBonus", 1 );
            char bonusText[0xc];
            FormatRaceTime( bonusText, bonus );
            pPlayer->setVariableValueNonLinked( "/:TimeBonus", bonusText );
        }
    }
    return true;
}

bool GameMechRaceHUD::onSessionRaceMessage(
    const int32_t playerSlot, const RaceSessionMessage *pMessage ) {
    // FUN_0072e520
    if ( pMessage->Type != kRaceMessageSplit ) {
        return false;
    }
    GameSession *pSession = pMech->getGameSession();
    GameParticipant *pParticipant = pSession->getParticipant( playerSlot );
    if ( pParticipant == nullptr ) {
        return false;
    }

    const uint32_t time = uint32_t( pMessage->Time * kCentisecondsPerSecond );
    const int32_t step = pMessage->Step;
    if ( pRecordTimes != nullptr ) {
        uint32_t &record = pRecordTimes[step - 1];
        if ( record == 0 || time < record ) {
            record = time;
        }
    }
    pParticipant->getSessionParams().LastCheckpoint = step;
    pParticipant->getSessionParams().LastCheckpointTime = time;
    pParticipant->getSessionParams().CurrentLap = pMessage->Lap;
    if ( isRivalOnSameStep( step, pParticipant, playerSlot ) ) {
        showBottomGap( int32_t( time - pSession->getLocalParticipant()
                                           ->getSessionParams()
                                           .LastCheckpointTime ) );
    }
    return true;
}

void GameMechRaceHUD::updateLoopCheckpointTemplate() {
    // FUN_0072f880
    if ( numLaps == 0 ) {
        return;
    }
    EntityCheckPoint *pLoopCheckpoint = findCheckpointForStep(
        int32_t( pEventParameters->getLoopCheckpointIndex().getValue() ) );
    if ( pLoopCheckpoint == nullptr ) {
        return;
    }

    const uint32_t currentType = pLoopCheckpoint->getLEDData()->Type;
    GameSession *pSession = pMech->getGameSession();
    GameParticipant *pLocal =
        pSession->getParticipant( pSession->getLocalParticipantIndex() );
    uint32_t wantedType = kLedTypeStart;
    if ( pLocal == nullptr || pLocal->getSessionParams().CurrentLap == 0 ) {
        wantedType =
            ( pEventParameters->getLoopCheckpointIndex().getValue() == 1 )
                ? kLedTypeStart
                : kLedTypeCheckpoint;
    } else {
        const int32_t lap =
            getParticipantLap( pSession->getLocalParticipantIndex() );
        if ( lap < numLaps ) {
            wantedType = kLedTypeCheckpoint;
        } else if ( lap == numLaps ) {
            wantedType = kLedTypeFinish;
        }
    }

    if ( wantedType == currentType ) {
        return;
    }
    templateSwapTimer += gpTimer->GameDeltaTime; // DAT_00fa3fc8
    if ( templateSwapTimer >= kTemplateSwapDelay ) {
        templateSwapTimer = 0.0f;
        swapCheckpointTemplate( pLoopCheckpoint, wantedType );
    }
}

bool GameMechRaceHUD::onMechStateProcess( const int32_t state,
                                          const float /*deltaTime*/,
                                          const eStateProcess process ) {
    // FUN_0072f9b0
    switch ( state ) {
    case GameMechanism::MS_WaitingStart:
        if ( process == SP_Init ) {
            pRankingHead = nullptr;
        }
        return false;
    case GameMechanism::MS_WaitingHUD:
        if ( process == SP_Term ) {
            updateObjectives();
            return false;
        }
        if ( process != SP_Init ) {
            return false;
        }
        if ( pRecordTimes != nullptr ) {
            const SavedRanking *pRanking =
                gpPlayerData->getLoadedSave()->getRanking();
            const bool bLoaded = pRanking->getSplitTimes(
                pMech->getSpotHashcode(), uint32_t( numSteps ), pRecordTimes );
            if ( !bLoaded || pMech->getOwner()->getGameMechID() ==
                                 EventParameters::EST_Multi ) {
                memset( pRecordTimes, 0,
                        size_t( numSteps ) * sizeof( uint32_t ) );
            }
        }
        if ( pCurrentTimes != nullptr ) {
            memset( pCurrentTimes, 0, size_t( numSteps ) * sizeof( uint32_t ) );
        }
        return false;
    case GameMechanism::MS_Running:
        break;
    default:
        return false;
    }

    switch ( process ) {
    case SP_Init:
        bActive = true;
        return true;
    case SP_Term:
        bActive = false;
        return true;
    case SP_Update: {
        updateLoopCheckpointTemplate();
        const uint32_t numPlayers = uint32_t( sortRanking( true ) );
        assignRanks();
        GameSession *pSession =
            ( pMech != nullptr ) ? pMech->getGameSession() : nullptr;
        if ( pSession != nullptr &&
             pSession->getLocalParticipant() != nullptr ) {
            UpdatePositionHud( GetHudPlayer(),
                               pSession->getLocalParticipant()
                                   ->getSessionParams()
                                   .LastPosition,
                               numPlayers );
        }
        return true;
    }
    default:
        return false;
    }
}

int32_t GameMechRaceHUD::fillResults( FlashPlayer *pPlayer ) {
    // FUN_0072ece0
    char rankText[0x20] = "??";
    int32_t cup = -1;
    int32_t rank = 1;
    int32_t numPlayers = 0;
    posPlayerList[0] = '\0';
    timePlayerList[0] = '\0';
    namePlayerList[0] = '\0';
    posList[0] = '\0';
    timeList[0] = '\0';
    nameList[0] = '\0';

    sortRanking( false );
    GameMechTime *pTime = pMech->getTimeComponent();
    if ( pTime != nullptr ) {
        pTime->setFinalTime( pTime->getRaceTime() ); // +0x44 = +0x40
    }

    if ( gpScenaric->isInstantChallenge() ) {
        OTDU_UNIMPLEMENTED; // TODO: GMMission (instant challenge results:
                            // /:PlayerRanking, /:Message 0x394d632 /
                            // 0x38fcd22, /:Player1-2, /:Time1-2, /:Gap)
        return cup;
    }

    pPlayer->setVariableValueNonLinked( "/:IsPenalty", "0" );
    GameSession *pSession = pMech->getGameSession();
    for ( GameParticipant *pParticipant = pRankingHead; pParticipant != nullptr;
          pParticipant = pParticipant->getSessionParams().pRankNextPlayer,
                          rank++ ) {
        snprintf( rankText, sizeof( rankText ), "%d", rank );
        strcat( posList, rankText );
        strcat( nameList, pParticipant->getName() );
        numPlayers++;

        const bool bFinished =
            pParticipant->getSessionParams().LastCheckpoint == numSteps;
        if ( bFinished ) {
            char timeText[0x20];
            FormatRaceTime(
                timeText,
                float( pParticipant->getSessionParams().LastCheckpointTime ) *
                    kSecondsPerCentisecond );
            strcat( timeList, timeText );
        } else {
            const char *pNetworkName =
                ( pSession->isOnline() && pParticipant->isRemote() )
                    ? gpDatabase->getStringByHashcode(
                          0, kNetworkPlayerNameHashcode )
                    : nullptr;
            strcat( timeList,
                    ( pNetworkName != nullptr ) ? pNetworkName : "--" );
        }
        strcat( nameList, "\n" );
        strcat( timeList, "\n" );
        strcat( posList, "\n" );
        pParticipant->setFinishPosition( bFinished ? rank - 1 : -1 );

        if ( pParticipant == pSession->getLocalParticipant() ) {
            strcat( namePlayerList, pParticipant->getName() );
            if ( bFinished ) {
                EventParameters *pEventParams =
                    pMech->getOwner()->getMissionDescription();
                const bool bPenalties =
                    pEventParams->getOffRoad().getValue() != 0 ||
                    pEventParams->getCollisions().getValue() != 0;
                if ( bPenalties &&
                     pMech->getComponent( GameMechanism::CS_Penalty )
                             ->getResult() == CR_Failure ) {
                    pPlayer->setVariableValueNonLinked( "/:FinalTime",
                                                        kNoTime );
                    cup = 0;
                    pMech->fillChallengeResults( pPlayer, -1, false );
                    pTime->setFinalTime( kNoFinalTime );
                } else if ( cup == -1 ) {
                    pMech->fillChallengeResults( pPlayer, rank, false );
                    cup = ( rank <= 3 ) ? rank : 0;
                    strcat( posPlayerList, rankText );
                    const float finalTime =
                        float( pParticipant->getSessionParams()
                                   .LastCheckpointTime ) *
                        kSecondsPerCentisecond;
                    pTime->setFinalTime( finalTime );
                    char timeText[0x20];
                    FormatRaceTime( timeText, finalTime );
                    pPlayer->setVariableValueNonLinked( "/:BaseTime",
                                                        timeText );
                    pPlayer->setVariableValueNonLinked( "/:FinalTime",
                                                        timeText );
                }
            } else {
                pPlayer->setVariableValueNonLinked( "/:FinalTime", kNoTime );
                cup = 0;
                pMech->fillChallengeResults( pPlayer, -1, false );
                pTime->setFinalTime( kNoFinalTime );
            }
            strcat( namePlayerList, "\n" );
            strcat( timePlayerList, "\n" );
            strcat( posPlayerList, "\n" );
        }
    }

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
