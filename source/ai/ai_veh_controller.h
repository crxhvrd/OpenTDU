#pragma once

#include "ai/ai_fsm.h"
#include "ai/ai_path.h"
#include "ai/ai_race.h"
#include "ai/ai_road_iterator.h"

class AiVehicle;
class CarState;
class DrawList;

// Feeds the navigation with the roads to drive on
class AiVehController {
public:
    virtual ~AiVehController() = default;

    virtual void attach( AiVehicle *pOwner ) = 0;
    virtual void reset( const bool bFull ) = 0;
    virtual void tick( const float deltaTime, const float totalTime ) = 0;
    virtual void release() = 0;
    virtual void debugDraw( DrawList *pDrawList ) {}
    virtual void debugText() {}
    virtual bool getFront( AiRoadIt &outFrom, AiRoadIt &outTo ) const = 0;
    virtual bool getSecond( AiRoadIt &outFrom, AiRoadIt &outTo ) const = 0;
    virtual int32_t getCount() const = 0;
    virtual void
    getAt( const int32_t index, AiRoadIt &outFrom, AiRoadIt &outTo ) const = 0;
    virtual void popFront() = 0;
    virtual void forceNextRoad( const uint32_t roadID ) = 0;
    virtual bool chooseNextRoad( const AiRoadIt &last,
                                 AiRoadIt &outFrom,
                                 AiRoadIt &outTo ) = 0;
    virtual void onRaceMessage( const AiRaceMessage &message ) {}
};

// Controller type 0: random route through the road network (traffic)
class AiVehCtrlRoute : public AiVehController {
public:
    struct RouteElem {
        AiRoadIt From;
        AiRoadIt To;
    };

public:
    AiVehCtrlRoute();

    void attach( AiVehicle *pOwner ) override;
    void reset( const bool bFull ) override;
    void tick( const float deltaTime, const float totalTime ) override;
    void release() override;
    bool getFront( AiRoadIt &outFrom, AiRoadIt &outTo ) const override;
    bool getSecond( AiRoadIt &outFrom, AiRoadIt &outTo ) const override;
    int32_t getCount() const override;
    void getAt( const int32_t index,
                AiRoadIt &outFrom,
                AiRoadIt &outTo ) const override;
    void popFront() override;
    void forceNextRoad( const uint32_t roadID ) override;
    bool chooseNextRoad( const AiRoadIt &last,
                         AiRoadIt &outFrom,
                         AiRoadIt &outTo ) override;

protected:
    AiVehicle *pVehicle;
    std::vector<RouteElem> route;
    int32_t count;
    int32_t front; // Index before the oldest element
    int32_t back;  // Next write

protected:
    void clear();
    void pushBack( const RouteElem &elem );
    void refill();
    const RouteElem &at( const int32_t index ) const;
};

// Controller type 7: road wandering for bots, AI-driven player cars and race
// opponents past the finish line
class AiVehCtrlFreeRoam : public AiVehCtrlRoute {
public:
    AiVehCtrlFreeRoam();

    void attach( AiVehicle *pOwner ) override;
    void reset( const bool bFull ) override;
    void tick( const float deltaTime, const float totalTime ) override;
    void release() override;
    bool getFront( AiRoadIt &outFrom, AiRoadIt &outTo ) const override;
    bool getSecond( AiRoadIt &outFrom, AiRoadIt &outTo ) const override;
    int32_t getCount() const override;
    void getAt( const int32_t index,
                AiRoadIt &outFrom,
                AiRoadIt &outTo ) const override;
    void popFront() override;
    bool chooseNextRoad( const AiRoadIt &last,
                         AiRoadIt &outFrom,
                         AiRoadIt &outTo ) override;

private:
    enum eMode : int32_t {
        M_Route = 0,
        M_Path = 1,
    };

private:
    // Path mode is never enabled by the retail game
    std::unique_ptr<AiPath> pPath;
    const AiRouteElem *pPathElem;
    const AiRouteElem *pNextPathElem;
    int32_t numPathElems;
    RouteElem pathFront;
    RouteElem pathSecond;
    eMode mode;
};

// Controller type 2: road route through the race checkpoints, one checkpoint
// leg at a time
class AiVehCtrlRacePath : public AiVehController, private AiFsm {
public:
    enum eState : int32_t {
        RPS_Reset = 1,
        RPS_Countdown = 2,
        RPS_Racing = 3,
        RPS_Idle = 4,
    };

public:
    AiVehCtrlRacePath();

    void attach( AiVehicle *pOwner ) override;
    void reset( const bool bFull ) override;
    void tick( const float deltaTime, const float totalTime ) override;
    void release() override;
    bool getFront( AiRoadIt &outFrom, AiRoadIt &outTo ) const override;
    bool getSecond( AiRoadIt &outFrom, AiRoadIt &outTo ) const override;
    int32_t getCount() const override;
    void getAt( const int32_t index,
                AiRoadIt &outFrom,
                AiRoadIt &outTo ) const override;
    void popFront() override;
    void forceNextRoad( const uint32_t roadID ) override;
    bool chooseNextRoad( const AiRoadIt &last,
                         AiRoadIt &outFrom,
                         AiRoadIt &outTo ) override;
    void onRaceMessage( const AiRaceMessage &message ) override;

private:
    AiVehicle *pVehicle;
    std::unique_ptr<AiPath> pPath;
    int32_t numCheckpoints;
    int32_t lastCheckpointRouted;
    int32_t maxRouteElems;

private:
    bool handle( const int32_t event, const int32_t state ) override;
    void updateRoute();
};

// Controller type 1: A* route toward a car (bot chase, police pursuit) or
// toward the last known location of a car (police search)
class AiVehCtrlChase : public AiVehController, private AiFsm {
public:
    enum eState : int32_t {
        CS_Chase = 1,
        CS_Goal = 2,
        CS_Idle = 3,
    };

public:
    AiVehCtrlChase();

    void attach( AiVehicle *pOwner ) override;
    void reset( const bool bFull ) override;
    void tick( const float deltaTime, const float totalTime ) override;
    void release() override;
    void debugDraw( DrawList *pDrawList ) override;
    bool getFront( AiRoadIt &outFrom, AiRoadIt &outTo ) const override;
    bool getSecond( AiRoadIt &outFrom, AiRoadIt &outTo ) const override;
    int32_t getCount() const override;
    void getAt( const int32_t index,
                AiRoadIt &outFrom,
                AiRoadIt &outTo ) const override;
    void popFront() override;
    void forceNextRoad( const uint32_t roadID ) override;
    bool chooseNextRoad( const AiRoadIt &last,
                         AiRoadIt &outFrom,
                         AiRoadIt &outTo ) override;

    void setTarget( CarState *pNewTarget );
    void setGoal( CarState *pNewGoal );
    bool isOnTargetRoad( const AiRoadIt &it ) const;

    inline CarState *getTarget() const { return pTarget; }

private:
    AiVehicle *pVehicle;
    std::unique_ptr<AiPath> pPath;
    AiRouteElem *pCurrentElem;
    AiRouteElem *pNextElem;
    int32_t count;
    AiRoadIt frontFrom;
    AiRoadIt frontTo;
    AiRoadIt secondFrom;
    AiRoadIt secondTo;
    CarState *pTarget;
    CarState *pGoal;
    AiRoadIt targetIt;
    AiRoadIt goalIt;
    AiRoadIt vehicleIt;
    Eigen::Vector4f debugLineStart;
    Eigen::Vector4f debugLineEnd;

private:
    bool handle( const int32_t event, const int32_t state ) override;
    void refreshRoute();
    void updateChaseRoute();
    void updateGoalRoute();
    void setSingleSegment( const AiRoadIt &from, const AiRoadIt &to );
};

// Controller type 6: route limited to the road the vehicle is on (arrested
// player car)
class AiVehCtrlCurrentRoad : public AiVehController, private AiFsm {
public:
    enum eState : int32_t {
        CRS_Follow = 1,
    };

public:
    AiVehCtrlCurrentRoad();

    void attach( AiVehicle *pOwner ) override;
    void reset( const bool bFull ) override;
    void tick( const float deltaTime, const float totalTime ) override;
    void release() override;
    bool getFront( AiRoadIt &outFrom, AiRoadIt &outTo ) const override;
    bool getSecond( AiRoadIt &outFrom, AiRoadIt &outTo ) const override;
    int32_t getCount() const override;
    void getAt( const int32_t index,
                AiRoadIt &outFrom,
                AiRoadIt &outTo ) const override;
    void popFront() override;
    void forceNextRoad( const uint32_t roadID ) override;
    bool chooseNextRoad( const AiRoadIt &last,
                         AiRoadIt &outFrom,
                         AiRoadIt &outTo ) override;

private:
    AiVehicle *pVehicle;
    AiRoadIt vehicleIt;
    AiRoadIt frontFrom;
    AiRoadIt frontTo;
    AiRoadIt secondFrom;
    AiRoadIt secondTo;

private:
    bool handle( const int32_t event, const int32_t state ) override;
};

bool HasAccessibleExit( const AiRoadIt &it );
