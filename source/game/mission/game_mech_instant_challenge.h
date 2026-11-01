#pragma once

#include "game/game_mechanism.h"
#include "game/scenaric/gs_scenaric.h"

// Instant challenge results (players, stake)
class GameMechInstantChallenge : public GameMechComponent {
public:
    explicit GameMechInstantChallenge( GameMechanism *pOwnerMech );

    bool initialize() override;
    bool onMechStateProcess( const int32_t state,
                             const float deltaTime,
                             const eStateProcess process ) override;

    void rankPlayers();

private:
    static constexpr int32_t kMaxNumPlayers = 16;

private:
    void showResults();
    void fillPlayerRows();
    void requestPlayerStats();
    void getPlayerStats( const bool bRemote,
                         const ICEChallenger *pPlayer,
                         uint32_t *pOutRank,
                         uint32_t *pOutPoints );
    void updateRanking( const float deltaTime );

private:
    ICEChallenger localPlayer;                    // +0x24
    const ICEChallenger *pRanked[kMaxNumPlayers]; // +0x30
    ICEChallenger *pChallengers;                  // +0x70
    int32_t field_0x74;                           // +0x74
    bool bLocalAccepted;                          // +0x78
    float rankingTime;                            // +0x7c
    bool bRankingCounted;                         // +0x80
    bool bLocalWon;                               // +0x81
    bool bStatsRequested;                         // +0x82
    bool field_0x83;                              // +0x83
    bool field_0x84;                              // +0x84
};
