#pragma once

#include "game/game_mechanism.h"

class CarState;

// Offroad penalty
class GameMechPenalty : public GameMechComponent {
public:
    enum ePenaltyState : int32_t {
        PS_OffRoad = 0,
        PS_OnRoad = 1,
    };

    enum eDisplayMode : int32_t {
        DM_Score = 0,
        DM_Money = 1,
    };

    static constexpr uint32_t kPenaltyOffRoad = 0x1;
    static constexpr uint32_t kPenaltyCollisions = 0x10;
    static constexpr uint32_t kPenaltyDefaultFlags =
        kPenaltyOffRoad | kPenaltyCollisions;

public:
    inline float getPenalty() const { return penalty; }
    inline float getMaxPenalty() const { return maxPenalty; }
    inline float getPenaltyRatio() const { return penalty / maxPenalty; }
    inline void setMaxPenalty( const float value ) { maxPenalty = value; }
    inline uint32_t getPenaltyFlags() const { return penaltyFlags; }
    inline void setPenaltyFlags( const uint32_t flags ) {
        penaltyFlags = flags;
    }
    inline void setDisplayMode( const eDisplayMode mode ) {
        displayMode = mode;
    }

public:
    explicit GameMechPenalty( GameMechanism *pOwnerMech );

    bool stateProcess( const int32_t state,
                       const float deltaTime,
                       const eStateProcess process ) override;
    bool initialize() override;
    bool setupHUD() override;
    void reset() override;
    void draw() override;
    void pause() override;
    void printDebug() override;
    bool onMechStateProcess( const int32_t state,
                             const float deltaTime,
                             const eStateProcess process ) override;

private:
    static void OnCollision( uint8_t collisionType,
                             CarState *pOther,
                             void *pOtherObject,
                             void *pUserData );

private:
    void addPenalty( const float amount );
    void updateWarningSound();
    void updatePendingPenalty( const float deltaTime );
    void displayPenalty();

private:
    float partialOffRoadRate;
    float fullOffRoadRate;
    float penalty;
    float maxPenalty;
    float offRoadEntryPenalty;
    uint32_t penaltyFlags;
    float pendingPenalty;
    float pendingDisplayTimer;
    int32_t pendingMessageSlot;
    bool bPlayWarningSound;
    bool bWarningSoundPlaying;
    eDisplayMode displayMode;
    int32_t offRoadFrameCount;
};
