#pragma once

#include "ai/ai_road_iterator.h"

class PoliceState {
public:
    static constexpr int32_t kNumCollidedCars = 16;

    struct CollidedCar {
        const void *pCarPointer = nullptr;
        float CollisionTime = 0.0f;
    };

public:
    inline bool isArrested() const { return bArrested; }
    inline float getWasHitLastTime() const { return WasHitLastTime; }
    inline int32_t getJunctionNotRespected() const {
        return JunctionNotRespected;
    }

public:
    PoliceState();
    ~PoliceState();

    void reset();
    bool isRecentCarCollision( const void *pCar ) const;
    bool checkRecentCollision( const void *pCar, const void *pObject );

private:
    int32_t NumPoliceDamForced = 0;
    float LastPoliceDamEventTime = 0.0f;
    float WasHitLastTime = 0.0f;
    float HitLastTime = 0.0f;
    float ReduceInfBaseTime = 0.0f;
    float LastInfTimeWitness = 0.0f;
    float MinTimeToStopSearch = 0.0f;
    float MinTimeToStopSearchMalus = 0.0f;
    float WitnessedOffence = 0.0f;
    float SpeedLimit = 0.0f;
    float Stop = 0.0f;
    float RedLight = 0.0f;
    float Collision = 0.0f;
    int32_t NumCollisionCar = 0;
    int32_t NumCollisionSettings = 0;
    float OnComingLane = 0.0f;
    float Yield = 0.0f;
    AiRoadIt LastValidRoadIT;
    float LastTimeForValidRoadIT = 0.0f;
    uint32_t CurControlDevice = 0;
    bool bCurControlDeviceRespected = false;
    float LastTimeForDeviceRespected = 0.0f;
    uint32_t SignalLastState = 4;
    int32_t JunctionNotRespected = -1;
    float AfterCaptureGraceTime = 0.0f;
    bool bIsSearched = false;
    AiRoadIt LastSearchPos;
    float LastSearchRefresh = 0.0f;
    bool bIsChased = false;
    int32_t NumChasers = 0;
    int32_t NumWantedChaser = 0;
    bool bArrestAfterBackOnTrack = false;
    bool bArrested = false;
    bool bPlayingArrestCinematic = false;
    float LastGameCounterTime = 0.0f;
    CollidedCar CarCollisions[kNumCollidedCars];
};

class PoliceStateJob {
public:
    virtual void check( class CarState *param_1, float param_2 ) = 0;
};
