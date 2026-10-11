#pragma once

#include "ai/ai_road_iterator.h"

class AiVehicle;

// Feeds the navigation with the roads to drive on
class AiVehController {
public:
    virtual ~AiVehController() = default;

    virtual void attach( AiVehicle *pOwner ) = 0;
    virtual void reset( const bool bFull ) = 0;
    virtual void tick( const float deltaTime, const float totalTime ) = 0;
    virtual void release() = 0;
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

private:
    AiVehicle *pVehicle;
    std::vector<RouteElem> route;
    int32_t count;
    int32_t front; // Index before the oldest element
    int32_t back;  // Next write

private:
    void clear();
    void pushBack( const RouteElem &elem );
    void refill();
    const RouteElem &at( const int32_t index ) const;
};
