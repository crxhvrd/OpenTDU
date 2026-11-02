#pragma once

#include "ai/ai_fsm.h"
#include "ai/ai_nav_path.h"
#include "ai/ai_opt_path.h"
#include "ai/ai_veh_obstacles.h"
#include "ai/ai_veh_nav_traffic.h"
#include "game/game_object.h"

class AiVehicle;

void TeleportToRoad( AiVehicle *pVehicle,
                     const AiRoadIt &it,
                     const float widthRatio,
                     const bool bPhantom );

// Navigation type 1: race opponents and bots
class AiVehNavOpponent : public AiVehNavigation, private AiFsm {
public:
    enum eState : int32_t {
        NS_Idle = 0,
        NS_Relocate = 1,
        NS_Grid = 2,
        NS_Race = 3,
        NS_Stopped = 5,
        NS_PoliceWaitRoute = 6,
        NS_PoliceRoute = 7,
        NS_PoliceChase = 8,
        NS_Idle4 = 9,
        NS_TurnAround = 0xa,
        NS_UTurn = 0xb,
        NS_RaceOver = 0xc,
        NS_Idle2 = 0xd,
        NS_Idle3 = 0xe,
        NS_LineEnded = 0xf,
        NS_BotCruise = 0x10,
        NS_BotFollow = 0x11,
        NS_TrainingStart = 0x12,
        NS_TrainingFollow = 0x13,
        NS_PostFinish = 0x14,
        NS_Halt = 0x15,
    };

    // Where the car is relative to its route (nav+0x4d0)
    struct Situation : public OptPathSituation {
        AiRoadIt RouteTo;
        AiRoadIt NextFrom;
        AiRoadIt NextTo;
        AiRoadIt LastRouteTo;
        AiRoadIt LastNextTo;
        float U = -1000.0f;
        float CarLengthU = -1000.0f;
        float V = -1000.0f;
        float CarWidthRatio = -1000.0f;
        float LastV = -1000.0f;
        float RouteFromU = -1000.0f;
        float RouteToU = -1000.0f;
        float NextFromU = -1000.0f;
        float NextToU = -1000.0f;
        float TolRoute = -1000.0f;
        float TolNext = -1000.0f;
        Eigen::Vector4f Velocity = Eigen::Vector4f::Zero();
        ObjectWorldLocation Loc;
        Matrix4x4f_t GroundMatrix = Matrix4x4f_t::Identity();
        const AiRoadMap::Junction *pJunction = nullptr;

        void init();
        void setup( AiVehicle *pNewVehicle,
                    OptPath *pNewLine,
                    NavPathOpponent *pNewPath );
        void update();
        void updateLocation();
        void updateVelocity();
        void updateRoute();
        void updateLineIndex();
        void copyGroundMatrix();
    };

public:
    inline int32_t getState() const { return getCurrentState(); }
    inline const NavPathOpponent &getPath() const { return path; }
    inline bool isGridRevHold() const { return bGridRevHold; }
    inline float getSpeedCap() const { return speedCap; }
    inline float getObstacleSpeedCap() const { return speedCap2; }
    inline bool isObstacleAhead() const { return bObstacleAhead; }
    inline const OptPath &getLine() const { return lineA; }
    inline const Situation &getSituation() const { return situation; }
    inline const Eigen::Vector4f &getTargetDirection() const {
        return targetDirection;
    }
    inline void requestState( const int32_t state ) { goTo( state ); }
    inline void setGridThrottle( const float low, const float high ) {
        gridThrottleLow = low;
        gridThrottleHigh = high;
    }

public:
    AiVehNavOpponent();

    void attach( AiVehicle *pOwner ) override;
    void reset( const bool bFull ) override;
    void tick( const float deltaTime, const float totalTime ) override;
    void detach() override;
    void debugText() override;
    void debugDraw( DrawList *pDrawList ) override;
    bool getTarget( Eigen::Vector4f &outDirection,
                    float &outSpeed ) const override;
    void getHeightAndTwist( float &outHeight, float &outTwist ) const override;

private:
    AiVehicle *pOwner;
    AiRoadIt prevEntry;
    AiRoadIt prevExit;
    AiRoadIt curEntry;
    AiRoadIt curExit;
    AiRoadIt curTarget;
    AiRoadIt nextEntry;
    AiRoadIt nextExit;
    AiRoadIt checkpointIt;
    const AiRoadMap::Junction *pCurEntryJunction;
    const AiRoadMap::Junction *pCurExitJunction;
    uint32_t curRouteRoadID;
    int32_t lastBuiltPointId;
    float now;
    float stuckRefTime;
    float graceTime;
    Eigen::Vector4f stuckRefPos;
    float stateTime;
    int32_t checkpointMissTicks;
    float speedCap;
    float speedCap2;
    NavPathOpponent path;
    OptPath lineA;
    OptPath lineB;
    Eigen::Vector4f targetDirection;
    float targetSpeed;
    float brakeDistFromMax;
    Situation situation;
    bool bGridRevHold;
    float gridThrottleLow;
    float gridThrottleHigh;
    float lastLineRebuildTime;
    bool bObstacleAhead;
    float stopTime;
    float finishSpeed;
    bool bTurningAround;
    int32_t field_0x764;
    Eigen::Vector4f turnBackDirection;
    Eigen::Vector4f turnStartDirection;
    float turnAngleLimit;
    bool bTurnLeft;
    int32_t turnPhase;
    int32_t turnReturnState;
    AiVehObstacles obstacles;
    OptPathBuilder builder;

private:
    bool handle( const int32_t event, const int32_t state ) override;

    void resetStuckRefs();
    bool isMyRoundRobinTurn() const;
    void updateRouteIts();
    void advanceRoute();
    void appendStraightEnd();
    void rebuildPath();
    void finaliseLine();
    void resetLine();
    void resetPath();
    void enterRelocate();
    void updateRelocate();
    void updateGrid();
    bool seedPathFromRoute();
    void exitLineEnded();
    void enterPostFinish();
    float computeRubberBand() const;
    void updatePostFinish();
    void updateRace();
    void advanceRouteOnProgress();
    bool isRouteEnded() const;
    void updateTrainingFollow();
    void updateLine( const bool bNoOptim );
    void updateLineFinished();
    void updateBotCruise();
    void updateBotFollow();
    void advanceRouteOnLocation();
    void exitPoliceWaitRoute();
    bool isWrongWayOnRoute() const;
    void updatePoliceRoute();
    void updatePoliceWrongWay();
    void updatePoliceChase();
    void chaseOnTargetRoad();
    void chaseOffRoad();
    void chaseTargetOffRoad();
    void startTurnAround( const bool bLeft );
    void updateTurnAround();
    void enterUTurn();
    void onLineMismatch();
    void avoidObstacles();
    bool checkStuck();
    void findRespawnLocation( AiRoadIt &outIt, float &outLateral );
    bool checkCheckpointMissed();
    void resetOnCheckpointRoad();
};
