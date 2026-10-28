#pragma once

#if OTDU_DEVBUILD
#include "ai/ai_road_map.h"
#include "game/mng_base.h"

#include <chrono>
#include <random>
#include <unordered_map>
#include <unordered_set>

class CarState;

// Gameplay automation to gather PSO permutations
class MngPSOCollector : public Manager {
public:
    const char *getName() const override { return "PSOCollector"; }

public:
    MngPSOCollector();
    ~MngPSOCollector();

    void tick( float deltaTime, float totalTime ) override;
    void reset() override;

private:
    enum class eState {
        CS_Start,
        CS_Teleport,
        CS_Settle,
        CS_Drive,
        CS_Look,
        CS_Done
    };

    struct RoadInfos {
        float Length = 0.0f;
        std::vector<uint64_t> Sectors;
    };

    // First drivable road point found in a sector (teleport destination)
    struct SectorSample {
        uint32_t RoadID;
        int32_t EntityIndex;
        int32_t CurveIndex;
        Eigen::Vector4f Position;
    };

private:
    void setState( const eState newState );
    bool isStreamingSettled() const;
    void logStopTimings( const std::chrono::steady_clock::time_point &now );
    void setAutopilotEnabled( CarState *pCar, const bool bEnabled );
    bool isOnUnvisitedRoad( CarState *pCar ) const;
    bool teleportToNextRoad( const Eigen::Vector4f &carPosition );
    void swapCar( const Matrix4x4f_t &pose );
    void randomizePaint( const int32_t carID, const int32_t rimID );
    int32_t randomInt( const int32_t min, const int32_t max );
    void loadVisitedSectors();
    void saveVisitedSectors();
    void buildRoadInfos();
    void steerAutopilot( CarState *pCar );
    uint32_t pickNextRoad( AiRoadMap::Junction *pJunction,
                           const AiRoadMap::Road *pCurrentRoad );
    bool planPathToUnvisitedRoad( AiRoadMap::Junction *pStart );
    uint32_t getNumUnvisitedSectors( const AiRoadMap::Road *pRoad ) const;
    void cycleCamera( CarState *pCar );
    void logProgress();

private:
    float elapsedTime;
    float nextLogTime;
    float nextCameraCycleTime;
    float nextSaveTime;
    float distanceTravelled;
    bool bAutopilotEnabled;
    Eigen::Vector4f lastCarPosition;
    size_t numPSOsAtStart;
    size_t numPSOsAtLastLog;
    float distanceAtLastLog;

    std::unordered_map<uint32_t, RoadInfos> roadInfos;
    std::unordered_map<uint64_t, SectorSample> roadSectors;
    std::unordered_set<uint64_t> visitedSectors;
    float totalRoadLength;

    uint32_t lastSteeredRoadID;
    std::vector<uint32_t> plannedPath; // Road IDs (front = next road)

    eState state;
    float stateTime;
    float settleIdleTime;
    float driveStartDistance;
    Eigen::Vector4f driveStartPosition;
    uint32_t numStops;
    uint32_t numViewsLeft;

    std::vector<int32_t> carIDs; // Database order
    size_t nextCarIndex;
    std::mt19937 random;
    std::string lastPaintDescription;

    std::chrono::steady_clock::time_point lastTickTime;
    std::chrono::steady_clock::time_point stopStartTime;
    float settleDuration;
    Matrix4x4f_t teleportPose;
    float maxFrameTime;
};

extern bool gbPSOCollectorEnabled;
#endif
