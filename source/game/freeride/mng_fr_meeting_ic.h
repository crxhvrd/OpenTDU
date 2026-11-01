#pragma once

#include "game/mng_base.h"
#include "game/scenaric/gs_scenaric.h"

class FlashPlayer;
class P2PPlayer;
struct FlashMessage;

enum eInstantChallengeState : int32_t {
    ICState_None = 0,
    ICState_ChallengeBoxShown = 1,
    ICState_GoingInIC = 2
};

struct ICEChallenger;
class FlashPlayer;

// Local player slot in the instant challenge lists.
static constexpr uint32_t kLocalICEPlayer = 999;

void GetICEPlayerNickname( const ICEChallenger &player,
                           char *pOutName,
                           const size_t nameLength );
bool IsICEPlayerValid( const ICEChallenger &player );
void FillICEPlayerInfo( const ICEChallenger &player,
                        const uint32_t lobbyIndex,
                        FlashPlayer *pPlayer );

// FreeRide Meeting Instance Challenge manager.
class MngFRMeetingIC : public Manager {
public:
    const char *getName() const override {
        // FUN_0042c570
        return "MeetIC";
    }

    inline eInstantChallengeState getState() const { return State; }
    inline int32_t getStake() const { return Stake; }
    inline bool isOpponentInICZone() const { return bOpponentInICZone; }
    inline void setFlashPlayer( FlashPlayer *pPlayer ) {
        pFlashVariable = pPlayer;
    }

public:
    MngFRMeetingIC();
    ~MngFRMeetingIC();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float deltaTime, float ) override;

    void reset() override {
        // FUN_00791830
        // NOP
    }

    void draw() override {
        // FUN_00791820
        // NOP
    }

    void pause() override {
        // FUN_00791810
        // NOP
    }

    void terminate() override {
        // FUN_00791800
        // NOP
    }

    bool onMessage( FlashMessage *pMessage );
    void setState( const eInstantChallengeState newState );
    void onScenaricMessage( const uint32_t senderPlayerIndex,
                            const uint32_t crc );

private:
    void refreshChallengerFrames();
    void showChallengeBox();
    void fillChallengersFromLobby( P2PPlayer *pOpponent );
    void setBotChallenger( const int32_t botID );
    void launchChallenge();
    void closeChallengeBox();

private:
    bool bOpponentInICZone;
    eInstantChallengeState State;

    ICEChallenger *pICEPlayers;
    ICEChallenger LocalPlayer;

    bool bInitializing;
    int32_t Stake;

    bool bOverWLAN;

    FlashPlayer *pFlashVariable;

    float TimeoutBox;
    float BotWaitingTime;

    P2PPlayer *pBlacklisted;
    float TimeoutBlacklist;

    bool bFlashCalled;
};

extern MngFRMeetingIC *gpMngFRMeetingIC;
