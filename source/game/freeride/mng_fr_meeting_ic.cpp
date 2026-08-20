#include "shared.h"
#include "mng_fr_meeting_ic.h"

#include "config/gs_config.h"
#include "flash/entities/flash_entity.h"
#include "flash/implementation/flash_player.h"
#include "flash/modal_message_box.h"
#include "game/gm_scene.h"
#include "game/mng_hud.h"
#include "game/mng_ingame_menu.h"
#include "game/scenaric/local_player.h"
#include "game/vehicle/car_state.h"
#include "player_data/gs_playerdata.h"
#include "system/gs_pc_options.h"

MngFRMeetingIC *gpMngFRMeetingIC = nullptr;

static constexpr float kICZoneHalfWidth = 10.0f;        // FLOAT_00f4166c
static constexpr float kICZoneLength = 70.0f;           // DAT_00f41a14
static constexpr float kICZoneNoCandidate = 1000000.0f; // DAT_00f03e70
static constexpr float kChallengeBoxTimeout = 20.0f;    // DAT_00f03ee0
static constexpr float kBlacklistTimeout = 30.0f;       // DAT_00f416e0
static constexpr float kBotAnswerDelay = 3.0f;          // DAT_00f416b4

static constexpr const char *kHudInstantPath = "/HUD/instant";
static constexpr const char *kHudInstantRelativePath = "HUD/instant";

static const char *StateToString( const eInstantChallengeState state ) {
    switch ( state ) {
    case ICState_None:
        return "none";
    case ICState_ChallengeBoxShown:
        return "ChallengeBoxShown";
    case ICState_GoingInIC:
        return "GoingInIC";
    default:
        return "??";
    }
}

static const char *GetLocalPlayerLogName() {
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE - needs CP2PGameService
    return "[UNCONNECTED]";
}

static bool IsOverWLAN() {
    // FUN_00897750
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE/SCENARIC
    return gpPlayerData != nullptr && gpPlayerData->isOnline() &&
           gpConfig != nullptr && gpConfig->bAutoConnectFreeride &&
           gpPlayerData->getOnlineOp( 0 );
}

static float
GetSquaredDistanceInICZone( const Matrix4x4f_t &localWorld,
                            const Eigen::Vector4f &opponentPosition ) {
    // FUN_00791a10
    const Eigen::Vector4f delta =
        opponentPosition - localWorld.row( 3 ).transpose();

    const float lateral = delta.dot( localWorld.row( 0 ).transpose() );
    const float forward = delta.dot( localWorld.row( 2 ).transpose() );

    if ( fabsf( lateral ) < kICZoneHalfWidth &&
         fabsf( forward ) < kICZoneLength && forward > 0.0f ) {
        return forward * forward + lateral * lateral;
    }

    return kICZoneNoCandidate;
}

MngFRMeetingIC::MngFRMeetingIC()
    : Manager(),
      bOpponentInICZone( false ),
      State( ICState_None ),
      pICEPlayers( gpScenaric->getICEChallengers() ),
      LocalPlayer(),
      bInitializing( false ),
      Stake( 0 ),
      bOverWLAN( true ),
      pFlashVariable( nullptr ),
      TimeoutBox( 0.0f ),
      BotWaitingTime( 0.0f ),
      pBlacklisted( nullptr ),
      TimeoutBlacklist( 0.0f ),
      bFlashCalled( false ) {
    // FUN_00792be0
    OTDU_ASSERT( gpScenaric != nullptr );
    LocalPlayer.reset();

    OTDU_ASSERT( gpMngFRMeetingIC == nullptr );
    gpMngFRMeetingIC = this;

    setState( ICState_None );
}

MngFRMeetingIC::~MngFRMeetingIC() {
    // FUN_00793750 (inlined)
    gpMngFRMeetingIC = nullptr;
}

bool MngFRMeetingIC::initialize( TestDriveGameInstance * ) {
    // FUN_00791840
    pFlashVariable = nullptr;
    return true;
}

void MngFRMeetingIC::tick( float deltaTime, float ) {
    // FUN_007926d0
    if ( TimeoutBlacklist > 0.0f ) {
        TimeoutBlacklist -= deltaTime;
        if ( TimeoutBlacklist <= 0.0f ) {
            TimeoutBlacklist = 0.0f;
            pBlacklisted = nullptr;
        }
    }

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr ) {
        return;
    }

    if ( gpMngInGameMenu != nullptr &&
         gpMngInGameMenu->getState() == IGMS_Open ) {
        return;
    }

    if ( gpFlashMessageBox != nullptr &&
         ( gpFlashMessageBox->getNumOpenMsgBox() != 0 ||
           gpFlashMessageBox->getUnknownFlag() ) ) {
        return;
    }

    if ( gpActiveGMScene != nullptr && gpActiveGMScene->isPaused() ) {
        return;
    }

    if ( gpScenaric->getIntroTutorial().isActive() ) {
        return;
    }

    if ( !gpPlayerData->getOnlineOp( 1 ) ) {
        if ( State == ICState_ChallengeBoxShown ) {
            closeChallengeBox();
            State = ICState_None;
        }
        return;
    }

    if ( State != ICState_None ) {
        if ( State != ICState_ChallengeBoxShown ) {
            return;
        }

        if ( TimeoutBox <= 0.0f ) {
            return;
        }

        TimeoutBox -= deltaTime;
        if ( TimeoutBox <= 0.0f && !bFlashCalled ) {
            TimeoutBox = 0.0f;
            bFlashCalled = true;
            closeChallengeBox();
            return;
        }

        if ( bOverWLAN ) {
            if ( IsOverWLAN() ) {
                return;
            }

            TimeoutBox = 0.0f;
            bFlashCalled = true;
            closeChallengeBox();
            State = ICState_None;
            return;
        }

        if ( IsOverWLAN() ) {
            TimeoutBox = 0.0f;
            bFlashCalled = true;
            closeChallengeBox();
            State = ICState_None;
            return;
        }

        // Let the bot "think" for a while before it accepts
        BotWaitingTime += deltaTime;
        if ( BotWaitingTime < kBotAnswerDelay ) {
            return;
        }

        ICEChallenger &bot = pICEPlayers[0];
        if ( bot.bAcceptedICE || bot.bRefusedICE ) {
            return;
        }

        bot.bAcceptedICE = true;
        bot.bRefusedICE = false;
        refreshChallengerFrames();
        return;
    }

    if ( !pLocalCar->getBody()->bFlashingLights ) {
        return;
    }

    const Matrix4x4f_t localWorld = pLocalCar->getBody()->WorldMatrix;

    float bestDistance = kICZoneNoCandidate;
    P2PPlayer *pBestOpponent = nullptr;
    int32_t bestBotID = 0;

    bOverWLAN = IsOverWLAN();
    if ( bOverWLAN ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    } else {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: AI
    }
    (void)localWorld;

    if ( bestDistance == kICZoneNoCandidate ) {
        return;
    }

    bInitializing = true;

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE
    Stake = 0;

    if ( !bOverWLAN ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: AI
        setBotChallenger( bestBotID );
    } else {
        fillChallengersFromLobby( pBestOpponent );
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    }

    OTDU_LOG_DEBUG( "INSTANTCHALLENGE : %s: new State: %s\n",
                    GetLocalPlayerLogName(),
                    StateToString( ICState_ChallengeBoxShown ) );

    TimeoutBox = 0.0f;
    bFlashCalled = false;
    showChallengeBox();
    State = ICState_ChallengeBoxShown;

    LocalPlayer.bAcceptedICE = true;
    LocalPlayer.bRefusedICE = false;
    refreshChallengerFrames();
}

bool MngFRMeetingIC::onMessage( FlashMessage *pMessage ) {
    // FUN_00792040
    OTDU_ASSERT( pMessage );

    switch ( *pMessage ) {
    case FlashMessage( "inic" ): { // the local player accepted the challenge
        if ( bOverWLAN ) {
            OTDU_LOG_DEBUG( "INSTANTCHALLENGE : %s: Flash Received: inic\n",
                            GetLocalPlayerLogName() );
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
        }

        LocalPlayer.bAcceptedICE = true;
        LocalPlayer.bRefusedICE = false;
        refreshChallengerFrames();
        return true;
    }

    case FlashMessage( "goic" ): { // everyone agreed, start the challenge
        OTDU_UNIMPLEMENTED_ONCE;   // TODO: ONLINE
        return true;
    }

    case FlashMessage( "icbo" ): { // the box finished opening
        refreshChallengerFrames();
        return true;
    }

    case FlashMessage( "noic" ): { // the local player refused the challenge
        OTDU_LOG_DEBUG( "INSTANTCHALLENGE : %s: Flash Received: noic\n",
                        GetLocalPlayerLogName() );

        if ( State == ICState_ChallengeBoxShown && bOverWLAN ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
        }

        TimeoutBlacklist = kBlacklistTimeout;
        setState( ICState_None );
        return true;
    }

    default:
        break;
    }

    return true;
}

void MngFRMeetingIC::setState( const eInstantChallengeState newState ) {
    // FUN_00791e50
    OTDU_LOG_DEBUG( "INSTANTCHALLENGE : %s: new State: %s\n",
                    GetLocalPlayerLogName(), StateToString( newState ) );

    TimeoutBox = 0.0f;
    bFlashCalled = false;

    if ( newState == ICState_None ) {
        bInitializing = false;
        for ( int32_t i = 0; i < kNumICEChallengers; i++ ) {
            pICEPlayers[i].reset();
        }
        LocalPlayer.reset();
    } else if ( newState == ICState_ChallengeBoxShown ) {
        showChallengeBox();
        State = ICState_ChallengeBoxShown;
        return;
    }

    State = newState;
}

void MngFRMeetingIC::onScenaricMessage( const uint32_t senderPlayerIndex,
                                        const uint32_t crc ) {
    // FUN_007922f0
    (void)senderPlayerIndex;
    (void)crc;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
}

void MngFRMeetingIC::refreshChallengerFrames() {
    // FUN_00791b60
    if ( pFlashVariable == nullptr ) {
        return;
    }

    bool bEveryoneAnswered = true;
    int32_t numAccepted = 0;

    for ( int32_t i = 0; i < kNumICEChallengers; i++ ) {
        const ICEChallenger &challenger = pICEPlayers[i];

        const char *pFrame = "default";
        if ( challenger.ID != ICEChallenger::kNoChallenger ) {
            if ( challenger.bAcceptedICE ) {
                numAccepted++;
                pFrame = "on";
            } else if ( challenger.bRefusedICE ) {
                pFrame = "off";
            } else {
                bEveryoneAnswered = false;
            }
        }

        char pSlotPath[64];
        sprintf( pSlotPath, "/HUD/instant/opponents/p%d", i + 1 );
        pFlashVariable->showFrame( pFrame, pSlotPath, false );
    }

    pFlashVariable->setVariableValue( "/:Stake", Stake );

    if ( LocalPlayer.bAcceptedICE ) {
        numAccepted++;
    } else if ( !LocalPlayer.bRefusedICE ) {
        if ( !bEveryoneAnswered || numAccepted != 0 ) {
            return;
        }
        pFlashVariable->showFrame( "cancel", kHudInstantRelativePath, true );
        return;
    }

    if ( !bEveryoneAnswered ) {
        return;
    }

    if ( numAccepted > 1 ) {
        launchChallenge();
        return;
    }

    pFlashVariable->showFrame( "cancel", kHudInstantRelativePath, true );
}

void MngFRMeetingIC::showChallengeBox() {
    // FUN_00791d20
    if ( pFlashVariable == nullptr ) {
        return;
    }

    uint32_t numPlayers = 0u;
    for ( int32_t i = 0; i < kNumICEChallengers; i++ ) {
        if ( pICEPlayers[i].ID == ICEChallenger::kNoChallenger ) {
            continue;
        }

        numPlayers++;

        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    }

    pFlashVariable->setVariableValue( "/:nb_players", numPlayers );
    pFlashVariable->setVariableValue( "/:Stake", Stake );
    pFlashVariable->setVariableValue( "/:IsICEHost",
                                      static_cast<uint32_t>( bInitializing ) );

    pFlashVariable->showFrame( "on", kHudInstantRelativePath, true );

    TimeoutBox = kChallengeBoxTimeout;
}

void MngFRMeetingIC::fillChallengersFromLobby( P2PPlayer *pOpponent ) {
    // FUN_00791850
    (void)pOpponent;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
}

void MngFRMeetingIC::setBotChallenger( const int32_t botID ) {
    // FUN_00791920
    ICEChallenger &bot = pICEPlayers[0];
    bot.ID = static_cast<uint32_t>( botID );
    bot.bBot = false;
    bot.bRefusedICE = false;
    bot.bAcceptedICE = false;

    // 999 is the "no bot" id used by the free-ride player list
    if ( botID != 999 ) {
        bot.bBot = true;
    }
}

void MngFRMeetingIC::launchChallenge() {
    // FUN_00791980
    OTDU_LOG_DEBUG( "%s launch the challenge!!\n", GetLocalPlayerLogName() );

    if ( pFlashVariable != nullptr ) {
        pFlashVariable->showFrame( "accept", kHudInstantRelativePath, true );

        // The main menu stays locked out until the challenge is over
        if ( gpPCOptions != nullptr ) {
            gpPCOptions->setMainMenuAllowed( false );
        }
    }
}

void MngFRMeetingIC::closeChallengeBox() {
    // FUN_007926d0 (inlined)
    if ( gpMngHud != nullptr && gpMngHud->getHudPlayer() != nullptr ) {
        gpMngHud->getHudPlayer()->showFrame( "cancel", kHudInstantPath, true );
    }
}
