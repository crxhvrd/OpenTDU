#pragma once

#include <cfloat>

#include "game/mng_base.h"
#include "core/mutex.h"
#include "ai/ai_path.h"

class DrawList;
struct ObjectWorldLocation;

// HUD GPS: route to the current objective (minimap line, distance, turn
// arrows and voice).
class MngGPS : public Manager {
public:
    static constexpr int32_t kArrowNone = 8;

public:
    const char *getName() const override {
        // FUN_00406490
        return "GPS";
    }

public:
    inline const AiPath &getRoute() const { return route; }
    inline bool isVoiceEnabled() const { return bVoiceEnabled; }
    inline void setVoiceEnabled( const bool bEnabled ) {
        bVoiceEnabled = bEnabled;
    }
    inline bool isDistanceCalloutsEnabled() const {
        return bDistanceCalloutsEnabled;
    }
    inline void setDistanceCalloutsEnabled( const bool bEnabled ) {
        bDistanceCalloutsEnabled = bEnabled;
    }

public:
    MngGPS();
    ~MngGPS();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float deltaTime, float totalTime ) override;
    void reset() override;
    void draw() override;
    void pause() override;
    void terminate() override;

    void drawRoute( Matrix4x4f_t &worldToMap, DrawList &drawList );
    void announceArrival();
    void onPlayerLeft( const int32_t numPlayers, const int32_t playerIndex );
    void onPlayerJoined( const int32_t numPlayers, const int32_t playerIndex );
    bool computeRouteDirectionOnRoad( const uint32_t roadID,
                                      const Eigen::Vector4f &position,
                                      Eigen::Vector4f &outDirection );

private:
    static constexpr int32_t kMaxNumRoutePoints = 400;

    struct RoutePoint {
        float X;
        float Z;
        uint32_t RoadID;
        float T;
    };

private:
    void recomputeRoute( const AiRoadIt &start,
                         const int32_t startSide,
                         const bool bIgnoreOneWay,
                         const bool bSecondLeg );
    void buildRoutePolyline();
    float computeRouteDistance( const float carFraction,
                                const bool bAddSecondLeg ) const;
    int32_t computeArrow( const ObjectWorldLocation &carLocation,
                          const float carFraction );
    void announceTurn( const float turnAngle, const float distance );
    void announceDistance( const float distance );
    void projectOnNearestRoad( const Eigen::Vector4f &position,
                               AiRoadIt &outIt );

private:
    TestDriveMutex mutex;

    bool bRouteIgnoreOneWay;
    bool bHadObjective;
    Eigen::Vector4f lastObjectivePosition;
    Eigen::Vector4f lastSecondaryObjectivePosition;
    float carRoadFraction;
    Eigen::Vector4f carDisplayPosition;
    // Road-snapped car position (w = -1 when invalid)
    Eigen::Vector4f carRoadPosition;
    bool bCarOnRoad;
    float wrongWayTimer;
    bool bWrongWayAnnounced;
    AiRoadIt objectiveIt;
    AiRoadIt secondaryIt;
    bool bBigArrowDisabled;
    bool bVoiceEnabled;
    bool bDistanceCalloutsEnabled;
    int32_t nextJunctionArrow;
    float distanceToNextJunction;
    AiPath route;
    AiPath secondaryRoute;
    float updateAccumulator;
    RoutePoint routePoints[kMaxNumRoutePoints];
    uint32_t numRoutePoints;
    bool bRouteExhausted;
    float lastCalloutDistance;
    float lastTurnCalloutDistance;
    float wrongWayVoiceTimer;
    bool bObjectiveInRange;
    int32_t cachedSessionPlayerCount;
};

extern MngGPS *gpMngGPS;
