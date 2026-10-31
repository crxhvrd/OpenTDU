#include "shared.h"
#include "ai_cop_manager.h"

#include "ai/ai_config.h"
#include "ai/ai_pool.h"
#include "ai/ai_road_map.h"
#include "ai/ai_road_shape.h"
#include "ai/ai_veh_nav_opponent.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "audio/gs_audio.h"
#include "audio/implementation/ed_audio_sample.h"
#include "audio/implementation/ed_audio_stream_entry.h"
#include "config/gs_config.h"
#include "core/format.h"
#include "core/maths_helper.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/mng_fade.h"
#include "game/game_mechanism.h"
#include "game/gm_base.h"
#include "game/gm_world.h"
#include "game/mng_hud.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/game_intro_tutorial.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "gs_timer.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_data/achievements_data.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_extra_challenges.h"
#include "render/camera/gs_camera.h"
#include "tdu_instance.h"
#include "world/gs_world_streaming.h"

AiCopMng gAiCopMng; // DAT_01450ca0

static constexpr float kFarAwayDistance = 1.0e8f; // DAT_00f41a50
static constexpr int32_t kPoliceCarID = 0x7f0;
static constexpr float kMillimetersToMeters = 0.001f; // FLOAT_00f4167c

static AiVehicleList &GetPoliceList( const eAiVehicleCategory category ) {
    return gpAI->getAIPool()->getList( AiPool::AVL_FirstCategory +
                                       static_cast<int32_t>( category ) );
}

static constexpr float kArrestFadeDuration = 0.5f; // FLOAT_00f41674
static constexpr float kVoiceRepeatDelay = 30.0f;  // DAT_00f416e0
static constexpr float kWarnVoiceMinDelay = 3.0f;
static constexpr float kWarnVoiceMaxDelay = 10.0f;
static constexpr const char *kCopVoiceClientName = "ClientVoiceCop";
static constexpr const char *kCopVoiceDescription = "AiCopMng";
static constexpr const char *kCopVoiceInstanceName = "Police Voice Instance";
static constexpr const char *kCopVoiceHPInstanceName =
    "Police Voice Instance HP";

// PTR_s_V_F_Police_Dispatcher_001_wav_00f7c400
static constexpr const char *kCopVoices[AiCopMng::kNumCopVoices] = {
    "V_F_Police_Dispatcher_001.wav", "V_F_Police_Dispatcher_002.wav",
    "V_F_Police_Dispatcher_003.wav", "V_F_Police_Dispatcher_004.wav",
    "V_F_Police_Dispatcher_005.wav", "V_F_Police_Dispatcher_006.wav",
    "V_F_Police_Dispatcher_007.wav", "V_F_Police_Dispatcher_008.wav",
    "V_F_Police_Dispatcher_009.wav", "V_F_Police_Dispatcher_010.wav",
    "V_F_Police_Dispatcher_011.wav", "V_F_Police_Dispatcher_012.wav",
    "V_F_Police_Dispatcher_013.wav", "V_F_Police_Dispatcher_014.wav",
    "V_F_Police_Dispatcher_015.wav", "V_F_Police_Dispatcher_016.wav",
    "V_F_Police_Dispatcher_017.wav", "V_F_Police_Dispatcher_018.wav",
    "V_F_Police_Dispatcher_019.wav", "V_F_Police_Dispatcher_020.wav",
    "V_F_Police_Dispatcher_021.wav", "V_F_Police_Dispatcher_022.wav",
    "V_F_Police_Dispatcher_023.wav", "V_F_Police_Dispatcher_024.wav",
    "V_F_Police_Dispatcher_025.wav", "V_F_Police_Dispatcher_026.wav",
    "V_F_Police_Dispatcher_027.wav", "V_F_Police_Dispatcher_028.wav",
    "V_F_Police_Dispatcher_029.wav", "V_F_Police_Dispatcher_030.wav",
    "V_F_Police_Dispatcher_031.wav", "V_F_Police_Dispatcher_032.wav",
    "V_F_Police_Dispatcher_033.wav", "V_F_Police_Dispatcher_034.wav",
    "V_F_Police_Dispatcher_035.wav", "V_F_Police_Dispatcher_036.wav",
    "V_F_Police_Dispatcher_037.wav", "V_F_Police_Dispatcher_038.wav",
    "V_F_Police_Dispatcher_039.wav", "V_F_Police_Dispatcher_040.wav",
    "V_F_Police_Dispatcher_041.wav", "V_F_Police_Dispatcher_042.wav",
    "V_F_Police_Dispatcher_043.wav", "V_F_Police_Dispatcher_044.wav",
    "V_F_Police_Dispatcher_045.wav", "V_F_Police_Dispatcher_046.wav",
    "V_M_Police_Car1_001.wav",       "V_M_Police_Car1_002.wav",
    "V_F_Police_Dispatcher_047.wav", "V_F_Police_Dispatcher_048.wav",
    "V_M_Police_Car1_003.wav",       "V_M_Police_Car1_004.wav",
    "V_M_Police_Car2_001.wav",       "V_M_Police_Car2_002.wav",
    "V_M_Police_Car1_005.wav",       "V_F_Police_Dispatcher_049.wav",
    "V_M_Police_Car3_001.wav",       "V_M_Police_Car2_003.wav",
    "V_M_Police_Car3_002.wav",       "V_F_Police_Dispatcher_050.wav",
    "V_F_Police_Dispatcher_051.wav", "V_M_Police_Car1_006.wav",
    "V_M_Police_Car1_007.wav",       "V_M_Police_Car2_004.wav",
    "V_M_Police_Car3_003.wav",       "V_M_Police_Car1_008.wav",
    "V_M_Police_Car2_005.wav",       "V_M_Police_Car3_004.wav",
    "V_F_Police_Dispatcher_052.wav", "V_M_Police_Car3_005.wav",
    "V_F_Police_Dispatcher_053.wav", "V_M_Police_Car2_006.wav",
    "V_F_Police_Dispatcher_054.wav", "V_F_Police_Dispatcher_055.wav",
    "V_M_Police_Car1_009.wav",       "V_F_Police_Dispatcher_056.wav",
    "V_M_Police_Car1_010.wav",       "V_M_Police_Car2_007.wav",
    "V_M_Police_Car3_006.wav",       "V_M_Police_Car1_011.wav",
    "V_M_Police_Car2_008.wav",       "V_M_Police_Car3_007.wav",
    "V_M_Police_Car1_012.wav",       "V_M_Police_Car2_009.wav",
    "V_M_Police_Car3_008.wav",       "V_M_Police_Car1_013.wav",
    "V_M_Police_Car2_010.wav",       "V_M_Police_Car3_009.wav",
    "V_M_Police_Car1_014.wav",       "V_M_Police_Car2_011.wav",
    "V_M_Police_Car3_010.wav",       "V_M_Police_Car1_015.wav",
    "V_M_Police_Car2_012.wav",       "V_M_Police_Car3_011.wav",
};

enum eCopVoice : int32_t {
    CV_InfLimitCar = 0x2b,
    CV_InfLimitBike = 0x2c,
    CV_SearchToChaseA = 0x2e,
    CV_SearchToChaseB = 0x2f,
    CV_SearchToChaseReplyA = 0x30,
    CV_SearchToChaseReplyB = 0x31,
    CV_AskFor2ndCarA = 0x32,
    CV_AskFor2ndCarB = 0x33,
    CV_AskFor2ndCarReplyA = 0x34,
    CV_AskFor2ndCarReplyB = 0x35,
    CV_AskFor3rdCar = 0x36,
    CV_AskFor3rdCarReply1 = 0x37,
    CV_AskFor3rdCarReply2 = 0x38,
    CV_AskFor3rdCarReply3 = 0x39,
    CV_AskForPoliceDam = 0x3a,
    CV_AskForPoliceDamReply = 0x3b,
    CV_AskForAggressive = 0x3c,
    CV_WitnessedOffence = 0x3d,
    CV_ArrestCar1 = 0x3e,
    CV_ArrestCar2 = 0x3f,
    CV_ArrestCar3 = 0x40,
    CV_ChaseToSearch1 = 0x41,
    CV_ChaseToSearch2 = 0x42,
    CV_ChaseToSearch3 = 0x43,
    CV_ChaseToSearchReply = 0x44,
    CV_SearchToPatrol2 = 0x45,
    CV_SearchToPatrol2Reply = 0x46,
    CV_SearchToPatrol1 = 0x47,
    CV_SearchToPatrol1Reply = 0x48,
    CV_SearchToPatrol0 = 0x49,
    CV_SearchToPatrol0Reply = 0x4a,
    CV_SearchToPatrol0End = 0x4b,
    CV_FirstWarnChased = 0x4c,
    CV_LastWarnChased = 0x54,
};

struct CopBrandVoice {
    const char *pBrandID;
    int32_t Voice;
};

static constexpr CopBrandVoice kCopBrandVoices[] = {
    {"AC",       0x00},
    {"ALFA",     0x02},
    {"ASCA",     0x03},
    {"ASTON",    0x01},
    {"AUDI",     0x04},
    {"CADILAC",  0x05},
    {"CATER",    0x06},
    {"CHEVY",    0x07},
    {"CHRYS",    0x08},
    {"DODGE",    0x09},
    {"DUCAT",    0x0a},
    {"EDO",      0x0b},
    {"FARB",     0x0c},
    {"FERRARI",  0x0d},
    {"FORD",     0x0e},
    {"JAG",      0x10},
    {"KAWA",     0x11},
    {"KOEN",     0x12},
    {"LAMBO",    0x13},
    {"LOTUS",    0x15},
    {"MASER",    0x16},
    {"MAZDA",    0x17},
    {"MCLAREN",  0x18},
    {"MERCO",    0x19},
    {"MV",       0x1a},
    {"NISSAN",   0x1b},
    {"NOBLE",    0x1c},
    {"PAGANI",   0x1d},
    {"PONT",     0x1e},
    {"RUF",      0x1f},
    {"SALEEN",   0x20},
    {"SATURN",   0x21},
    {"SHELBY",   0x22},
    {"SPYK",     0x23},
    {"SUZ",      0x24},
    {"TRIUMPH",  0x25},
    {"TVR",      0x26},
    {"VAG",      0x27},
    {"Wiesmann", 0x28},
    {"YES",      0x2a},
};

static bool IsWarnChasedVoice( const int32_t voice ) {
    return voice >= CV_FirstWarnChased && voice <= CV_LastWarnChased;
}

static bool RandomHalf( RandomGen &random ) {
    return random.getRandomUint32() < 0x7fffffffu;
}
static constexpr float kArrestTeleportMinT = 0.1f; // FLOAT_00f41668
static constexpr float kArrestTeleportMaxT = 0.9f; // DAT_00f416a0
static constexpr int32_t kFinesPaidCounter = 0xf0f4;

static Eigen::Vector4f GetWorldShift() {
    if ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() ) {
        return gpWorldStreaming->getPosition();
    }
    return Eigen::Vector4f::Zero();
}

static void ComputeArrestTeleportLocation( AiVehicle *pVehicle,
                                           AiRoadIt &outIt,
                                           float &outWidthRatio ) {
    // FUN_00911c80
    if ( pVehicle == nullptr ) {
        return;
    }
    const Matrix4x4f_t groundMatrix = pVehicle->getInfos().GroundMatrix;
    AiVehController *pController = pVehicle->getController();
    if ( pController == nullptr ) {
        return;
    }

    AiRoadIt frontFrom;
    AiRoadIt frontTo;
    pController->getFront( frontFrom, frontTo );
    if ( frontFrom.pRoad == nullptr ) {
        return;
    }

    AiRoadIt location;
    Matrix4x4f_t projectedFrame = Matrix4x4f_t::Identity();
    ProjectOnRoadShape( groundMatrix, location, &projectedFrame, false,
                        frontFrom.pRoad->getShape() );
    location.setWay( frontFrom.bWay );

    const float halfWidth =
        pVehicle->getCarState()->getBody()->BodyDimensions.x() * 0.5f + 1.0f;
    const float widthRatio =
        halfWidth / ( frontFrom.getEntity()->pProfile->InnerWidth * 0.5f );
    outWidthRatio = frontFrom.bWay ? 1.0f - widthRatio : widthRatio - 1.0f;

    if ( location.isSamePosition( frontFrom, 0.0f ) ) {
        if ( location.T == 0.0f ) {
            location.T = kArrestTeleportMinT;
        } else if ( location.T == 1.0f ) {
            location.T = kArrestTeleportMaxT;
        }
    }
    outIt = location;
}

static int32_t ComputeFineLevelFactor() {
    // FUN_00910840
    if ( gpActiveGameMode->getCurrentGameMode() == GM_Mission &&
         gpActiveGM->getGameMechType() != GameMechanism::MT_ExtraChallenge ) {
        return 0;
    }

    switch (
        gpPlayerData->getLoadedSave()->getAchievements()->getPlayerLevel() ) {
    case 1:
        return 2;
    case 2:
        return 5;
    case 3:
        return 7;
    case 4:
    case 5:
        return 15;
    default:
        return 1;
    }
}

// TODO: Move to right cpp
static void ShowFineHud( FlashPlayer *pHudPlayer, const AiCopMng::Fine &fine ) {
    // FUN_006ee150
    char moneyString[32];
    char totalString[32];
    pHudPlayer->setVariableValue( "/:car_crashed", fine.NumCarsCrashed );
    FormatMoneyString( moneyString, fine.CarsTotalValue );
    pHudPlayer->setVariableValueNonLinked( "/:car_total", moneyString );
    pHudPlayer->setVariableValue( "/:sign_pulled_up", fine.NumSettingsCrashed );
    FormatMoneyString( moneyString, fine.SettingsTotalValue );
    pHudPlayer->setVariableValueNonLinked( "/:sign_total", moneyString );
    pHudPlayer->setVariableValueVariadic( "/:level_factor", "x %d",
                                          fine.LevelFactor );
    FormatMoneyString( totalString, fine.TotalValue );
    pHudPlayer->setVariableValueNonLinked( "/:Total", totalString );
    pHudPlayer->setVariableValue( "/:IsPolice", 1 );
    pHudPlayer->showFrame( "cinematik", "/", true );
}

static bool IsCopSpawnAllowedByTutorial() {
    const int32_t tutorialStep = gpScenaric->getIntroTutorial().getStep();
    return tutorialStep == kTutorialStatePending ||
           tutorialStep == kTutorialStateRunning;
}

AiCopMng::AiCopMng()
    : bEnabled( false ),
      pArrestCinematic( nullptr ),
      pArrestCinematicBank( nullptr ),
      bRequestPoliceDam( false ),
      bNeedPoliceDam( false ),
      damRoadIt(),
      numPoliceDams( 0 ),
      numWantedPoliceDams( 0 ),
      pPoliceDamTarget( nullptr ),
      nextDamLateralRatio( kNoDamLateralRatio ),
      pPoliceTarget( nullptr ),
      playerToCopMinDistance( kFarAwayDistance ),
      bCanBeAggressive( true ),
      bCanDoPoliceDams( true ),
      lastGameCounterValue( 0.0f ),
      pAudioClient( nullptr ),
      radio(),
      highPriorityRadio(),
      lastWarnTime( 0.0f ),
      copVoicesTime{},
      pInstantFinePlayer( nullptr ),
      interceptedMessage( 0 ),
      pInterceptedPlayer( nullptr ),
      pInterceptedBookmark( nullptr ),
      currentFine(),
      bSendPlayerToJail( false ),
      previousNumWantedChasers( 0 ),
      previousNumChasers( 0 ),
      bWasChased( false ),
      bWasSearched( false ),
      savedInfringement(),
      bInfringementSaved( false ) {
    // FUN_00910a40
    reset();
}

AiCopMng::~AiCopMng() {}

void AiCopMng::initialize() {
    // FUN_00911b60
    reset();
    if ( pAudioClient == nullptr && gpAudio != nullptr ) {
        pAudioClient = gpAudio->createClient( kCopVoiceClientName );
        if ( pAudioClient != nullptr &&
             pAudioClient->getStereoPipeline() != nullptr ) {
            for ( RadioChannel *pChannel : { &radio, &highPriorityRadio } ) {
                pChannel->pInstance = new edAudioStreamEntry();
                pAudioClient->getStereoPipeline()->addStereoEntry(
                    pChannel->pInstance );
                pChannel->pInstance->debugSetDescription(
                    kCopVoiceDescription );
            }
        }
    }
    OTDU_UNIMPLEMENTED_ONCE; // TODO: GENERAL flash player (instant fine)
    pInstantFinePlayer = nullptr;
}

void AiCopMng::reset() {
    // FUN_009108e0
    bEnabled = false;
    pArrestCinematic = nullptr;
    pArrestCinematicBank = nullptr;
    bRequestPoliceDam = false;
    bNeedPoliceDam = false;
    damRoadIt = AiRoadIt();
    nextDamLateralRatio = kNoDamLateralRatio;
    pPoliceDamTarget = nullptr;
    pPoliceTarget = nullptr;
    numPoliceDams = 0;
    numWantedPoliceDams = 0;
    playerToCopMinDistance = kFarAwayDistance;
    radio.LastVoice = -1;
    highPriorityRadio.LastVoice = -1;
    lastWarnTime = 0.0f;
    lastGameCounterValue = 0.0f;
    interceptedMessage = 0;
    pInterceptedPlayer = nullptr;
    pInterceptedBookmark = nullptr;
    bSendPlayerToJail = false;
    currentFine = Fine();
    bCanBeAggressive = true;
    bCanDoPoliceDams = true;
    previousNumWantedChasers = 0;
    previousNumChasers = 0;
    bWasChased = false;
    bWasSearched = false;
    bInfringementSaved = false;
    savedInfringement.reset();
    for ( RadioChannel *pChannel : { &radio, &highPriorityRadio } ) {
        // A pending load keeps its buffer: the load callback discards it.
        if ( pChannel->pInstance != nullptr && !pChannel->bLoading ) {
            releaseVoice( *pChannel, true );
        }
    }
    radio.bLoading = false;
    copVoicesTime.fill( 0.0f );
}

void AiCopMng::terminate() {
    for ( RadioChannel *pChannel : { &radio, &highPriorityRadio } ) {
        pChannel->LoadGeneration++;
        if ( pChannel->bLoading && gpFile != nullptr &&
             !gpFile->cancelAsyncRequest( pChannel ) ) {
            // The worker thread is still writing into the buffer.
            pChannel->pBuffer = nullptr;
        }
        pChannel->bLoading = false;

        if ( pChannel->pInstance != nullptr ) {
            pChannel->pInstance->stop( 0.0f );
            pChannel->pInstance->setSample( nullptr );
            if ( pAudioClient != nullptr &&
                 pAudioClient->getStereoPipeline() != nullptr ) {
                pAudioClient->getStereoPipeline()->removeStereoEntry(
                    pChannel->pInstance );
            }
            delete pChannel->pInstance;
            pChannel->pInstance = nullptr;
        }
        pChannel->pSample.reset();
        if ( pChannel->pBuffer != nullptr ) {
            TestDrive::Free( pChannel->pBuffer );
            pChannel->pBuffer = nullptr;
        }
    }

    if ( pAudioClient != nullptr && gpAudio != nullptr ) {
        gpAudio->releaseClient( pAudioClient );
    }
    pAudioClient = nullptr;
}

void AiCopMng::tick() {
    // FUN_00915010
    GameMode *pActiveGameMode = gpActiveGameMode;
    TestDriveGameInstance *pInstance = gpTestDriveInstance;
    if ( pActiveGameMode != nullptr &&
         ( pActiveGameMode->getCurrentGameMode() == GM_FreeRide ||
           pActiveGameMode->getCurrentGameMode() == GM_TestCar ) &&
         bSendPlayerToJail ) {
        bSendPlayerToJail = false;
        pInstance->setNextGameMode( GM_Jail );
        pActiveGameMode->requestExit();
    }
    if ( pInstance->getActiveGameMode() == GM_Jail ) {
        return;
    }

    if ( pInstantFinePlayer == nullptr ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: GENERAL flash player (getFlashPlayer)
    }

    const float totalTime = gpTimer->GameTotalTime;
    if ( lastGameCounterValue <= totalTime ) {
        lastGameCounterValue = totalTime;
    } else {
        rebaseTimers();
    }

    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    playerToCopMinDistance = ( pPlayerCar != nullptr )
                                 ? computeClosestCopDistance( pPlayerCar )
                                 : kFarAwayDistance;
    if ( pPlayerCar == nullptr ) {
        return;
    }

    static constexpr bool bOnlineSession = false;
    if ( bOnlineSession ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (arrest fine FUN_00914a90)
        return;
    }

    updateChaseState();
    updateVoiceEvents();

    PoliceState &infringement = pPlayerCar->getPoliceState();
    if ( !infringement.isChased() && !infringement.isSearched() &&
         !infringement.isWanted() ) {
        pPoliceTarget = nullptr;
    } else {
        pPoliceTarget = pPlayerCar;
    }

    numPoliceDams = static_cast<uint32_t>(
        GetPoliceList( eAiVehicleCategory::AVC_CopDam ).Count );
    if ( damRoadIt.pRoad != nullptr ) {
        updateNumWantedPoliceDams();

        bool bDamComplete = false;
        switch ( numWantedPoliceDams ) {
        case 3:
            if ( numPoliceDams > 2 ) {
                bDamComplete = true;
            } else if ( numPoliceDams == 1 ) {
                nextDamLateralRatio = 0.0f;
            } else if ( numPoliceDams == 2 ) {
                nextDamLateralRatio = 0.66f;
            }
            break;
        case 2:
            if ( numPoliceDams > 1 ) {
                bDamComplete = true;
            } else if ( numPoliceDams == 1 ) {
                nextDamLateralRatio = 0.5f;
            }
            break;
        case 1:
            bDamComplete = ( numPoliceDams != 0 );
            break;
        default:
            bDamComplete = true;
            break;
        }

        if ( bDamComplete ) {
            numWantedPoliceDams = 0;
            bNeedPoliceDam = false;
            bRequestPoliceDam = false;
            nextDamLateralRatio = kNoDamLateralRatio;
        }
    }
    if ( !bNeedPoliceDam ) {
        damRoadIt = AiRoadIt();
    }

    if ( !radio.bLoading && radio.pInstance != nullptr &&
         radio.pInstance->isStopped() && radio.pBuffer != nullptr ) {
        releaseVoice( radio, true );
        onVoiceFinished();
    }
    if ( !highPriorityRadio.bLoading &&
         highPriorityRadio.pInstance != nullptr &&
         highPriorityRadio.pInstance->isStopped() &&
         highPriorityRadio.pBuffer != nullptr ) {
        releaseVoice( highPriorityRadio, true );
        onVoiceFinished();
    }
}

void AiCopMng::rebaseTimers() {
    // FUN_00910210
    const float delta = lastGameCounterValue - gpTimer->GameTotalTime;
    lastWarnTime -= delta;
    for ( float &voiceTime : copVoicesTime ) {
        voiceTime -= delta;
    }
    lastGameCounterValue = gpTimer->GameTotalTime;
}

float AiCopMng::computeClosestCopDistance( CarState *pCar ) const {
    // FUN_00910ba0
    const Eigen::Vector4f position = pCar->getWorldPosition();
    float closestDistanceSq = kFarAwayDistance;

    const AiVehicleList &cops = GetPoliceList( eAiVehicleCategory::AVC_Cop );
    const int32_t numCops = cops.Count;
    for ( int32_t i = 0; i < numCops; i++ ) {
        const AiVehicle *pCop = cops.Items[i];
        if ( pCop == nullptr ) {
            break;
        }
        const Eigen::Vector4f delta =
            pCop->getCarState()->getWorldPosition() - position;
        const float distanceSq = delta.squaredNorm();
        if ( distanceSq < closestDistanceSq ) {
            closestDistanceSq = distanceSq;
        }
    }

    return ( numCops > 0 ) ? std::sqrt( closestDistanceSq ) : closestDistanceSq;
}

int32_t AiCopMng::computeNumWantedChasers( CarState *pTarget ) const {
    // FUN_00910d90
    AiPool *pAIPool = gpAI->getAIPool();
    if ( pAIPool->getMaxPolice() <= 0 ) {
        return 0;
    }

    PoliceState &infringement = pTarget->getPoliceState();
    int32_t numWantedChasers = 0;
    if ( infringement.computeOffenceLevel() >= gAIConfig.InfThirdCopLimit ) {
        numWantedChasers = 3;
    } else if ( infringement.computeOffenceLevel() >=
                gAIConfig.InfSecondCopLimit ) {
        numWantedChasers = 2;
    } else if ( infringement.computeOffenceLevel() >= gAIConfig.InfLimit ) {
        numWantedChasers = 1;
    }
    return std::min( numWantedChasers, pAIPool->getMaxPolice() );
}

int32_t AiCopMng::countChasers( const CarState *pTarget ) const {
    // FUN_00910e50
    int32_t numChasers = 0;
    const AiVehicleList &cops = GetPoliceList( eAiVehicleCategory::AVC_Cop );
    for ( int32_t i = 0; i < cops.Count; i++ ) {
        const AiVehicle *pCop = cops.Items[i];
        if ( pCop == nullptr ) {
            break;
        }
        if ( pCop->getBehaviourType() == 0 &&
             pCop->getPoliceBehaviour().getTarget() == pTarget &&
             !pCop->isSoftCollision() ) {
            numChasers++;
        }
    }
    return numChasers;
}

void AiCopMng::updateChaseState() {
    // FUN_00910ef0
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr ) {
        return;
    }

    PoliceState &infringement = pPlayerCar->getPoliceState();
    previousNumWantedChasers = infringement.getNumWantedChaser();
    previousNumChasers = infringement.getNumChasers();
    bWasChased = infringement.isChased();
    bWasSearched = infringement.isSearched();

    infringement.setNumWantedChaser( computeNumWantedChasers( pPlayerCar ) );
    const int32_t numChasers = countChasers( pPlayerCar );
    infringement.setNumChasers( numChasers );
    infringement.setChased( numChasers > 0 );
    if ( infringement.isSearched() && !infringement.isWanted() ) {
        infringement.stopSearch();
    }
}

void AiCopMng::updateVoiceEvents() {
    // FUN_00914730
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr ) {
        return;
    }

    PoliceState &infringement = pPlayerCar->getPoliceState();
    if ( infringement.isArrested() ) {
        for ( RadioChannel *pChannel : { &radio, &highPriorityRadio } ) {
            if ( pChannel->pInstance != nullptr &&
                 !pChannel->pInstance->isStopped() ) {
                pChannel->pInstance->stop( 0.0f );
            }
        }
    }

    if ( previousNumWantedChasers == 1 ) {
        if ( previousNumChasers == 1 &&
             infringement.getNumWantedChaser() == 2 ) {
            onVoiceEvent( eCopVoiceEvent::CVE_AskFor2ndCar );
        }
    } else if ( previousNumWantedChasers == 2 && previousNumChasers == 2 &&
                infringement.getNumWantedChaser() == 3 ) {
        onVoiceEvent( eCopVoiceEvent::CVE_AskFor3rdCar );
    }

    if ( bWasChased && !infringement.isChased() ) {
        onVoiceEvent( eCopVoiceEvent::CVE_ChaseToSearch );
        infringement.setSearched( true );
    }
    if ( bWasSearched && !infringement.isSearched() ) {
        onVoiceEvent( eCopVoiceEvent::CVE_SearchToPatrol );
    }
}

uint32_t AiCopMng::updateNumWantedPoliceDams() {
    // FUN_00911170
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr ) {
        return numWantedPoliceDams;
    }

    const Eigen::Vector4f damPosition = damRoadIt.computeWidthPosition( 0.0f );
    const float distance =
        ( damPosition - pPlayerCar->getWorldPosition() ).norm();
    if ( gAIConfig.PoliceDeleteDistance < distance ) {
        bNeedPoliceDam = false;
        bRequestPoliceDam = false;
        nextDamLateralRatio = kNoDamLateralRatio;
    }

    const float roadWidth = damRoadIt.getEntity()->pProfile->InnerWidth;
    const DBEntryCarPhysicsData *pPoliceCar =
        gpDatabase->getCarDBEntry( kPoliceCarID );
    OTDU_ASSERT( pPoliceCar != nullptr );
    const float policeCarLength =
        static_cast<float>( pPoliceCar->Length ) * kMillimetersToMeters;
    if ( policeCarLength * 3.0f < roadWidth ) {
        numWantedPoliceDams = 3;
    } else if ( policeCarLength * 2.0f < roadWidth ) {
        numWantedPoliceDams = 2;
    } else if ( policeCarLength < roadWidth ) {
        numWantedPoliceDams = 1;
    } else {
        numWantedPoliceDams = 0;
    }
    return numWantedPoliceDams;
}

bool AiCopMng::needsMoreChasers() const {
    // FUN_00910cb0
    if ( !bEnabled ) {
        return false;
    }
    if ( !IsCopSpawnAllowedByTutorial() ) {
        return false;
    }

    AiPool *pAIPool = gpAI->getAIPool();
    if ( pAIPool->getMaxPolice() <= 0 || pPoliceTarget == nullptr ) {
        return false;
    }
    return GetPoliceList( eAiVehicleCategory::AVC_Cop ).Count <
           pPoliceTarget->getPoliceState().getNumWantedChaser();
}

bool AiCopMng::canSpawnPoliceDam() const {
    // FUN_00910d50
    if ( !bEnabled ) {
        return false;
    }
    if ( !IsCopSpawnAllowedByTutorial() ) {
        return false;
    }
    return bRequestPoliceDam && gpAI->getAIPool()->getMaxPolice() > 0;
}

bool AiCopMng::getDamRoadIt( AiRoadIt &outIt ) const {
    // FUN_00910810
    outIt = damRoadIt;
    return outIt.pRoad != nullptr;
}

void AiCopMng::startPoliceDam( const AiRoadIt &it ) {
    // FUN_0040bcb0
    damRoadIt = it;
    updateNumWantedPoliceDams();
}

bool AiCopMng::canPlayWarnVoice() const {
    // FUN_00910270
    if ( !bEnabled || radio.pBuffer != nullptr ) {
        return false;
    }
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr ||
         pPlayerCar->getPoliceState().getNumChasers() <= 0 ) {
        return false;
    }

    const float delay = RandomUnit( gpAI->getAIPool()->getRandom() ) *
                            ( kWarnVoiceMaxDelay - kWarnVoiceMinDelay ) +
                        kWarnVoiceMinDelay;
    return delay + lastWarnTime < gpTimer->GameTotalTime;
}

void AiCopMng::onVoiceEvent( const eCopVoiceEvent event ) {
    // FUN_00911e70
    if ( !bEnabled ) {
        return;
    }
    if ( !gAIConfig.Unknown0x59 ) {
        if ( event == eCopVoiceEvent::CVE_AskForPoliceDam ) {
            bNeedPoliceDam = true;
        }
        return;
    }

    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr ) {
        return;
    }

    RandomGen &random = gpAI->getRandom();
    PoliceState &infringement = pPlayerCar->getPoliceState();
    switch ( event ) {
    case eCopVoiceEvent::CVE_InfLimitReached:
        playInfLimitVoice( pPlayerCar );
        break;
    case eCopVoiceEvent::CVE_SearchToChase:
        if ( countChasers( pPlayerCar ) == 1 &&
             !gpDatabase->isBike(
                 gpDatabase->getCarDBEntry( pPlayerCar->getCarID() ) ) ) {
            playVoice( RandomHalf( random ) ? CV_SearchToChaseA
                                            : CV_SearchToChaseB,
                       false );
        }
        break;
    case eCopVoiceEvent::CVE_AskFor2ndCar:
        playVoice( RandomHalf( random ) ? CV_AskFor2ndCarA : CV_AskFor2ndCarB,
                   false );
        break;
    case eCopVoiceEvent::CVE_AskFor3rdCar:
        playVoice( CV_AskFor3rdCar, false );
        break;
    case eCopVoiceEvent::CVE_AskForPoliceDam:
        playVoice( CV_AskForPoliceDam, false );
        break;
    case eCopVoiceEvent::CVE_AskForAggressive:
        playVoice( CV_AskForAggressive, false );
        break;
    case eCopVoiceEvent::CVE_WitnessedOffence:
        playVoice( CV_WitnessedOffence, false );
        break;
    case eCopVoiceEvent::CVE_Arrest: {
        const int32_t numChasers = infringement.getNumChasers();
        if ( numChasers == 0 ) {
            break;
        }
        const int32_t chaser = rand() % numChasers;
        if ( chaser > 2 ) {
            break;
        }
        const int32_t voice = CV_ArrestCar1 + chaser;
        if ( gpTimer->GameTotalTime <= copVoicesTime[voice] ) {
            copVoicesTime[voice] = 0.0f;
        }
        if ( highPriorityRadio.pBuffer == nullptr ) {
            loadVoice( highPriorityRadio, voice );
        }
        break;
    }
    case eCopVoiceEvent::CVE_ChaseToSearch: {
        const int32_t numWantedChasers = infringement.getNumWantedChaser();
        if ( numWantedChasers == 0 ) {
            break;
        }
        const uint32_t voice =
            RandomBelow( random, static_cast<uint32_t>( numWantedChasers ) );
        if ( voice <= 2u ) {
            playVoice( CV_ChaseToSearch1 + static_cast<int32_t>( voice ),
                       false );
        }
        break;
    }
    case eCopVoiceEvent::CVE_SearchToPatrol:
        switch ( infringement.getNumWantedChaser() ) {
        case 0:
            playVoice( CV_SearchToPatrol0, false );
            break;
        case 1:
            playVoice( CV_SearchToPatrol1, false );
            break;
        case 2:
            playVoice( CV_SearchToPatrol2, false );
            break;
        default:
            break;
        }
        break;
    case eCopVoiceEvent::CVE_WarnChased: {
        static constexpr int32_t kWarnVoices[] = { 0x4c, 0x4f, 0x52, 0x4d, 0x50,
                                                   0x53, 0x4e, 0x51, 0x54 };
        const uint32_t voice = RandomBelow(
            random, static_cast<uint32_t>( infringement.getNumChasers() * 3 ) );
        if ( voice < std::size( kWarnVoices ) ) {
            playVoice( kWarnVoices[voice], false );
        }
        break;
    }
    default:
        break;
    }
}

void AiCopMng::playInfLimitVoice( CarState *pPlayerCar ) {
    // FUN_00911e70 (inlined)
    const DBEntryCarPhysicsData *pCarData =
        gpDatabase->getCarDBEntry( pPlayerCar->getCarID() );
    if ( pCarData == nullptr || pCarData->Brand == nullptr ) {
        return;
    }

    const char *pBrandID = pCarData->Brand->BrandID.pAsString;
    for ( const CopBrandVoice &brandVoice : kCopBrandVoices ) {
        if ( strcmp( pBrandID, brandVoice.pBrandID ) == 0 ) {
            playVoice( brandVoice.Voice, false );
            return;
        }
    }
    playVoice( gpDatabase->isBike( pCarData ) ? CV_InfLimitBike
                                              : CV_InfLimitCar,
               false );
}

float AiCopMng::computeVoiceElapsedTime( const int32_t voice ) {
    // FUN_00911460 (inlined)
    if ( gpTimer->GameTotalTime <= copVoicesTime[voice] ) {
        copVoicesTime[voice] = 0.0f;
        return gpTimer->GameTotalTime;
    }
    return gpTimer->GameTotalTime - copVoicesTime[voice];
}

void AiCopMng::playVoice( const int32_t voice, const bool bHighPriority ) {
    // FUN_00911460
    const float elapsedTime = computeVoiceElapsedTime( voice );
    if ( !bHighPriority &&
         ( elapsedTime < kVoiceRepeatDelay || radio.LastVoice == voice ||
           radio.pBuffer != nullptr ) ) {
        if ( voice == CV_AskForPoliceDam || voice == CV_AskForPoliceDamReply ) {
            bNeedPoliceDam = true;
        }
        return;
    }

    if ( IsWarnChasedVoice( voice ) ) {
        lastWarnTime = gpTimer->GameTotalTime;
    }
    RadioChannel &channel = bHighPriority ? highPriorityRadio : radio;
    if ( channel.pBuffer == nullptr ) {
        loadVoice( channel, voice );
    }
}

bool AiCopMng::loadVoice( RadioChannel &channel, const int32_t voice ) {
    // FUN_00911460 (inlined)
    char path[0x104] = {};
    snprintf( path, sizeof( path ), "%sSound/Voices/%s/%s",
              gpConfig->getResRootPath(), gpPlayerData->getLanguage(),
              kCopVoices[voice] );

    channel.bLoading = true;
    copVoicesTime[voice] = gpTimer->GameTotalTime;
    channel.LastVoice = voice;

    const uint32_t generation = ++channel.LoadGeneration;
    RadioChannel *pChannel = &channel;
    const bool bLoaded =
        channel.pInstance != nullptr &&
        gpFile->loadBank(
            path, &channel.pBuffer, &channel.BufferSize, pChannel,
            [this, pChannel, generation]( int32_t type, int32_t bSuccess ) {
                // LAB_00910650 / LAB_00910760
                if ( type == 4 && bSuccess ) {
                    onVoiceLoaded( *pChannel, generation );
                }
            } );
    if ( !bLoaded ) {
        // Retail leaves the loading flag set forever, which stalls the voice
        // chain (and the police dam request riding on it): finish the voice
        // right away instead.
        OTDU_LOG_WARN( "Failed to play police voice '%s'\n", path );
        if ( channel.pBuffer != nullptr ) {
            TestDrive::Free( channel.pBuffer );
            channel.pBuffer = nullptr;
        }
        channel.bLoading = false;
        onVoiceFinished();
    }
    return bLoaded;
}

void AiCopMng::onVoiceLoaded( RadioChannel &channel,
                              const uint32_t generation ) {
    // LAB_00910650 / LAB_00910760
    if ( generation != channel.LoadGeneration ) {
        return;
    }

    if ( &channel == &highPriorityRadio ) {
        if ( channel.bLoading ) {
            startVoice( channel );
            if ( radio.pInstance != nullptr &&
                 radio.pInstance->getPlayingState() ==
                     edAudioStreamEntry::APES_Play ) {
                radio.pInstance->stop( 0.0f );
            }
            return;
        }
    } else {
        const bool bHighPriorityBusy =
            highPriorityRadio.bLoading ||
            ( highPriorityRadio.pInstance != nullptr &&
              highPriorityRadio.pInstance->getPlayingState() ==
                  edAudioStreamEntry::APES_Play );
        if ( !bHighPriorityBusy && channel.bLoading ) {
            startVoice( channel );
            return;
        }
    }

    channel.bLoading = false;
    if ( channel.pInstance != nullptr ) {
        releaseVoice( channel, true );
    }
}

void AiCopMng::startVoice( RadioChannel &channel ) {
    // FUN_00910520
    channel.pSample = std::make_unique<edAudioSample>();
    const bool bDecoded = channel.pSample->loadFromMemory(
        channel.pBuffer, static_cast<int32_t>( channel.BufferSize ) );
    channel.pInstance->setSample( bDecoded ? channel.pSample.get() : nullptr );
    channel.pInstance->debugSetDescription( ( &channel == &highPriorityRadio )
                                                ? kCopVoiceHPInstanceName
                                                : kCopVoiceInstanceName );
    // An undecodable voice stays stopped so the tick still releases it and
    // queues the next one.
    if ( bDecoded ) {
        channel.pInstance->playSubtitled( kCopVoices[channel.LastVoice], 0.0f );
    }
    channel.bLoading = false;
}

void AiCopMng::releaseVoice( RadioChannel &channel, const bool bStop ) {
    if ( bStop ) {
        channel.pInstance->stop( 0.0f );
    }
    channel.pInstance->setSample( nullptr );
    channel.pSample.reset();
    if ( channel.pBuffer != nullptr ) {
        TestDrive::Free( channel.pBuffer );
    }
    channel.pBuffer = nullptr;
}

void AiCopMng::onVoiceFinished() {
    // FUN_009102d0
    if ( !bEnabled ) {
        return;
    }

    RandomGen &random = gpAI->getRandom();
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    switch ( radio.LastVoice ) {
    case CV_SearchToChaseA:
    case CV_SearchToChaseB:
        playVoice( RandomHalf( random ) ? CV_SearchToChaseReplyA
                                        : CV_SearchToChaseReplyB,
                   false );
        break;
    case CV_AskFor2ndCarA:
    case CV_AskFor2ndCarB:
        playVoice( RandomHalf( random ) ? CV_AskFor2ndCarReplyA
                                        : CV_AskFor2ndCarReplyB,
                   false );
        break;
    case CV_AskFor3rdCar:
        playVoice( CV_AskFor3rdCarReply1, false );
        break;
    case CV_AskFor3rdCarReply1:
        playVoice( CV_AskFor3rdCarReply2, false );
        break;
    case CV_AskFor3rdCarReply2:
        playVoice( CV_AskFor3rdCarReply3, false );
        break;
    case CV_AskForPoliceDam:
        playVoice( CV_AskForPoliceDamReply, false );
        break;
    case CV_AskForPoliceDamReply:
        bNeedPoliceDam = true;
        break;
    case CV_ChaseToSearch1:
    case CV_ChaseToSearch2:
    case CV_ChaseToSearch3:
        playVoice( CV_ChaseToSearchReply, false );
        break;
    case CV_SearchToPatrol2:
        playVoice( CV_SearchToPatrol2Reply, false );
        break;
    case CV_SearchToPatrol1:
        playVoice( CV_SearchToPatrol1Reply, false );
        break;
    case CV_SearchToPatrol0:
        if ( ( pPlayerCar != nullptr &&
               pPlayerCar->getPoliceState().isWanted() ) ||
             RandomHalf( random ) ) {
            playVoice( CV_SearchToPatrol0Reply, false );
        }
        break;
    case CV_SearchToPatrol0Reply:
        if ( pPlayerCar != nullptr &&
             !pPlayerCar->getPoliceState().isWanted() ) {
            playVoice( CV_SearchToPatrol0End, false );
        }
        break;
    default:
        break;
    }
}

bool AiCopMng::findArrestCinematic( const char *pName ) {
    // FUN_00911050
    ScenaricBank &bank = gpScenaric->getCinematicPackBank();
    pArrestCinematicBank = &bank;
    if ( bank.getState() == ScenaricBank::BBS_Unused ||
         bank.getState() == ScenaricBank::BBS_LoadFailed ) {
        return false;
    }

    pArrestCinematic = static_cast<EntityCinematic *>(
        bank.findEntity( eEntityType::ET_CINE, pName ) );
    if ( pArrestCinematic == nullptr ) {
        return false;
    }
    pArrestCinematic->setStopWhenSkipped( true ); // +0x26dc
    return ( pArrestCinematic->getFlags() & 0x4u ) == 0;
}

bool AiCopMng::startArrestFade() {
    // FUN_009110c0
    if ( !findArrestCinematic( "c_police_defeat" ) ) {
        return false;
    }

    if ( gpActiveGM != nullptr ) {
        gpActiveGM->setBlockPauseByStart( true );
    }
    FlashPlayer *pHudPlayer =
        ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;
    if ( pHudPlayer != nullptr ) {
        pHudPlayer->setVariableValue( "/:IsMapAvailable", 0 );
    }

    gpMngFade->setTransitionColorRGB( 0, 0, 0 );
    gpMngFade->startTransition(
        ( gpMngFade->getTransitionFlags() & ~0xeu ) | 1u, kArrestFadeDuration,
        &AiCopMng::OnArrestFadeOut, nullptr, this );
    return true;
}

void AiCopMng::OnArrestFadeOut( void *pData, void * /*pSource*/ ) {
    // FUN_00915480
    AiCopMng *pCopMng = static_cast<AiCopMng *>( pData );
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar != nullptr ) {
        if ( gpTimer->GameTotalTime - gpPlayerData->getLastArrestedTime() <
             gAIConfig.InfACGraceDelay ) {
            return;
        }

        AiVehicle *pVehicle =
            static_cast<AiVehicle *>( pPlayerCar->getAIController() );
        if ( pVehicle != nullptr && pVehicle->getLocomotion() != nullptr ) {
            AiRoadIt location;
            float widthRatio = 0.0f;
            ComputeArrestTeleportLocation( pVehicle, location, widthRatio );
            TeleportToRoad( pVehicle, location, widthRatio, false );
        }
        gpAI->getAIPool()->updateArrestedCar( AiPool::AS_ReleaseAI,
                                              pPlayerCar );
    }

    pCopMng->damRoadIt = AiRoadIt();
    pCopMng->bRequestPoliceDam = false;
    pCopMng->bNeedPoliceDam = false;
    pCopMng->nextDamLateralRatio = kNoDamLateralRatio;
    pCopMng->numPoliceDams = 0;
    pCopMng->numWantedPoliceDams = 0;
    gpAI->setTrafficCategories( false, 1u );
    gpAI->setOpponentsHidden( true );
    pCopMng->startArrestCinematic();
}

void AiCopMng::computeArrestCinematicFrame( Matrix4x4f_t &outFrame ) const {
    // FUN_00911950 (inlined)
    PhysicsVehicle *pPhysics =
        gpScenaric->getLocalPlayer().getCarState()->getPhysicsVehicle();
    outFrame = pPhysics->getChassisWorldMatrix(); // FUN_0082e1e0
    const Eigen::Vector4f up = outFrame.row( 1 ).transpose();
    Eigen::Vector4f position = outFrame.row( 3 ).transpose();

    float groundHeight = kNoSurfaceSentinel;
    Eigen::Vector4f groundNormal;
    gpPhysics->probeSurface( position, false, groundHeight, groundNormal,
                             true );
    position.y() = groundHeight;

    const float pitch = std::atan2( up.z(), up.y() );
    const float roll = std::atan2( up.x(), up.y() );
    outFrame.row( 3 ) = Eigen::Vector4f::Zero().transpose();
    MatrixRotateX( outFrame, -pitch );
    MatrixRotateZ( outFrame, roll );
    position.head<3>() -= GetWorldShift().head<3>();
    outFrame.row( 3 ) = position.transpose();
}

bool AiCopMng::startArrestCinematic() {
    // FUN_00911950
    if ( !findArrestCinematic( "c_police_defeat" ) ) {
        return false;
    }

    if ( gpMngFade != nullptr ) {
        gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() | 8u );
    }
    pArrestCinematic->setGameModeInterface( this );
    Matrix4x4f_t frame;
    computeArrestCinematicFrame( frame );
    pArrestCinematic->setTransformOffset( frame );
    pArrestCinematic->start( true );

    ScenaricBank &bank = gpScenaric->getCinematicPackBank();
    pArrestCinematicBank = &bank;
    if ( bank.getState() != ScenaricBank::BBS_Unused &&
         bank.getState() != ScenaricBank::BBS_LoadFailed ) {
        EntityCinematic *pLoopCinematic = static_cast<EntityCinematic *>(
            bank.findEntity( eEntityType::ET_CINE, "c_police_defeat_l" ) );
        if ( pLoopCinematic != nullptr ) {
            pLoopCinematic->createTDUInterface();
        }
    }
    return true;
}

bool AiCopMng::startArrestLoopCinematic() {
    // FUN_00911770
    if ( !findArrestCinematic( "c_police_defeat_l" ) ) {
        return false;
    }

    pArrestCinematic->setGameModeInterface( this );
    Matrix4x4f_t frame;
    computeArrestCinematicFrame( frame );
    pArrestCinematic->setTransformOffset( frame );
    pArrestCinematic->start( true );
    return true;
}

void AiCopMng::stopVoices() {
    // FUN_009100d0
    if ( highPriorityRadio.bLoading ) {
        highPriorityRadio.bLoading = false;
    } else if ( highPriorityRadio.pInstance != nullptr ) {
        releaseVoice( highPriorityRadio, false );
    }
    if ( radio.bLoading ) {
        radio.bLoading = false;
    } else if ( radio.pInstance != nullptr ) {
        releaseVoice( radio, false );
    }
}

void AiCopMng::computeFine( CarState *pCar ) {
    // FUN_00910b50
    const int32_t levelFactor = ComputeFineLevelFactor();
    const PoliceState &infringement = pCar->getPoliceState();
    currentFine.NumCarsCrashed = infringement.getNumCollisionCar();
    currentFine.CarsTotalValue =
        infringement.getNumCollisionCar() * gAIConfig.FineCarValue;
    currentFine.NumSettingsCrashed = infringement.getNumCollisionSettings();
    currentFine.SettingsTotalValue =
        infringement.getNumCollisionSettings() * gAIConfig.FineSetValue;
    currentFine.LevelFactor = levelFactor;
    currentFine.TotalValue =
        ( currentFine.SettingsTotalValue + currentFine.CarsTotalValue ) *
        levelFactor;
}

void AiCopMng::showInstantFine() {
    // FUN_00914900
    OTDU_UNIMPLEMENTED; // TODO: instant fine popup (DisplayModalPrompt, popup
                        // callback vtbl 0xf0ee74 -> FUN_009158c0)
}

void AiCopMng::applyFine( CarState *pCar,
                          const bool bShowInstantFine,
                          const bool bChargeFine,
                          const bool bResetInfringement,
                          const bool bShowFine ) {
    // FUN_00914a90
    if ( pCar == nullptr ) {
        return;
    }

    computeFine( pCar );
    if ( bChargeFine ) {
        IPlayerData *pSave = gpPlayerData->getLoadedSave();
        const uint32_t money = pSave->getDriver()->getMoney();
        const uint32_t fine = static_cast<uint32_t>( currentFine.TotalValue );
        pSave->getExtraChallenges()->countArrest();
        const uint32_t paid = ( money < fine ) ? money : fine;
        Driver *pDriver = pSave->getDriver();
        pSave->getExtraChallenges()->addCounter(
            CounterIndex( kFinesPaidCounter ), static_cast<int32_t>( paid ) );
        pDriver->setMoneyAndNotify(
            static_cast<int32_t>( pDriver->getMoney() - paid ) );
        if ( money < fine ) {
            bSendPlayerToJail = gAIConfig.bEnableJail;
        }
    }

    if ( bShowFine ) {
        gpPlayerData->setLastArrestedTime( gpTimer->GameTotalTime );
        if ( !bSendPlayerToJail ||
             gpTestDriveInstance->getActiveGameMode() != GM_FreeRide ) {
            ShowFineHud( gpMngHud->getHudPlayer(), currentFine );
        }
    }
    if ( bShowInstantFine ) {
        showInstantFine();
    }
    if ( bResetInfringement ) {
        pCar->getPoliceState().reset();
    }
}

bool AiCopMng::executeAction( const int32_t action,
                              void *pSource,
                              void * /*pData*/ ) {
    // FUN_009152c0
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr || action != CE_Ended ) {
        return false;
    }

    const std::string &cinematicName =
        static_cast<EntityCinematic *>( pSource )->getInstanceName();
    FlashPlayer *pHudPlayer =
        ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;
    if ( cinematicName == "C_POLICE_DEFEAT" ) {
        if ( pHudPlayer != nullptr ) {
            bool bChargeFine = true;
            if ( gpActiveGM != nullptr &&
                 gpActiveGM->getCurrentGameMode() == GM_Mission &&
                 gpActiveGM->getGameMech() != nullptr &&
                 gpActiveGM->getGameMech()->getUnknown75C() ) {
                bChargeFine = false;
            }
            applyFine( pPlayerCar, false, bChargeFine, false, true );
            gpFlash->getMouse()->field12_0x30 = false;
        }
        stopVoices();
        startArrestLoopCinematic();
        return false;
    }

    if ( cinematicName != "C_POLICE_DEFEAT_L" ) {
        return false;
    }

    gpFlash->getMouse()->field12_0x30 = true;
    PoliceState &infringement = pPlayerCar->getPoliceState();
    infringement.reset();
    AiPool *pAIPool = gpAI->getAIPool();
    pAIPool->updateArrestedCar( AiPool::AS_RestoreControl, pPlayerCar );
    gpCarState->setPoliceStateJob(
        pPlayerCar, &gpScenaric->getLocalPlayer().getInfringementCheck() );
    gpCamera->restoreCamera();
    pAIPool->setTrafficPaused( false ); // +0xc
    pAIPool->setField0x7C( true );
    pAIPool->setField0x85( true );
    gpAI->setPauseTraffic( false );
    gpAI->setOpponentsHidden( false );
    infringement.setArrested( false );
    infringement.setPlayingArrestCinematic( false );
    if ( gpActiveGM != nullptr ) {
        gpActiveGM->setPauseMenuLocked( false ); // +0x103228
    }
    if ( pHudPlayer != nullptr ) {
        pHudPlayer->setVariableValue( "/:IsMapAvailable", 1 );
    }
    if ( gpActiveGM != nullptr && gpActiveGM->getGameMech() != nullptr ) {
        // Retail passes the cinematic interface as the sender; message 8
        // never reads it.
        gpActiveGM->getGameMech()->onMessage(
            nullptr, GameMechanism::kMsgUnknown8, nullptr );
    }
    return false;
}
