#pragma once

struct FlashMessage;

#include "game/game_mechanism.h"

// Average speed
class GameMechSpeedGauge : public GameMechComponent {
public:
    static constexpr int32_t kResultTargetReached = 3;

public:
    inline float getTargetSpeed() const { return targetSpeed; }
    inline float getMinSpeed() const { return minSpeed; }
    inline float getMaxSpeed() const { return maxSpeed; }
    inline float getInstantSpeed() const { return instantSpeed; }
    inline float getAverageSpeed() const { return averageSpeed; }
    inline float getTopSpeed() const { return topSpeed; }

public:
    explicit GameMechSpeedGauge( GameMechanism *pOwnerMech );

    bool initialize() override;
    bool setupHUD() override;
    void printDebug() override;
    int32_t fillResults( FlashPlayer *pPlayer ) override;

    void onHudMessage( const FlashMessage &message );
    bool onMechStateProcess( const int32_t state,
                             const float deltaTime,
                             const eStateProcess process ) override;

private:
    static constexpr int32_t kMaxNumListedPlayers = 16;

private:
    FlashPlayer *getHudPlayer() const;

private:
    float targetSpeed;
    int32_t field_0x28;
    float minSpeed;
    float maxSpeed;
    float instantSpeed;
    float startDistance;
    float distance;
    float elapsedTime;
    float averageSpeed;
    float topSpeed;
    char nameListST[0x200];
    char totalScoreListST[0x200];
    char posListST[0x100];
    char namePlayerList[0x80];
    char totalScorePlayerList[0x80];
    char posPlayerList[0x80];
};
