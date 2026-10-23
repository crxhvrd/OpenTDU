#pragma once

#include "game/game_mechanism.h"

// Time Attack
class GameMechTime : public GameMechComponent {
public:
    inline float getElapsedTime() const { return elapsedTime; }
    inline float getStartGameTime() const { return startGameTime; }
    inline float getTimeLimit() const { return timeLimit; }
    inline bool hasTimeLimit() const { return bHasTimeLimit; }
    inline bool isCountdownActive() const { return bCountdownActive; }
    inline float getRaceTime() const { return raceTime; }
    inline float getFinalTime() const { return finalTime; }
    inline void setFinalTime( const float time ) { finalTime = time; }
    inline void setTimeLimit( const float seconds ) {
        timeLimit = seconds;
        bHasTimeLimit = true;
    }
    inline void setHasTimeLimit( const bool bValue ) { bHasTimeLimit = bValue; }

public:
    explicit GameMechTime( GameMechanism *pOwnerMech );

    bool initialize() override;
    bool setupHUD() override;
    void printDebug() override;
    int32_t fillResults( FlashPlayer *pPlayer ) override;
    bool onMechStateProcess( const int32_t state,
                             const float deltaTime,
                             const eStateProcess process ) override;

    bool startCountdown( const float duration );
    void addTimeBonus( const float bonus );
    void setTimeVisible( const bool bVisible );

private:
    static constexpr int32_t kNumTimeOutTicks = 6;

private:
    FlashPlayer *getHudPlayer() const;
    bool setTimeOutVisible( const bool bVisible );
    void displayTime( const float seconds, const bool bCountdown );

private:
    float elapsedTime;
    float startGameTime;
    float elapsedSystemTime;
    float startSystemTime;
    float timeLimit;
    bool bHasTimeLimit;
    bool bCountdownActive;
    float endGameTime;
    float raceTime;
    float finalTime;
    int32_t timeOutTicks;
};
