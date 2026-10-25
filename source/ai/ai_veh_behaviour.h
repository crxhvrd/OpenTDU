#pragma once

#include "ai/ai_fsm.h"
#include "ai/ai_race.h"

class AiVehicle;
class CarState;
class DrawList;
struct CollisionEvent;

class AiVehBehaviour {
public:
    virtual ~AiVehBehaviour() = default;

    virtual void attach( AiVehicle *pOwner ) = 0;
    virtual void reset( const bool bFull ) = 0;
    virtual void tick( const float deltaTime, const float totalTime ) = 0;
    virtual void release() = 0;
    virtual void debugDraw( DrawList *pDrawList ) {}
    virtual void onCollision( CarState *pCarA,
                              CarState *pCarB,
                              const CollisionEvent &event ) = 0;
    virtual void onRaceMessage( const AiRaceMessage &message ) {}
};

// Behaviour type 2: traffic gives up its navigation when stuck or crashed
class AiVehBhvTraffic : public AiVehBehaviour {
public:
    AiVehBhvTraffic();

    void attach( AiVehicle *pOwner ) override;
    void reset( const bool bFull ) override;
    void tick( const float deltaTime, const float totalTime ) override;
    void release() override;
    void onCollision( CarState *pCarA,
                      CarState *pCarB,
                      const CollisionEvent &event ) override;

private:
    AiVehicle *pVehicle;
};

// Behaviour type 1: race opponent, driven by the race messages
class AiVehBhvOpponent : public AiVehBehaviour {
public:
    inline int32_t getOpponentIndex() const { return opponentIndex; }
    inline void setOpponentIndex( const int32_t index ) {
        opponentIndex = index;
    }

public:
    AiVehBhvOpponent();

    void attach( AiVehicle *pOwner ) override;
    void reset( const bool bFull ) override;
    void tick( const float deltaTime, const float totalTime ) override;
    void release() override;
    void onCollision( CarState *pCarA,
                      CarState *pCarB,
                      const CollisionEvent &event ) override;
    void onRaceMessage( const AiRaceMessage &message ) override;

private:
    AiVehicle *pVehicle;
    int32_t opponentIndex;
    float field_0xc;
    float raceStartTime;

private:
    void prepareRace();
    void applyCatchUp();
};

// Behaviour type 4: no behaviour logic (training driver on the player car)
class AiVehBhvPassive : public AiVehBehaviour {
public:
    AiVehBhvPassive();

    void attach( AiVehicle *pOwner ) override;
    void reset( const bool bFull ) override {}
    void tick( const float deltaTime, const float totalTime ) override {}
    void release() override {}
    void onCollision( CarState *pCarA,
                      CarState *pCarB,
                      const CollisionEvent &event ) override {}
    void onRaceMessage( const AiRaceMessage &message ) override;

private:
    AiVehicle *pVehicle;
};

// Behaviour type 3: free ride bot, cruises and chases the local player
class AiVehBhvBot : public AiVehBehaviour, private AiFsm {
public:
    enum eState : int32_t {
        BS_Idle = 0,
        BS_Start = 1,
        BS_Cruise = 2,
        BS_Chase = 4,
    };

public:
    inline int32_t getState() const { return getCurrentState(); }
    inline float getDistanceToPlayer() const { return distanceToPlayer; }

public:
    AiVehBhvBot();

    void attach( AiVehicle *pOwner ) override;
    void reset( const bool bFull ) override;
    void tick( const float deltaTime, const float totalTime ) override;
    void release() override;
    void onCollision( CarState *pCarA,
                      CarState *pCarB,
                      const CollisionEvent &event ) override;
    void initialize();

private:
    AiVehicle *pVehicle;
    float field_0x2c;
    bool field_0x30;
    bool field_0x31;
    float distanceToPlayer;
    float field_0x38;
    Eigen::Vector4f field_0x40;
    float lastTauntTime;
    float chaseEndTime;

private:
    bool handle( const int32_t event, const int32_t state ) override;

    bool isTooFar();
    bool shouldStartChase() const;
    bool shouldStopChase() const;
    void taunt();
    void removeTooFar();
};
