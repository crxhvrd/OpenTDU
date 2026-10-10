#pragma once

#include "game/mng_base.h"
#include "ai/ai_path.h"
#include "render/draw_list.h"
#include "world/gs_world_streaming.h"

struct AiRoadLocation;
class MngSpots;

// Route ribbon drawn on the map (one solved path per leg between two
// waypoints).
class MngTracRoad : public Manager, public StreamingCallback {
public:
    static constexpr int32_t kMaxNumLegs = 101;

public:
    const char *getName() const override {
        // FUN_0042c890
        return "TracRoad";
    }

public:
    MngTracRoad();
    ~MngTracRoad();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float deltaTime, float totalTime ) override;
    void reset() override;
    void draw() override;
    void pause() override;
    void terminate() override;

    bool onWorldStreamed( Eigen::Vector4f *param_1,
                          Eigen::Vector4f *param_2,
                          Eigen::Vector4f *pShiftPosition ) override;

    virtual AiRoadLocation *getWaypoint( const int32_t index ) = 0;
    virtual void setNumWaypoints( const int32_t numWaypoints ) = 0;
    virtual bool isActive() const = 0;
    virtual int32_t getNumWaypoints() const = 0;
    virtual bool isLoop() const = 0;
    virtual int32_t getLoopWaypointCount() const = 0;

    void solveLegs( const bool bIgnoreOneWay );

private:
    static constexpr int32_t kMaxNumPoints = 400;

private:
    void solveLeg( const int32_t legIndex, const bool bIgnoreOneWay );
    void computeViewBox();
    void drawLeg( const AiPath &path );
    void drawRoadSpan( const AiRoadIt &start, const AiRoadIt &end );
    void drawJoin( const AiRoadIt &from, const AiRoadIt &to );
    void addSpanPoint( const AiRoadIt &it, const AiRoadIt &end );
    void buildRibbon( const int32_t numRibbonPoints,
                      const float ribbonWidth,
                      const Eigen::Vector4f *pStartDirection,
                      const Eigen::Vector4f *pEndDirection );

private:
    DrawList drawList;
    int32_t numPoints;
    Eigen::Vector4f *pPoints;
    int32_t numPrimitivesUsed;
    int32_t numVerticesUsed;
    AiPath paths[kMaxNumLegs];
    float legLengths[kMaxNumLegs];
    bool field_0x93f4;
    // 1: one point per road entity, 2/3: one point per curve
    int32_t detailMode;
    float width;
    float height;
    Eigen::Vector4f viewBoxMin;
    Eigen::Vector4f viewBoxMax;
    uint32_t alpha;
};

class MngTracRoadMap : public MngTracRoad {
public:
    inline void setOwner( MngSpots *pSpots ) { pOwner = pSpots; }

public:
    MngTracRoadMap();
    ~MngTracRoadMap();

    AiRoadLocation *getWaypoint( const int32_t index ) override;
    void setNumWaypoints( const int32_t numWaypoints ) override;
    bool isActive() const override;
    int32_t getNumWaypoints() const override;
    bool isLoop() const override;
    int32_t getLoopWaypointCount() const override;

private:
    int32_t numWaypoints;
    MngSpots *pOwner;
    bool bLobby;
};

extern MngTracRoad *gpMngTracRoad;
