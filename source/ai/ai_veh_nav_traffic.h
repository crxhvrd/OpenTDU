#pragma once

#include "ai/ai_fsm.h"
#include "ai/ai_nav_path.h"
#include "ai/ai_road_iterator.h"

class AiVehicle;
class DrawList;
class GameObject;
struct AiVehSignals;

void QueueHorn( AiVehSignals &signals, const float random );

class AiVehNavigation {
public:
    virtual ~AiVehNavigation() = default;

    virtual void attach( AiVehicle *pOwner ) = 0;
    virtual void reset( const bool bFull ) = 0;
    virtual void tick( const float deltaTime, const float totalTime ) = 0;
    virtual void detach() = 0;
    virtual void debugDraw( DrawList *pDrawList ) {}
    virtual bool getTarget( Eigen::Vector4f &outDirection,
                            float &outSpeed ) const = 0;
    virtual void getHeightAndTwist( float &outHeight,
                                    float &outTwist ) const = 0;
};

class AiVehNavTraffic : public AiVehNavigation, private AiFsm {
public:
    enum eState : int32_t {
        NS_Lost = 0,
        NS_OnRoad = 1,
        NS_UrgentBrake = 2,
        NS_JunctionApproach = 4,
        NS_JunctionWait = 5,
        NS_OnJunction = 6,
        NS_NoPlan = 8,
        NS_LaneChange = 9,
        NS_BackOvertakeStop = 10,
        NS_BackOvertakeReverse = 11,
        NS_BackOvertakeForward = 12,
        NS_BackOvertakePass = 13,
        NS_CancelBackOvertake = 14,
        NS_UrgentTurn = 15,
        NS_UrgentTurn2 = 16,
    };

    enum eObstacle : int32_t {
        NO_None = 0,
        NO_Far = 1,
        NO_Blocked = 2,
        NO_SlowPassable = 3,
    };

    enum eWaitReason : int32_t {
        WR_Continue = 0,
        WR_VehicleAhead = 1,
        WR_JunctionOccupied = 2,
        WR_DestinationOccupied = 3,
        WR_Priority = 4,
        WR_RedLight = 5,
        WR_Courtesy = 6,
    };

    struct StopRamp {
        int32_t State = 0;
        float InitialDistance = 0.0f;
        float RemainingDistance = 0.0f;
        float BrakingDistance = 0.0f;
    };

public:
    inline int32_t getState() const { return getCurrentState(); }
    inline const NavPath &getPath() const { return path; }

public:
    AiVehNavTraffic();

    void attach( AiVehicle *pOwner ) override;
    void reset( const bool bFull ) override;
    void tick( const float deltaTime, const float totalTime ) override;
    void detach() override;
    void debugDraw( DrawList *pDrawList ) override;
    bool getTarget( Eigen::Vector4f &outDirection,
                    float &outSpeed ) const override;
    void getHeightAndTwist( float &outHeight, float &outTwist ) const override;

private:
    AiVehicle *pVehicle;
    AiRoadIt segFrom;
    AiRoadIt segTo;
    int32_t curLane;
    int32_t turnLane;
    AiRoadIt nextFrom;
    AiRoadIt nextTo;
    int32_t nextLane;
    AiRoadMap::Junction *pJunction;
    int32_t junctionSlotIn;
    int32_t junctionSlotOut;
    bool bMustStopAtJunction;
    AiRoadIt pathEnd;
    int32_t pathEndLane;
    float obstacleContactTime;
    int32_t waitReason;
    bool bLaneChangeLock;
    int32_t targetLane;
    int32_t junctionTurnType;
    float distanceToJunction;
    float followSpeed;
    float speedCoef;
    float stoppedSince;
    NavPath path;
    Eigen::Vector4f targetDirection;
    float targetSpeed;
    StopRamp stop;
    AiVehicle *pContextVehicle;
    bool bBackOvertaking;
    Eigen::Vector4f backOvertakeStart;
    float backOvertakeTime;
    float backOvertakeDistance;
    float backOvertakePassDistance;
    bool bPullOver;
    float pullOverDecisionTime;
    bool bZeroDeltaTime;
    float speedDistribCoef;
    float secDistCoef;
    float lateralOffsetCoefs[2];

private:
    bool handle( const int32_t event, const int32_t state ) override;

    void updateRoute();
    bool updateAll( const uint8_t skipFlags, const int32_t lane );
    bool updateFollowing( const int32_t lane );
    bool checkObstacleTransition();

    float computeDistanceToJunction() const;
    float computeStopDistance( const float distance ) const;
    float computeLaneWidthRatio( const AiRoadIt &it,
                                 const int32_t lane,
                                 const float coefOverride ) const;
    float computeSpeedLimit( const AiRoadIt &it ) const;
    int32_t computeTurnLane( const AiRoadMap::Junction *pTurnJunction,
                             const AiRoadMap::Road *pRoadIn,
                             const AiRoadMap::Road *pRoadOut,
                             const int32_t lane,
                             bool &bOutSplit );
    int32_t computeNextLane( const AiRoadIt &to,
                             const AiRoadIt &next,
                             const int32_t lane ) const;
    bool isStopRequired( const AiRoadIt &to,
                         const AiRoadIt &next,
                         const int32_t turnType ) const;

    int32_t checkObstacle( AiVehicle *pContext,
                           float &outDistance,
                           const float secDistance );
    GameObject *findLeader( float *pOutGap, float *pOutLeaderSpeed ) const;
    void computeFollowing( float &outFollowSpeed,
                           float &outSpeedCoef,
                           const bool bDebug );
    void updatePullOver();
    bool isBackOvertakeWanted();
    bool isLaneChangeWanted();
    bool abortBlockedLaneChange();
    bool checkLaneChangeGap( const int32_t laneFrom,
                             const int32_t laneTo,
                             const bool bWay,
                             float distance ) const;
    bool isOnLane( const int32_t lane ) const;
    bool mustWaitAtJunction( int32_t &outReason, const bool bUpdate );
    bool isBackOvertakePossible();
    float computeUrgentTurnSteer( const bool bDebug );
    void moveToTurnLane( const AiRoadIt &position, const int32_t lane );
    bool isLaneFree( const int32_t lane,
                     const AiRoadIt &position,
                     const float distance,
                     const float rangeMin,
                     const float rangeMax ) const;
    int32_t computeCurrentLane() const;
};

void ApplyStopRamp( AiVehNavTraffic::StopRamp &stop,
                    bool &bHandbrake,
                    float &speed,
                    const bool bZeroDeltaTime );
