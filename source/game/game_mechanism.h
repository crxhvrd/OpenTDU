#pragma once

#include "game/scenaric/gs_scenaric.h"

class edAudioStreamEntry;
class Entity;
class EntityCinematic;
class FlashPlayer;
class GameMechanism;
class GameMechTime;
class GameModeWorld;
class GameParticipant;
class GameSession;

struct CheckpointPassedParam {
    int32_t CheckpointIndex;
    uint32_t CarID;     // CarState object ID
    int32_t PlayerSlot; // -1 = every slot
};

class StateMachine {
public:
    enum eStateProcess : int32_t {
        SP_Init = 0xc0c0,
        SP_Update = 0xc0c1,
        SP_Term = 0xc0c2,
    };

    static constexpr int32_t kNoState = -1;
    static constexpr int32_t kMsgRestartState = 2;

public:
    inline int32_t getCurrentState() const { return curState; }
    inline int32_t getPreviousState() const { return prevState; }
    inline float getStateInitTime() const { return stateInitTime; }

public:
    StateMachine();
    virtual ~StateMachine() {}

    bool receiveMessage( const int32_t messageID, void *pParam );
    virtual bool
    onMessage( Entity *pSender, const int32_t messageID, void *pParam );
    virtual bool stateProcess( const int32_t state,
                               const float deltaTime,
                               const eStateProcess process ) = 0;
    virtual bool stateChange( const int32_t state );

protected:
    int32_t curState;
    int32_t prevState;
    int32_t nextState;
    float stateInitTime;
};

// Game mechanism component (race HUD, time, radars, penalties, ...)
class GameMechComponent : public StateMachine {
public:
    enum eResult : int32_t {
        CR_None = 0,
        CR_Success = 1,
        CR_Failure = 2,
    };

public:
    inline int32_t getResult() const { return result; }
    inline int32_t getResultReason() const { return resultReason; }

public:
    explicit GameMechComponent( GameMechanism *pOwnerMech );

    bool stateProcess( const int32_t /*state*/,
                       const float /*deltaTime*/,
                       const eStateProcess /*process*/ ) override {
        // FUN_00609bc0
        return false;
    }

    virtual bool initialize();
    virtual bool setupHUD() { return false; }
    virtual void reset() {}
    virtual void draw() {}
    virtual void restart() {
        // FUN_0042e240
        initialize();
    }
    virtual void pause() {}
    virtual bool terminate() { return true; }
    virtual void printDebug();
    virtual int32_t fillResults( FlashPlayer * /*pPlayer*/ ) {
        // FUN_00423430
        return -1;
    }
    virtual bool onMechStateProcess( const int32_t /*state*/,
                                     const float /*deltaTime*/,
                                     const eStateProcess /*process*/ ) {
        // FUN_00609bc0
        return false;
    }
    virtual int32_t getResultState() const {
        // FUN_006093c0
        return endState;
    }
    virtual bool
    onSessionRaceMessage( const int32_t /*playerSlot*/,
                          const struct RaceSessionMessage * /*pMessage*/ ) {
        // FUN_004182d0
        return false;
    }

protected:
    GameMechanism *pMech;
    int32_t endState;
    int32_t result;
    int32_t resultReason;
};

// Extra challenge penalty listener (mech +0x7b0)
class GameMechPenaltyListener {
public:
    virtual bool onPenalty( const float remainingRatio,
                            const float pendingRatio ) = 0;
};

// Race rules
class GameMechanism : public StateMachine, public CinematicPlayer {
public:
    enum eMechState : int32_t {
        MS_WaitingHUD = 0,
        MS_InstallingResources = 1,
        MS_LoadingMap = 2,
        MS_WaitingOnlinePlayers = 3,
        MS_WaitingStart = 4,
        MS_Running = 5,
        MS_Ranking = 6,
        MS_Driven = 7,
        MS_ExtraChallengeOutro = 8,
        MS_ExtraChallengeSucceeded = 9,
        MS_ExtraChallengeAborted = 10,
        MS_Finished = 11,
        MS_SessionError = 12,
    };

    enum eMechType : int32_t {
        MT_TimeAttack = 0,
        MT_Speed = 1,
        MT_Driving = 2,
        MT_Race = 3,
        MT_Player = 4,
        MT_None = 5,
        MT_ExtraChallenge = 6,
    };

    enum eComponentSlot : int32_t {
        CS_RaceHUD = 0,
        CS_SpeedRadar = 1,
        CS_Time = 2,
        CS_Unknown3 = 3,
        CS_SpeedGauge = 4,
        CS_Unknown5 = 5,
        CS_Unknown6 = 6,
        CS_Penalty = 7,
        CS_Unknown8 = 8,
        CS_InstantChallenge = 9,
        CS_Unknown10 = 10,
        CS_Player = 11,

        CS_Count
    };

    static constexpr int32_t kMsgCheckpointPassed = 3;
    static constexpr int32_t kMsgUnknown8 = 8;

public:
    inline int32_t getMechType() const { return mechType; }
    inline uint32_t getMechID() const { return mechID; }
    inline void setOwner( GameModeWorld *pGameMode ) { pOwner = pGameMode; }
    inline GameModeWorld *getOwner() const { return pOwner; }
    inline GameSession *getGameSession() const { return pGameSession; }
    inline bool getUnknown75C() const { return bUnknown75C; }
    inline EntityCinematic *getCinematic() const { return pCinematic; }
    inline GameMechComponent *getComponent( const eComponentSlot slot ) const {
        return pComponents[slot];
    }
    inline void setUnknown75C( const bool bValue ) { bUnknown75C = bValue; }
    inline uint64_t getSpotHashcode() const { return spotHashcode; }
    inline GameParticipant *getScoreRankingHead() const {
        return pScoreRankingHead;
    }
    GameMechTime *getTimeComponent() const;
    void fillChallengeResults( FlashPlayer *pPlayer,
                               const int32_t rank,
                               const bool bTimedResult );
    bool isEndlessLoopRace() const;
    inline void setCountdownGo( const bool bValue ) { bCountdownGo = bValue; }
    inline void requestAutopilotCinematic() { bAutopilotRequested = true; }
    inline edAudioStreamEntry *getOffroadWarningEntry() const {
        return pOffroadWarningEntry;
    }
    inline void setOffroadWarningEntry( edAudioStreamEntry *pEntry ) {
        pOffroadWarningEntry = pEntry;
    }
    inline GameMechPenaltyListener *getPenaltyListener() const {
        return pPenaltyListener;
    }
    inline void setPenaltyListener( GameMechPenaltyListener *pListener ) {
        pPenaltyListener = pListener;
    }
    inline bool hasRaceHUD() const {
        // FUN_007311a0 / FUN_00731170
        return pComponents[CS_RaceHUD] != nullptr;
    }

public:
    GameMechanism( const int32_t type, const uint32_t id );
    ~GameMechanism() override;

    bool onMessage( Entity *pSender,
                    const int32_t messageID,
                    void *pParam ) override;
    bool stateProcess( const int32_t state,
                       const float deltaTime,
                       const eStateProcess process ) override;

    virtual bool setupHUD();
    virtual bool initialize();
    virtual void reset();
    virtual void pause();
    virtual void restart();
    virtual void tick( const float deltaTime );
    virtual bool terminate();
    virtual void draw();
    virtual void initializeDriven();
    virtual void setGameSession( GameSession *pSession );

    bool executeAction( int32_t action, void *pSource, void *pData ) override;

    static void OnRankingFadedOut( void *pData, void *pSource );

protected:
    void resetRaceState();
    bool isCompetitiveEvent() const;
    bool areOthersStillRacing() const;
    bool findFinishedComponents( int32_t &outSuccessSlot,
                                 int32_t &outFailureSlot ) const;
    int32_t getComponentEndReason() const;
    bool checkTimeOut();
    bool isLocalPlayerArrested();
    void resetParticipantsRaceData();
    void placeParticipantsOnGrid( const bool bPausePhysics );
    void updateStartLineCamera();
    void updateCountdownCamera();
    void startCountdown();
    void updateWaitingForPlayersText( const int32_t numFinished );
    void sendPlayerFinished();
    void gotoResultsFrame();
    void showExtraChallengeResults();
    void showResults();
    void startEndCinematic( const Matrix4x4f_t &pose );
    void startEndCinematicAtPlayerCar();
    int32_t sortParticipantsByScore( const bool bHighestFirst );
    bool showPlayerRecord( const uint64_t hashcode );
    void setExtraChallengeName();

    bool processWaitingHUD( const eStateProcess process );
    bool processInstallingResources( const eStateProcess process );
    bool processLoadingMap( const eStateProcess process );
    bool processWaitingStart( const eStateProcess process );
    bool processRunning( const eStateProcess process );
    bool processRanking( const eStateProcess process );
    bool processExtraChallengeOutro( const eStateProcess process );
    bool processExtraChallengeSucceeded( const eStateProcess process );
    bool processExtraChallengeAborted( const eStateProcess process );
    bool processFinished( const eStateProcess process );

protected:
    void *pUnknown1C;
    Matrix4x4f_t countdownFrame;
    EntityCinematic *pCountdownCinematic;
    bool field_0x6e4;
    bool bCountdownGo;
    GameModeWorld *pOwner;
    GameSession *pGameSession;
    int32_t field_0x6f8;
    int32_t field_0x6fc[12];
    GameMechComponent *pComponents[CS_Count];
    bool bUnknown75C;
    int32_t mechType;
    uint32_t mechID;
    bool bRaceWon;
    char waitingForPlayersText[0x43];
    edAudioStreamEntry *pOffroadWarningEntry;
    GameMechPenaltyListener *pPenaltyListener;
    int32_t numParticipantsAtStart;
    int32_t numOnlineParticipants;
    float finishTime;
    float firstFinishTime;
    float timeOutDuration;
    float countdownDelay;
    int32_t firstFinisherIndex;
    uint32_t disconnectTick;
    bool bSavedGPSVoice;
    bool bSavedGPSDistanceCallouts;
    bool bUnknown7D6;
    bool bRestarted;
    bool field_0x7d8;
    int32_t endReason;
    const char *pDefaultPlayerName;
    GameParticipant *pScoreRankingHead;
    EntityCinematic *pCinematic;
    bool bAutopilotFadeStarted;
    bool bAutopilotPending;
    bool bAutopilotRequested;
    bool field_0x7ef;
    uint32_t rankingStartTick;
    bool bDriveInMessageShown;
    uint64_t spotHashcode;
    int32_t field_0x800[32];
};

void SetClipVisible( FlashPlayer *pPlayer,
                     const char *pClipPath,
                     const bool bVisible );

extern bool gGameMechsDebug; // DAT_010e7f37
