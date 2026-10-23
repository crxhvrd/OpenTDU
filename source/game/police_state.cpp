#include "shared.h"
#include "police_state.h"

#include "ai/ai_config.h"
#include "gs_timer.h"

PoliceState::PoliceState() {
    // FUN_008ae8b0
    for ( CollidedCar &car : CarCollisions ) {
        car = CollidedCar();
    }
    reset();

    LastValidRoadIT = AiRoadIt();
    LastTimeForValidRoadIT = 0.0f;
    CurControlDevice = 0;
    bCurControlDeviceRespected = false;
    LastTimeForDeviceRespected = 0.0f;
    LastGameCounterTime = 0.0f;
    SignalLastState = 4;
}

PoliceState::~PoliceState() {}

void PoliceState::reset() {
    // FUN_008ae790
    WitnessedOffence = 0.0f;
    Collision = 0.0f;
    NumCollisionCar = 0;
    NumCollisionSettings = 0;
    OnComingLane = 0.0f;
    RedLight = 0.0f;
    SpeedLimit = 0.0f;
    Stop = 0.0f;
    Yield = 0.0f;
    bIsChased = false;
    NumChasers = 0;
    NumWantedChaser = 0;
    bIsSearched = false;
    LastSearchRefresh = 0.0f;
    bArrested = false;
    bArrestAfterBackOnTrack = false;
    bPlayingArrestCinematic = false;
    AfterCaptureGraceTime = 0.0f;
    JunctionNotRespected = -1;
    WasHitLastTime = 0.0f;
    HitLastTime = 0.0f;
    ReduceInfBaseTime = 0.0f;
    LastInfTimeWitness = 0.0f;
    MinTimeToStopSearch = 0.0f;
    MinTimeToStopSearchMalus = 0.0f;
    NumPoliceDamForced = 0;
    LastPoliceDamEventTime = 0.0f;
    for ( CollidedCar &car : CarCollisions ) {
        car = CollidedCar();
    }
    LastSearchPos = AiRoadIt();
}

bool PoliceState::isRecentCarCollision( const void *pCar ) const {
    // FUN_008aff60 (inlined)
    for ( const CollidedCar &car : CarCollisions ) {
        if ( car.pCarPointer == pCar &&
             gpTimer->GameTotalTime <=
                 gAIConfig.InfGracePeriod + car.CollisionTime ) {
            return true;
        }
    }
    return false;
}

bool PoliceState::checkRecentCollision( const void *pCar,
                                        const void *pObject ) {
    // FUN_008aef10
    const void *pKey = ( pCar != nullptr ) ? pCar : pObject;
    for ( CollidedCar &car : CarCollisions ) {
        if ( pKey == nullptr || car.pCarPointer != pKey ) {
            continue;
        }
        if ( gpTimer->GameTotalTime - car.CollisionTime <=
             gAIConfig.CollisionRepeatDelay ) {
            return true;
        }
        car.CollisionTime = gpTimer->GameTotalTime;
        return false;
    }

    for ( CollidedCar &car : CarCollisions ) {
        if ( car.pCarPointer != nullptr ) {
            continue;
        }
        if ( pKey != nullptr ) {
            car.pCarPointer = pKey;
        }
        car.CollisionTime = gpTimer->GameTotalTime;
        return false;
    }
    return false;
}
