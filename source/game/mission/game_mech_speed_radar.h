#pragma once

struct FlashMessage;

#include "game/mission/game_mech_race_hud.h"

// Speed trap
class GameMechSpeedRadar : public GameMechRaceHUD {
public:
    inline int32_t getNumRadars() const { return numRadars; }
    inline uint32_t getBaseScore() const { return baseScore; }
    inline const uint32_t *getRadarRecords() const { return pRecordTimes; }
    inline float getFinalScore() const {
        // FUN_00416800
        return float( finalScore ) * 0.001f;
    }

public:
    explicit GameMechSpeedRadar( GameMechanism *pOwnerMech );

    bool onMessage( Entity *pSender,
                    const int32_t messageID,
                    void *pParam ) override;
    bool initialize() override;
    bool setupHUD() override;
    void reset() override;
    void printDebug() override;
    int32_t fillResults( FlashPlayer *pPlayer ) override;

    void onHudMessage( const FlashMessage &message );
    bool onMechStateProcess( const int32_t state,
                             const float deltaTime,
                             const eStateProcess process ) override;
    bool onSessionRaceMessage( const int32_t playerSlot,
                               const RaceSessionMessage *pMessage ) override;
    void updateObjectives() override;

private:
    static constexpr int32_t kMaxNumRadars = 8;

private:
    uint32_t computeScore();
    void sendScore( const uint32_t score );
    bool onRadarPassed( EntityCheckPoint *pCheckpoint );
    void showRadarResult( const char *pPosText,
                          const char *pFrameLabel,
                          const int32_t radarIndex,
                          const float displaySpeed );
    void setupRadarHud();
    int32_t fillRadarResults( FlashPlayer *pPlayer );

private:
    uint16_t numRadarsPassed;
    int32_t numRadars;
    uint32_t baseScore;
    uint32_t finalScore;
    bool bHasStartCheckpoint;
    float penaltyScore;
};
