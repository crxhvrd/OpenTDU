#include "shared.h"
#include "game_mech_speed_radar.h"

#include "ai/ai_game_session.h"
#include "database/gs_database.h"
#include "debug/ed_debug_text.h"
#include "flash/gs_flash.h"
#include "flash/entities/flash_entity.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/gm_world.h"
#include "game/mission/game_mech_penalty.h"
#include "game/mng_hud.h"
#include "game/scenaric/entity/entity_check_point.h"
#include "game/scenaric/event_parameters.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/mng_actor.h"
#include "game/vehicle/car_state.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_ranking.h"

static constexpr float kMetersPerSecondToKmh = 3.6f;        // DAT_00f41768
static constexpr float kMetersPerSecondToMph = 2.2374146f;  // DAT_00f419f4
static constexpr float kKmhToMetersPerSecond = 0.27777779f; // DAT_00f41868
static constexpr float kMetersToMillimeters = 1000.0f;      // DAT_00f41718
static constexpr float kMillimetersToMeters = 0.001f;       // FLOAT_00f4167c
static constexpr float kSpeedParameterScale = 0.01f;        // DAT_00f416d4
static constexpr float kPenaltyScoreScale = 0.1f;           // FLOAT_00f41668
static constexpr uint32_t kCurrentSpeedBase = 900000;
static constexpr uint32_t kLedTypeStart = 1;
static constexpr uint32_t kLedTypeRadar = 4;
static constexpr int32_t kMaxResultPlayers = 16;
static constexpr int32_t kMsgRadarPassed = 5;
static constexpr int32_t kRaceMessageRadarSpeed = 4;
static constexpr int32_t kRaceMessageScore = 5;
static constexpr uint64_t kNetworkPlayerNameHashcode = 0x3383742;

static FlashPlayer *GetHudPlayer() {
    return ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;
}

static float GetSpeedUnitScale() {
    return gpPlayerData->isUsingImperialUnits() ? kMetersPerSecondToMph
                                                : kMetersPerSecondToKmh;
}

static float GetLocalCarTrapSpeed() {
    return gpScenaric->getLocalPlayer()
        .getCarState()
        ->getBody()
        ->ShortSustainedSpeed;
}

static float DecodeCurrentSpeed( const uint32_t encodedSpeed ) {
    return ( encodedSpeed != 0 ) ? float( kCurrentSpeedBase - encodedSpeed )
                                 : 0.0f;
}

static void SetClipDisplayFlag( FlashPlayer *pPlayer,
                                const char *pClipPath,
                                const bool bSet ) {
    FlashClip clip;
    if ( !pPlayer->getClipPlayer( pClipPath, &clip ) ) {
        return;
    }

    ScopedMutexLock lock = gpFlash->scopedLock();
    if ( bSet ) {
        clip.pSprite->getDisplayList()->getFlagsRW() |= 1;
    } else {
        clip.pSprite->getDisplayList()->getFlagsRW() &= ~1u;
    }
}

GameMechSpeedRadar::GameMechSpeedRadar( GameMechanism *pOwnerMech )
    : GameMechRaceHUD( pOwnerMech ),
      numRadarsPassed( 0 ),
      numRadars( 0 ),
      baseScore( 0 ),
      finalScore( 0 ),
      bHasStartCheckpoint( false ),
      penaltyScore( 0.0f ) {
    // FUN_0072bc80
}

bool GameMechSpeedRadar::initialize() {
    // FUN_0072bc10
    resetCheckpoints();
    numRadarsPassed = 0;
    bHasStartCheckpoint = false;
    numRadars = 0;
    baseScore = 0;
    penaltyScore = 0.0f;
    return true;
}

void GameMechSpeedRadar::reset() {
    // FUN_0072bc00
    pRankingHead = nullptr;
}

void GameMechSpeedRadar::printDebug() {
    // FUN_0072bab0
    gEdDebugText.drawText( "TRAPS : State: %02d / NbTraps: %d",
                           getResultState(), numRadars );
}

bool GameMechSpeedRadar::setupHUD() {
    // FUN_0072cb90
    FlashPlayer *pPlayer = GetHudPlayer();
    SetClipDisplayFlag( pPlayer, "/HUD/radars", true );

    const CommonDB &commonDB = gpDatabase->getCommonDB();
    pPlayer->setVariableValueNonLinked( "/:m_SpeedUnit",
                                        gpPlayerData->isUsingImperialUnits()
                                            ? commonDB.CommonStrings[0]
                                            : commonDB.CommonStrings[1] );
    return true;
}

void GameMechSpeedRadar::updateObjectives() {
    // FUN_0072bad0
    int32_t markerIndex = 0;
    int32_t actorIndex = 0;
    for ( ; actorIndex < pMngActor->getActorCount(); actorIndex++ ) {
        Entity *pActor = pMngActor->getActor( actorIndex );
        if ( pActor->getType() != eEntityType::ET_CHPT ) {
            continue;
        }
        EntityCheckPoint *pCheckpoint =
            static_cast<EntityCheckPoint *>( pActor );
        if ( pCheckpoint->getLEDData()->Type != kLedTypeRadar ) {
            continue;
        }

        const char *pType =
            ( pCheckpoint->getPassedData() > 0.0f ) ? "radar_off" : "radar_on";
        gpMngHud->setObjectiveMarker(
            markerIndex, pCheckpoint->getModelMatrix().row( 3 ).transpose(),
            pType );
        markerIndex++;
    }

    for ( int32_t i = actorIndex; i < MngHud::kMaxObjectiveMarkers; i++ ) {
        gpMngHud->setObjectiveMarkerType( i, "" );
    }
}

uint32_t GameMechSpeedRadar::computeScore() {
    // FUN_0072b990
    float sum = 0.0f;
    for ( int32_t i = 0; i < numRadars; i++ ) {
        sum += DecodeCurrentSpeed( pCurrentTimes[i] );
    }

    baseScore = ( numRadars != 0 )
                    ? uint32_t( int64_t( sum / float( numRadars ) ) )
                    : 0;

    EventParameters *pEventParams = pMech->getOwner()->getMissionDescription();
    if ( pEventParams->getOffRoad().getValue() == 0 &&
         pEventParams->getCollisions().getValue() == 0 ) {
        penaltyScore = 0.0f;
        return baseScore;
    }

    const GameMechPenalty *pPenalty = static_cast<const GameMechPenalty *>(
        pMech->getComponent( GameMechanism::CS_Penalty ) );
    if ( pPenalty->getResult() == CR_Failure ) {
        return 0;
    }

    penaltyScore =
        pPenalty->getPenaltyRatio() * kPenaltyScoreScale * float( baseScore );
    return baseScore - uint32_t( int64_t( penaltyScore ) );
}

void GameMechSpeedRadar::sendScore( const uint32_t score ) {
    // FUN_0072bc50
    RaceSessionMessage message;
    message.Type = kRaceMessageScore;
    message.ID = 0;
    message.Step = int32_t( score );
    pMech->getGameSession()->sendRaceMessage( message );
}

void GameMechSpeedRadar::showRadarResult( const char *pPosText,
                                          const char *pFrameLabel,
                                          const int32_t radarIndex,
                                          const float displaySpeed ) {
    // FUN_0072c0d0
    FlashPlayer *pPlayer = GetHudPlayer();
    char speedText[0x20] = "--";
    if ( displaySpeed != 0.0f ) {
        snprintf( speedText, sizeof( speedText ), "%d",
                  int32_t( displaySpeed ) );
    }

    char path[0x40];
    snprintf( path, sizeof( path ), "/HUD/radars/%d:Radar", radarIndex );
    pPlayer->setVariableValueNonLinked( path, speedText );
    snprintf( path, sizeof( path ), "/HUD/radars/%d:PosRadar", radarIndex );
    pPlayer->setVariableValueNonLinked( path, pPosText );
    snprintf( path, sizeof( path ), "/HUD/radars/%d", radarIndex );
    pPlayer->showFrame( pFrameLabel, path, false );
}

bool GameMechSpeedRadar::onSessionRaceMessage(
    const int32_t playerSlot, const RaceSessionMessage *pMessage ) {
    // FUN_0072c1c0
    if ( pMessage->Type == kRaceMessageScore ) {
        GameParticipant *pParticipant =
            pMech->getGameSession()->getParticipant( playerSlot );
        pParticipant->getSessionParams().LastCheckpointTime =
            uint32_t( pMessage->Step );
        return true;
    }
    if ( pMessage->Type != kRaceMessageRadarSpeed ) {
        return false;
    }

    const uint32_t speed =
        uint32_t( int64_t( pMessage->Time * kMetersToMillimeters ) );
    const int32_t radarIndex = pMessage->Step;
    uint32_t &record = pRecordTimes[radarIndex - 1];
    if ( record != 0 && speed <= record ) {
        return true;
    }

    const float unitScale = GetSpeedUnitScale();
    char posText[0x20];
    snprintf( posText, sizeof( posText ), "(%d)",
              int32_t( float( speed ) * kMillimetersToMeters * unitScale ) );
    record = speed;

    const uint32_t current = pCurrentTimes[radarIndex - 1];
    if ( current == 0 ) {
        showRadarResult( posText, "default", radarIndex, 0.0f );
    } else {
        showRadarResult( posText, "bad", radarIndex,
                         float( kCurrentSpeedBase - current ) *
                             kMillimetersToMeters * unitScale );
    }
    return true;
}

bool GameMechSpeedRadar::onRadarPassed( EntityCheckPoint *pCheckpoint ) {
    // FUN_0072c340
    if ( pCheckpoint->getLEDData()->Type != kLedTypeRadar ) {
        return false;
    }

    int32_t radarIndex = pCheckpoint->getIndex();
    if ( bHasStartCheckpoint ) {
        radarIndex--;
    }

    if ( radarIndex < 1 || radarIndex > numRadars ) {
        return false;
    }

    FlashPlayer *pPlayer = GetHudPlayer();
    const float speed = GetLocalCarTrapSpeed();
    const float displaySpeed = speed * GetSpeedUnitScale();
    const uint32_t record = pRecordTimes[radarIndex - 1];
    uint32_t &current = pCurrentTimes[radarIndex - 1];
    const int32_t trapSpeed = int32_t(
        pMech->getOwner()->getMissionDescription()->getTrapSpeed().getValue() );

    if ( speed < float( trapSpeed / 100 ) * kKmhToMetersPerSecond ) {
        if ( current == 0 ) {
            char path[0x20];
            snprintf( path, sizeof( path ), "/HUD/radars/%d", radarIndex );
            pPlayer->showFrame( "bad", path, false );
        }
    } else {
        char flashOK[0x20] = "0";
        if ( current == 0 ) {
            numRadarsPassed++;
            if ( numRadarsPassed >= numRadars ) {
                SetClipVisible( pPlayer, "/HUD/endRace", true );
            }
        }

        if ( speed > DecodeCurrentSpeed( current ) * kMillimetersToMeters ) {
            const uint32_t speedMm =
                uint32_t( int64_t( speed * kMetersToMillimeters ) );
            current = kCurrentSpeedBase - speedMm;
            pCheckpoint->setPassedData( speed );

            RaceSessionMessage message;
            message.Type = kRaceMessageRadarSpeed;
            message.ID = 0;
            message.Step = radarIndex;
            message.Time = speed;
            pMech->getGameSession()->sendRaceMessage( message );

            const float unitScale = GetSpeedUnitScale();
            char posText[0x20];
            if ( record == 0 ) {
                flashOK[0] = '1';
                showRadarResult( "(--)", "good", radarIndex, displaySpeed );
                pRecordTimes[radarIndex - 1] = speedMm;
            } else if ( speedMm > record ) {
                flashOK[0] = '1';
                if ( pMech->getMechID() == EventParameters::EST_Solo ) {
                    snprintf( posText, sizeof( posText ), "(%d)",
                              int32_t( float( record ) * kMillimetersToMeters *
                                       unitScale ) );
                    showRadarResult( posText, "good", radarIndex,
                                     displaySpeed );
                } else {
                    showRadarResult( "(--)", "good", radarIndex, displaySpeed );
                }
                pRecordTimes[radarIndex - 1] = speedMm;
            } else {
                snprintf( posText, sizeof( posText ), "(%d)",
                          int32_t( float( record ) * kMillimetersToMeters *
                                   unitScale ) );
                showRadarResult( posText, "bad", radarIndex, displaySpeed );
            }
        }

        pPlayer->setVariableValueNonLinked( "/:IsFlashOK", flashOK );
        pPlayer->showFrame( "on", "/HUD/flash", true );
    }

    float sum = 0.0f;
    for ( int32_t i = 0; i < numRadars; i++ ) {
        sum += DecodeCurrentSpeed( pCurrentTimes[i] );
    }
    char scoreText[0x20];
    snprintf( scoreText, sizeof( scoreText ), "%.2f",
              double( sum / float( numRadars ) * kMillimetersToMeters *
                      GetSpeedUnitScale() ) );
    pPlayer->setVariableValueNonLinked( "/:Score", scoreText );

    updateObjectives();
    return true;
}

bool GameMechSpeedRadar::onMessage( Entity *pSender,
                                    const int32_t messageID,
                                    void *pParam ) {
    // FUN_0072c830
    if ( messageID == GameMechanism::kMsgCheckpointPassed ) {
        pMech->onMessage( pSender, kMsgRadarPassed, pParam );
        return true;
    }
    if ( messageID != kMsgRadarPassed ) {
        return false;
    }

    onRadarPassed( static_cast<EntityCheckPoint *>( pSender ) );
    return true;
}

void GameMechSpeedRadar::setupRadarHud() {
    // FUN_0072c870
    FlashPlayer *pPlayer = GetHudPlayer();
    SetClipDisplayFlag( pPlayer, "/HUD/endRace", false );

    char text[0x20];
    snprintf( text, sizeof( text ), "%d", numRadars );
    pPlayer->setVariableValueNonLinked( "/:NbRadars", text );

    const float unitScale = GetSpeedUnitScale();
    for ( int32_t radar = 1; radar <= kMaxNumRadars; radar++ ) {
        char path[0x40];
        snprintf( path, sizeof( path ), "/HUD/radars/%d", radar );
        FlashClip clip;
        if ( !pPlayer->getClipPlayer( path, &clip ) ) {
            continue;
        }

        if ( radar <= numRadars ) {
            SetClipDisplayFlag( pPlayer, path, true );
            pPlayer->showFrame( "default", path, false );
        } else {
            SetClipDisplayFlag( pPlayer, path, false );
        }

        snprintf( path, sizeof( path ), "/HUD/radars/%d:Radar", radar );
        pPlayer->setVariableValueNonLinked( path, "--" );

        snprintf( path, sizeof( path ), "/HUD/radars/%d:PosRadar", radar );

        const uint32_t record =
            ( radar <= numRadars ) ? pRecordTimes[radar - 1] : 0;
        if ( pMech->getMechID() == EventParameters::EST_Solo && record != 0 ) {
            snprintf(
                text, sizeof( text ), "(%d)",
                int32_t( float( record ) * kMillimetersToMeters * unitScale ) );
            pPlayer->setVariableValueNonLinked( path, text );
        } else {
            pPlayer->setVariableValueNonLinked( path, "(-)" );
        }
    }
}

int32_t GameMechSpeedRadar::fillRadarResults( FlashPlayer *pPlayer ) {
    // FUN_0072bcd0
    const float unitScale = GetSpeedUnitScale();
    for ( int32_t i = 0; i < kMaxNumRadars; i++ ) {
        char clipPath[0x40];
        char radarPath[0x40];
        char posPath[0x40];
        snprintf( clipPath, sizeof( clipPath ), "/cases/results/radars/%d",
                  i + 1 );
        SetClipDisplayFlag( pPlayer, clipPath, i < numRadars );
        snprintf( radarPath, sizeof( radarPath ),
                  "/cases/results/radars/%d:Radar", i + 1 );
        snprintf( posPath, sizeof( posPath ),
                  "/cases/results/radars/%d:PosRadar", i + 1 );

        if ( i >= numRadars ) {
            pPlayer->setVariableValueNonLinked( radarPath, " " );
            pPlayer->setVariableValueNonLinked( posPath, " " );
            continue;
        }

        const float current = DecodeCurrentSpeed( pCurrentTimes[i] ) *
                              kMillimetersToMeters * unitScale;
        const float record =
            float( pRecordTimes[i] ) * kMillimetersToMeters * unitScale;
        char text[0x40];
        if ( current >= record ) {
            pPlayer->showFrame( "good", clipPath, false );
            strcpy( text, " " );
        } else {
            pPlayer->showFrame( "bad", clipPath, false );
            snprintf( text, sizeof( text ), "(%0.2f)", double( record ) );
        }
        pPlayer->setVariableValueNonLinked( posPath, text );
        snprintf( text, sizeof( text ), "%0.2f", double( current ) );
        pPlayer->setVariableValueNonLinked( radarPath, text );
    }
    return numRadars;
}

int32_t GameMechSpeedRadar::fillResults( FlashPlayer *pPlayer ) {
    // FUN_0072cc50
    posPlayerList[0] = '\0';
    timePlayerList[0] = '\0';
    namePlayerList[0] = '\0';
    posList[0] = '\0';
    timeList[0] = '\0';
    nameList[0] = '\0';
    int32_t cup = -1;
    bool bPenalties = false;

    pPlayer->setVariableValueNonLinked( "/:IsTraps", "1" );
    GameModeWorld *pOwner = pMech->getOwner();
    EventParameters *pEventParams = pOwner->getMissionDescription();
    const bool bSolo = pOwner->getGameMechID() == EventParameters::EST_Solo;
    if ( ( pEventParams->getOffRoad().getValue() != 0 ||
           pEventParams->getCollisions().getValue() != 0 ) &&
         bSolo ) {
        bPenalties = true;
        const GameMechComponent *pPenalty =
            pMech->getComponent( GameMechanism::CS_Penalty );
        if ( pPenalty->getResult() == CR_Failure ) {
            pPlayer->setVariableValueNonLinked( "/:IsPenalty", "0" );
            cup = 0;
        } else {
            pPlayer->setVariableValueNonLinked( "/:IsPenalty", "1" );
        }
    } else {
        pPlayer->setVariableValueNonLinked( "/:IsPenalty", "0" );
    }

    fillRadarResults( pPlayer );

    const float unitScale = GetSpeedUnitScale();
    char text[0x40];
    if ( bSolo ) {
        if ( bPenalties && cup == 0 ) {
            pPlayer->setVariableValueNonLinked( "/:FinalScore", "--" );
            pMech->fillChallengeResults( pPlayer, -1, true );
            finalScore = 0;
        } else {
            if ( bPenalties ) {
                snprintf( text, sizeof( text ), "%.2f",
                          double( float( baseScore ) * kMillimetersToMeters *
                                  unitScale ) );
                pPlayer->setVariableValueNonLinked( "/:BaseScore", text );

                const int32_t penalty =
                    std::max( int32_t( baseScore - finalScore ), 0 );
                snprintf( text, sizeof( text ), "%.2f",
                          double( float( penalty ) * kMillimetersToMeters *
                                  unitScale ) );
                pPlayer->setVariableValueNonLinked( "/:Penalty", text );
            }
            snprintf( text, sizeof( text ), "%.2f",
                      double( float( finalScore ) * kMillimetersToMeters *
                              unitScale ) );
            pPlayer->setVariableValueNonLinked( "/:FinalScore", text );
        }

        pPlayer->setVariableValueNonLinked(
            "/:TimeList_ref1", pEventParams->getGoldSpeed().getDisplayText() );
        pPlayer->setVariableValueNonLinked(
            "/:TimeList_ref2",
            pEventParams->getSilverSpeed().getDisplayText() );
        pPlayer->setVariableValueNonLinked(
            "/:TimeList_ref3",
            pEventParams->getBronzeSpeed().getDisplayText() );
        pPlayer->setVariableValueNonLinked( "/:TimePlayerList_ref", " " );

        if ( cup == -1 ) {
            const float goldSpeed =
                float( int32_t( pEventParams->getGoldSpeed().getValue() ) ) *
                kSpeedParameterScale * kKmhToMetersPerSecond;
            const float silverSpeed =
                float( int32_t( pEventParams->getSilverSpeed().getValue() ) ) *
                kSpeedParameterScale * kKmhToMetersPerSecond;
            const float bronzeSpeed =
                float( int32_t( pEventParams->getBronzeSpeed().getValue() ) ) *
                kSpeedParameterScale * kKmhToMetersPerSecond;
            const float score = getFinalScore();
            int32_t rank = -1;
            if ( score >= goldSpeed ) {
                cup = rank = 1;
            } else if ( score >= silverSpeed ) {
                cup = rank = 2;
            } else if ( score >= bronzeSpeed ) {
                cup = rank = 3;
            } else {
                cup = 0;
            }
            pMech->fillChallengeResults( pPlayer, rank, true );
        }

        pPlayer->setVariableValue( "/:WhatCup", uint32_t( cup ) );
        return cup;
    }

    GameSession *pSession = pMech->getGameSession();
    int32_t numPlayers = 0;
    int32_t playerCup = -1;
    char rankText[0x40];

    for ( GameParticipant *pParticipant = pMech->getScoreRankingHead();
          pParticipant != nullptr;
          pParticipant = pParticipant->getSessionParams().pRankNextPlayer ) {
        pParticipant->setFinishPosition( numPlayers );
        numPlayers++;
        const bool bLocal = pParticipant == pSession->getLocalParticipant();
        const char *pName = pParticipant->getName();
        strcat( nameList, pName );
        snprintf( text, sizeof( text ), "%.2f",
                  double( float( pParticipant->getSessionParams().FinalScore ) *
                          kMillimetersToMeters * unitScale ) );
        strcat( timeList, text );
        snprintf( rankText, sizeof( rankText ), "%d", numPlayers );
        strcat( posList, rankText );

        if ( bLocal ) {
            strcat( namePlayerList, pParticipant->getName() );
            strcat( timePlayerList, text );
            strcat( posPlayerList, rankText );
            pPlayer->setVariableValueNonLinked( "/:FinalScore", text );
            playerCup = ( numPlayers > 3 ) ? 0 : numPlayers;
            pMech->fillChallengeResults( pPlayer, numPlayers, false );
        }

        strcat( namePlayerList, "\n" );
        strcat( timePlayerList, "\n" );
        strcat( posPlayerList, "\n" );
        strcat( nameList, "\n" );
        strcat( timeList, "\n" );
        strcat( posList, "\n" );
    }

    for ( int32_t i = 0; i < pSession->getCapacity(); i++ ) {
        GameParticipant *pParticipant = pSession->getParticipant( i );
        if ( pParticipant == nullptr || !pParticipant->isActive() ||
             !pParticipant->isRemote() || numPlayers >= kMaxResultPlayers ) {
            continue;
        }
        numPlayers++;
        strcat( nameList, pParticipant->getName() );
        const char *pNetworkName =
            gpDatabase->getStringByHashcode( 0, kNetworkPlayerNameHashcode );
        strcat( timeList, ( pNetworkName != nullptr ) ? pNetworkName : "--" );
        strcat( posList, "-" );
        strcat( nameList, "\n" );
        strcat( timeList, "\n" );
        strcat( posList, "\n" );
    }

    pPlayer->setVariableValue( "/:nb_players", numPlayers );
    pPlayer->setVariableValue( "/:PosListST", posList );
    pPlayer->setVariableValue( "/:NameListST", nameList );
    pPlayer->setVariableValue( "/:TotalScoreListST", timeList );
    pPlayer->setVariableValue( "/:PosPlayerList", posPlayerList );
    pPlayer->setVariableValue( "/:NamePlayerList", namePlayerList );
    pPlayer->setVariableValue( "/:TotalScorePlayerList", timePlayerList );
    pPlayer->setVariableValue( "/:WhatCup", uint32_t( playerCup ) );

    return cup;
}

bool GameMechSpeedRadar::onMechStateProcess( const int32_t state,
                                             const float /*deltaTime*/,
                                             const eStateProcess process ) {
    // FUN_0072d870
    switch ( state ) {
    case GameMechanism::MS_WaitingStart:
        if ( process == SP_Init ) {
            pRankingHead = nullptr;
        }
        return false;
    case GameMechanism::MS_Running:
        if ( process != SP_Term ) {
            return false;
        }
        finalScore = computeScore();
        if ( GameParticipant *pLocal =
                 pMech->getGameSession()->getLocalParticipant() ) {
            pLocal->getSessionParams().LastCheckpointTime = finalScore;
            sendScore( finalScore );
        }
        return true;
    case GameMechanism::MS_WaitingHUD:
        break;
    default:
        return false;
    }

    if ( process == SP_Term ) {
        updateObjectives();
        setupRadarHud();
        return false;
    }
    if ( process != SP_Init ) {
        return false;
    }

    bHasStartCheckpoint = false;
    numRadarsPassed = 0;
    finalScore = 0;
    baseScore = 0;
    penaltyScore = 0.0f;
    numRadars = 0;
    for ( int32_t i = 0; i < pMngActor->getActorCount(); i++ ) {
        Entity *pActor = pMngActor->getActor( i );
        if ( pActor->getType() != eEntityType::ET_CHPT ) {
            continue;
        }
        const uint32_t ledType =
            static_cast<EntityCheckPoint *>( pActor )->getLEDData()->Type;
        if ( ledType == kLedTypeRadar ) {
            numRadars++;
        } else if ( ledType == kLedTypeStart ) {
            bHasStartCheckpoint = true;
        }
    }
    numRadars = std::min( numRadars, kMaxNumRadars );

    const size_t bufferSize = size_t( numRadars ) * sizeof( uint32_t );
    pRecordTimes = static_cast<uint32_t *>(
        TestDrive::AllocAligned( bufferSize, alignof( uint32_t ) ) );
    memset( pRecordTimes, 0, bufferSize );
    gpPlayerData->getLoadedSave()->getRanking()->getSplitTimes(
        pMech->getSpotHashcode(), uint32_t( numRadars ), pRecordTimes );
    pCurrentTimes = static_cast<uint32_t *>(
        TestDrive::AllocAligned( bufferSize, alignof( uint32_t ) ) );
    memset( pCurrentTimes, 0, bufferSize );
    return false;
}

void GameMechSpeedRadar::onHudMessage( const FlashMessage &message ) {
    // FUN_0072b950
    if ( message == FlashMessage( "endr" ) ) {
        result = CR_Success;
        resultReason = 1;
    }
}
