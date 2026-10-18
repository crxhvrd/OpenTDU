#pragma once

#include "ai/ai_road_iterator.h"
#include "ai/ai_veh_behaviour.h"
#include "ai/ai_veh_controller.h"
#include "ai/ai_veh_locomotion.h"
#include "ai/ai_veh_nav_traffic.h"
#include "core/random/bcpl.h"
#include "game/vehicle/vehicle_base.h"

#include "ai/ai_pool_traffic.h"

class CarState;
class GameObject;
struct ObjectWorldLocation;

enum class eAiVehicleCategory : int32_t {
    AVC_Traffic = 0,
    AVC_Cop = 1,
    AVC_CopDam = 2,
    AVC_Opponent = 3,
    AVC_Parked = 4,
    AVC_Cinematic = 5,
    AVC_Bot = 6,
    AVC_Network = 7,
    AVC_Autopilot = 8,
    AVC_None = 9
};

struct AiVehicleInfo {
    Matrix4x4f_t GroundMatrix;
    bool bValid;
    float Speed;
    float SmoothedSpeed;
    float BrakingDistC1;
    float BrakingDistC2;
    float TurningSpeedC1;
    float TurningSpeedC2;
    float HandlingMark;
    float MaxSpeed;
    float LastTimeSeenFront;
    float LastTimeSeenRear;
    float Visibility;
    bool bVisible;
    bool bHandbrake;
    float BlockedTime;
    AiRoadIt LastRoadPos;
    int32_t LastRoadLane;
    Eigen::Vector4f NetPos;
    bool bCrashed;

    AiVehicleInfo();

    void reset();
};

struct AiVehSignals {
    float HornCommandsQueue[8];
    int32_t HornCommandIndex;
    bool bHornActive;
    float HornLastTime;
    float HeadlightCommandQueue[8];
    int32_t HeadlightCommandIndex;
    bool bHeadlightsActivated;
    float HeadlightLastTime;
    float HeadlightBlinkingCycle;
    float HeadlightBlinkingDelay;
    bool bHeadlightActivated;
    bool bHeadlightBlinking;
    bool bWarningLeft;
    bool bWarningRight;
    bool bWarningActive;
    float WarningTimeNextBlink;
    bool bHornOn;
    bool bHeadlightOn;
    bool bWarningLeftOn;
    bool bWarningRightOn;
    bool bFlashlightOn;
    bool bIsCops;
    CarState *pCarState;

    AiVehSignals();

    void reset();
    void tick( const float deltaTime, const float totalTime );
    void apply();
};

class AiVehicle : public CarStateEventListener {
public:
    static constexpr int32_t kRemovalReasonLength = 0x80;
    static constexpr int32_t kLocomotionNone = 3;
    static constexpr int32_t kNavigationNone = 4;
    static constexpr int32_t kControllerNone = 9;
    static constexpr int32_t kBehaviourNone = 5;

public:
    inline CarState *getCarState() const { return pCarState; }
    inline int32_t getCarStateIndex() const { return carStateIndex; }
    inline void setCarStateIndex( const int32_t index ) {
        carStateIndex = index;
    }
    inline AiPoolTraffic::Template *getTemplate() const { return pTemplate; }
    inline void setTemplate( AiPoolTraffic::Template *pNewTemplate ) {
        pTemplate = pNewTemplate;
    }
    inline eAiVehicleCategory getCategory() const { return category; }
    inline int32_t getBehaviourType() const { return behaviourType; }
    inline int32_t getControllerType() const { return controllerType; }
    inline int32_t getNavigationType() const { return navType; }
    inline int32_t getLocomotionType() const { return locoType; }
    inline AiVehLocomotion *getLocomotion() const { return pLoco; }
    inline AiVehController *getController() const { return pController; }
    inline AiVehNavigation *getNavigation() const { return pNav; }
    inline AiVehBehaviour *getBehaviour() const { return pBehaviour; }
    inline bool isSoftCollision() const { return bSoftCollision; }
    inline AiVehicleInfo &getInfos() { return infos; }
    inline const AiVehicleInfo &getInfos() const { return infos; }
    inline AiVehSignals &getSignals() { return signals; }
    inline bool isRemovable() const { return bRemovable; }
    inline void setRemovable( const bool bValue ) { bRemovable = bValue; }
    inline bool isRemovalPending() const { return bPendingRemoval; }
    inline float getRemovalTime() const { return removalTime; }
    inline const char *getRemovalReason() const { return removalReason; }
    inline RandomGen &getRandom() { return random; }
    inline void setRandomSeed( const uint32_t seed ) { random.setSeed( seed ); }
    inline void setCarState( CarState *pNewCarState ) {
        pCarState = pNewCarState;
    }

    bool isPhysicsDriven() const;
    void onNavigationLost();
    float computeTurningSpeed( const float radius ) const;
    float computeBrakingDistance( const float speedFrom,
                                  const float speedTo,
                                  const float slope ) const;
    int32_t computeCurrentLane() const;
    float findClosestObstacle( GameObject **ppOutObstacle ) const;
    float computeSphereContactTime( GameObject *pObject ) const;
    float computeRoadContactTime( GameObject *pObject ) const;
    GameObject *findObjectAheadInLane( const AiRoadIt &it,
                                       const int32_t lane,
                                       float *pOutGap,
                                       float *pOutSpeed ) const;
    bool isJunctionOccupied( const uint32_t junctionID ) const;
    bool isCopInPursuit() const;
    void initializeParked( CarState *pCar );
    inline AiVehLocoParked &getParkedLocomotion() { return locoParked; }
    bool isLaneFree( const int32_t lane,
                     const AiRoadIt &position,
                     const float timeHorizon,
                     const float distanceMin,
                     const float distanceMax ) const;

public:
    AiVehicle();

    void initialize( const eAiVehicleCategory newCategory,
                     CarState *pCar,
                     const AiRoadIt &location,
                     const int32_t lane,
                     const int32_t newBehaviourType,
                     const int32_t newControllerType,
                     const int32_t newNavType,
                     const int32_t newLocoType,
                     const bool bKeepPhysics );
    void release();
    void tick( const float deltaTime, const float totalTime );
    void requestRemoval( const float delay, const char *pReason );
    void detachCarState();

    void onEvent( CarState *pCar, float deltaTime, void *pData ) override;

    Eigen::Vector4f computeCenterPosition() const;
    void setGroundMatrixFromCenter( const Matrix4x4f_t &center );

private:
    float brakingDistanceCoef;
    float turningSpeedCoef;
    AiVehicleInfo infos;
    AiVehSignals signals;
    int32_t carStateIndex;
    CarState *pCarState;
    AiPoolTraffic::Template *pTemplate;
    eAiVehicleCategory category;

    AiVehLocoTraffic locoTraffic;
    AiVehLocoParked locoParked;
    AiVehLocomotion *pLoco;
    int32_t locoType;
    AiVehNavTraffic navTraffic;
    AiVehNavigation *pNav;
    int32_t navType;
    AiVehCtrlRoute ctrlRoute;
    AiVehController *pController;
    int32_t controllerType;
    AiVehBhvTraffic bhvTraffic;
    AiVehBehaviour *pBehaviour;
    int32_t behaviourType;

    bool bRemovable;
    bool bPendingRemoval;
    float removalTime;
    float fadeInEndTime;
    bool bFadeByDistance;
    bool bFadeActive;
    bool bPlayerCar;
    bool bSoftCollision;
    bool bFlashFade;
    bool bFlashStarted;
    float flashStartTime;
    float netSendTime;
    float netReceiveTime;
    RandomGen random;
    char removalReason[kRemovalReasonLength];

private:
    void setLocomotion( const int32_t type,
                        const bool bReset,
                        const bool bKeepPhysics );
    void setNavigation( const int32_t type );
    void setController( const int32_t type );
    void setBehaviour( const int32_t type );
    void updateFade( const float totalTime );
    void updateFlashFade();
};

bool IsLocallyOwned( CarState *pCarState );
float ComputeBrakingDistance( const float coefLinear,
                              const float coefQuadratic,
                              const float speedFrom,
                              const float speedTo,
                              const float slope );
GameObject *FindObjectAheadOnRoad( const ObjectWorldLocation &location,
                                   const uint32_t selfID,
                                   float *pOutGap,
                                   float *pOutSpeed );
bool FindObjectApproachingJunction( const uint32_t junctionID,
                                    const uint32_t roadID,
                                    GameObject **ppOutObject,
                                    float *pOutDistance );
int32_t CollectRoadObjects( const uint32_t roadID,
                            GameObject **ppOutObjects,
                            const int32_t maxObjects,
                            const float fractionMin,
                            const float fractionMax,
                            const int32_t lane,
                            const bool bWay );
GameObject *FindObjectInLaneNear( const uint32_t roadID,
                                  const float fraction,
                                  const int32_t lane,
                                  const bool bWay,
                                  bool bAhead,
                                  const uint32_t selfID );
