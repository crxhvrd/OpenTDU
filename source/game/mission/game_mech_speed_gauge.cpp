#include "shared.h"
#include "game_mech_speed_gauge.h"

#include "ai/ai_game_session.h"
#include "database/gs_database.h"
#include "debug/ed_debug_text.h"
#include "flash/entities/flash_entity.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/gm_world.h"
#include "game/mng_hud.h"
#include "game/scenaric/event_parameters.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "player_data/gs_playerdata.h"

static constexpr float kNoSpeed = -1.0f;                // FLOAT_00f41664
static constexpr float kSpeedParamScale = 0.01f;        // DAT_00f416d4
static constexpr float kKmhToMs = 0.2777778f;           // DAT_00f41868
static constexpr float kMsToKmh = 3.6f;                 // DAT_00f41768
static constexpr float kMsToMph = 2.2374146f;           // DAT_00f419f4
static constexpr float kKmhToMph = 0.621504f;           // DAT_00f418c0
static constexpr float kGaugeSpeedScale = 0.010285715f; // DAT_00f41fa0
static constexpr float kGaugeCursorTravel = -142.0f;    // DAT_00f41f9c
static constexpr float kGaugeFrameScale = -999.0f;      // DAT_00f41f98
static constexpr uint32_t kSoloMechID = 0xc0c0;
static constexpr uint32_t kCommonStringDB = 0;
static constexpr uint64_t kNoScoreHashcode = 0x3383742ull;
static constexpr int32_t kNumCups = 3;
static constexpr int32_t kCupNone = 0;
static constexpr int32_t kCupGold = 1;
static constexpr int32_t kCupSilver = 2;
static constexpr int32_t kCupBronze = 3;

static inline float GetSpeedUnitScale() {
    return gpPlayerData->isUsingImperialUnits() ? kMsToMph : kMsToKmh;
}

static inline const char *GetSpeedUnitText() {
    return gpDatabase->getCommonDB()
        .CommonStrings[gpPlayerData->isUsingImperialUnits() ? 0 : 1];
}

static inline float GetGaugeRatio( const float speed ) {
    return std::min( speed * kGaugeSpeedScale, 1.0f );
}

static inline float GetSustainedSpeed( const CarState *pCarState ) {
    return pCarState->getBody()->SustainedSpeed;
}

GameMechSpeedGauge::GameMechSpeedGauge( GameMechanism *pOwnerMech )
    : GameMechComponent( pOwnerMech ),
      targetSpeed( 0.0f ),
      field_0x28( 0 ),
      minSpeed( kNoSpeed ),
      maxSpeed( kNoSpeed ),
      instantSpeed( 0.0f ),
      startDistance( 0.0f ),
      distance( 0.0f ),
      elapsedTime( 0.0f ),
      averageSpeed( 0.0f ),
      topSpeed( 0.0f ),
      nameListST{ '\0' },
      totalScoreListST{ '\0' },
      posListST{ '\0' },
      namePlayerList{ '\0' },
      totalScorePlayerList{ '\0' },
      posPlayerList{ '\0' } {
    // Inlined in FUN_00733830
}

FlashPlayer *GameMechSpeedGauge::getHudPlayer() const {
    return pMech->getOwner()->getHudChallenge().getHudPlayer();
}

bool GameMechSpeedGauge::initialize() {
    // FUN_0072a920
    resultReason = 0;
    endState = -1;
    result = CR_None;
    field_0x28 = 0;
    minSpeed = kNoSpeed;
    maxSpeed = kNoSpeed;
    averageSpeed = 0.0f;
    instantSpeed = 0.0f;
    elapsedTime = 0.0f;
    topSpeed = 0.0f;
    targetSpeed = 0.0f;

    if ( pMech != nullptr && pMech->getOwner() != nullptr ) {
        EventParameters *pEventParameters =
            pMech->getOwner()->getMissionDescription();
        if ( pEventParameters->getStartChallenge().getValue() == 0 ) {
            targetSpeed = float( int32_t( pMech->getOwner()
                                              ->getMissionDescription()
                                              ->getGoldSpeed()
                                              .getValue() ) ) *
                          kSpeedParamScale;
        }
    }
    return true;
}

bool GameMechSpeedGauge::setupHUD() {
    // FUN_0072b790
    if ( targetSpeed <= 0.0f ) {
        return true;
    }

    FlashPlayer *pPlayer = getHudPlayer();
    SetClipVisible( pPlayer, "HUD/speed_gauge", true );
    pPlayer->goToFrameAndPlay( 1, "HUD/speed_gauge", false );
    pPlayer->setVariableValueNonLinked( "/:cur_speed", "0" );

    const float ratio = GetGaugeRatio( targetSpeed * kKmhToMs );
    FlashClip clip;
    if ( pPlayer->getClipPlayer( "HUD/speed_gauge/cursor_min", &clip ) ) {
        clip.setProperty( eFlashProperty::FP_Visible, true );
        clip.setProperty( eFlashProperty::FP_Y, ratio * kGaugeCursorTravel );
    }
    SetClipVisible( pPlayer, "HUD/speed_gauge/cursor_max", false );

    const float displayedTarget = gpPlayerData->isUsingImperialUnits()
                                      ? targetSpeed * kKmhToMph
                                      : targetSpeed;
    pPlayer->setVariableValueNonLinked( "/:m_SpeedUnit", GetSpeedUnitText() );
    pPlayer->setVariableValue( "/:speed_min",
                               uint32_t( int32_t( displayedTarget ) ) );
    return true;
}

void GameMechSpeedGauge::printDebug() {
    // FUN_0072a8b0
    gEdDebugText.drawText(
        "SPEED : State: %02d / Min:%.2f / Max:%.2f / Instant:%.2f / "
        "Average:%.2f",
        getResultState(), double( minSpeed ), double( maxSpeed ),
        double( instantSpeed ), double( averageSpeed ) );
}

int32_t GameMechSpeedGauge::fillResults( FlashPlayer *pPlayer ) {
    // FUN_0072a9e0
    posPlayerList[0] = '\0';
    totalScorePlayerList[0] = '\0';
    namePlayerList[0] = '\0';
    posListST[0] = '\0';
    totalScoreListST[0] = '\0';
    nameListST[0] = '\0';

    if ( targetSpeed <= 0.0f ) {
        return -1;
    }

    GameModeWorld *pOwner = pMech->getOwner();
    pPlayer->setVariableValueNonLinked(
        "/:TimeList_ref1",
        pOwner->getMissionDescription()->getGoldSpeed().getDisplayText() );
    pPlayer->setVariableValueNonLinked(
        "/:TimeList_ref2",
        pOwner->getMissionDescription()->getSilverSpeed().getDisplayText() );
    pPlayer->setVariableValueNonLinked(
        "/:TimeList_ref3",
        pOwner->getMissionDescription()->getBronzeSpeed().getDisplayText() );
    pPlayer->setVariableValueNonLinked( "/:IsTraps", "0" );
    pPlayer->setVariableValueNonLinked( "/:IsPenalty", "0" );
    pPlayer->setVariableValueNonLinked( "/:Unit", GetSpeedUnitText() );
    pPlayer->setVariableValueNonLinked( "/:m_SpeedUnit", GetSpeedUnitText() );

    const auto getCupSpeed = []( GameParameterSpeed &parameter ) {
        return float( int32_t( parameter.getValue() ) ) * kSpeedParamScale *
               kKmhToMs;
    };
    const float goldSpeed =
        getCupSpeed( pOwner->getMissionDescription()->getGoldSpeed() );
    const float silverSpeed =
        getCupSpeed( pOwner->getMissionDescription()->getSilverSpeed() );
    const float bronzeSpeed =
        getCupSpeed( pOwner->getMissionDescription()->getBronzeSpeed() );

    int32_t cup = kCupNone;
    if ( maxSpeed >= goldSpeed ) {
        cup = kCupGold;
    } else if ( maxSpeed >= silverSpeed ) {
        cup = kCupSilver;
    } else if ( maxSpeed >= bronzeSpeed ) {
        cup = kCupBronze;
    }

    if ( pOwner->getGameMechID() == kSoloMechID ) {
        char scoreText[32];
        sprintf( scoreText, "%.2f", double( topSpeed * GetSpeedUnitScale() ) );
        pPlayer->setVariableValueNonLinked( "/:FinalScore", scoreText );
        pPlayer->setVariableValueNonLinked( "/:TimePlayerList_ref", " " );
        pPlayer->setVariableValue( "/:nb_players", 1 );
        pPlayer->setVariableValue( "/:WhatCup", uint32_t( cup ) );
        pMech->fillChallengeResults( pPlayer, ( cup == kCupNone ) ? -1 : cup,
                                     true );
        return cup;
    }

    GameSession *pSession = pMech->getGameSession();
    int32_t numPlayers = 0;
    cup = -1;
    for ( GameParticipant *pParticipant = pMech->getScoreRankingHead();
          pParticipant != nullptr && numPlayers < kMaxNumListedPlayers;
          pParticipant = pParticipant->getSessionParams().pRankNextPlayer ) {
        const float score =
            float( pParticipant->getSessionParams().FinalScore ) *
            kSpeedParamScale;
        float displayedScore = 0.0f;
        int32_t finishPosition = -1;
        if ( score > 0.0f ) {
            displayedScore = score * GetSpeedUnitScale();
            if ( displayedScore > 0.0f ) {
                finishPosition = numPlayers;
            }
        }
        numPlayers++;
        pParticipant->setFinishPosition( finishPosition );

        const bool bIsLocal = pParticipant == pSession->getLocalParticipant();
        strcat( nameListST, pParticipant->getName() );
        if ( bIsLocal ) {
            strcat( namePlayerList, pParticipant->getName() );
        }

        char scoreText[64];
        sprintf( scoreText, "%.2f", double( displayedScore ) );
        strcat( totalScoreListST, scoreText );

        char posText[64];
        if ( displayedScore > 0.0f ) {
            sprintf( posText, "%d", numPlayers );
        } else {
            strcpy( posText, "-" );
        }
        strcat( posListST, posText );

        if ( bIsLocal ) {
            strcat( totalScorePlayerList, scoreText );
            strcat( posPlayerList, posText );
            pPlayer->setVariableValueNonLinked( "/:FinalScore", scoreText );
            if ( displayedScore > 0.0f ) {
                cup = ( numPlayers > kNumCups ) ? kCupNone : numPlayers;
                pMech->fillChallengeResults( pPlayer, numPlayers, false );
            } else {
                cup = kCupNone;
                pMech->fillChallengeResults( pPlayer, -1, false );
            }
        }

        strcat( namePlayerList, "\n" );
        strcat( totalScorePlayerList, "\n" );
        strcat( posPlayerList, "\n" );
        strcat( nameListST, "\n" );
        strcat( totalScoreListST, "\n" );
        strcat( posListST, "\n" );
    }

    for ( int32_t i = 0; i < pSession->getCapacity(); i++ ) {
        GameParticipant *pParticipant = pSession->getParticipant( i );
        if ( pParticipant == nullptr || !pParticipant->isActive() ||
             !pParticipant->isRemote() || numPlayers >= kMaxNumListedPlayers ) {
            continue;
        }

        numPlayers++;
        strcat( nameListST, pParticipant->getName() );
        const char *pNoScoreText = gpDatabase->getStringByHashcode(
            kCommonStringDB, kNoScoreHashcode );
        strcat( totalScoreListST,
                ( pNoScoreText != nullptr ) ? pNoScoreText : "--" );
        strcat( posListST, "-" );
        strcat( nameListST, "\n" );
        strcat( totalScoreListST, "\n" );
        strcat( posListST, "\n" );
    }

    pPlayer->setVariableValue( "/:nb_players", numPlayers );
    pPlayer->setVariableValue( "/:NamePlayerList", namePlayerList );
    pPlayer->setVariableValue( "/:TotalScorePlayerList", totalScorePlayerList );
    pPlayer->setVariableValue( "/:PosPlayerList", posPlayerList );
    pPlayer->setVariableValue( "/:TotalScoreListST", totalScoreListST );
    pPlayer->setVariableValue( "/:PosListST", posListST );
    pPlayer->setVariableValue( "/:NameListST", nameListST );
    pPlayer->setVariableValue( "/:WhatCup", uint32_t( cup ) );
    return cup;
}

bool GameMechSpeedGauge::onMechStateProcess( const int32_t state,
                                             const float deltaTime,
                                             const eStateProcess process ) {
    // FUN_0072b4f0
    FlashPlayer *pPlayer = getHudPlayer();
    if ( state != GameMechanism::MS_Running ) {
        return false;
    }

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( process == SP_Init ) {
        startDistance = pLocalCar->getBody()->Distance;
        averageSpeed = 0.0f;
        elapsedTime = 0.0f;
        distance = 0.0f;
        return false;
    }

    if ( process != SP_Update ) {
        return false;
    }

    instantSpeed = 0.0f;
    if ( pLocalCar != nullptr ) {
        instantSpeed = GetSustainedSpeed( pLocalCar );
    }
    const float ratio = GetGaugeRatio( instantSpeed );

    if ( instantSpeed > maxSpeed || maxSpeed == kNoSpeed ) {
        maxSpeed = instantSpeed;
        topSpeed = instantSpeed;
        if ( targetSpeed > 0.0f && result == kResultTargetReached ) {
            FlashClip clip;
            if ( pPlayer->getClipPlayer( "HUD/speed_gauge/cursor_max",
                                         &clip ) ) {
                clip.setProperty( eFlashProperty::FP_Visible, true );
                clip.setProperty( eFlashProperty::FP_Y,
                                  ratio * kGaugeCursorTravel );
            }
            pPlayer->setVariableValue(
                "/:speed_max", uint32_t( maxSpeed * GetSpeedUnitScale() ) );
        }
    }

    if ( minSpeed > instantSpeed || minSpeed == kNoSpeed ) {
        minSpeed = instantSpeed;
    }

    elapsedTime += deltaTime;
    distance = pLocalCar->getBody()->Distance - startDistance;
    averageSpeed = ( elapsedTime > 0.0f ) ? distance / elapsedTime : 0.0f;

    if ( targetSpeed <= 0.0f ) {
        return false;
    }

    pPlayer->goToFrameAndPlay( 1 - int32_t( ratio * kGaugeFrameScale ),
                               "HUD/speed_gauge", false );
    pPlayer->setVariableValue(
        "/:cur_speed",
        uint32_t( int32_t( instantSpeed * GetSpeedUnitScale() ) ) );

    if ( result != kResultTargetReached && result != CR_Success &&
         instantSpeed > targetSpeed * kKmhToMs ) {
        SetClipVisible( pPlayer, "/HUD/endRace", true );
        result = kResultTargetReached;
    }
    return false;
}

void GameMechSpeedGauge::onHudMessage( const FlashMessage &message ) {
    // FUN_0072a870
    if ( message == FlashMessage( "endr" ) ) {
        result = CR_Success;
        resultReason = 1;
    }
}
