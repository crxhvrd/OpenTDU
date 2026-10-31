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
    inline void setArrested( const bool bValue ) { bArrested = bValue; }
    inline bool isPlayingArrestCinematic() const {
        return bPlayingArrestCinematic;
    }
    inline void setPlayingArrestCinematic( const bool bValue ) {
        bPlayingArrestCinematic = bValue;
    }
    inline void setAfterCaptureGraceTime( const float time ) {
        AfterCaptureGraceTime = time;
    }
    inline const AiRoadIt &getLastValidRoadIt() const {
        return LastValidRoadIT;
    }
    inline int32_t getNumCollisionCar() const { return NumCollisionCar; }
    inline int32_t getNumCollisionSettings() const {
        return NumCollisionSettings;
    }
    inline bool isChased() const { return bIsChased; }
    inline float getWasHitLastTime() const { return WasHitLastTime; }
    inline int32_t getJunctionNotRespected() const {
        return JunctionNotRespected;
    }
    inline bool isSearched() const { return bIsSearched; }
    inline void setSearched( const bool bSearched ) { bIsSearched = bSearched; }
    inline void setChased( const bool bChased ) { bIsChased = bChased; }
    inline int32_t getNumChasers() const { return NumChasers; }
    inline void setNumChasers( const int32_t numChasers ) {
        NumChasers = numChasers;
    }
    inline int32_t getNumWantedChaser() const { return NumWantedChaser; }
    inline void setNumWantedChaser( const int32_t numWantedChaser ) {
        NumWantedChaser = numWantedChaser;
    }
    inline void stopSearch() {
        bIsSearched = false;
        LastSearchPos = AiRoadIt();
        LastSearchRefresh = 0.0f;
    }
    inline float getAfterCaptureGraceTime() const {
        return AfterCaptureGraceTime;
    }
    inline float getLastInfTimeWitness() const { return LastInfTimeWitness; }
    inline void setLastInfTimeWitness( const float time ) {
        LastInfTimeWitness = time;
    }
    inline void setWitnessedOffence( const float value ) {
        WitnessedOffence = value;
    }
    inline float getMinTimeToStopSearchMalus() const {
        return MinTimeToStopSearchMalus;
    }
    inline void setMinTimeToStopSearch( const float time, const float malus ) {
        MinTimeToStopSearch = time;
        MinTimeToStopSearchMalus = malus;
    }
    inline bool isArrestAfterBackOnTrack() const {
        return bArrestAfterBackOnTrack;
    }
    inline void setArrestAfterBackOnTrack( const bool bArrest ) {
        bArrestAfterBackOnTrack = bArrest;
    }
    inline const AiRoadIt &getLastSearchPos() const { return LastSearchPos; }
    inline float getLastSearchRefresh() const { return LastSearchRefresh; }
    inline void setLastSearch( const AiRoadIt &position, const float time ) {
        LastSearchPos = position;
        LastSearchRefresh = time;
    }

public:
    PoliceState();
    ~PoliceState();

    void reset();
    bool isRecentCarCollision( const void *pCar ) const;
    bool checkRecentCollision( const void *pCar, const void *pObject );
    float computeOffenceLevel();
    bool isWanted();
    void updateAllTimeCounters();
    void checkInfringements( class CarState *pOwner, const float deltaTime );
    void onPoliceCollision( const uint8_t collisionType,
                            class CarState *pOther );

private:
    void updateLastValidRoadIt( AiRoadMap::Road *pRoad,
                                const float fraction,
                                const bool bWay );
    bool checkYield( class CarState *pOwner, const uint32_t junctionID );
    void reduceOffences( class CarState *pOwner );

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
