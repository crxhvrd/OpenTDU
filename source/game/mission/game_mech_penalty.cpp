#include "shared.h"
#include "game_mech_penalty.h"

#include "ai/ai_vehicle.h"
#include "core/format.h"
#include "debug/ed_debug_text.h"
#include "flash/implementation/flash_player.h"
#include "game/game_object.h"
#include "game/gm_world.h"
#include "game/mng_hud.h"
#include "game/police_state.h"
#include "game/scenaric/entity/entity_convoy.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "audio/implementation/ed_audio_stream_entry.h"

static constexpr float kDefaultMaxPenalty = 100.0f;     // DAT_00f41708
static constexpr float kOffRoadPenaltyRate = 6.0f;      // DAT_00f41840
static constexpr float kOffRoadEntryPenalty = 9.0f;     // DAT_00f4192c
static constexpr float kCollisionPenalty = 20.0f;       // FLOAT_00f03ee0
static constexpr float kPendingPenaltyThreshold = 1.0f; // FLOAT_00f4165c
static constexpr float kPendingDisplayInterval = 1.0f;  // FLOAT_00f4165c
static constexpr float kGaugeNumFrames = 1000.0f;       // DAT_00f41718
static constexpr int32_t kGaugeMaxFrame = 1000;
static constexpr int32_t kNumWheels = 4;
static constexpr int32_t kNumWheelsPartialOffRoad = 3;
static constexpr int32_t kPartialOffRoadGraceFrames = 5;
static constexpr int32_t kResultReasonPenalty = 1;

GameMechPenalty::GameMechPenalty( GameMechanism *pOwnerMech )
    : GameMechComponent( pOwnerMech ),
      partialOffRoadRate( 0.0f ),
      fullOffRoadRate( 0.0f ),
      penalty( 0.0f ),
      maxPenalty( kDefaultMaxPenalty ),
      offRoadEntryPenalty( 0.0f ),
      penaltyFlags( kPenaltyDefaultFlags ),
      pendingPenalty( 0.0f ),
      pendingDisplayTimer( 0.0f ),
      pendingMessageSlot( 0 ),
      bPlayWarningSound( false ),
      bWarningSoundPlaying( false ),
      displayMode( DM_Score ),
      offRoadFrameCount( 0 ) {
    // FUN_00733830 (inlined)
}

bool GameMechPenalty::stateProcess( const int32_t state,
                                    const float deltaTime,
                                    const eStateProcess process ) {
    // FUN_00729ed0
    switch ( state ) {
    case PS_OffRoad:
        if ( process == SP_Init ) {
            addPenalty( offRoadEntryPenalty );
        } else if ( process == SP_Update ) {
            offRoadFrameCount++;
            CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
            if ( pCarState == nullptr ||
                 pCarState->getPoliceState().isArrested() ) {
                return false;
            }

            const CarBodyState *pBody = pCarState->getBody();
            addPenalty( fullOffRoadRate * deltaTime );
            if ( pBody->NumWheelsGrounded == pBody->NumWheelsOnRoad ) {
                stateChange( PS_OnRoad );
            }
        }
        return false;

    case PS_OnRoad: {
        if ( process == SP_Init ) {
            offRoadFrameCount = 0;
            return false;
        }
        if ( process != SP_Update ) {
            return false;
        }

        CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
        if ( pCarState == nullptr ) {
            offRoadFrameCount = 0;
            return false;
        }

        const CarBodyState *pBody = pCarState->getBody();
        int32_t numWheelsOnRoad = pBody->NumWheelsOnRoad;
        const int32_t numWheelsGrounded = pBody->NumWheelsGrounded;
        if ( numWheelsOnRoad == numWheelsGrounded &&
             numWheelsOnRoad == kNumWheels &&
             !pCarState->getPoliceState().isArrested() ) {
            const ObjectWorldLocation &location = pCarState->getWorldLocation();
            if ( location.LocationType != ObjectWorldLocation::OWL_Road &&
                 location.LocationType != ObjectWorldLocation::OWL_Junction &&
                 !location.bNearJunction ) {
                numWheelsOnRoad = kNumWheelsPartialOffRoad;
            }
        }

        if ( numWheelsGrounded < 1 ) {
            offRoadFrameCount = 0;
            return false;
        }
        if ( numWheelsOnRoad == 0 ) {
            stateChange( PS_OffRoad );
            return false;
        }
        if ( numWheelsOnRoad >= numWheelsGrounded ) {
            return false;
        }

        offRoadFrameCount++;
        if ( offRoadFrameCount >= kPartialOffRoadGraceFrames ) {
            addPenalty( partialOffRoadRate * deltaTime );
        }
        return false;
    }

    default:
        return false;
    }
}

bool GameMechPenalty::initialize() {
    // FUN_0072a070
    resultReason = 0;
    result = CR_None;
    partialOffRoadRate = kOffRoadPenaltyRate;
    fullOffRoadRate = kOffRoadPenaltyRate;
    bPlayWarningSound = false;
    bWarningSoundPlaying = false;
    endState = -1;
    penalty = 0.0f;
    offRoadEntryPenalty = kOffRoadEntryPenalty;
    pendingPenalty = 0.0f;
    stateChange( PS_OnRoad );
    return true;
}

bool GameMechPenalty::setupHUD() {
    // FUN_0072a800
    SetClipVisible( pMech->getOwner()->getHudChallenge().getHudPlayer(),
                    "/HUD/driving", true );
    displayPenalty();
    return true;
}

void GameMechPenalty::reset() {
    // FUN_0072a170
    CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
    if ( pCarState != nullptr ) {
        pCarState->addCollisionCallback( &OnCollision, this );
    }
}

void GameMechPenalty::draw() {
    // FUN_0072a710
    displayPenalty();
}

void GameMechPenalty::pause() {
    // FUN_0072a130
    CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
    if ( pCarState != nullptr ) {
        pCarState->removeCollisionCallback( &OnCollision );
    }
}

void GameMechPenalty::printDebug() {
    // FUN_00729ea0
    gEdDebugText.drawText( "OFF_ROAD : State: %02d / Penalty:%.2f/%.2f",
                           getResultState(), double( penalty ),
                           double( maxPenalty ) );
}

bool GameMechPenalty::onMechStateProcess( const int32_t state,
                                          const float deltaTime,
                                          const eStateProcess process ) {
    // FUN_0072a720
    if ( state != GameMechanism::MS_Running ) {
        return false;
    }

    switch ( process ) {
    case SP_Init:
        penalty = 0.0f;
        return false;

    case SP_Update: {
        CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
        if ( pCarState != nullptr &&
             pCarState->getPoliceState().isArrested() ) {
            updateWarningSound();
            return true;
        }

        if ( ( penaltyFlags & kPenaltyOffRoad ) != 0 ) {
            // FUN_004257b0
            stateProcess( curState, deltaTime, SP_Update );
        }
        updatePendingPenalty( deltaTime );
        updateWarningSound();

        if ( penalty >= maxPenalty ) {
            penalty = maxPenalty;
            result = CR_Failure;
            resultReason = kResultReasonPenalty;
            endState = 1;
            return false;
        }
        endState = 0;
        return false;
    }

    case SP_Term:
        if ( bWarningSoundPlaying &&
             pMech->getOffroadWarningEntry() != nullptr ) {
            pMech->getOffroadWarningEntry()->stop( 0.0f );
        }
        return false;

    default:
        return false;
    }
}

void GameMechPenalty::OnCollision( uint8_t /*collisionType*/,
                                   CarState *pOther,
                                   void * /*pOtherObject*/,
                                   void *pUserData ) {
    // FUN_0072a0c0
    if ( pOther != nullptr ) {
        bool bIgnore = false;
        const AiVehicle *pAiVehicle =
            static_cast<const AiVehicle *>( pOther->getAIController() );
        if ( pAiVehicle != nullptr ) {
            const eAiVehicleCategory category = pAiVehicle->getCategory();
            if ( category == eAiVehicleCategory::AVC_Opponent ) {
                bIgnore = true;
            }
            if ( category == eAiVehicleCategory::AVC_Bot ) {
                bIgnore = true;
            }
        }

        if ( bIgnore ) {
            return;
        }
    }

    GameMechPenalty *pPenalty = static_cast<GameMechPenalty *>( pUserData );
    if ( ( pPenalty->penaltyFlags & kPenaltyCollisions ) != 0 ) {
        pPenalty->addPenalty( kCollisionPenalty );
    }
}

void GameMechPenalty::addPenalty( const float amount ) {
    penalty += amount;
    pendingPenalty += amount;
    if ( amount > 0.0f ) {
        bPlayWarningSound = true;
    }
}

void GameMechPenalty::updateWarningSound() {
    // FUN_00729de0
    edAudioStreamEntry *pEntry = pMech->getOffroadWarningEntry();
    if ( pEntry != nullptr ) {
        if ( bPlayWarningSound && !bWarningSoundPlaying ) {
            // FUN_00613620
            pEntry->setLoopCount( -1 );
            pEntry->play( 0.0f );
            bPlayWarningSound = false;
            bWarningSoundPlaying = true;
            return;
        }
        if ( !bPlayWarningSound && bWarningSoundPlaying ) {
            pEntry->stop( 0.0f );
            bWarningSoundPlaying = false;
        }
    }
    bPlayWarningSound = false;
}

void GameMechPenalty::updatePendingPenalty( const float deltaTime ) {
    // FUN_0072a490
    pendingDisplayTimer += deltaTime;
    const float absPendingPenalty =
        ( pendingPenalty >= 0.0f ) ? pendingPenalty : 0.0f - pendingPenalty;
    if ( absPendingPenalty < kPendingPenaltyThreshold ||
         pendingDisplayTimer < kPendingDisplayInterval ) {
        return;
    }

    pendingDisplayTimer = 0.0f;
    if ( result != CR_Failure && pMech->getPenaltyListener() != nullptr ) {
        const float invMaxPenalty = 1.0f / maxPenalty;
        pMech->getPenaltyListener()->onPenalty(
            ( maxPenalty - penalty ) * invMaxPenalty,
            pendingPenalty * invMaxPenalty );
    }

    FlashPlayer *pPlayer = gpMngHud->getHudPlayer();
    if ( pPlayer == nullptr ) {
        return;
    }

    if ( displayMode == DM_Score ) {
        pPlayer->setVariableValue( "/:Score_driving_tmp",
                                   -int32_t( pendingPenalty ) );
    } else if ( displayMode == DM_Money ) {
        const EntityConvoy *pConvoy = static_cast<const EntityConvoy *>(
            gpScenaric->getActiveExtraChallenge() );
        if ( pConvoy != nullptr ) {
            const int64_t money =
                int64_t( double( pConvoy->getReward() ) *
                         ( double( pendingPenalty ) / double( maxPenalty ) ) );
            char moneyText[32];
            FormatMoneyString( moneyText, -int32_t( money ) );
            pPlayer->setVariableValueNonLinked( "/:Score_driving_tmp",
                                                moneyText );
        }
    }

    pendingMessageSlot = 1 - pendingMessageSlot;
    pendingPenalty = 0.0f;
    if ( pendingMessageSlot == 0 ) {
        pPlayer->showFrame( "open", "/HUD/driving/msg_tmp_01", true );
    } else if ( pendingMessageSlot == 1 ) {
        pPlayer->showFrame( "open", "/HUD/driving/msg_tmp_02", true );
    }
}

void GameMechPenalty::displayPenalty() {
    // FUN_0072a5f0
    FlashPlayer *pPlayer = gpMngHud->getHudPlayer();
    int32_t gaugeFrame = int32_t( penalty / maxPenalty * kGaugeNumFrames );
    if ( gaugeFrame < 0 ) {
        gaugeFrame = 0;
    } else if ( gaugeFrame > kGaugeMaxFrame ) {
        gaugeFrame = kGaugeMaxFrame;
    }
    pPlayer->goToFrameAndPlay( gaugeFrame, "/HUD/driving", false );

    if ( displayMode == DM_Score ) {
        pPlayer->setVariableValue(
            "/:Score_driving",
            int32_t( std::ceil( double( maxPenalty ) - double( penalty ) ) ) );
    } else if ( displayMode == DM_Money ) {
        const EntityConvoy *pConvoy = static_cast<const EntityConvoy *>(
            gpScenaric->getActiveExtraChallenge() );
        if ( pConvoy == nullptr ) {
            return;
        }

        const float ratio = std::clamp( penalty / maxPenalty, 0.0f, 1.0f );
        const int64_t money = int64_t( ( 1.0 - double( ratio ) ) *
                                       double( pConvoy->getReward() ) );
        char moneyText[32];
        FormatMoneyString( moneyText, int32_t( money ) );
        pPlayer->setVariableValueNonLinked( "/:Score_driving", moneyText );
    }
}
