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

// Behaviour type 0: police patrol, search, chase and arrest
class AiVehBhvPolice : public AiVehBehaviour, private AiFsm {
public:
    enum eState : int32_t {
        PS_Lost = 0,
        PS_Init = 1,
        PS_Patrol = 2,
        PS_Search = 3,
        PS_Chase = 4,
        PS_Aggressive = 5,
        PS_Arrest = 6,
        PS_PoliceDam = 7,
    };

public:
    inline int32_t getState() const { return getCurrentState(); }
    inline bool isPursuing() const {
        return getCurrentState() == PS_Chase ||
               getCurrentState() == PS_Aggressive;
    }
    inline float getRandomFactor() const { return randomFactor; }
    inline CarState *getTarget() const { return pTarget; }
    inline CarState *getSearchedCar() const { return pSearchedCar; }
    inline void setSearchedCar( CarState *pCar ) { pSearchedCar = pCar; }
    inline float getDistanceToTarget() const { return distanceToTarget; }
    inline void requestState( const int32_t state ) { goTo( state ); }

public:
    AiVehBhvPolice();

    void attach( AiVehicle *pOwner ) override;
    void reset( const bool bFull ) override;
    void tick( const float deltaTime, const float totalTime ) override;
    void release() override;
    void debugDraw( DrawList *pDrawList ) override;
    void onCollision( CarState *pCarA,
                      CarState *pCarB,
                      const CollisionEvent &event ) override;
    void initialize();

private:
    AiVehicle *pVehicle;
    float randomFactor;
    float distanceToTarget;
    CarState *pTarget;
    CarState *pSearchedCar;
    int32_t field_0x3c;
    float captureStartTime;
    float lastTargetScanTime;

private:
    bool handle( const int32_t event, const int32_t state ) override;

    bool checkRemoval();
    bool canSeeCar( CarState *pCar ) const;
    void checkTargetVisibility( CarState *pCar,
                                bool &bOutVisible,
                                bool &bOutEligible ) const;
    void onTargetWitnessed( CarState *pCar ) const;
    bool shouldChase( CarState *pCar, bool &bOutWanted ) const;
    void findTarget( CarState *&pOutWantedCar );
    void updateDistanceToTarget();
    void loseTarget();
    void startArrest();
    bool checkArrest( bool &bOutArrestPending );
    void updatePatrolOrSearch();
    void updatePursuit();
    void setRouteController();
    void setChaseController();
    void setTrafficNavigation();
    void setOpponentNavigation();
    void setTrafficLocomotion();
    void setOpponentLocomotion();
    void enableSirens();
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
