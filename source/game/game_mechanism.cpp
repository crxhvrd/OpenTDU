#include "shared.h"
#include "game_mechanism.h"

#include "core/hash/tdu_concat.h"
#include "database/gs_database.h"
#include "game/gm_world.h"
#include "game/mng_gps.h"
#include "game/scenaric/entity/entity_check_point.h"
#include "game/scenaric/event_parameters.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/mng_actor.h"
#include "game/scenaric/spots.h"
#include "ai/ai_game_session.h"
#include "ai/ai_pool.h"
#include "ai/ai_vehicle.h"
#include "ai/ai_road_query.h"
#include "ai/gs_ai.h"
#include "debug/ed_debug_text.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/mission/game_mech_penalty.h"
#include "game/mission/game_mech_eliminator.h"
#include "game/mission/game_mech_race_hud.h"
#include "game/mission/game_mech_speed_gauge.h"
#include "game/mission/game_mech_speed_radar.h"
#include "game/mission/game_mech_time.h"
#include "audio/gs_audio.h"
#include "flash/audio/flash_sounds.h"
#include "flash/gs_flash.h"
#include "game/game_mechanism_player.h"
#include "game/mission/gm_mission.h"
#include "game/mng_hud.h"
#include "game/mng_ingame_menu.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/entity/entity_convoy.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/scenaric/entity/entity_hitch_hiker.h"
#include "game/scenaric/entity/entity_shopping_girl.h"
#include "game/scenaric/entity/entity_transporter.h"
#include "game/vehicle/car_desc.h"
#include "game/vehicle/car_state.h"
#include "online/p2p_game_service.h"
#include "physics/gs_physics.h"
#include "physics/replay.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_data/achievement_checks.h"
#include "player_data/achievements_data.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "tdu_instance.h"
#include "core/format.h"
#include "core/time.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_extra_challenges.h"
#include "player_data/saved_item_table.h"
#include "player_data/saved_ranking.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "gs_timer.h"

static constexpr const char *kNoTime = "--'--\"--";        // 0x00f37758
static constexpr float kCentisecondsPerSecond = 100.0f;    // DAT_00f41708
static constexpr float kNoRankingScore = 4294967296.0f;    // FLOAT_00f41670
static constexpr float kMetersPerSecondToKmh = 3.6f;       // DAT_00f41768
static constexpr float kMetersPerSecondToMph = 2.2374146f; // DAT_00f419f4
static constexpr float kSilverRewardRatio = 0.25f;         // DAT_00f416dc
static constexpr float kBronzeRewardRatio = 0.1f;          // FLOAT_00f41668
static constexpr float kMillimetersToMeters = 0.001f;      // FLOAT_00f4167c
static constexpr float kPenaltyTimeScale = 0.1f;           // FLOAT_00f41668
static constexpr uint32_t kNoRecordHashcode = 0x3a8ac52;
static constexpr uint32_t kMessageTrainingHashcode = 0x5a34f5;
static constexpr uint32_t kMessageTestHashcode = 0x58ae55;
static constexpr uint32_t kMessageDefaultHashcode = 0x38fcd22;
static constexpr uint32_t kMessageRaceWonHashcode = 0x64659d2;
static constexpr uint32_t kMessageGoldHashcode = 0x39f0f62;
static constexpr uint32_t kMessageCupHashcode = 0x6371792;
static constexpr uint32_t kExponentFirstHashcode = 0x33d6762;
static constexpr uint32_t kExponentSecondHashcode = 0x62a4652;
static constexpr uint32_t kExponentThirdHashcode = 0x6398892;
static constexpr uint32_t kExponentNthHashcode = 0x648cad2;
static constexpr uint64_t kDefaultPlayerNameHashcode = 0x3435ad2;
static constexpr float kTimeLimitScale = 0.001f; // FLOAT_00f4167c

StateMachine::StateMachine()
    : curState( kNoState ),
      prevState( kNoState ),
      nextState( 0 ),
      stateInitTime( 0.0f ) {}

bool StateMachine::receiveMessage( const int32_t messageID, void *pParam ) {
    // FUN_00801450
    return onMessage( nullptr, messageID, pParam );
}

bool StateMachine::onMessage( Entity * /*pSender*/,
                              const int32_t messageID,
                              void * /*pParam*/ ) {
    // FUN_00801420
    if ( messageID != kMsgRestartState ) {
        return false;
    }
    stateChange( curState );
    return true;
}

bool StateMachine::stateChange( const int32_t state ) {
    // FUN_00801470
    nextState = state;
    if ( curState != kNoState ) {
        stateProcess( curState, 0.0f, SP_Term );
    }
    prevState = curState;
    curState = state;
    stateInitTime = gpTimer->GameTotalTime;
    if ( state != kNoState ) {
        stateProcess( state, 0.0f, SP_Init );
    }
    return true;
}

void SetClipVisible( FlashPlayer *pPlayer,
                     const char *pClipPath,
                     const bool bVisible ) {
    // FUN_00731770
    FlashClip clip;
    if ( pPlayer->getClipPlayer( pClipPath, &clip ) ) {
        clip.setProperty( eFlashProperty::FP_Visible, bVisible );
    }
}

bool ShowHudClip( FlashPlayer *pPlayer, const char *pClipPath ) {
    FlashClip clip;
    if ( !pPlayer->getClipPlayer( pClipPath, &clip ) ) {
        return false;
    }

    ScopedMutexLock lock = gpFlash->scopedLock();
    clip.pSprite->getDisplayList()->getFlagsRW() |= 1;
    return true;
}

GameMechComponent::GameMechComponent( GameMechanism *pOwnerMech )
    : StateMachine(),
      pMech( pOwnerMech ),
      endState( -1 ),
      result( CR_None ),
      resultReason( 0 ) {}

bool GameMechComponent::initialize() {
    // FUN_00730680
    resultReason = 0;
    result = CR_None;
    endState = -1;
    return true;
}

void GameMechComponent::printDebug() {
    // FUN_007306a0
    gEdDebugText.drawText( "Unknown component : State: %02d", endState );
}

GameMechanism::GameMechanism( const int32_t type, const uint32_t id )
    : StateMachine(),
      pUnknown1C( nullptr ),
      countdownFrame( Matrix4x4f_t::Identity() ),
      pCountdownCinematic( nullptr ),
      field_0x6e4( false ),
      bCountdownGo( false ),
      pOwner( nullptr ),
      pGameSession( nullptr ),
      field_0x6f8( 0 ),
      field_0x6fc{},
      pComponents{},
      bUnknown75C( false ),
      mechType( type ),
      mechID( id ),
      bRaceWon( false ),
      waitingForPlayersText{},
      pOffroadWarningEntry( nullptr ),
      pPenaltyListener( nullptr ),
      numParticipantsAtStart( 0 ),
      numOnlineParticipants( 8 ),
      finishTime( 0.0f ),
      firstFinishTime( 0.0f ),
      timeOutDuration( 0.0f ),
      countdownDelay( 0.0f ),
      firstFinisherIndex( -1 ),
      disconnectTick( 0u ),
      bSavedGPSVoice( false ),
      bSavedGPSDistanceCallouts( false ),
      bUnknown7D6( false ),
      bRestarted( false ),
      field_0x7d8( false ),
      endReason( 0 ),
      pDefaultPlayerName( nullptr ),
      pScoreRankingHead( nullptr ),
      pCinematic( nullptr ),
      bAutopilotFadeStarted( false ),
      bReplayExitPending( false ),
      bAutopilotRequested( false ),
      field_0x7ef( false ),
      rankingStartTick( 0u ),
      bDriveInMessageShown( false ),
      spotHashcode( 0ull ),
      field_0x800{} {
    // FUN_00731da0
}

GameMechanism::~GameMechanism() {
    // FUN_0042e060
}

bool GameMechanism::onMessage( Entity *pSender,
                               const int32_t messageID,
                               void *pParam ) {
    // FUN_00731670
    for ( GameMechComponent *pComponent : pComponents ) {
        if ( pComponent != nullptr ) {
            pComponent->onMessage( pSender, messageID, pParam );
        }
    }

    if ( messageID == kMsgCheckpointPassed ) {
        if ( mechType == MT_Speed ) {
            const int32_t checkpointIndex = *static_cast<int32_t *>( pParam );
            MngActor &mngActor = pOwner->getActorManager();
            for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
                Entity *pActor = mngActor.getActor( i );
                if ( pActor->getType() != eEntityType::ET_CHPT ) {
                    continue;
                }
                EntityCheckPoint *pCheckpoint =
                    static_cast<EntityCheckPoint *>( pActor );
                if ( pCheckpoint->getIndex() >= 0 &&
                     pCheckpoint->getIndex() == checkpointIndex &&
                     pCheckpoint->getLEDData()->Type == 1 ) {
                    pCheckpoint->setVisible( false );
                }
            }
        }
        return false;
    }

    if ( messageID == kMsgUnknown8 ) {
        bUnknown7D6 = true;
    }
    return false;
}

bool GameMechanism::initialize() {
    // FUN_00733830
    SpotMap *pCurrentSpot = nullptr;
    if ( mechType != MT_ExtraChallenge ) {
        pCurrentSpot = gpScenaric->getSpotList().getWorldSpotChecked(
            gpScenaric->getCurrentSpotIndex() );
    }
    spotHashcode = ( pCurrentSpot != nullptr ) ? pCurrentSpot->Hashcode : 0ull;

    if ( mechType != MT_Player ) {
        const char *pRequestedName =
            pOwner->getMissionDescription()->getRequestedName();
        const uint64_t requestedHashcode =
            TestDriveConcatToString8( pRequestedName );
        if ( spotHashcode != requestedHashcode ) {
            spotHashcode = requestedHashcode;
        }

        EventParameters *pEventParameters = pOwner->getMissionDescription();
        GameMechTime *pTime = new GameMechTime( this );
        pComponents[CS_Time] = pTime;
        if ( pEventParameters->getCountdownMode().getValue() != 0 ) {
            pTime->setTimeLimit(
                float( pEventParameters->getCountdown().getValue() ) *
                kTimeLimitScale );
        }

        if ( mechType == MT_Speed ) {
            if ( pEventParameters->getStartChallenge().getValue() != 0 ) {
                pComponents[CS_SpeedRadar] =
                    new GameMechSpeedRadar( this ); // +0x730
            }
        } else if ( mechType == MT_ExtraChallenge ) {
            GameMechRaceHUD *pRaceHUD = new GameMechRaceHUD( this );
            pComponents[CS_RaceHUD] = pRaceHUD;
            pRaceHUD->setShowCheckpointHud( false );
            const int32_t goldTime =
                int32_t( pEventParameters->getGoldTime().getValue() );
            if ( goldTime > 0 ) {
                pTime->setTimeLimit( float( goldTime ) * kTimeLimitScale );
            }
        } else {
            GameMechRaceHUD *pRaceHUD = new GameMechRaceHUD( this );
            pComponents[CS_RaceHUD] = pRaceHUD;
            pRaceHUD->setShowNextCheckpointOnly(
                pEventParameters->getNextChallenge().getValue() < 1 );
        }

        if ( isEndlessLoopRace() && !gpScenaric->isTrainingLevel() ) {
            pComponents[CS_Eliminator] = new GameMechEliminator( this );
        }
        if ( isEndlessLoopRace() ) {
            pEventParameters->getOffRoad().setValue( 0, false );    // +0x920
            pEventParameters->getCollisions().setValue( 0, false ); // +0x990
        }
        pComponents[CS_SpeedGauge] = new GameMechSpeedGauge( this ); // +0x73c
        OTDU_UNIMPLEMENTED; // TODO: GMMission (instant challenge FUN_00729260;
                            // race data FUN_00882c40 + FUN_00882370)
        if ( pEventParameters->getOffRoad().getValue() != 0 ||
             pEventParameters->getCollisions().getValue() != 0 ) {
            GameMechPenalty *pPenalty = new GameMechPenalty( this );
            pComponents[CS_Penalty] = pPenalty;
            pPenalty->setMaxPenalty(
                float( int32_t( pEventParameters->getPenaltyCredits()
                                    .getValue() ) ) ); // +0x1570
            pPenalty->setPenaltyFlags( GameMechPenalty::kPenaltyDefaultFlags );
            Entity *pChallenge = gpScenaric->getActiveExtraChallenge();
            if ( mechType == MT_ExtraChallenge && pChallenge != nullptr &&
                 pChallenge->getType() == eEntityType::ET_CVOY ) {
                pPenalty->setDisplayMode( GameMechPenalty::DM_Money );
            }
        }

        if ( gpMngGPS != nullptr ) {
            bSavedGPSVoice = gpMngGPS->isVoiceEnabled();
            gpMngGPS->setVoiceEnabled( false );
            bSavedGPSDistanceCallouts = gpMngGPS->isDistanceCalloutsEnabled();
            gpMngGPS->setDistanceCalloutsEnabled( false );
        }
    }

    pDefaultPlayerName =
        gpDatabase->getStringByHashcode( 0, kDefaultPlayerNameHashcode );
    for ( GameMechComponent *pComponent : pComponents ) {
        if ( pComponent != nullptr ) {
            pComponent->initialize();
        }
    }
    bUnknown7D6 = false;
    return true;
}

void GameMechanism::reset() {
    // FUN_007356e0
    static constexpr float kCopsDensityScale = 0.33333334f; // DAT_00f41764
    static constexpr float kTrafficDensityScale = 0.2f;     // DAT_00f41730
    static constexpr int32_t kHighCopsDensity = 3;
    static constexpr int32_t kMaxCopsDensity = 4;

    resetRaceState();
    pCinematic = nullptr;
    bAutopilotFadeStarted = false;
    bReplayExitPending = false;
    bAutopilotRequested = false;
    if ( mechType == MT_ExtraChallenge ) {
        static_cast<GMMission *>( pOwner )
            ->getExtraChallenge()
            .stopCinematics(); // FUN_0073d9d0
    }

    for ( GameMechComponent *pComponent : pComponents ) {
        if ( pComponent != nullptr ) {
            pComponent->reset();
        }
    }

    AiPool *pAIPool = gpAI->getAIPool();
    if ( gpScenaric->isInstantChallenge() ) {
        gpAI->setDisableCops( true );
        gpAI->setCopsActive( false );
        pAIPool->removeCops();
        pAIPool->setPoliceScale( 0.0f );
        pAIPool->setForceDensity( true );
        gpAI->getCopManager().setLevel( false, false );
        stateChange( MS_WaitingHUD );
        return;
    }

    EventParameters *pEventParameters = pOwner->getMissionDescription();
    int32_t trafficDensity =
        int32_t( pEventParameters->getTrafficDensity().getValue() );
    if ( pEventParameters->getTraffic().getValue() == 0 ||
         gpScenaric->isTrainingLevel() ) {
        trafficDensity = 0;
    }
    if ( pEventParameters->getSpotType() == EventParameters::EST_Club ) {
        pEventParameters->getCops().setValue( 0, false );
    }

    const int32_t copsDensity =
        int32_t( pEventParameters->getCopsDensity().getValue() );
    const bool bCops = pEventParameters->getCops().getValue() != 0;
    if ( bCops && !gpScenaric->isTrainingLevel() && copsDensity > 0 ) {
        const float copsScale =
            std::clamp( float( copsDensity ) * kCopsDensityScale, 0.0f, 1.0f );
        gpAI->setCopsLevel( copsScale, copsDensity > kHighCopsDensity,
                            copsDensity > kMaxCopsDensity );
        OTDU_UNIMPLEMENTED; // TODO: GSAI
    } else if ( bCops && !gpScenaric->isTrainingLevel() && copsDensity != 0 ) {
        pAIPool->setPoliceScale( 1.0f );
        pAIPool->setForceDensity( false );
        gpAI->getCopManager().setLevel( true, true );
        gpAI->setDisableCops( false );
    } else {
        gpAI->setCopsLevel( 0.0f, false, false );
    }

    if ( trafficDensity < 0 ) {
        pAIPool->setTrafficScale( 1.0f );
        pAIPool->setIgnoreZoneDensity( false );
    } else {
        const float trafficScale =
            float( trafficDensity ) * kTrafficDensityScale;
        pAIPool->setTrafficScale(
            ( trafficScale < 0.0f ) ? 0.0f : std::min( trafficScale, 1.0f ) );
        pAIPool->setIgnoreZoneDensity( true );
    }
    stateChange( MS_WaitingHUD );
}

void GameMechanism::pause() {
    // FUN_00731330
    for ( GameMechComponent *pComponent : pComponents ) {
        if ( pComponent != nullptr ) {
            pComponent->pause();
        }
    }
}

void GameMechanism::restart() {
    // FUN_00735c30
    bRestarted = true;
    field_0x7d8 = true;
    OTDU_UNIMPLEMENTED; // TODO: GSAI (FUN_00969b50(0, 1), GSAI +0x2a984:
                        // +0x30 = 0, +0x14/+0x15 = 0, +0x38 = DAT_00f417f8)
    resetRaceState();
    if ( pCinematic != nullptr ) {
        pCinematic->stop( true );
    }
    bReplayExitPending = false;
    bAutopilotRequested = false;
    if ( gpMngNavPathDisplay != nullptr ) {
        gpMngNavPathDisplay->reset();
    }
    if ( mechType == MT_ExtraChallenge ) {
        MissionExtraChallenge &extraChallenge =
            static_cast<GMMission *>( pOwner )->getExtraChallenge();
        extraChallenge.stopCinematics(); // FUN_0073d9d0
        extraChallenge.start();          // FUN_0073ee20
    }
    for ( GameMechComponent *pComponent : pComponents ) {
        if ( pComponent != nullptr ) {
            pComponent->restart();
        }
    }
    if ( gpScenaric->getLocalPlayer().getCarState() != nullptr ) {
        OTDU_UNIMPLEMENTED; // TODO: AI (CarState AiInfringement reset)
    }
    pGameSession->sendRaceMessage( AiRaceMessage{ 0, ARM_Restart } );
    stateChange( MS_WaitingHUD );
}

void GameMechanism::tick( const float deltaTime ) {
    // FUN_00733020
    stateProcess( curState, deltaTime, SP_Update );
    if ( gGameMechsDebug ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: DEBUG (FUN_00732910 state display)
    }
}

bool GameMechanism::terminate() {
    // FUN_00733e60
    if ( pUnknown1C != nullptr ) {
        OTDU_UNIMPLEMENTED; // TODO: GMMission (FUN_00882eb0 + FUN_0042e090)
    }

    for ( GameMechComponent *&pComponent : pComponents ) {
        if ( pComponent != nullptr ) {
            pComponent->terminate();
            delete pComponent;
            pComponent = nullptr;
        }
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (spectator reset, FUN_0085f540)

    if ( mechType != MT_Player ) {
        if ( gpMngGPS != nullptr ) {
            gpMngGPS->setVoiceEnabled( bSavedGPSVoice );
            gpMngGPS->setDistanceCalloutsEnabled( bSavedGPSDistanceCallouts );
        }
        gpScenaric->getObjective( 3 ).Type = "";
        gpScenaric->getObjective( 4 ).Type = "";
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: GSCamera, CarState
    return true;
}

bool GameMechanism::isEndlessLoopRace() const {
    // FUN_007319b0
    if ( pOwner->getGameMechType() != MT_Race ) {
        return false;
    }
    EventParameters *pEventParameters = pOwner->getMissionDescription();
    return pEventParameters->getLoop().getValue() >= 1 &&
           pEventParameters->getNumLaps().getValue() == 0;
}

GameMechTime *GameMechanism::getTimeComponent() const {
    return static_cast<GameMechTime *>( pComponents[CS_Time] );
}

void GameMechanism::fillChallengeResults( FlashPlayer *pPlayer,
                                          const int32_t rank,
                                          const bool bTimedResult ) {
    // FUN_00732b80 (EBX = rank, CL = bTimedResult)
    char rankText[0x20] = "--";
    const auto showExponent = [&]( const uint32_t exponentHashcode,
                                   const char *pDigitText,
                                   const char *pDotText ) {
        const char *pExponent =
            gpDatabase->getStringByHashcode( 0, exponentHashcode );
        if ( pExponent == nullptr ) {
            return false;
        }
        if ( *pExponent == '.' ) {
            strcpy( rankText, pDotText );
            return false;
        }
        strcpy( rankText, pDigitText );
        pPlayer->setVariableValueNonLinked( "/:Exposant", pExponent );
        return true;
    };

    const char *pMessage = nullptr;
    bool bExponentShown = false;
    switch ( rank ) {
    case -3:
        strcpy( rankText, "-" );
        pMessage =
            gpDatabase->getStringByHashcode( 0, kMessageTrainingHashcode );
        break;
    case -2:
        strcpy( rankText, "-" );
        pMessage = gpDatabase->getStringByHashcode( 0, kMessageTestHashcode );
        break;
    case -1:
    case 0:
        strcpy( rankText, "-" );
        pMessage =
            gpDatabase->getStringByHashcode( 0, kMessageDefaultHashcode );
        break;
    case 1:
        pMessage = gpDatabase->getStringByHashcode(
            0, bTimedResult ? kMessageGoldHashcode : kMessageRaceWonHashcode );
        bExponentShown = showExponent( kExponentFirstHashcode, "1", "1." );
        break;
    case 2:
        pMessage = gpDatabase->getStringByHashcode(
            0, bTimedResult ? kMessageCupHashcode : kMessageDefaultHashcode );
        bExponentShown = showExponent( kExponentSecondHashcode, "2", "2." );
        break;
    case 3:
        pMessage = gpDatabase->getStringByHashcode(
            0, bTimedResult ? kMessageCupHashcode : kMessageDefaultHashcode );
        bExponentShown = showExponent( kExponentThirdHashcode, "3", "3." );
        break;
    default: {
        if ( rank >= 4 && rank <= 16 ) {
            pMessage =
                gpDatabase->getStringByHashcode( 0, kMessageDefaultHashcode );
        }
        char digitText[0x20];
        char dotText[0x20];
        snprintf( digitText, sizeof( digitText ), "%d", rank );
        snprintf( dotText, sizeof( dotText ), "%d.", rank );
        bExponentShown =
            showExponent( kExponentNthHashcode, digitText, dotText );
        break;
    }
    }

    if ( !bExponentShown ) {
        pPlayer->setVariableValueNonLinked( "/:Exposant", " " );
    }
    pPlayer->setVariableValueNonLinked( "/:PlayerRanking", rankText );
    if ( pMessage == nullptr ) {
        pPlayer->setVariableValueNonLinked( "/:Message", " " );
        return;
    }
    pPlayer->setVariableValue( "/:Message", pMessage );
}

void GameMechanism::draw() {
    // FUN_00731300
    for ( GameMechComponent *pComponent : pComponents ) {
        if ( pComponent != nullptr ) {
            pComponent->draw();
        }
    }
}

void GameMechanism::initializeDriven() {
    // FUN_00731a00
    stateChange( MS_Driven );
    initialize();
}

void GameMechanism::setGameSession( GameSession *pSession ) {
    // FUN_007315a0
    if ( pGameSession != nullptr ) {
        pGameSession->setListener( nullptr );
    }
    pGameSession = pSession;
    if ( pSession != nullptr && pOwner->getGameMechID() != 0xc0c0 ) {
        pSession->setListener( this );
    }
}

static constexpr uint32_t kSoloSpotType = 0xc0c0;
static constexpr uint32_t kClubSpotType = 0xc0c2;

static constexpr int32_t kSessionEventWaitingOnline = 0x99;
static constexpr int32_t kSessionEventWaitingHUD = 0x9a;
static constexpr int32_t kSessionEventInstall = 0x9b;
static constexpr int32_t kSessionEventPollInstall = 0x9c;
static constexpr int32_t kSessionEventMapLoaded = 0x9d;

static constexpr int32_t kRaceMessageRaceOver = 5;
static constexpr int32_t kRaceMessagePlayerFinished = 0xb;

static constexpr float kDefaultTimeOutDuration = 16.0f;   // DAT_00f418d8
static constexpr float kDefaultCountdownDelay = 10.0f;    // FLOAT_00f4166c
static constexpr float kICTimeOutDuration = 5.0f;         // DAT_00f41750
static constexpr float kICCountdownDelay = 3.0f;          // DAT_00f416b4
static constexpr float kRaceOverDelay = 1.5f;             // DAT_00f416b8
static constexpr float kFadeDuration = 1.0f;              // FLOAT_00f4165c
static constexpr float kMillisecondsToSeconds = 0.001f;   // FLOAT_00f4167c
static constexpr float kPercent = 100.0f;                 // DAT_00f41708
static constexpr float kSpeedScoreScale = 0.01f;          // DAT_00f416d4
static constexpr uint32_t kDriveInMessageDelayMs = 1500u; // 0x5dc
static constexpr uint32_t kSessionLostTimeoutMs = 10000u; // 0x2710
static constexpr float kCountdownCineBackOffset = -3.5f;  // DAT_00f41bac

static float gStartCameraSideOffset = 10.0f;  // DAT_00fab968
static float gStartCameraTargetAhead = 20.0f; // DAT_00fab96c
static float gStartCameraHeight = 12.0f;      // DAT_00fab970
static float gStartCameraForward = 40.0f;     // DAT_00fab974

static constexpr uint64_t kQuitHostHashcode = 0x364c582;
static constexpr uint64_t kWaitingForPlayersHashcode = 0x36c66a2;
static constexpr uint64_t kPauseQuitEditorHashcode = 0x3ab89bf;
static constexpr uint64_t kPauseQuitHashcode = 0x39c477f;
static constexpr uint64_t kShoppingGirlNameHashcode = 0x3725a12;
static constexpr uint64_t kConvoyNameHashcode = 0x390de92;
static constexpr uint64_t kHitchHikerNameHashcode = 0x3819c52;
static constexpr uint64_t kTransporterNameHashcode = 0x3a020d2;
static constexpr uint64_t kExtraChallengeFailedHashcode = 0x38fcd22;
static constexpr uint64_t kExtraChallengeSucceededHashcode = 0x394d632;

struct RacePlayerFinishedMessage : public AiRaceMessage {
    float Score = 0.0f;
    float PenaltyRatio = 0.0f;
    int32_t PlayerLevel = 0;
    float TimeOutDuration = 0.0f;
    float CountdownDelay = 0.0f;
    int32_t EndReason = 0;
};

static Eigen::Vector4f GetWorldShift() {
    if ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() ) {
        return gpWorldStreaming->getPosition();
    }
    return Eigen::Vector4f::Zero();
}

static bool GetCarStateFlagDD( CarState * /*pCarState*/ ) {
    OTDU_UNIMPLEMENTED_ONCE; // TODO: VEHICLE (CarState +0xdd)
    return false;
}

static FlashPlayer *GetHudPlayer() {
    return ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;
}

static void ShowLocalPlayerCar( const bool bShow ) {
    // FUN_00731390
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr || pLocalCar->getVehicleInstance() == nullptr ) {
        return;
    }

    pLocalCar->getVehicleInstance()->setShouldDraw( bShow );
    EntityDriver *pDriver = pLocalCar->getDriver();
    if ( pDriver == nullptr ) {
        return;
    }
    while ( ( pDriver->getInstanceRefCount() > 0 ) != bShow ) {
        if ( bShow ) {
            pDriver->addInstanceReference();
        } else if ( pDriver->getInstanceRefCount() > 0 ) {
            pDriver->removeInstanceReference();
        }
    }
}

static void SetLocalPlayerAutopilot( const bool bEnable ) {
    // FUN_0090dd50
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr ||
         pLocalCar->getSimulationType() !=
             CarState::eSimulationType::CSST_Local ||
         pLocalCar->getOwner() != CarState::eOwner::CSO_LocalPlayer ||
         ( pLocalCar->getAIController() != nullptr ) == bEnable ) {
        return;
    }
    gpAI->getAIPool()->togglePlayerAutopilot( bEnable );
}

static void PauseLocalPlayerPhysics() {
    // FUN_00735e10 (inlined)
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    PhysicsVehicle *pVehicle =
        ( pLocalCar != nullptr ) ? pLocalCar->getPhysicsVehicle() : nullptr;
    if ( pVehicle == nullptr ) {
        return;
    }
    PhysicsLock lock;
    pVehicle->setPostponedPause( 1 );
}

bool GameMechanism::isCompetitiveEvent() const {
    // FUN_007313f0
    switch ( pOwner->getGameMechType() ) {
    case MT_Speed:
    case MT_Race:
        return pOwner->getGameMechID() != kSoloSpotType;
    case MT_Player:
        return true;
    default:
        return false;
    }
}

bool GameMechanism::areOthersStillRacing() const {
    // FUN_00731110
    const int32_t numParticipants = pGameSession->getNumParticipants();
    return numParticipants > 1 &&
           pGameSession->countParticipantsInState(
               GameParticipant::PS_Finished ) < numParticipants;
}

bool GameMechanism::findFinishedComponents( int32_t &outSuccessSlot,
                                            int32_t &outFailureSlot ) const {
    // FUN_00731200
    outSuccessSlot = -1;
    outFailureSlot = -1;
    for ( int32_t i = 0; i < CS_Count; i++ ) {
        const GameMechComponent *pComponent = pComponents[i];
        if ( pComponent == nullptr ) {
            continue;
        }
        if ( pComponent->getResult() == GameMechComponent::CR_Success ) {
            outSuccessSlot = i;
        } else if ( pComponent->getResult() == GameMechComponent::CR_Failure ) {
            outFailureSlot = i;
        }
    }
    return outSuccessSlot >= 0 || outFailureSlot >= 0;
}

int32_t GameMechanism::getComponentEndReason() const {
    // FUN_007311c0
    for ( const GameMechComponent *pComponent : pComponents ) {
        if ( pComponent != nullptr && pComponent->getResult() > 0 &&
             pComponent->getResult() <= 3 &&
             pComponent->getResultReason() != 0 ) {
            return pComponent->getResultReason();
        }
    }
    return endReason;
}

bool GameMechanism::checkTimeOut() {
    // FUN_00731490
    GameMechTime *pTime = static_cast<GameMechTime *>( pComponents[CS_Time] );
    if ( pTime == nullptr || pTime->hasTimeLimit() ||
         pTime->isCountdownActive() || firstFinisherIndex < 0 ) {
        return false;
    }
    if ( pTime->getElapsedTime() <
         timeOutDuration - countdownDelay + firstFinishTime ) {
        return false;
    }
    pTime->startCountdown( countdownDelay ); // FUN_0072fda0 (xmm1)
    return true;
}

bool GameMechanism::isLocalPlayerArrested() {
    // FUN_00731a20
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr || !pLocalCar->getPoliceState().isArrested() ) {
        return false;
    }

    bUnknown7D6 = false;
    if ( mechType != MT_ExtraChallenge ) {
        return true;
    }

    int32_t typeIndex = 0;
    int32_t nameIndex = 0;
    GetExtraChallengeDisplayIndices( gpScenaric->getActiveExtraChallenge(),
                                     &typeIndex, &nameIndex );
    switch ( nameIndex ) {
    case PlayerGameMech::SDN_ShoppingGirl:
    case PlayerGameMech::SDN_Hiker:
    case PlayerGameMech::SDN_Convoy:
        return false;
    default:
        return true;
    }
}

void GameMechanism::resetParticipantsRaceData() {
    // FUN_007317e0
    for ( int32_t i = 0; i < pGameSession->getCapacity(); i++ ) {
        GameParticipant *pParticipant = pGameSession->getParticipant( i );
        if ( pParticipant != nullptr && pParticipant->isActive() ) {
            pParticipant->resetRaceData();
        }
    }
}

void GameMechanism::placeParticipantsOnGrid( const bool bPausePhysics ) {
    // FUN_00730db0
    int32_t gridIndex = 0;
    int32_t onlineGridIndex = 7;
    for ( int32_t i = 0; i < pGameSession->getCapacity();
          i++, onlineGridIndex-- ) {
        GameParticipant *pParticipant = pGameSession->getParticipant( i );
        if ( pParticipant == nullptr || !pParticipant->isActive() ) {
            continue;
        }

        Matrix4x4f_t gridFrame;
        GMMission *pMission = static_cast<GMMission *>( pOwner );
        if ( pGameSession->isOnline() ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_00854720, instant
                                     // challenge reversed grid)
            gridFrame = pMission->computeGridMatrix( onlineGridIndex, 8 );
        } else {
            gridFrame = pMission->computeGridMatrix(
                gridIndex, pGameSession->getNumParticipants() );
        }

        CarState *pCarState = pParticipant->getCarState();
        PhysicsVehicle *pVehicle =
            ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;
        if ( pVehicle != nullptr && pVehicle->isSimulated() ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS (+0xbd8 / +0xbdc = 0)
            if ( pCarState->getAIController() != nullptr ) {
                static_cast<AiVehicle *>( pCarState->getAIController() )
                    ->setGroundMatrixFromCenter( gridFrame );
            }
            if ( bPausePhysics && !pVehicle->isPaused() ) {
                PhysicsLock lock;
                pVehicle->setPostponedPause( 1 );
            }

            pVehicle->resetToTransform( gridFrame, true );
            OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS

            if ( !bPausePhysics && pVehicle->isPaused() ) {
                pVehicle->setVirtualFlag( false );
                PhysicsLock lock;
                pVehicle->setPostponedPause( 2 );
            }
        }
        gridIndex++;
    }
}

void GameMechanism::updateStartLineCamera() {
    // FUN_007308a0
    MngActor &mngActor = pOwner->getActorManager();
    for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
        Entity *pActor = mngActor.getActor( i );
        if ( pActor->getType() != eEntityType::ET_CHPT ||
             static_cast<EntityCheckPoint *>( pActor )->getLEDData()->Type !=
                 1 ) {
            continue;
        }

        const Matrix4x4f_t &frame = pActor->getModelMatrix();
        const Eigen::Vector4f position = frame.row( 3 ).transpose();
        const Eigen::Vector4f forward = frame.row( 2 ).transpose();
        const Eigen::Vector4f side = frame.row( 0 ).transpose();
        const Eigen::Vector4f target =
            position + forward * gStartCameraTargetAhead;
        const Eigen::Vector4f cameraPosition =
            position + forward * gStartCameraForward +
            Eigen::Vector4f( 0.0f, gStartCameraHeight, 0.0f, 0.0f ) +
            side * gStartCameraSideOffset;

        CameraGame *pCamera =
            gpCamera->findCamera( gpCamera->getBoundCarState() );
        pCamera->setPosition( cameraPosition );
        pCamera->setTarget( target );
        gpCamera->setNextCamera( pCamera, 0, kFadeDuration );
        return;
    }
}

void GameMechanism::updateCountdownCamera() {
    // FUN_00730860
    if ( pCountdownCinematic == nullptr ) {
        return;
    }
    CameraGame *pActiveCamera = gpCamera->getActiveCamera(); // gGSCamera+0x200
    if ( pActiveCamera != nullptr && pActiveCamera->getType() != 10 ) {
        return;
    }
    if ( ( pCountdownCinematic->getFlags() & 4u ) == 0 ) { // +0xa0 bit 2
        gpCamera->restoreCamera();
    }
}

void GameMechanism::startCountdown() {
    // FUN_00730ac0
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    PhysicsVehicle *pVehicle = pLocalCar->getPhysicsVehicle();
    countdownFrame = pVehicle->getPhysicsMatrix();

    const char *pCinematicName = nullptr;
    if ( mechType == MT_Race ||
         ( mechType == MT_Speed && mechID != kSoloSpotType ) ) {
        countdownFrame = static_cast<GMMission *>( pOwner )->getSpawnFrame();
        const Eigen::Vector4f shift = GetWorldShift();
        if ( !gpScenaric->isInstantChallenge() ) {
            pCinematicName = "C_321GO_MULTI";
            countdownFrame.row( 3 ) -= shift.transpose();
        } else {
            pCinematicName = "C_321GO_ICE";
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (instant challenge: car local
                                // gravity vector y)
            const float height = countdownFrame( 3, 1 );
            countdownFrame( 3, 0 ) -= shift.x();
            countdownFrame( 3, 1 ) = height;
            countdownFrame( 3, 2 ) -= shift.z();
            countdownFrame( 3, 3 ) -= shift.w();
        }
        countdownFrame.row( 3 ) -=
            countdownFrame.row( 2 ) * kCountdownCineBackOffset;
    } else if ( mechType == MT_ExtraChallenge ) {
        pCinematicName = "C_321GO_SGIRL";
        Entity *pChallenge = gpScenaric->getActiveExtraChallenge();
        if ( pChallenge != nullptr ) {
            if ( pChallenge->getType() == eEntityType::ET_CVOY ) {
                pCinematicName = "C_321GO_MISSION";
            }
            if ( pChallenge->getType() == eEntityType::ET_CVOY ||
                 pChallenge->getType() == eEntityType::ET_TRAN ) {
                FlashPlayer *pGeneral = gpFlash->getFlashPlayer( "GENERAL" );
                if ( pGeneral != nullptr ) {
                    pGeneral->showFrame( "one_shot", "/avert2", true );
                }
            }
        }
    } else {
        countdownFrame = pVehicle->computeDisplayMatrix(); // FUN_0082f8e0
        pCinematicName = "C_321GO_SOLO";
    }

    pCountdownCinematic = static_cast<EntityCinematic *>(
        gpScenaric->getCinematicPackBank().findEntity( eEntityType::ET_CINE,
                                                       pCinematicName ) );
    field_0x6e4 = false;
    bCountdownGo = false;
    if ( pCountdownCinematic != nullptr ) {
        pCountdownCinematic->setTransformOffset(
            countdownFrame ); // +0x180, bCineOffset
        pCountdownCinematic->getCinResCollection().bindPlayerCar(
            pLocalCar ); // FUN_007e1bb0 (+0x110)
        pCountdownCinematic->setDetachPlayerCarInput( false ); // +0x26e4
        pCountdownCinematic->setActorsDriveStreaming( false ); // +0x2338
        pCountdownCinematic->start( true );
        return;
    }
    GetHudPlayer()->showFrame( "go", "/3_2_1_go", true );
}

void GameMechanism::updateWaitingForPlayersText( const int32_t numFinished ) {
    // FUN_00732f70
    if ( pDefaultPlayerName == nullptr ) {
        waitingForPlayersText[0] = '\0';
    } else {
        strncpy( waitingForPlayersText, pDefaultPlayerName, 0x36 );
    }
    char countText[0x20];
    snprintf( countText, sizeof( countText ), " (%d/%d)", numFinished,
              pGameSession->getNumParticipants() );
    strncat( waitingForPlayersText, countText,
             sizeof( waitingForPlayersText ) - strlen( waitingForPlayersText ) -
                 1 );
    gpMngHud->getHudPlayer()->setVariableValueWritableLinked(
        "/:m_WaitingForPlayers", waitingForPlayersText );
}

void GameMechanism::sendPlayerFinished() {
    // FUN_00731ea0
    EventParameters *pEventParameters = pOwner->getMissionDescription();
    float score = 0.0f;
    float penaltyRatio = 0.0f;
    if ( pEventParameters->getOffRoad().getValue() != 0 ||
         pEventParameters->getCollisions().getValue() != 0 ) {
        if ( pComponents[CS_Penalty] != nullptr ) {
            penaltyRatio = kPercent;
            if ( pComponents[CS_Penalty]->getResult() !=
                 GameMechComponent::CR_Failure ) {
                penaltyRatio =
                    static_cast<GameMechPenalty *>( pComponents[CS_Penalty] )
                        ->getPenaltyRatio();
            }
        }
    }

    GameParticipant *pLocal = pGameSession->getLocalParticipant();
    if ( mechType == MT_Speed ) {
        if ( pEventParameters->getStartChallenge().getValue() !=
             0 ) { // ChST +0xae0
            const uint32_t radarScore =
                static_cast<GameMechSpeedRadar *>( pComponents[CS_SpeedRadar] )
                    ->getBaseScore(); // +0x6c0
            pLocal->getSessionParams().FinalScore = int32_t( radarScore );
            score = float( uint32_t( radarScore ) ) * kSpeedScoreScale;
        } else {
            score =
                static_cast<GameMechSpeedGauge *>( pComponents[CS_SpeedGauge] )
                    ->getTopSpeed(); // +0x48
            pLocal->getSessionParams().FinalScore = int32_t( score * kPercent );
        }
    } else if ( mechType == MT_Race ) {
        score = static_cast<GameMechTime *>( pComponents[CS_Time] )
                    ->getRaceTime(); // +0x40
        pLocal->getSessionParams().FinalScore = int32_t( score * kPercent );
    }

    pLocal->getSessionParams().Level =
        gpPlayerData->getLoadedSave()->getAchievements()->getPlayerLevel();
    pLocal->getSessionParams().Penalty = penaltyRatio;

    RacePlayerFinishedMessage message;
    message.Type = kRaceMessagePlayerFinished;
    message.ID = pGameSession->getLocalParticipantIndex();
    message.Score = score;
    message.PenaltyRatio = penaltyRatio;
    message.PlayerLevel =
        gpPlayerData->getLoadedSave()->getAchievements()->getPlayerLevel();
    message.TimeOutDuration = timeOutDuration;
    message.CountdownDelay = countdownDelay;
    message.EndReason = endReason;

    if ( endReason == 2 && firstFinisherIndex < 0 ) {
        firstFinisherIndex = pGameSession->getLocalParticipantIndex();
        firstFinishTime = finishTime;
    }

    pLocal->setState( GameParticipant::PS_Finished );
    pGameSession->sendRaceMessage( message );
}

void GameMechanism::gotoResultsFrame() {
    // FUN_007348f0
    FlashPlayer *pHud = GetHudPlayer();
    pHud->setVariableValueNonLinked( "/:IsRankings", "0" );
    pHud->setVariableValue( "/:IsSolo", int32_t( mechID == kSoloSpotType ) );
    const char *pType = ( mechID == kSoloSpotType )   ? "solo"
                        : ( mechID == kClubSpotType ) ? "club"
                                                      : "multi";
    pHud->setVariableValueNonLinked( "/:WhatChaType", pType );

    switch ( mechType ) {
    case MT_TimeAttack:
        pHud->showFrame( "results", "/", true );
        break;
    case MT_Speed:
        pHud->showFrame( "speed_trap_results", "/", true );
        break;
    case MT_Race:
        if ( pComponents[CS_InstantChallenge] == nullptr ) {
            pHud->showFrame( "race_results", "/", true );
        }
        break;
    case MT_ExtraChallenge:
        showExtraChallengeResults();
        break;
    default:
        break;
    }
}

void GameMechanism::showExtraChallengeResults() {
    // FUN_00733f90
    static constexpr float kPerfectRatioEpsilon = 1e-4f; // DAT_00f41760
    static constexpr float kPerfectBonusRatio = 0.5f;    // FLOAT_00f41674

    FlashPlayer *pHud = GetHudPlayer();
    OTDU_UNIMPLEMENTED_ONCE; // TODO: GSAI (DAT_013d1a54 +0x290 flag)
    const bool bBotFlag = false;
    pHud->setVariableValue( "/:IsRetry", int32_t( !bBotFlag ) );
    pHud->setVariableValue( "/:IsReward", 0 );
    pHud->setVariableValue( "/:IsPerfect", 0 );

    EntityExtraChallenge *pChallenge = static_cast<EntityExtraChallenge *>(
        gpScenaric->getActiveExtraChallenge() );
    int32_t typeIndex = 0;
    int32_t nameIndex = -1;
    GetExtraChallengeDisplayIndices( pChallenge, &typeIndex, &nameIndex );

    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    const auto rewardPassengerCoupons = [&]( const uint32_t numCoupons,
                                             const bool bShowCoupons ) {
        SavedItemTable *pChallengeStates = pSave->getItemTable();
        uint16_t &kind = pChallengeStates->Kinds[pChallenge->getIndex()];
        bool bPerfect = false;
        if ( ( kind & SavedItemTable::kExtraChallengePerfectFlag ) == 0 ) {
            const GameMechPenalty *pPenalty =
                static_cast<const GameMechPenalty *>( pComponents[CS_Penalty] );
            if ( fabsf( pPenalty->getPenaltyRatio() ) <=
                 kPerfectRatioEpsilon ) {
                bPerfect = true;
                pSave->getExtraChallenges()->countExtraChallengePerfect();
                pHud->setVariableValue( "/:IsPerfect", 1 );
                pHud->setVariableValue( "/:base_reward", 0 );
                pHud->setVariableValue( "/:perfect_reward", 1 );
                kind |= SavedItemTable::kExtraChallengePerfectFlag;
                pChallengeStates->notify( 0u );
            }
        }

        uint32_t numEarned = 0u;
        if ( ( kind & SavedItemTable::kExtraChallengeGoldFlag ) == 0 ) {
            pHud->setVariableValue( "/:IsReward", 1 );
            if ( bShowCoupons ) {
                pHud->setVariableValue( "/:extra_reward",
                                        int32_t( numCoupons ) );
            }
            pHud->setVariableValue( "/:IsRewardCoupon", 1 );
            numEarned = numCoupons;
            if ( bPerfect ) {
                pHud->setVariableValue( "/:base_reward", int32_t( numEarned ) );
                numEarned++;
            } else {
                pHud->setVariableValue( "/:base_reward", 0 );
            }
        } else {
            if ( !bPerfect ) {
                return;
            }
            pHud->setVariableValue( "/:IsReward", 1 );
            pHud->setVariableValue( "/:base_reward", 0 );
            pHud->setVariableValue( "/:IsRewardCoupon", 1 );
            numEarned = 1u;
        }
        pHud->setVariableValue( "/:CouponReward", int32_t( numEarned ) );
        pSave->getDriver()->earnCoupons( numEarned );
    };

    switch ( nameIndex ) {
    case PlayerGameMech::SDN_ShoppingGirl:
        pHud->setVariableValueNonLinked( "/:cur_gm", "sgirl" );
        pHud->setVariableValueNonLinked( "/:extra_reward", "--" );
        if ( bRaceWon ) {
            rewardPassengerCoupons(
                static_cast<EntityShoppingGirl *>( pChallenge )->getCoupon(),
                false );
        }
        break;
    case PlayerGameMech::SDN_Hiker:
        pHud->setVariableValueNonLinked( "/:cur_gm", "hiker" );
        pHud->setVariableValueNonLinked( "/:extra_reward", "--" );
        if ( bRaceWon ) {
            rewardPassengerCoupons(
                static_cast<EntityHitchHiker *>( pChallenge )->getCoupon(),
                true );
        }
        break;
    case PlayerGameMech::SDN_Transporter: {
        pHud->setVariableValueNonLinked( "/:cur_gm", "transporter" );
        Driver *pDriver = pSave->getDriver();
        SavedExtraChallenges *pCounters = pSave->getExtraChallenges();
        const EntityTransporter *pTransporter =
            static_cast<EntityTransporter *>( pChallenge );
        const uint32_t fare = pTransporter->getReward();
        const uint32_t deposit = pTransporter->getDeposit();
        pHud->setVariableValue( "/:extra_reward", int32_t( fare ) );
        char moneyText[0x40];
        if ( !bRaceWon ) {
            pHud->setVariableValue( "/:IsRewardMoney", 1 );
            pHud->setVariableValueNonLinked(
                "/:MoneyTransfert",
                FormatMoneyString( moneyText, int32_t( deposit ) ) );
            pCounters->addCounter( CounterIndex( 0xf098 ), int32_t( deposit ) );
            pDriver->setMoneyAndNotify(
                int32_t( pDriver->getMoney() + deposit ) );
            pCounters->addCounter( CounterIndex( 0xf0dc ), int32_t( deposit ) );
            pDriver->setPendingMoneyOperation(
                pDriver->getPendingMoneyOperation() -
                int32_t( deposit ) ); // FUN_00430c50
            if ( pDriver->getMoney() < deposit || bBotFlag ) {
                pHud->setVariableValue( "/:IsRetry", 0 );
            }
            break;
        }

        pHud->setVariableValue( "/:IsReward", 1 );
        pHud->setVariableValue( "/:IsRetry", 0 );
        pCounters->addCounter( CounterIndex( 0xf098 ), int32_t( deposit ) );
        pDriver->setMoneyAndNotify( int32_t( pDriver->getMoney() + deposit ) );
        pCounters->addCounter( CounterIndex( 0xf098 ), int32_t( fare ) );
        pDriver->setPendingMoneyOperation( pDriver->getPendingMoneyOperation() +
                                           int32_t( fare ) ); // FUN_00430c00
        pHud->setVariableValue( "/:IsRewardMoney", 1 );
        pHud->setVariableValueNonLinked(
            "/:MoneyTransfert",
            FormatMoneyString( moneyText, int32_t( fare ) ) );

        SavedItemTable *pChallengeStates = pSave->getItemTable();
        uint16_t &kind = pChallengeStates->Kinds[pChallenge->getIndex()];
        kind = ( kind & 0xff00u ) | SavedItemTable::ECS_Done;
        pChallengeStates->notify( 0u );
        break;
    }
    case PlayerGameMech::SDN_Convoy: {
        pHud->setVariableValueNonLinked( "/:cur_gm", "convoy" );
        const GameMechPenalty *pPenalty =
            static_cast<const GameMechPenalty *>( pComponents[CS_Penalty] );
        const float ratio =
            std::clamp( pPenalty->getPenaltyRatio(), 0.0f, 1.0f );
        const uint32_t reward = uint32_t(
            float( static_cast<EntityConvoy *>( pChallenge )->getReward() ) *
            ( 1.0f - ratio ) );
        pHud->setVariableValue( "/:extra_reward", int32_t( reward ) );
        if ( !bRaceWon ) {
            break;
        }

        pHud->setVariableValue( "/:IsRetry", 0 );
        pHud->setVariableValue( "/:IsReward", 1 );
        char moneyText[0x40];
        uint32_t perfectBonus = 0u;
        if ( fabsf( pPenalty->getPenaltyRatio() ) <= kPerfectRatioEpsilon ) {
            perfectBonus = uint32_t( float( reward ) * kPerfectBonusRatio );
            pSave->getExtraChallenges()->countExtraChallengePerfect();
            pHud->setVariableValue( "/:IsPerfect", 1 );
            pHud->setVariableValueNonLinked(
                "/:base_reward",
                FormatMoneyString( moneyText, int32_t( reward ) ) );
            pHud->setVariableValueNonLinked(
                "/:perfect_reward",
                FormatMoneyString( moneyText, int32_t( perfectBonus ) ) );
        }

        const int32_t totalReward = int32_t( reward + perfectBonus );
        pSave->getExtraChallenges()->addCounter( CounterIndex( 0xf098 ),
                                                 totalReward );
        Driver *pDriver = pSave->getDriver();
        pDriver->setPendingMoneyOperation( pDriver->getPendingMoneyOperation() +
                                           totalReward ); // FUN_00430c00
        pHud->setVariableValue( "/:IsRewardMoney", 1 );
        char totalText[0x40];
        pHud->setVariableValueNonLinked(
            "/:MoneyTransfert", FormatMoneyString( totalText, totalReward ) );

        SavedItemTable *pChallengeStates = pSave->getItemTable();
        uint16_t &kind = pChallengeStates->Kinds[pChallenge->getIndex()];
        kind = ( kind & 0xff00u ) | SavedItemTable::ECS_Done;
        pChallengeStates->notify( 0u );
        break;
    }
    default:
        break;
    }

    pHud->setVariableValueNonLinked( "/:Message2", " " );
    if ( !bRaceWon ) {
        const char *pMessage =
            gpDatabase->getStringByHashcode( 0, kExtraChallengeFailedHashcode );
        if ( pMessage == nullptr ) {
            pHud->setVariableValueNonLinked( "/:Message", " " );
        } else {
            pHud->setVariableValue( "/:Message", pMessage );
        }
        pHud->showFrame( "failed", "/", true );
    } else {
        SavedItemTable *pChallengeStates = pSave->getItemTable();
        const int32_t index = pChallenge->getIndex();
        pChallengeStates->Kinds[index] |=
            SavedItemTable::kExtraChallengeGoldFlag;
        // NOTE: Retail reads the garage slot of the current car without a
        // null check.
        CarDesc *pCurrentCar = gpPlayerData->getCurrentCar();
        pChallengeStates->Values[index] =
            ( pCurrentCar != nullptr ) ? pCurrentCar->getCarConfigID() : 0u;
        pChallengeStates->notify( 0u );

        const eEntityType kind = pChallenge->getType();
        pHud->setVariableValue( "/:nb_done",
                                gpScenaric->countGoldExtraChallenges( kind ) );
        pHud->setVariableValue( "/:nb_total",
                                gpScenaric->countExtraChallenges( kind ) );
        const char *pMessage = gpDatabase->getStringByHashcode(
            0, kExtraChallengeSucceededHashcode );
        if ( pMessage == nullptr ) {
            pHud->setVariableValueNonLinked( "/:Message", " " );
        } else {
            pHud->setVariableValue( "/:Message", pMessage );
        }
        pHud->showFrame( "success", "/", true );
        gpPlayerData->requestSave( 2 );
    }
    pSave->getExtraChallenges()->countExtraChallengeResult( bRaceWon );
}

int32_t GameMechanism::sortParticipantsByScore( const bool bHighestFirst ) {
    // FUN_00731c00
    int32_t numRanked = 0;
    for ( int32_t i = 0; i < pGameSession->getCapacity(); i++ ) {
        GameParticipant *pParticipant = pGameSession->getParticipant( i );
        if ( pParticipant == nullptr || !pParticipant->isActive() ||
             pParticipant->isRemote() ) {
            continue;
        }

        GameParticipant::SessionParams &params =
            pParticipant->getSessionParams();
        const auto isAhead = [&]( const GameParticipant *pOther ) {
            const uint32_t otherScore = const_cast<GameParticipant *>( pOther )
                                            ->getSessionParams()
                                            .FinalScore;
            return bHighestFirst ? ( otherScore < params.FinalScore )
                                 : ( params.FinalScore < otherScore );
        };

        numRanked++;
        if ( numRanked == 1 ) {
            params.pRankNextPlayer = nullptr;
            pScoreRankingHead = pParticipant;
            continue;
        }
        if ( isAhead( pScoreRankingHead ) ) {
            params.pRankNextPlayer = pScoreRankingHead;
            pScoreRankingHead = pParticipant;
            continue;
        }

        GameParticipant *pPrevious = pScoreRankingHead;
        GameParticipant *pNext = pPrevious->getSessionParams().pRankNextPlayer;
        while ( pNext != nullptr && !isAhead( pNext ) ) {
            pPrevious = pNext;
            pNext = pPrevious->getSessionParams().pRankNextPlayer;
        }
        params.pRankNextPlayer = pNext;
        pPrevious->getSessionParams().pRankNextPlayer = pParticipant;
    }
    return numRanked;
}

bool GameMechanism::showPlayerRecord( const uint64_t hashcode ) {
    // FUN_00732550
    FlashPlayer *pPlayer = GetHudPlayer();
    const SavedRanking *pRanking = gpPlayerData->getLoadedSave()->getRanking();
    const int32_t result = pRanking->getChallengeResult( hashcode );
    if ( result == -1 ) {
        pPlayer->setVariableValue( "/:MyCup", 0u );
    } else if ( mechType == MT_Speed ) {
        const float bestSpeed = pRanking->getChallengeBestScore( hashcode );
        if ( bestSpeed > 0.0f ) {
            const bool bImperial = gpPlayerData->isUsingImperialUnits();
            char recordText[0x20];
            snprintf(
                recordText, sizeof( recordText ), "%.2f %s",
                double( bestSpeed * ( bImperial ? kMetersPerSecondToMph
                                                : kMetersPerSecondToKmh ) ),
                gpDatabase->getCommonDB().CommonStrings[bImperial ? 0 : 1] );
            pPlayer->setVariableValueNonLinked( "/:Player_record", recordText );
            pPlayer->setVariableValue( "/:MyCup", uint32_t( result ) );
            return true;
        }
    } else if ( mechType == MT_TimeAttack || mechType == MT_Race ) {
        const float bestTime = pRanking->getChallengeBestScore( hashcode );
        if ( bestTime > 0.0f ) {
            char recordText[0x20];
            FormatRaceTime( recordText, bestTime );
            pPlayer->setVariableValueNonLinked( "/:Player_record", recordText );
            pPlayer->setVariableValue( "/:MyCup", uint32_t( result ) );
            return true;
        }
    }
    pPlayer->setVariableValueNonLinked(
        "/:Player_record",
        gpDatabase->getStringByHashcode( 0, kNoRecordHashcode ) );
    return false;
}

void GameMechanism::showResults() {
    // FUN_007349e0
    FlashPlayer *pPlayer = GetHudPlayer();
    EventParameters *pEventParameters = pOwner->getMissionDescription();
    GameMechTime *pTime = getTimeComponent();
    pPlayer->setVariableValue( "/:IsNotRefTime", 0 );
    pPlayer->setVariableValue( "/:IsAchievements", 0 );
    pPlayer->setVariableValue( "/:IsLevelUp", 0 );

    int32_t cup = -1;
    int32_t rewardMoney = 0;
    bool bDuel = false;
    if ( gpScenaric->isTrainingLevel() ) {
        pPlayer->setVariableValue( "/:IsRetry", 0 );
    } else if ( mechType != MT_ExtraChallenge ) {
        pPlayer->setVariableValueNonLinked( "/:Message", " " );
        if ( pOwner->getGameMechID() == kSoloSpotType ) {
            pPlayer->setVariableValue( "/:IsRetry", 1 );
        }
    }

    if ( bUnknown75C || gpScenaric->isTrainingLevel() ) {
        pPlayer->setVariableValueNonLinked( "/:NamePlayerList", " " );
        pPlayer->setVariableValueNonLinked( "/:TimePlayerList", " " );
        pPlayer->setVariableValueNonLinked( "/:PosPlayerList", " " );
        pPlayer->setVariableValueNonLinked( "/:NameList", " " );
        pPlayer->setVariableValueNonLinked( "/:TimeList", " " );
        pPlayer->setVariableValueNonLinked( "/:PosList", " " );
        pPlayer->setVariableValueNonLinked( "/:IsPenalty", "0" );
        char timeText[0x20];
        if ( mechType == MT_TimeAttack ) {
            pPlayer->setVariableValueNonLinked(
                "/:TimeList_ref1",
                pEventParameters->getGoldTime().getDisplayText() );
            pPlayer->setVariableValueNonLinked(
                "/:TimeList_ref2",
                pEventParameters->getSilverTime().getDisplayText() );
            pPlayer->setVariableValueNonLinked(
                "/:TimeList_ref3",
                pEventParameters->getBronzeTime().getDisplayText() );
            uint32_t time =
                uint32_t( pTime->getRaceTime() * kCentisecondsPerSecond );
            if ( !bRaceWon ) {
                pPlayer->setVariableValueNonLinked( "/:IsPenalty", "0" );
                pPlayer->setVariableValueNonLinked( "/:FinalTime", kNoTime );
            } else {
                const bool bPenalties =
                    pEventParameters->getOffRoad().getValue() != 0 ||
                    pEventParameters->getCollisions().getValue() != 0;
                const GameMechPenalty *pPenalty =
                    static_cast<const GameMechPenalty *>(
                        pComponents[CS_Penalty] );
                if ( !bPenalties ) {
                    pPlayer->setVariableValueNonLinked( "/:IsPenalty", "0" );
                } else if ( pPenalty->getResult() ==
                            GameMechComponent::CR_Failure ) {
                    pPlayer->setVariableValueNonLinked( "/:IsPenalty", "0" );
                    pPlayer->setVariableValueNonLinked( "/:FinalTime",
                                                        kNoTime );
                    time = 0;
                } else {
                    pPlayer->setVariableValueNonLinked( "/:IsPenalty", "1" );
                    pPlayer->setVariableValueNonLinked(
                        "/:BaseTime", FormatCentiseconds( timeText, time ) );
                    const uint32_t penaltyTime = uint32_t( int32_t(
                        pPenalty->getPenaltyRatio() * kPenaltyTimeScale *
                        float( int32_t( time ) ) ) );
                    time += penaltyTime;
                    pPlayer->setVariableValueNonLinked(
                        "/:Penalty",
                        FormatCentiseconds( timeText, penaltyTime ) );
                }
                if ( time != 0 ) {
                    pPlayer->setVariableValueNonLinked(
                        "/:FinalTime", FormatCentiseconds( timeText, time ) );
                }
            }
        } else if ( mechType == MT_Speed ) {
            pPlayer->setVariableValueNonLinked(
                "/:TimeList_ref1",
                pEventParameters->getGoldSpeed().getDisplayText() );
            if ( pEventParameters->getSpotType() == int32_t( kSoloSpotType ) ) {
                pPlayer->setVariableValueNonLinked(
                    "/:TimeList_ref2",
                    pEventParameters->getSilverSpeed().getDisplayText() );
                pPlayer->setVariableValueNonLinked(
                    "/:TimeList_ref3",
                    pEventParameters->getBronzeSpeed().getDisplayText() );
            } else {
                pPlayer->setVariableValue( "/:IsNotRefTime", 1 );
                pPlayer->setVariableValueNonLinked( "/:TimeList_ref2", " " );
                pPlayer->setVariableValueNonLinked( "/:TimeList_ref3", " " );
            }
            float score = 0.0f;
            if ( pEventParameters->getStartChallenge().getValue() == 0 ) {
                score = static_cast<const GameMechSpeedGauge *>(
                            pComponents[CS_SpeedGauge] )
                            ->getTopSpeed(); // +0x48
            } else {
                score = float( static_cast<const GameMechSpeedRadar *>(
                                   pComponents[CS_SpeedRadar] )
                                   ->getBaseScore() ) *
                        kMillimetersToMeters; // +0x6c0
            }
            const bool bImperial = gpPlayerData->isUsingImperialUnits();
            char scoreText[0x40];
            snprintf( scoreText, sizeof( scoreText ), "%.2f",
                      double( score * ( bImperial ? kMetersPerSecondToMph
                                                  : kMetersPerSecondToKmh ) ) );
            pPlayer->setVariableValueNonLinked( "/:FinalScore", scoreText );
        } else if ( mechType == MT_Race ) {
            pPlayer->setVariableValueNonLinked( "/:BaseTime", " " );
            pPlayer->setVariableValueNonLinked(
                "/:FinalTime",
                FormatRaceTime( timeText, pTime->getRaceTime() ) );
        }
        pPlayer->setVariableValue( "/:MyCup", 0u );
        pPlayer->setVariableValue( "/:WhatCup", 0u );
        if ( !gpScenaric->isInstantChallenge() ) {
            if ( bUnknown75C && bRaceWon ) {
                OTDU_UNIMPLEMENTED; // TODO: SCENARIC (gGSScenaric bTestedLevel
                                    // / TestedScore best editor test score)
            }
            int32_t testRank = -2;
            if ( !gpScenaric->isTrainingLevel() ) {
                pPlayer->setVariableValueNonLinked( "/:Player_record", "--" );
            } else {
                showPlayerRecord( spotHashcode );
                testRank = -3;
            }
            fillChallengeResults( pPlayer, testRank, false );
        }
    } else {
        SavedRanking *pRanking = gpPlayerData->getLoadedSave()->getRanking();
        if ( mechType == MT_TimeAttack ) {
            cup = pTime->fillResults( pPlayer );
            sortParticipantsByScore( false );
            if ( !gpScenaric->isFromDriveIn() ) {
                rewardMoney =
                    int32_t( pEventParameters->getRewardMoney().getValue() );
                const float finalTime = pTime->getFinalTime();
                if ( finalTime > 0.0f ) {
                    const float previousBest =
                        pRanking->getBestTime( spotHashcode );
                    pRanking->submitTimeResult( spotHashcode, cup, finalTime );
                    if ( previousBest == kNoRankingScore ||
                         finalTime < previousBest ) {
                        const GameMechRaceHUD *pRaceHUD =
                            static_cast<const GameMechRaceHUD *>(
                                pComponents[CS_RaceHUD] );
                        pRanking->storeSplitTimes(
                            spotHashcode, pRaceHUD->getNumSteps(),
                            pRaceHUD->getCurrentTimes() );
                    }
                }
            }
        } else if ( mechType == MT_Speed ) {
            rewardMoney =
                int32_t( pEventParameters->getRewardMoney().getValue() );
            sortParticipantsByScore( true );
            if ( pEventParameters->getStartChallenge().getValue() == 0 ) {
                GameMechSpeedGauge *pGauge = static_cast<GameMechSpeedGauge *>(
                    pComponents[CS_SpeedGauge] );
                cup = pGauge->fillResults( pPlayer );
                if ( pGauge->getTopSpeed() > 0.0f ) {
                    pRanking->submitScoreResult( spotHashcode, cup,
                                                 pGauge->getTopSpeed() );
                }
            } else {
                GameMechSpeedRadar *pRadar = static_cast<GameMechSpeedRadar *>(
                    pComponents[CS_SpeedRadar] );
                cup = pRadar->fillResults( pPlayer );
                if ( pOwner->getGameMechID() == kSoloSpotType ) {
                    if ( pRadar->getFinalScore() > 0.0f ) {
                        const float previousBest =
                            pRanking->getBestSpeed( spotHashcode );
                        pRanking->submitScoreResult( spotHashcode, cup,
                                                     pRadar->getFinalScore() );
                        if ( previousBest == kNoRankingScore ||
                             previousBest < pRadar->getFinalScore() ) {
                            pRanking->storeSplitTimes(
                                spotHashcode,
                                uint32_t( pRadar->getNumRadars() ), // +0x6bc
                                pRadar->getRadarRecords() );        // +0x24
                        }
                    }
                } else if ( pRadar->getBaseScore() != 0 ) {
                    pRanking->submitScoreResult(
                        spotHashcode, cup,
                        float( pRadar->getBaseScore() ) *
                            kMillimetersToMeters );
                }
            }
        } else if ( mechType == MT_Race ) {
            rewardMoney =
                int32_t( pEventParameters->getRewardMoney().getValue() );
            sortParticipantsByScore( false );
            if ( !isEndlessLoopRace() ) {
                cup = pComponents[CS_RaceHUD]->fillResults( pPlayer );
                if ( mechID == kSoloSpotType &&
                     pEventParameters->getNumOpponents().getValue() == 1 ) {
                    bDuel = true;
                }
            } else {
                cup = pComponents[CS_Eliminator]->fillResults( pPlayer );
                if ( cup != 1 ) {
                    rewardMoney = 0;
                }
            }
            if ( pTime->getFinalTime() > 0.0f &&
                 !gpScenaric->isInstantChallenge() ) {
                pRanking->submitTimeResult( spotHashcode, cup,
                                            pTime->getFinalTime() );
            }
            if ( gpScenaric->isInstantChallenge() ) {
                OTDU_UNIMPLEMENTED; // TODO: ONLINE (instant challenge stake,
                                    // /:IsEnoughMoney)
            }
            if ( pComponents[CS_InstantChallenge] != nullptr ) {
                OTDU_UNIMPLEMENTED; // TODO: ONLINE (FUN_00729740)
            }
        }
        if ( cup == 1 ) {
            bRaceWon = true;
        }

        if ( gpScenaric->isInstantChallenge() ||
             mechType != MT_ExtraChallenge ) {
            if ( !gpScenaric->isInstantChallenge() ) {
                showPlayerRecord( spotHashcode );
            }
            GameParticipant *pLocal = ( pGameSession != nullptr )
                                          ? pGameSession->getLocalParticipant()
                                          : nullptr;
            if ( mechType != MT_ExtraChallenge &&
                 !gpScenaric->isInstantChallenge() && !bUnknown75C &&
                 !gpScenaric->isTrainingLevel() && pLocal != nullptr &&
                 !pLocal->isRemote() ) {
                OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (DAT_011e28d9 ranked
                                         // session flag)
                gpPlayerData->getLoadedSave()
                    ->getExtraChallenges()
                    ->countChallengeResult( mechID, cup == 1, false );
            }
        }
        gpPlayerData->getLoadedSave()->getAchievements()->notify(
            ACF_Challenge | ACF_Road | ACF_Collection, false );
        gpPlayerData->requestSave( 2 );
    }

    if ( mechType != MT_ExtraChallenge ) {
        if ( cup == 0 || cup == -1 || rewardMoney < 1 || bUnknown75C ||
             gpScenaric->isFromDriveIn() ||
             pOwner->getGameMechID() != kSoloSpotType ||
             ( bDuel && cup != 1 ) ) {
            pPlayer->setVariableValue( "/:IsReward", 0 );
            char moneyText[0x40];
            pPlayer->setVariableValueNonLinked(
                "/:IsRewardMoney", FormatMoneyString( moneyText, 0 ) );
            pPlayer->setVariableValue( "/:MoneyTransfert", 0 );
        } else {
            if ( cup != 1 ) {
                rewardMoney = int32_t( float( rewardMoney ) *
                                       ( ( cup == 2 ) ? kSilverRewardRatio
                                                      : kBronzeRewardRatio ) );
            }
            pPlayer->setVariableValue( "/:IsReward", 1 );
            pPlayer->setVariableValue( "/:IsRewardMoney", 1 );
            char moneyText[0x40];
            pPlayer->setVariableValueNonLinked(
                "/:MoneyTransfert",
                FormatMoneyString( moneyText, rewardMoney ) );
            IPlayerData *pSave = gpPlayerData->getLoadedSave();
            pSave->getExtraChallenges()->addCounter( CounterIndex( 0xf08c ),
                                                     rewardMoney );
            Driver *pDriver = pSave->getDriver();
            pDriver->setPendingMoneyOperation(
                pDriver->getPendingMoneyOperation() + rewardMoney ); // +0x38
            gpPlayerData->requestSave( 2 );
        }
    }
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_00882f70 leaderboards)
}

void GameMechanism::setExtraChallengeName() {
    // FUN_007327e0
    Entity *pChallenge = gpScenaric->getActiveExtraChallenge();
    if ( pChallenge == nullptr ) {
        return;
    }
    const char *pName = "";
    switch ( pChallenge->getType() ) {
    case eEntityType::ET_SHOG:
        pName = "sgirl";
        break;
    case eEntityType::ET_CVOY:
        pName = "convoy";
        break;
    case eEntityType::ET_HIHI:
        pName = "hiker";
        break;
    case eEntityType::ET_TRAN:
        pName = "transporter";
        break;
    default:
        break;
    }
    GetHudPlayer()->setVariableValueNonLinked( "/:cur_gm", pName );
}

bool GameMechanism::setupHUD() {
    // FUN_00733300
    FlashPlayer *pHud = GetHudPlayer();
    if ( pHud == nullptr ) {
        return false;
    }

    pHud->setVariableValue(
        "/:IsRestart", int32_t( mechID != 0xc0c1 && mechID != kClubSpotType ) );
    const char *pGameMode = "timeAttack";
    if ( mechType == MT_Speed ) {
        pGameMode = "speed";
    } else if ( mechType == MT_Driving ) {
        pGameMode = "driving";
    } else if ( mechType == MT_Race ) {
        pGameMode = "race";
    }
    pHud->setVariableValueNonLinked( "/:cur_gm", pGameMode );
    setExtraChallengeName();

    FlashClip clip;
    if ( !pHud->getClipPlayer( "/HUD/time", &clip ) ) {
        return false;
    }
    if ( pHud->getClipPlayer( "/waiting", &clip ) ) {
        clip.setProperty( eFlashProperty::FP_Visible, true );
    }
    if ( pHud->getClipPlayer( "/test", &clip ) ) {
        clip.setProperty( eFlashProperty::FP_Visible, bUnknown75C );
    }

    EventParameters *pEventParameters = pOwner->getMissionDescription();
    if ( pHud->getClipPlayer( "/HUD/score", &clip ) ) {
        if ( mechType == MT_Speed ) {
            if ( pEventParameters->getStartChallenge().getValue() != 0 ) {
                clip.setProperty( eFlashProperty::FP_Visible, true );
                pHud->setVariableValueNonLinked( "/:Score", "0" );
                pHud->setVariableValueNonLinked(
                    "/:ScoreUnit",
                    gpDatabase->getCommonDB().CommonStrings
                        [gpPlayerData->isUsingImperialUnits() ? 0 : 1] );
            }
        } else {
            clip.setProperty( eFlashProperty::FP_Visible, false );
        }
    }

    if ( mechID == kSoloSpotType &&
         pHud->getClipPlayer( "/HUD/ref_score", &clip ) ) {
        char referenceText[0x24];
        if ( !gpScenaric->isFromDriveIn() ) {
            if ( mechType == MT_Speed || mechType == MT_TimeAttack ) {
                clip.setProperty( eFlashProperty::FP_Visible, true );
                if ( mechType != MT_Speed ) {
                    FormatRaceTime(
                        referenceText,
                        float( pEventParameters->getGoldTime().getValue() ) *
                            kMillisecondsToSeconds ); // timG +0x11c0
                    pHud->setVariableValueNonLinked( "/:TimeRef_IG",
                                                     referenceText );
                } else if ( pEventParameters->getStartChallenge().getValue() ==
                            0 ) {
                    clip.setProperty( eFlashProperty::FP_Visible, false );
                } else {
                    pHud->setVariableValueNonLinked(
                        "/:TimeRef_IG",
                        pEventParameters->getGoldSpeed()
                            .getDisplayText() ); // SP2R +0x1608 slot 0
                }
            } else {
                clip.setProperty( eFlashProperty::FP_Visible, false );
            }
        } else {
            clip.setProperty( eFlashProperty::FP_Visible, true );
            OTDU_UNIMPLEMENTED; // TODO: SCENARIC (drive-in best score text,
                                // "%.2f %s" or FUN_009af790)
        }
    }

    for ( GameMechComponent *pComponent : pComponents ) {
        if ( pComponent != nullptr ) {
            pComponent->setupHUD();
        }
    }

    const char *pChallengeName =
        pEventParameters->getDatabaseName().getValue() < 0
            ? pEventParameters->getNameBuffer() // +0xe28
            : pEventParameters->getDatabaseName()
                  .getDisplayText(); // dbna +0xf28
    if ( pChallengeName != nullptr ) {
        pHud->setVariableValueWritableLinked(
            "/:ChallengeName", const_cast<char *>( pChallengeName ) );
    }

    const char *pPauseQuit = gpDatabase->getStringByHashcode(
        0, gpTestDriveInstance->getPreviousGameMode() == GM_GameModeEdit
               ? kPauseQuitEditorHashcode
               : kPauseQuitHashcode );
    if ( pPauseQuit != nullptr ) {
        pHud->setVariableValueNonLinked( "/:PauseQuit", pPauseQuit );
    }

    if ( mechType != MT_ExtraChallenge ) {
        pHud->goToFrameAndPlay( 0, "/3_2_1_go", false );
    } else {
        Entity *pChallenge = gpScenaric->getActiveExtraChallenge();
        if ( pChallenge != nullptr ) {
            uint64_t nameHashcode = 0ull;
            switch ( pChallenge->getType() ) {
            case eEntityType::ET_SHOG:
                nameHashcode = kShoppingGirlNameHashcode;
                break;
            case eEntityType::ET_CVOY:
                nameHashcode = kConvoyNameHashcode;
                break;
            case eEntityType::ET_HIHI:
                nameHashcode = kHitchHikerNameHashcode;
                break;
            case eEntityType::ET_TRAN:
                nameHashcode = kTransporterNameHashcode;
                break;
            default:
                break;
            }
            const char *pExtraChallengeName =
                ( nameHashcode != 0ull )
                    ? gpDatabase->getStringByHashcode( 0, nameHashcode )
                    : nullptr;
            if ( pExtraChallengeName != nullptr ) {
                pHud->setVariableValue( "/:ChallengeName",
                                        pExtraChallengeName );
            }
            if ( pChallenge->getType() == eEntityType::ET_CVOY ) {
                getTimeComponent()->setTimeVisible( false ); // FUN_0072ff00
            }
        }
        pHud->goToFrameAndPlay( 0, "/go_extra", false );
    }
    pOwner->setBlockPauseByStart( true );
    return true;
}

void GameMechanism::resetRaceState() {
    // FUN_00731870
    EventParameters *pEventParameters = pOwner->getMissionDescription();
    if ( mechType != MT_ExtraChallenge &&
         pEventParameters->getCountdownMode().getValue() != 0 ) {
        static_cast<GameMechTime *>( pComponents[CS_Time] )
            ->setTimeLimit(
                float( pEventParameters->getCountdown().getValue() ) *
                kMillisecondsToSeconds ); // CtDw +0x1370
    }
    finishTime = 0.0f;
    firstFinishTime = 0.0f;
    timeOutDuration = kDefaultTimeOutDuration;
    endReason = 0;
    pScoreRankingHead = nullptr;
    numParticipantsAtStart = 2;
    firstFinisherIndex = -1;
    countdownDelay = kDefaultCountdownDelay;
    gpScenaric->getObjective( 3 ).Type = "";
    gpScenaric->getObjective( 4 ).Type = "";
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AUDIO (stop the GPS voice instance)

    MngActor &mngActor = pOwner->getActorManager();
    for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
        Entity *pActor = mngActor.getActor( i );
        if ( pActor->getType() != eEntityType::ET_CHPT ) {
            continue;
        }
        EntityCheckPoint *pCheckpoint =
            static_cast<EntityCheckPoint *>( pActor );
        if ( pCheckpoint->getIndex() < 0 ) {
            continue;
        }
        pCheckpoint->setFlags( pCheckpoint->getFlags() & ~2u );
        if ( pCheckpoint->getLEDData()->Type == 1 ) {
            pCheckpoint->setFlags( pCheckpoint->getFlags() | 1u );
        }
    }
}

bool GameMechanism::stateProcess( const int32_t state,
                                  const float deltaTime,
                                  const eStateProcess process ) {
    // FUN_00735e10
    for ( GameMechComponent *pComponent : pComponents ) {
        if ( pComponent != nullptr ) {
            pComponent->onMechStateProcess( state, deltaTime, process );
        }
    }

    switch ( state ) {
    case MS_WaitingHUD:
        return processWaitingHUD( process );
    case MS_InstallingResources:
        return processInstallingResources( process );
    case MS_LoadingMap:
        return processLoadingMap( process );
    case MS_WaitingOnlinePlayers:
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (0x73638c: "/waiting" clip,
                                 // session event 0x99, ready flag polling)
        return false;
    case MS_WaitingStart:
        return processWaitingStart( process );
    case MS_Running:
        return processRunning( process );
    case MS_Ranking:
        return processRanking( process );
    case MS_ExtraChallengeOutro:
        return processExtraChallengeOutro( process );
    case MS_ExtraChallengeSucceeded:
        return processExtraChallengeSucceeded( process );
    case MS_ExtraChallengeAborted:
        return processExtraChallengeAborted( process );
    case MS_Finished:
        return processFinished( process );
    case MS_SessionError:
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (0x736d37: session lost popup
                                 // FUN_009a03e0, player car phantom)
        return process == SP_Update;
    default:
        return false;
    }
}

bool GameMechanism::processWaitingHUD( const eStateProcess process ) {
    // FUN_00735e10
    switch ( process ) {
    case SP_Init: {
        memset( field_0x800, 0, sizeof( field_0x800 ) );
        if ( pGameSession->isOnline() ) {
            pGameSession->onRaceStopped();
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (FUN_00852650)
            numOnlineParticipants = pGameSession->getNumParticipants();
        }
        if ( pCountdownCinematic != nullptr &&
             ( pCountdownCinematic->getFlags() & 4u ) != 0 ) {
            pCountdownCinematic->stop( true );
        }
        placeParticipantsOnGrid( true );
        ShowLocalPlayerCar( true );
        resetParticipantsRaceData();
        pGameSession->onEvent( kSessionEventWaitingHUD );
        gpPhysics->setPlayerSlowDownForced( true );
        gpPhysics->setPlayerParkedForced( true );
        OTDU_UNIMPLEMENTED; // TODO: GSCamera
        if ( pUnknown1C != nullptr ) {
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (leaderboards FUN_00883760)
        }
        return true;
    }
    case SP_Update:
        updateStartLineCamera();
        if ( setupHUD() ) {
            if ( gpScenaric->isInstantChallenge() ) {
                OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (DAT_01214860,
                                         // "/:m_QuitHost?")
            }
            stateChange( MS_InstallingResources );
        }
        return true;
    case SP_Term:
        return true;
    default:
        return false;
    }
}

bool GameMechanism::processInstallingResources( const eStateProcess process ) {
    // FUN_00735e10
    switch ( process ) {
    case SP_Init: {
        FlashPlayer *pHud = GetHudPlayer();
        pHud->setVariableValue( "/:IsIGReplay", 1 );
        if ( pOwner->getGameMechID() != kSoloSpotType &&
             !gpScenaric->isTrainingLevel() ) {
            pHud->showFrame( "in", "/waiting", true );
            const char *pWaitingText = gpDatabase->getStringByHashcode(
                0, kWaitingForPlayersHashcode );
            if ( pWaitingText != nullptr ) {
                pHud->setVariableValue( "/:m_WaitingForPlayers", pWaitingText );
            } else {
                pHud->setVariableValueNonLinked( "/:m_WaitingForPlayers",
                                                 "..." );
            }
            pHud->setVariableValue( "/:IsIGReplay", 0 );
        }
        pGameSession->onEvent( kSessionEventInstall );
        placeParticipantsOnGrid( true );
        if ( pGameSession->isOnline() ) {
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (FUN_0042e1c0)
        }
        return true;
    }
    case SP_Update:
        updateStartLineCamera();
        if ( pGameSession->isOnline() ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (install timeout)
        }
        if ( pGameSession->onEvent( kSessionEventPollInstall ) != 0 ) {
            OTDU_LOG_DEBUG( "in CGameMech::StateProcess STATE "
                            "[MS_ONLINE_RACE_Installing] Complete...\n" );
            stateChange( MS_LoadingMap );
        } else {
            OTDU_LOG_DEBUG( "in CGameMech::StateProcess STATE "
                            "[MS_ONLINE_RACE_Installing] Failed\n" );
        }
        return true;
    case SP_Term:
        return true;
    default:
        return false;
    }
}

bool GameMechanism::processLoadingMap( const eStateProcess process ) {
    // FUN_00735e10
    switch ( process ) {
    case SP_Init:
        OTDU_LOG_DEBUG( "OnInit(Loading_Map)\n" );
        return true;
    case SP_Update: {
        updateStartLineCamera();
        Eigen::Vector4f streamPosition = static_cast<GMMission *>( pOwner )
                                             ->getSpawnFrame()
                                             .row( 3 )
                                             .transpose();
        CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
        if ( pLocalCar != nullptr ) {
            streamPosition =
                pLocalCar->getBody()->WorldMatrix.row( 3 ).transpose();
        }
        if ( !gpWorld->isStreamedAround( &streamPosition, true, true ) ) {
            OTDU_LOG_DEBUG( "in CGameMech::StateProcess STATE "
                            "[MS_ONLINE_RACE_LoadingMap] waiting "
                            "WorldStreaming...\n" );
            return true;
        }

        if ( bRestarted && gpMngFade != nullptr ) {
            gpMngFade->startTransition(
                ( gpMngFade->getTransitionFlags() & ~0xau ) | 5u,
                kFadeDuration );
            gpMngFade->setRenderLogo( false ); // +0xec
            gpCP2PGameService->FreeRideMgr.bUnknownBool3 =
                static_cast<GMMission *>( pOwner )->getSavedFreeRideBool3();
            SetHUDMoviesPaused( false ); // level 1 movies, +0xad0 &= ~2
        }

        const bool bStatsRead = ( pUnknown1C == nullptr );
        if ( !bStatsRead && !gpScenaric->isTrainingLevel() &&
             mechType != MT_ExtraChallenge ) {
            return true;
        }

        OTDU_LOG_DEBUG( "in CGameMech::StateProcess STATE "
                        "[MS_ONLINE_RACE_LoadingMap] Complete...\n" );
        if ( pOwner->getGameMechID() != kSoloSpotType &&
             !gpScenaric->isTrainingLevel() ) {
            stateChange( MS_WaitingOnlinePlayers );
        } else {
            stateChange( MS_WaitingStart );
        }
        return true;
    }
    case SP_Term:
        pGameSession->onEvent( kSessionEventMapLoaded );
        placeParticipantsOnGrid( false );
        resetParticipantsRaceData();
        if ( gpMngGPS != nullptr ) {
            gpMngGPS->onPlayerJoined( pGameSession->getNumParticipants(), -1 );
        }
        return true;
    default:
        return false;
    }
}

bool GameMechanism::processWaitingStart( const eStateProcess process ) {
    // FUN_00735e10
    switch ( process ) {
    case SP_Init:
        OTDU_LOG_DEBUG( "OnInit(Waiting_Start)\n" );
        numParticipantsAtStart = pGameSession->getNumParticipants();
        gpPhysics->setPlayerSlowDownForced( false );
        gpPhysics->setPlayerParkedForced( true );
        startCountdown();
        pGameSession->sendRaceMessage( AiRaceMessage{ 0, ARM_Prepare } );
        placeParticipantsOnGrid( false );
        resetParticipantsRaceData();
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_00854760)
        return true;
    case SP_Update:
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_00854760)
        if ( bCountdownGo ) {
            if ( pOwner->getMissionDescription()->getCockpitView().getValue() ==
                 1 ) {              // Cock +0x1500
                OTDU_UNIMPLEMENTED; // TODO: GSCamera
                                    // (_force_player_cam_cockpit = 1)
            }
            stateChange( MS_Running );
            return true;
        }
        updateCountdownCamera();
        return true;
    case SP_Term:
        if ( nextState == MS_Running ) {
            pGameSession->sendRaceMessage( AiRaceMessage{ 0, ARM_Go } );
        }
        return true;
    default:
        return false;
    }
}

bool GameMechanism::processRunning( const eStateProcess process ) {
    // FUN_00735e10
    switch ( process ) {
    case SP_Init: {
        field_0x7d8 = false;
        MngActor &mngActor = pOwner->getActorManager();
        for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
            Entity *pActor = mngActor.getActor( i );
            if ( pActor->getType() == eEntityType::ET_CHPT &&
                 static_cast<EntityCheckPoint *>( pActor )->getIndex() >= 0 ) {
                pActor->setFlags( pActor->getFlags() | 2u );
            }
        }
        pGameSession->setLocalParticipantState( GameParticipant::PS_Racing );
        if ( pUnknown1C != nullptr ) {
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (leaderboards FUN_00882bf0)
        }
        pOwner->setPauseMenuLocked( false ); // +0x103228
        if ( gpMngInGameMenu != nullptr ) {
            gpMngInGameMenu->setEnabled( true ); // +0x24
        }
        {
            AiPool *pAIPool = gpAI->getAIPool();
            pAIPool->setTrafficPaused( false ); // +0xc
            pAIPool->setField0x7C( true );
            pAIPool->setField0x85( true );
        }
        OTDU_UNIMPLEMENTED; // TODO: GSAI (DAT_013d1a77 = 0)
        pGameSession->onRaceStarted();
        if ( mechType == MT_ExtraChallenge ) {
            OTDU_UNIMPLEMENTED; // TODO: GMMission (DAT_013d1a75 = 0)
        }
        gpPhysics->setForceFeedbackAllowed( true );
        return false;
    }
    case SP_Update: {
        CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
        const bool bCarFlagDD =
            ( pLocalCar != nullptr ) && GetCarStateFlagDD( pLocalCar );
        const bool bArrested = isLocalPlayerArrested();
        OTDU_UNIMPLEMENTED_ONCE; // TODO: GSAI (DAT_013d1a54 +0x290 flag)
        const bool bBotFlag = false;
        checkTimeOut();

        if ( false /* DAT_01214741 */ ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: GMMission (FUN_007be5b0)
        }
        pOwner->updateOdometerAutosave();
        pOwner->updateOdometer();

        if ( gpScenaric->isInstantChallenge() && mechID == kSoloSpotType &&
             pGameSession->getParticipant( 1 )
                     ->getSessionParams()
                     .LastCheckpoint == 2 ) {
            stateChange( MS_Finished );
        }

        int32_t successSlot = -1;
        int32_t failureSlot = -1;
        findFinishedComponents( successSlot, failureSlot );
        endReason = getComponentEndReason();

        const bool bExtraChallengeAbort =
            ( bArrested || bBotFlag ) && mechType == MT_ExtraChallenge;
        if ( mechType == MT_ExtraChallenge ) {
            if ( endReason == 0 && !bExtraChallengeAbort ) {
                return false;
            }
            bRaceWon = failureSlot < 0;
            if ( bBotFlag ) {
                bRaceWon = false;
            }
            if ( bCarFlagDD ) {
                if ( bArrested || bBotFlag ) {
                    bRaceWon = false;
                }
                stateChange( MS_ExtraChallengeAborted );
                return false;
            }
            stateChange( static_cast<GMMission *>( pOwner )
                                 ->isExtraChallengeSucceeded() // FUN_0073d220
                             ? MS_ExtraChallengeSucceeded
                             : MS_ExtraChallengeOutro );
            return false;
        }

        if ( endReason == 0 || bCarFlagDD ) {
            return false;
        }
        bRaceWon = failureSlot < 0;
        if ( pLocalCar != nullptr ) {
            if ( pGameSession->isOnline() ) {
                ShowLocalPlayerCar( true );
            }
            SetLocalPlayerAutopilot( true );
        }
        if ( gpScenaric->isInstantChallenge() ) {
            timeOutDuration = kICTimeOutDuration;
            countdownDelay = kICCountdownDelay;
        }
        stateChange( MS_Finished );
        return false;
    }
    case SP_Term:
        if ( gpMngInGameMenu != nullptr ) {
            gpMngInGameMenu->setEnabled( false );
        }
        gpPhysics->setForceFeedbackAllowed( false );
        if ( mechType != MT_Player ) {
            // DAT_012149c8 / DAT_012149e8
            gpScenaric->getObjective( 3 ).Type = "";
            gpScenaric->getObjective( 4 ).Type = "";
        }
        return false;
    default:
        return false;
    }
}

bool GameMechanism::processRanking( const eStateProcess process ) {
    // FUN_00735e10
    switch ( process ) {
    case SP_Init:
        static_cast<GMMission *>( pOwner )->setRankingShown(
            true ); // +0x107093
        gpAudio->changeMixState( gpAudio->getMixFlags( "MixMenu" ) );
        gpFlash->getFlashSound()->playFrontEndMusic( false );
        showResults();
        if ( pGameSession->isOnline() ) {
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (FUN_00853290, fade, GameSpy
                                // results FUN_00418bb0 / FUN_0087ff60, players
                                // reset FUN_008526a0)
        }
        pOwner->setBlockPauseByStart( true );
        OTDU_UNIMPLEMENTED; // TODO: SCENARIC (FUN_00732110 drive-in results)
        bDriveInMessageShown = false;
        if ( gpScenaric->isTrainingLevel() ) {
            bAutopilotRequested = true;
        }
        OTDU_UNIMPLEMENTED; // TODO: GSCamera (+0x939 = 0)
        return true;
    case SP_Update:
        if ( gpScenaric->isFromDriveIn() && !bDriveInMessageShown &&
             uint32_t( MonotonicToMs( GetTimeMonotonic() ) ) >
                 rankingStartTick + kDriveInMessageDelayMs ) {
            bDriveInMessageShown = true;
        }
        if ( mechID == kSoloSpotType && !bAutopilotFadeStarted &&
             mechType != MT_ExtraChallenge ) {
            const eReplayState replayState = gReplay.getState();
            if ( bAutopilotRequested &&
                 replayState != eReplayState::RS_StartPlayback &&
                 replayState != eReplayState::RS_Playback ) {
                if ( gpMngFade != nullptr ) {
                    gpMngFade->startTransition(
                        ( gpMngFade->getTransitionFlags() & ~0xeu ) | 1u,
                        kFadeDuration, &GameMechanism::OnRankingFadedOut,
                        nullptr, this ); // LAB_00735d90
                    bAutopilotFadeStarted = true;
                }
                bAutopilotRequested = false;
            } else if ( bReplayExitPending ) {
                startEndCinematicAtPlayerCar();
                bReplayExitPending = false;
            }
        }
        if ( pGameSession->isOnline() ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (stream fade, leaderboard
                                     // writes, 10 s session timeout)
        }
        return true;
    case SP_Term:
        gpFlash->getFlashSound()->stopFrontEndMusic();
        pGameSession->sendRaceMessage(
            AiRaceMessage{ 0, kRaceMessageRaceOver } );
        gpActiveGM->setPauseMenuLocked( false ); // +0x103228
        if ( gpMngInGameMenu != nullptr ) {
            gpMngInGameMenu->setEnabled( true );
        }
        OTDU_UNIMPLEMENTED; // TODO: GSCamera (+0x939 = 1)
        static_cast<GMMission *>( pOwner )->setRankingShown( false );
        return true;
    default:
        return false;
    }
}

void GameMechanism::OnRankingFadedOut( void *pData, void * /*pSource*/ ) {
    // LAB_00735d90
    GameMechanism *pMech = static_cast<GameMechanism *>( pData );
    if ( gpMngFade != nullptr ) {
        gpMngFade->startTransition(
            ( gpMngFade->getTransitionFlags() & ~0xau ) | 5u, kFadeDuration,
            nullptr /* LAB_00731850 */, nullptr, pMech );
    }
    if ( gReplay.getState() == eReplayState::RS_StartPlayback ||
         gReplay.isPlayingBack() ) {
        return;
    }
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr ) {
        return;
    }
    AiVehicle *pAutopilot =
        static_cast<AiVehicle *>( pLocalCar->getAIController() );
    if ( pAutopilot != nullptr ) {
        pMech->startEndCinematic(
            pAutopilot->getInfos().GroundMatrix ); // +0xf0
        return;
    }
    pMech->startEndCinematicAtPlayerCar();
}

bool GameMechanism::executeAction( const int32_t action,
                                   void *pSource,
                                   void * /*pData*/ ) {
    // FUN_00732e90
    if ( action == CE_Ended ) {
        if ( pSource == pCinematic ) {
            pCinematic = nullptr;
            bAutopilotFadeStarted = false;
            AiPool *pAIPool = gpAI->getAIPool();
            pAIPool->setTrafficPaused( false ); // +0xc
            pAIPool->setField0x7C( true );
            pAIPool->setField0x85( true );
            gpAI->setOpponentsHidden( false );
            if ( gpActiveGameMode->getCurrentGameMode() ==
                 eGameMode::GM_Mission ) {
                static_cast<GMMission *>( pOwner )
                    ->getBlockedRoads()
                    .setBlockersVisible( true );
            }
        }
    } else if ( action == CE_Started && pSource == pCinematic ) {
        GetHudPlayer()->setVariableValue( "/:IsCineLoading", 0 );
    }
    return true;
}

void GameMechanism::startEndCinematicAtPlayerCar() {
    // FUN_00735bc0
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr || pLocalCar->getPhysicsVehicle() == nullptr ) {
        return;
    }
    Matrix4x4f_t pose = pLocalCar->getPhysicsVehicle()->computeDisplayMatrix();
    pose.row( 3 ).head<3>() += GetWorldShift().head<3>().transpose();
    startEndCinematic( pose );
}

void GameMechanism::startEndCinematic( const Matrix4x4f_t &pose ) {
    // FUN_00735980
    Matrix4x4f_t frame = pose;
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar != nullptr ) {
        const Eigen::Vector4f position = pose.row( 3 ).transpose();
        SnapToRoadFrame( position, frame, eRoadSnapMode::RSM_KeepAwayFromEnds,
                         false, false );
        PhysicsVehicle *pVehicle = pLocalCar->getPhysicsVehicle();
        if ( pLocalCar->getAIController() == nullptr && pVehicle != nullptr ) {
            PhysicsLock lock;
            pVehicle->resetToTransform( frame, true );
        }
    }

    const char *pCinematicName = nullptr;
    if ( gpScenaric->isInstantChallenge() ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (C_WIN_L / C_LOOSE_L from the extra
                            // challenge cinematic bank)
    } else {
        pCinematicName = bRaceWon ? "C_VICTORY" : "C_DEFEAT";
    }
    pCinematic = ( pCinematicName != nullptr )
                     ? static_cast<EntityCinematic *>(
                           gpScenaric->getCinematicPackBank().findEntity(
                               eEntityType::ET_CINE, pCinematicName ) )
                     : nullptr;
    if ( pCinematic == nullptr ) {
        GetHudPlayer()->setVariableValue( "/:IsCineLoading", 0 );
        return;
    }

    gpAI->setTrafficCategories( false, 1 );
    gpAI->setOpponentsHidden( true );
    if ( gpActiveGameMode->getCurrentGameMode() == eGameMode::GM_Mission ) {
        static_cast<GMMission *>( pOwner )
            ->getBlockedRoads()
            .setBlockersVisible( false );
    }
    frame.row( 3 ).head<3>() -= GetWorldShift().head<3>().transpose();
    pCinematic->setTransformOffset( frame );  // +0x180, bCineOffset (+0x173)
    pCinematic->setGameModeInterface( this ); // +0x2598 = CGameMech +0x18
    pCinematic->setActorsDriveStreaming( false ); // +0x2338
    pCinematic->start( true );
}

bool GameMechanism::processExtraChallengeOutro( const eStateProcess process ) {
    // FUN_00735e10
    switch ( process ) {
    case SP_Init: {
        pOwner->setBlockPauseByStart( true );
        FlashPlayer *pHud = GetHudPlayer();
        SetClipVisible( pHud, "/HUD/time", false );
        SetClipVisible( pHud, "/HUD/time_cd", false );
        SetClipVisible( pHud, "/HUD/driving", false );
        static_cast<GMMission *>( pOwner )->endExtraChallenge(
            bRaceWon ); // FUN_007408d0 / FUN_007408b0
        return true;
    }
    case SP_Update:
        if ( !static_cast<GMMission *>( pOwner )
                  ->isExtraChallengeOutroPending() ) {
            stateChange( MS_Ranking );
        }
        return true;
    case SP_Term:
        gotoResultsFrame();
        return true;
    default:
        return false;
    }
}

bool GameMechanism::processExtraChallengeSucceeded(
    const eStateProcess process ) {
    // FUN_00735e10
    switch ( process ) {
    case SP_Init: {
        pOwner->setBlockPauseByStart( true );
        FlashPlayer *pHud = GetHudPlayer();
        SetClipVisible( pHud, "/HUD/time", false );
        SetClipVisible( pHud, "/HUD/time_cd", false );
        SetClipVisible( pHud, "/HUD/driving", false );
        SetLocalPlayerAutopilot( true );
        return false;
    }
    case SP_Update:
        if ( !static_cast<GMMission *>( pOwner )
                  ->isExtraChallengeSucceeded() ) {
            stateChange( MS_ExtraChallengeOutro );
        }
        return true;
    case SP_Term:
        SetLocalPlayerAutopilot( false );
        return false;
    default:
        return false;
    }
}

bool GameMechanism::processExtraChallengeAborted(
    const eStateProcess process ) {
    // FUN_00735e10
    switch ( process ) {
    case SP_Init: {
        FlashPlayer *pHud = GetHudPlayer();
        SetClipVisible( pHud, "/HUD/time", false );
        SetClipVisible( pHud, "/HUD/time_cd", false );
        SetClipVisible( pHud, "/HUD/driving", false );
        pOwner->setBlockPauseByStart( true );
        return false;
    }
    case SP_Update: {
        CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
        if ( pLocalCar == nullptr || !GetCarStateFlagDD( pLocalCar ) ) {
            stateChange( MS_ExtraChallengeOutro );
        }
        return true;
    }
    default:
        return false;
    }
}

bool GameMechanism::processFinished( const eStateProcess process ) {
    // FUN_00735e10
    FlashPlayer *pHud = GetHudPlayer();
    switch ( process ) {
    case SP_Init:
        pOwner->setBlockPauseByStart( true );
        OTDU_UNIMPLEMENTED; // TODO: GSCamera (_force_player_cam_cockpit = 0)
        if ( false /* DAT_011ecfe0 spectator */ ) {
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (FUN_0085f540, /:IsSpectator)
        }
        finishTime = gpTimer->GameTotalTime -
                     static_cast<GameMechTime *>( pComponents[CS_Time] )
                         ->getStartGameTime(); // +0x28
        gpPhysics->setPlayerSlowDownForced( true );
        sendPlayerFinished();
        if ( isCompetitiveEvent() ) {
            pHud->setVariableValue( "/:IsRaceOver", 0 );
            pHud->setVariableValueNonLinked( "/:Countdown", " " );
        }
        if ( mechID == kSoloSpotType && mechType != MT_ExtraChallenge ) {
            pHud->setVariableValue( "/:IsCineLoading", 1 );
        }
        gotoResultsFrame();
        return false;
    case SP_Update: {
        const float elapsed =
            static_cast<GameMechTime *>( pComponents[CS_Time] )
                ->getElapsedTime() -
            firstFinishTime;
        const float remaining = timeOutDuration - elapsed;

        const bool bCompetitive = isCompetitiveEvent();
        const bool bWaitForOthers =
            ( bCompetitive && areOthersStillRacing() ) ||
            !( elapsed > kRaceOverDelay );
        if ( !bWaitForOthers ) {
            CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
            if ( pLocalCar != nullptr &&
                 pLocalCar->getPhysicsVehicle() != nullptr &&
                 pGameSession->isOnline() ) {
                ShowLocalPlayerCar( false );
                SetLocalPlayerAutopilot( false );
                PauseLocalPlayerPhysics();
            }
            pGameSession->onRaceStopped();
            pHud->setVariableValue( "/:IsRaceOver", 1 );
            pHud->setVariableValue( "/:ShowCountDown", 0 );
            pHud->setVariableValueNonLinked( "/:m_WaitingForPlayers", " " );
            stateChange( MS_Ranking );
            return false;
        }

        if ( areOthersStillRacing() ) {
            updateWaitingForPlayersText( pGameSession->countParticipantsInState(
                GameParticipant::PS_Racing ) );
        } else {
            pHud->setVariableValueNonLinked( "/:m_WaitingForPlayers", " " );
        }
        if ( false /* DAT_01214741 */ ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: GMMission (FUN_007be5b0)
        }
        pOwner->updateOdometerAutosave();
        pOwner->updateOdometer();
        if ( pGameSession->isOnline() ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (spectator switch after
                                     // 1.5 s: phantom car, FUN_0085f410,
                                     // /:IsSpectator)
        }

        if ( firstFinisherIndex >= 0 && countdownDelay >= remaining ) {
            pHud->setVariableValue( "/:ShowCountDown", 1 );
            const int32_t seconds = int32_t( remaining ) + 1;
            pHud->setVariableValue( "/:Countdown", seconds > 0 ? seconds : 0 );
        }
        return false;
    }
    case SP_Term:
        if ( false /* DAT_011ecfe0 spectator */ ) {
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (FUN_0085f540, /:IsSpectator)
        }
        if ( pGameSession->isOnline() ) {
            ShowLocalPlayerCar( false );
            pGameSession->onRaceStopped();
        }
        return false;
    default:
        return false;
    }
}
