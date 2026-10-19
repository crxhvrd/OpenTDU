#pragma once

class AiVehicle;
class DrawList;
struct ColorRGBA;

// GSAI+0x1384
struct AiDebugDraw {
    DrawList *pDrawList;           // "AI_DBG"
    DrawList *pDrawList2D;         // "AI_DBG_2D"
    DrawList *pDrawListBackground; // "AI_DBG_BK"
    bool bCreated;

    AiDebugDraw();

    void createBackground();
    bool create();
    void release();
    void releaseBackground();
};

void SetDebuggedAiVehicle( AiVehicle *pVehicle );
void TickAiDebugOverlay( float deltaTime, float time );
void SubmitAiDebugDrawLists();

// AiVehicle vtable slot 1 (FUN_00925940)
void DrawAiVehicleDebug( AiVehicle *pVehicle, DrawList *pDrawList );

ColorRGBA ToDiffuse( const uint32_t color );
Eigen::Vector4f GetShiftWorldPosition();
void DrawDebugLine( const Eigen::Vector4f &start,
                    const Eigen::Vector4f &end,
                    const uint32_t color,
                    DrawList &drawList );
void DrawDebugBox( const Matrix4x4f_t &matrix,
                   const uint32_t color,
                   DrawList &drawList,
                   const int32_t mode );
void DrawDebugSphereMarker( const Eigen::Vector4f &position,
                            const float radius,
                            const uint32_t color,
                            DrawList &drawList );

// FUN_00972cc0
void DrawAiDebugCarBox( AiDebugDraw &debugDraw,
                        const Matrix4x4f_t &modelMatrix,
                        const uint32_t color );

// FUN_009bace0
void DrawDebugText3D( const char *pText,
                      const Eigen::Vector4f &position,
                      const ColorRGBA &color );

// FUN_00972fe0
void SelectNextHandmadeJunction();

extern bool gDistributionZoneDebugDraw; // DAT_010e7384
extern bool gDisplayRoadMap;            // DAT_010e7385
extern int32_t gProjectOnRoad;          // DAT_010e7388
extern bool gDisplayJunctionDetail;     // DAT_010e738c
extern bool gDisplayLastSearch;         // DAT_010e738e
extern bool gDisplayCullingShapes;      // DAT_010e738f
extern bool gTogglePIDSteerGraph;       // DAT_010e739a
extern bool gTogglePIDSpeedGraph;       // DAT_010e739b
extern bool gToggleSpeedGraph;          // DAT_010e739c
extern bool gDisplayGameObjects;        // DAT_010e739d
extern bool gDisplayRoadBuckets;        // DAT_010e739e
extern bool gDisplaySimBubble;          // DAT_010e739f
extern bool gDisplaySimGroups;          // DAT_010e73a0
extern bool gDisplayNetInterp;          // DAT_010e73a1
extern bool gDisplayTrafficLights;      // DAT_010e73a2
extern int32_t gDebugAiVehicleState;    // DAT_010e73a4
extern bool gDisplayCollisionRisks;     // DAT_010e73a8
extern bool gDisplayScenaricCarPos;     // DAT_010e73a9
extern bool gDisplayRemovedVehicles;    // DAT_010e73ab
extern bool gDisplayJunctionsID;        // DAT_010e7398
extern bool gDisplayEntitiesNames;      // DAT_010e7399
extern bool gbDebugFirstOpponent;       // DAT_010e73d5
extern bool gbAiDebugPageOpen;          // DAT_010e73db
extern bool gDisplayPedestrians;        // DAT_010e73da
extern bool gToggleCameraOnTrafficCar;  // DAT_010e7370
extern bool gFlushInstanciation;        // DAT_010e73e8
extern float gBoundBoxWidth;            // DAT_00f7a10c
extern int32_t gAiDebugPage;            // DAT_00f7a118
extern int32_t gNumAiHits;              // INT_010e737c
extern float gAiHitEnergy;              // DAT_010e7380

extern AiDebugDraw gAiDebugDraw;
