#pragma once

#include "game/game_mechanism.h"

class GameMechRaceHUD;

// Endless loop race: the last participant of each lap is eliminated until a
// single one remains
class GameMechEliminator : public GameMechComponent {
public:
    explicit GameMechEliminator( GameMechanism *pOwnerMech );

    bool initialize() override;
    bool setupHUD() override;
    void printDebug() override;
    int32_t fillResults( FlashPlayer *pPlayer ) override;
    bool onMechStateProcess( const int32_t state,
                             const float deltaTime,
                             const eStateProcess process ) override;

private:
    static constexpr int32_t kMaxNumParticipants = 8;
    static constexpr float kLastPlayerWaitDuration = 1.5f; // DAT_00f416b8

    struct Entry {
        int32_t field_0x0 = 0;
        bool bInRace = false;
        bool bParticipant = false;
        int32_t EliminationRank = -1;
    };

private:
    FlashPlayer *getHudPlayer() const;
    void updateHud();
    void updateElimination( const float deltaTime );
    void eliminate( const int32_t index );
    void updateLapCounts();

private:
    GameMechRaceHUD *pRaceHUD;
    int32_t numParticipants;
    int32_t numRemaining;
    Entry entries[kMaxNumParticipants];
    float waitTime;
    int32_t numInactive;
    int32_t numBehind;

    char nameList[0x200];
    char timeList[0x200];
    char posList[0x100];
    char namePlayerList[0x80];
    char timePlayerList[0x80];
    char posPlayerList[0x80];
};
