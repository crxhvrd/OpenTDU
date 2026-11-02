#include "shared.h"
#include "mng_fr_meeting_ic.h"

#include "ai/ai_bot_manager.h"
#include "ai/ai_pool.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "database/gs_database.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_desc.h"
#include "config/gs_config.h"
#include "flash/entities/flash_entity.h"
#include "flash/implementation/flash_player.h"
#include "flash/mng_flash.h"
#include "flash/modal_message_box.h"
#include "game/gm_scene.h"
#include "game/mng_hud.h"
#include "game/mng_ingame_menu.h"
#include "game/scenaric/local_player.h"
#include "game/vehicle/car_state.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
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
        AiVehicleList &bots = gpAI->getAIPool()->getList(
            AiPool::AVL_FirstCategory +
            static_cast<int32_t>( eAiVehicleCategory::AVC_Bot ) );
        for ( AiVehicle *pBot : bots ) {
            if ( pBot == nullptr || pBot->getCarState() == nullptr ) {
                continue;
            }

            const float distance = GetSquaredDistanceInICZone(
                localWorld, pBot->getCarState()
                                ->getBody()
                                ->WorldMatrix.row( 3 )
                                .transpose() );
            if ( distance < bestDistance ) {
                BotWaitingTime = 0.0f;
                bestBotID = pBot->getDPC().BotID;
                bestDistance = distance;
            }
        }
    }

    if ( bestDistance == kICZoneNoCandidate ) {
        return;
    }

    bInitializing = true;

    const LocalPlayerData &localData = gpPlayerData->getLocalPlayerData();
    Stake = localData.bStakeEnabled ? int32_t( localData.StakeValue ) : 0;
    const uint32_t money =
        gpPlayerData->getLoadedSave()->getDriver()->getMoney();
    if ( money < uint32_t( Stake ) ) {
        Stake = int32_t( money );
    }

    if ( !bOverWLAN ) {
        Stake = std::min( GetBotMaxStake( bestBotID ), Stake );
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
        gpScenaric->removeRefusedICEChallengers();

        ICRoute &route = gpScenaric->getICDatas();
        route.field_0x10 = 0;
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_008032b0 route owner
                                 // pick, P2P keep alive + tolerance)
        route.OwnerPlayerIndex = 0;

        CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
        if ( pLocalCar != nullptr ) {
            route.StartPosition =
                pLocalCar->getBody()->WorldMatrix.row( 3 ).transpose();
        }
        route.Stake = Stake;

        gpScenaric->setInstantChallenge( true );
        TransitionFromFreerideToMap( GM_InstantChallenge );
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

void GetICEPlayerNickname( const ICEChallenger &player,
                                  char *pOutName,
                                  const size_t nameLength ) {
    // FUN_008039f0
    pOutName[0] = '\0';
    if ( player.bBot ) {
        // FUN_0090c1d0
        const DBEntryBot *pBot = GetBotDBEntry( int32_t( player.ID ) );
        if ( pBot != nullptr && nameLength >= 0x20 ) {
            strncpy( pOutName, pBot->FirstName.pAsString, 0x20 );
        }
        return;
    }
    if ( player.ID == kLocalICEPlayer ) {
        strncpy( pOutName, gpScenaric->getLocalPlayer().getDriver()->Nickname,
                 nameLength );
        pOutName[nameLength - 1] = '\0';
        return;
    }
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (P2P player nickname)
}

bool IsICEPlayerValid( const ICEChallenger &player ) {
    // FUN_00803260
    if ( player.bBot ) {
        return player.ID != ICEChallenger::kNoChallenger;
    }
    if ( player.ID == kLocalICEPlayer ) {
        return true;
    }
    if ( player.ID < 0x10 ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (P2P player slot check)
    }
    return false;
}

void FillICEPlayerInfo( const ICEChallenger &player,
                        const uint32_t lobbyIndex,
                        FlashPlayer *pPlayer ) {
    // FUN_00805260
    char variable[0x40];
    char name[100];
    snprintf( variable, sizeof( variable ), "/:Player%dName", lobbyIndex );
    GetICEPlayerNickname( player, name, 0x20 );
    pPlayer->setVariableValueNonLinked( variable, name );

    int32_t rank = 0;
    if ( player.bBot ) {
        rank = GetBotTier( int32_t( player.ID ) );
    } else {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (P2P player rank)
    }

    const char *pCarName = "";
    const char *pGroupName = "";
    int32_t upgradeLevel = 0;
    if ( player.bBot || player.ID == kLocalICEPlayer ) {
        int32_t carID = -1;
        uint64_t packHashcode = 0ull;
        if ( player.bBot ) {
            carID = GetBotCarID( int32_t( player.ID ) );
        } else {
            const CarDesc *pCar = gpPlayerData->getCurrentCar();
            if ( pCar != nullptr ) {
                carID = int32_t( pCar->getCarConfigID() );
                packHashcode = pCar->packHashcode;
            }
        }
        for ( const CarConfig &car : gpDatabase->getCarList() ) {
            if ( car.Hash == carID ) {
                pCarName = car.FullName.c_str();
                break;
            }
        }
        DBEntryCarPhysicsData *pEntry = gpDatabase->getCarDBEntry( carID );
        if ( pEntry != nullptr ) {
            pGroupName = pEntry->CarGroup.pAsString;
            upgradeLevel = gpDatabase->getPackLevel( pEntry, packHashcode );
        }
    } else {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (P2P player car)
    }

    snprintf( variable, sizeof( variable ), "/:Player%dCar", lobbyIndex );
    if ( strlen( pCarName ) < 2 ) {
        pPlayer->setVariableValueNonLinked( variable, pCarName );
    } else {
        pPlayer->setVariableValue( variable, pCarName );
    }
    snprintf( variable, sizeof( variable ), "/:Player%dGroup", lobbyIndex );
    if ( strlen( pCarName ) < 2 ) {
        pPlayer->setVariableValueNonLinked( variable, pGroupName );
    } else {
        pPlayer->setVariableValue( variable, pGroupName );
    }
    snprintf( variable, sizeof( variable ), "/:Player%dUpgrade", lobbyIndex );
    pPlayer->setVariableValue( variable, upgradeLevel );
    snprintf( variable, sizeof( variable ), "/:Player%dRank", lobbyIndex );
    if ( rank == 0 ) {
        pPlayer->setVariableValueNonLinked( variable, "--" );
    } else {
        pPlayer->setVariableValue( variable, rank );
    }
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
        FillICEPlayerInfo( pICEPlayers[i], numPlayers, pFlashVariable );
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
