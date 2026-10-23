#pragma once

#include "game/game_mechanism.h"

class EntityCheckPoint;
class EventParameters;
class GameParticipant;
class MngActor;

// Checkpoint progression, GPS objectives and result list
class GameMechRaceHUD : public GameMechComponent {
public:
    explicit GameMechRaceHUD( GameMechanism *pOwnerMech );
    ~GameMechRaceHUD() override;

    inline void setShowCheckpointHud( const bool b ) { bShowCheckpointHud = b; }
    inline void setShowNextCheckpointOnly( const bool b ) {
        bShowNextCheckpointOnly = b;
    }
    inline int32_t getNumSteps() const { return numSteps; }
    inline GameParticipant *getRankingHead() const { return pRankingHead; }
    inline const uint32_t *getCurrentTimes() const { return pCurrentTimes; }
    inline int32_t getNumLaps() const { return numLaps; }

    bool onMessage( Entity *pSender,
                    const int32_t messageID,
                    void *pParam ) override;
    bool initialize() override;
    bool setupHUD() override;
    void reset() override;
    void restart() override { initialize(); }
    bool terminate() override;
    void printDebug() override;
    int32_t fillResults( FlashPlayer *pPlayer ) override;
    bool onMechStateProcess( const int32_t state,
                             const float deltaTime,
                             const eStateProcess process ) override;
    bool onSessionRaceMessage( const int32_t playerSlot,
                               const RaceSessionMessage *pMessage ) override;
    virtual void updateObjectives();

    int32_t wrapStep( const int32_t step ) const;
    bool isOnLapCheckpoint( const int32_t slot ) const;
    int32_t stepFromIndexAndLap( const int32_t checkpointIndex,
                                 const int32_t lap ) const;
    int32_t getParticipantLap( const int32_t slot ) const;
    EntityCheckPoint *findCheckpointForStep( int32_t step ) const;

protected:
    void resetCheckpoints();

private:
    EntityCheckPoint *swapCheckpointTemplate( EntityCheckPoint *pCheckpoint,
                                              const uint32_t ledType );
    void computeCheckpointCounts();
    void updateLoopCheckpointTemplate();
    int32_t sortRanking( const bool bUseDistance );
    void assignRanks();
    bool isRivalOnSameStep( const int32_t step,
                            GameParticipant *pParticipant,
                            const int32_t slot ) const;
    bool recordCheckpointTime( EntityCheckPoint *pCheckpoint,
                               const CheckpointPassedParam *pParam );
    bool showCheckpointGap( const uint32_t recordTime,
                            const uint32_t time,
                            const bool bShowGap );
    void showBottomGap( const int32_t gap );
    void updateCountersHud();
    static void UpdatePositionHud( FlashPlayer *pPlayer,
                                   const int32_t rank,
                                   const uint32_t numPlayers );

protected:
    uint32_t *pRecordTimes;
    uint32_t *pCurrentTimes;
    EventParameters *pEventParameters;
    MngActor *pMngActor;
    GameParticipant *pRankingHead;
    char nameList[0x200];
    char timeList[0x200];
    char posList[0x100];
    char namePlayerList[0x80];
    char timePlayerList[0x80];
    char posPlayerList[0x80];

private:
    int32_t numSteps;
    int32_t numLaps;
    int32_t loopStartIndex;
    int32_t checkpointsPerLap;
    float templateSwapTimer;
    bool bShowCheckpointHud;
    bool bShowNextCheckpointOnly;
    bool bActive;
};
