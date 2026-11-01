#include "shared.h"
#include "game_mech_instant_challenge.h"

#include "ai/ai_game_session.h"
#include "ai/ai_race.h"
#include "core/format.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "game/freeride/mng_fr_meeting_ic.h"
#include "game/gm_world.h"
#include "game/mission/game_mech_race_hud.h"
#include "game/mng_hud.h"
#include "physics/gs_physics.h"
#include "player_data/achievements_data.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_extra_challenges.h"

static constexpr float kRankingCountDelay = 0.5f; // FLOAT_00f41674

static FlashPlayer *GetHudPlayer() {
    return ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;
}

GameMechInstantChallenge::GameMechInstantChallenge( GameMechanism *pOwnerMech )
    : GameMechComponent( pOwnerMech ),
      localPlayer(),
      pRanked{},
      pChallengers( gpScenaric->getICEChallengers() ),
      field_0x74( -1 ),
      bLocalAccepted( false ),
      rankingTime( 0.0f ),
      bRankingCounted( false ),
      bLocalWon( false ),
      bStatsRequested( false ),
      field_0x83( false ),
      field_0x84( false ) {
    // FUN_00729260
}

bool GameMechInstantChallenge::initialize() {
    // FUN_007291c0
    GameMechComponent::initialize();
    bLocalAccepted = false;
    for ( int32_t i = 0; i < kMaxNumPlayers; i++ ) {
        pChallengers[i].bAcceptedICE = false;
        pChallengers[i].bRefusedICE = false;
    }
    bStatsRequested = false;
    field_0x83 = false;
    return true;
}

bool GameMechInstantChallenge::onMechStateProcess(
    const int32_t state,
    const float deltaTime,
    const eStateProcess process ) {
    // FUN_00729cd0
    if ( state == GameMechanism::MS_Running ) {
        if ( process == SP_Init ) {
            field_0x84 = false;
        }
        return false;
    }
    if ( state != GameMechanism::MS_Ranking ) {
        return false;
    }

    switch ( process ) {
    case SP_Init: {
        gpPhysics->setPlayerSlowDownForced( true );
        AiRaceMessage message;
        message.Type = 0;
        message.ID = 4;
        pMech->getGameSession()->sendRaceMessage( message );
        showResults();
        if ( pChallengers[0].bBot ) {
            pChallengers[0].bAcceptedICE = true;
        }
        rankingTime = 0.0f;
        bRankingCounted = false;
        bLocalWon = false;
        fillPlayerRows();
        return true;
    }
    case SP_Update:
        updateRanking( deltaTime );
        fillPlayerRows();
        if ( gpFlash->getMouse()->field12_0x30 ) {
            gpFlash->getMouse()->field12_0x30 = false;
        }
        return true;
    case SP_Term:
        return true;
    default:
        return false;
    }
}

void GameMechInstantChallenge::rankPlayers() {
    // FUN_00729740
    uint32_t numPlayers = 0;
    const GameMechRaceHUD *pRaceHUD = static_cast<const GameMechRaceHUD *>(
        pMech->getComponent( GameMechanism::CS_RaceHUD ) );
    GameParticipant *pHead =
        ( pRaceHUD != nullptr ) ? pRaceHUD->getRankingHead() : nullptr;
    GameParticipant *pLocal = pMech->getGameSession()->getLocalParticipant();
    bool bChallengerUsed[kMaxNumPlayers] = {};
    for ( GameParticipant *pParticipant = pHead; pParticipant != nullptr;
          pParticipant = pParticipant->getSessionParams().pRankNextPlayer ) {
        const ICEChallenger *pSlot = nullptr;
        if ( pParticipant == pLocal ) {
            pSlot = &localPlayer;
            if ( pParticipant == pHead ) {
                bLocalWon = true;
            }
        } else {
            if ( pParticipant == pHead ) {
                bLocalWon = false;
            }
            // NOTE: retail matches the participant id against each
            // challenger (FUN_00803940); offline only bots race.
            for ( int32_t i = 0; i < kMaxNumPlayers; i++ ) {
                if ( !bChallengerUsed[i] &&
                     IsICEPlayerValid( pChallengers[i] ) ) {
                    bChallengerUsed[i] = true;
                    pSlot = &pChallengers[i];
                    break;
                }
            }
        }
        if ( numPlayers < kMaxNumPlayers ) {
            pRanked[numPlayers] = pSlot;
        }
        numPlayers++;
    }

    FlashPlayer *pPlayer = GetHudPlayer();
    if ( pPlayer != nullptr ) {
        pPlayer->setVariableValue( "/:nb_players", numPlayers );
    }
}

void GameMechInstantChallenge::showResults() {
    // FUN_00729200
    FlashPlayer *pPlayer = GetHudPlayer();
    for ( const ICEChallenger *&pSlot : pRanked ) {
        pSlot = nullptr;
    }
    localPlayer.bBot = false;
    localPlayer.ID = kLocalICEPlayer;
    localPlayer.bRefusedICE = false;
    localPlayer.bAcceptedICE = bLocalAccepted;
    if ( pPlayer != nullptr ) {
        pPlayer->showFrame( "instant_results", "/", true );
    }
}

void GameMechInstantChallenge::requestPlayerStats() {
    // FUN_007294e0
    if ( bStatsRequested || pChallengers[0].bBot ) {
        return;
    }
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (GameSpy stats query for the
                             // challengers, FUN_008774f0)
    bStatsRequested = true;
}

void GameMechInstantChallenge::getPlayerStats( const bool /*bRemote*/,
                                               const ICEChallenger * /*pPlayer*/,
                                               uint32_t *pOutRank,
                                               uint32_t *pOutPoints ) {
    // FUN_00729020
    *pOutRank = 0;
    *pOutPoints = 0;
    if ( !bStatsRequested ) {
        return;
    }
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (GameSpy stats results)
}

void GameMechInstantChallenge::fillPlayerRows() {
    // FUN_00729880
    FlashPlayer *pPlayer = GetHudPlayer();
    requestPlayerStats();
    if ( pPlayer == nullptr ) {
        return;
    }

    const int32_t stake = gpScenaric->getICDatas().Stake;
    for ( int32_t i = 0; i < kMaxNumPlayers; i++ ) {
        const ICEChallenger *pSlot = pRanked[i];
        if ( pSlot == nullptr || !IsICEPlayerValid( *pSlot ) ) {
            continue;
        }

        const int32_t lobbyIndex = i + 1;
        uint32_t rank = 0;
        uint32_t points = 0;
        getPlayerStats( pSlot != &localPlayer, pSlot, &rank, &points );

        char variable[0x40];
        char name[0x40];
        GetICEPlayerNickname( *pSlot, name, 0x20 );
        if ( name[0] != '\0' ) {
            snprintf( variable, sizeof( variable ), "/:Player%dName",
                      lobbyIndex );
            pPlayer->setVariableValueNonLinked( variable, name );
            snprintf( variable, sizeof( variable ), "/cases/results/p%d/:name",
                      lobbyIndex );
            pPlayer->setVariableValueNonLinked( variable, name );

            char valueText[0x40];
            snprintf( valueText, sizeof( valueText ), "%u", points );
            const char *pPoints = ( rank != 0 ) ? valueText : "-";
            snprintf( variable, sizeof( variable ), "/:Player%dPoints",
                      lobbyIndex );
            pPlayer->setVariableValueNonLinked( variable, pPoints );
            snprintf( variable, sizeof( variable ),
                      "/cases/results/p%d/:points", lobbyIndex );
            pPlayer->setVariableValueNonLinked( variable, pPoints );

            snprintf( valueText, sizeof( valueText ), "%u", rank );
            const char *pRank = ( rank != 0 ) ? valueText : "-";
            snprintf( variable, sizeof( variable ), "/:Player%dRank",
                      lobbyIndex );
            pPlayer->setVariableValueNonLinked( variable, pRank );
            snprintf( variable, sizeof( variable ), "/cases/results/p%d/:rank",
                      lobbyIndex );
            pPlayer->setVariableValueNonLinked( variable, pRank );

            char moneyText[0x40];
            char moneyValue[0x40];
            if ( lobbyIndex == 1 ) {
                int32_t numParticipants = 0;
                const GameMechRaceHUD *pRaceHUD =
                    static_cast<const GameMechRaceHUD *>(
                        pMech->getComponent( GameMechanism::CS_RaceHUD ) );
                if ( pRaceHUD != nullptr ) {
                    for ( GameParticipant *pParticipant =
                              pRaceHUD->getRankingHead();
                          pParticipant != nullptr;
                          pParticipant = pParticipant->getSessionParams()
                                             .pRankNextPlayer ) {
                        numParticipants++;
                    }
                }
                if ( pRaceHUD != nullptr && numParticipants == 1 &&
                     !pChallengers[0].bBot && field_0x84 ) {
                    snprintf( moneyText, sizeof( moneyText ), "-%s",
                              FormatMoneyString( moneyValue, stake ) );
                } else {
                    snprintf(
                        moneyText, sizeof( moneyText ), "+%s",
                        FormatMoneyString( moneyValue,
                                           ( numParticipants - 1 ) * stake ) );
                }
            } else {
                snprintf( moneyText, sizeof( moneyText ), "-%s",
                          FormatMoneyString( moneyValue, stake ) );
            }
            snprintf( variable, sizeof( variable ), "/:Player%dMoney",
                      lobbyIndex );
            pPlayer->setVariableValueNonLinked( variable, moneyText );
            snprintf( variable, sizeof( variable ), "/cases/results/p%d/:money",
                      lobbyIndex );
            pPlayer->setVariableValueNonLinked( variable, moneyText );

            if ( pSlot == &localPlayer ) {
                pPlayer->setVariableValue( "/:my_rank", lobbyIndex );
                localPlayer.bAcceptedICE = bLocalAccepted;
            }
        }

        const char *pFrame = "default";
        if ( pSlot->bAcceptedICE ) {
            pFrame = "on";
        }
        if ( pSlot->bRefusedICE ) {
            pFrame = "off";
        }
        snprintf( variable, sizeof( variable ), "/cases/results/p%d",
                  lobbyIndex );
        pPlayer->showFrame( pFrame, variable, false );
    }
}

void GameMechInstantChallenge::updateRanking( const float deltaTime ) {
    // FUN_00729c10
    if ( bRankingCounted ) {
        return;
    }
    rankingTime += deltaTime;
    if ( rankingTime <= kRankingCountDelay ) {
        return;
    }
    bRankingCounted = true;

    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    if ( bLocalWon ) {
        AchievementsData *pAchievements = pSave->getAchievements();
        if ( pChallengers[0].bBot ) {
            pAchievements->addInstantChallengeWonOffline(
                int32_t( pChallengers[0].ID ) );
        } else {
            pAchievements->addInstantChallengeWonOnline();
        }
        pSave->getAchievements()->notify( 7u, false );
    }
    pSave->getExtraChallenges()->addCounter(
        CounterIndex( bLocalWon ? 0xf1a8 : 0xf1ac ), 1 );
}
