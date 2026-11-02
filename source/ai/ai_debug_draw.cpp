#include "shared.h"
#include "ai_debug_draw.h"

#include "ai/ai_config.h"
#include "ai/ai_path_optimiser.h"
#include "ai/ai_pool.h"
#include "ai/ai_road_shape.h"
#include "ai/ai_sim_bubble.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "core/color.h"
#include "core/maths_helper.h"
#include "debug/debug_graph.h"
#include "debug/ed_debug_text.h"
#include "game/mng_hud.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "render/camera/camera_focus_object.h"
#include "render/camera/camera_mouse.h"
#include "render/camera/camera_vehicle.h"
#include "render/camera/gs_camera.h"
#include "render/draw_list.h"
#include "render/gs_render.h"
#include "render/render_scene.h"
#include "gs_timer.h"
#include "system/ed_sys.h"
#include "world/gs_world_streaming.h"

#include <cstdio>
#include <cstring>

bool gDistributionZoneDebugDraw = false; // DAT_010e7384
bool gDisplayRoadMap = false;            // DAT_010e7385
int32_t gProjectOnRoad = 0;              // DAT_010e7388
bool gDisplayJunctionDetail = false;     // DAT_010e738c
bool gDisplayLastSearch = false;         // DAT_010e738e
bool gDisplayCullingShapes = false;      // DAT_010e738f
bool gDisplayJunctionsID = false;        // DAT_010e7398
bool gDisplayEntitiesNames = false;      // DAT_010e7399
bool gTogglePIDSteerGraph = false;       // DAT_010e739a
bool gTogglePIDSpeedGraph = false;       // DAT_010e739b
bool gToggleSpeedGraph = false;          // DAT_010e739c
bool gDisplayGameObjects = false;        // DAT_010e739d
bool gDisplayRoadBuckets = false;        // DAT_010e739e
bool gDisplaySimBubble = false;          // DAT_010e739f
bool gDisplaySimGroups = false;          // DAT_010e73a0
bool gDisplayNetInterp = false;          // DAT_010e73a1
bool gDisplayTrafficLights = false;      // DAT_010e73a2
int32_t gDebugAiVehicleState = 0;        // DAT_010e73a4
int32_t gDisplayCarStatus = 0;           // DAT_010e7390
bool gDisplayCollisionRisks = false;     // DAT_010e73a8
bool gDisplayScenaricCarPos = false;     // DAT_010e73a9
bool gDisplayRemovedVehicles = false;    // DAT_010e73ab
bool gbDebugObstacleParser = false;      // DAT_010e73b0
bool gbDebugNavPath = false;             // DAT_010e73b1
bool gbDebugOptimizedPath = false;       // DAT_010e73b2
bool gbDisplayVehicleLocInfo = false;    // DAT_010e73b3
bool gbForceParsingWhenPaused = false;   // DAT_010e73b4
bool gbDebugFirstOpponent = false;       // DAT_010e73d5
bool gDisplayPedestrians = false;        // DAT_010e73da
bool gbAiDebugPageOpen = false;          // DAT_010e73db
bool gToggleCameraOnTrafficCar = false;  // DAT_010e7370
bool gFlushInstanciation = false;        // DAT_010e73e8
float gBoundBoxWidth = 1000.0f;          // DAT_00f7a10c
int32_t gAiDebugPage = 1;                // DAT_00f7a118
int32_t gNumAiHits = 0;                  // INT_010e737c
float gAiHitEnergy = 0.0f;               // DAT_010e7380

static AiVehicle *gpDebuggedAiVehicle = nullptr; // DAT_010e736c
static AiVehicle *gpDebuggedOpponent = nullptr;  // DAT_010e73ac
static int32_t gDebuggedAiVehicleIndex = -1;     // DAT_00f7a0dc
static int32_t gDebuggedAiVehicleIndex2 = -1;    // DAT_00f7a0e0
static uint32_t gHandmadeJunctionID = 0;         // DAT_010e7374
static int32_t gNumHandmadeJunctions = 0;        // DAT_010e7378
static int32_t gHandmadeJunctionIndex = -1;      // DAT_00f7a0e8

static DebugGraph gPIDSpeedGraph;                      // DAT_00fa9020
static DebugGraph gPIDSteerGraph;                      // DAT_00fa9048
static DebugGraph gSpeedGraph;                         // DAT_00fa9070
static const PIDCtrl *gpPIDSpeedGraphSource = nullptr; // DAT_00fa9044
static const PIDCtrl *gpPIDSteerGraphSource = nullptr; // DAT_00fa906c

static constexpr uint32_t kMaxNumDebugPrimitives = 2000;  // 0x7d0
static constexpr uint32_t kMaxNumDebugVertices = 14000;   // 0x36b0
static constexpr uint32_t kMaxNumDebugPrimitives2D = 100; // 0x64
static constexpr uint32_t kMaxNumDebugVertices2D = 0x157c;
static constexpr uint32_t kMaxNumBackgroundPrimitives = 4;
static constexpr uint32_t kMaxNumBackgroundVertices = 0x10;

static constexpr float kSphereLODScale = 150.0f;     // DAT_00f4182c
static constexpr float kJunctionRadius = 10.0f;      // FLOAT_00f4166c
static constexpr float kSimBubbleHeight = 20.0f;     // DAT_00faa9e4
static constexpr float kSimBubbleBoxWidth = 3.0f;    // DAT_00faa9e8
static constexpr float kRoadFrameHeight = 0.25f;     // DAT_00faa748
static constexpr float kRoadFrameAxisLength = 4.0f;  // DAT_00faa744
static constexpr float kCullingCircleStep = kPI / 8; // _DAT_00f41bfc

// DAT_016a7f14
static const Eigen::Vector4f kUnitBoxVertices[8] = {
    Eigen::Vector4f( -0.5f, -0.5f, -0.5f, 1.0f ),
    Eigen::Vector4f( 0.5f, -0.5f, -0.5f, 1.0f ),
    Eigen::Vector4f( -0.5f, -0.5f, 0.5f, 1.0f ),
    Eigen::Vector4f( 0.5f, -0.5f, 0.5f, 1.0f ),
    Eigen::Vector4f( -0.5f, 0.5f, -0.5f, 1.0f ),
    Eigen::Vector4f( 0.5f, 0.5f, -0.5f, 1.0f ),
    Eigen::Vector4f( -0.5f, 0.5f, 0.5f, 1.0f ),
    Eigen::Vector4f( 0.5f, 0.5f, 0.5f, 1.0f ),
};

// DAT_00fabdd8
static const int32_t kUnitBoxFaces[6][4] = {
    {0, 1, 2, 3},
    {0, 1, 4, 5},
    {2, 3, 6, 7},
    {1, 3, 5, 7},
    {0, 2, 4, 6},
    {4, 5, 6, 7},
};

// DAT_016a7dd0
static const Eigen::Vector4f kUnitSphereVertices[20] = {
    Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 1.0f ),
    Eigen::Vector4f( 0.707f, 0.707f, 0.0f, 1.0f ),
    Eigen::Vector4f( 0.353f, 0.707f, 0.612f, 1.0f ),
    Eigen::Vector4f( -0.353f, 0.707f, 0.612f, 1.0f ),
    Eigen::Vector4f( -0.707f, 0.707f, 0.0f, 1.0f ),
    Eigen::Vector4f( -0.353f, 0.707f, -0.612f, 1.0f ),
    Eigen::Vector4f( 0.353f, 0.707f, -0.612f, 1.0f ),
    Eigen::Vector4f( 1.0f, 0.0f, 0.0f, 1.0f ),
    Eigen::Vector4f( 0.5f, 0.0f, 0.866f, 1.0f ),
    Eigen::Vector4f( -0.5f, 0.0f, 0.866f, 1.0f ),
    Eigen::Vector4f( -1.0f, 0.0f, 0.0f, 1.0f ),
    Eigen::Vector4f( -0.5f, 0.0f, -0.866f, 1.0f ),
    Eigen::Vector4f( 0.5f, 0.0f, -0.866f, 1.0f ),
    Eigen::Vector4f( 0.707f, -0.707f, 0.0f, 1.0f ),
    Eigen::Vector4f( 0.353f, -0.707f, 0.612f, 1.0f ),
    Eigen::Vector4f( -0.353f, -0.707f, 0.612f, 1.0f ),
    Eigen::Vector4f( -0.707f, -0.707f, 0.0f, 1.0f ),
    Eigen::Vector4f( -0.353f, -0.707f, -0.612f, 1.0f ),
    Eigen::Vector4f( 0.353f, -0.707f, -0.612f, 1.0f ),
    Eigen::Vector4f( 0.0f, -1.0f, 0.0f, 1.0f ),
};

// DAT_00fabc58
static const int32_t kUnitSphereFaces[24][4] = {
    {0,  1,  2,  0 },
    {0,  2,  3,  0 },
    {0,  3,  4,  0 },
    {0,  4,  5,  0 },
    {0,  5,  6,  0 },
    {0,  6,  1,  0 },
    {2,  1,  8,  7 },
    {3,  2,  9,  8 },
    {4,  3,  10, 9 },
    {5,  4,  11, 10},
    {6,  5,  12, 11},
    {1,  6,  7,  12},
    {14, 13, 8,  7 },
    {15, 14, 9,  8 },
    {16, 15, 10, 9 },
    {17, 16, 11, 10},
    {18, 17, 12, 11},
    {13, 18, 7,  12},
    {19, 13, 14, 19},
    {19, 14, 15, 19},
    {19, 15, 16, 19},
    {19, 16, 17, 19},
    {19, 17, 18, 19},
    {19, 18, 13, 19},
};

AiDebugDraw gAiDebugDraw; // DAT_01450c80

// The debug helpers take R8G8B8A8 colors; draw lists store A8R8G8B8.
ColorRGBA ToDiffuse( const uint32_t color ) {
    return ColorRGBA( ( color & 0xff00ff00u ) | ( ( color >> 16 ) & 0xffu ) |
                      ( ( color & 0xffu ) << 16 ) );
}

Eigen::Vector4f GetShiftWorldPosition() {
    if ( gpWorldStreaming->isEnabled() ) {
        return gpWorldStreaming->getPosition();
    }
    return Eigen::Vector4f::Zero();
}

static int32_t ComputeDebugLOD( const Eigen::Vector4f &center,
                                const float radius,
                                const int32_t maxSegments ) {
    CameraVehicle *pCamera = gpCamera->getActiveVehicleCamera();
    if ( pCamera == nullptr ) {
        return 4;
    }

    const float distance =
        ( center.head<3>() - pCamera->getWorldPositionOffset() ).norm();
    const int32_t numSegments =
        static_cast<int32_t>( ( radius / distance ) * kSphereLODScale );
    return std::clamp( numSegments, 4, maxSegments );
}

void DrawDebugLine( const Eigen::Vector4f &start,
                    const Eigen::Vector4f &end,
                    const uint32_t color,
                    DrawList &drawList ) {
    // FUN_009b9a10
    drawList.setIdentityTransform();
    drawList.setDiffuse( 0, ToDiffuse( color ) );
    drawList.setActiveMaterial( nullptr );
    if ( drawList.beginPrimitive( PT_LineStrip, 2 ) ) {
        drawList.pushVertex( start.x(), start.y(), start.z() );
        drawList.pushVertex( end.x(), end.y(), end.z() );
        drawList.commitPrimitive();
    }
}

static void DrawDebugSphere( const Eigen::Vector4f &sphere,
                             const uint32_t color,
                             DrawList &drawList ) {
    // FUN_009ba020
    const float radius = sphere.w();
    const Eigen::Vector4f center( sphere.x(), sphere.y(), sphere.z(), 1.0f );
    const int32_t numSegments = ComputeDebugLOD( center, radius, 12 );
    const int32_t numRings = numSegments / 2;
    const float stepTheta = kTwoPI / static_cast<float>( numSegments );
    const float stepPhi = kPI / static_cast<float>( numRings );

    float theta = 0.0f;
    for ( int32_t i = 0; i <= numSegments; i++ ) {
        if ( !drawList.beginPrimitive( PT_LineStrip, numRings + 1 ) ) {
            break;
        }
        drawList.setDiffuse( 0, ToDiffuse( color ) );
        const float sinTheta = sinf( theta );
        const float cosTheta = cosf( theta );
        float phi = 0.0f;
        for ( int32_t j = 0; j <= numRings; j++ ) {
            const float sinPhi = sinf( phi );
            drawList.pushVertex( cosTheta * radius * sinPhi + sphere.x(),
                                 sinTheta * radius * sinPhi + sphere.y(),
                                 cosf( phi ) * radius + sphere.z() );
            phi += stepPhi;
        }
        drawList.commitPrimitive();
        theta += stepTheta;
    }

    float phi = 0.0f;
    for ( int32_t i = 0; i <= numRings; i++ ) {
        if ( !drawList.beginPrimitive( PT_LineStrip, numSegments + 1 ) ) {
            return;
        }
        drawList.setDiffuse( 0, ToDiffuse( color ) );
        const float cosPhi = cosf( phi );
        const float sinPhi = sinf( phi );
        float angle = 0.0f;
        for ( int32_t j = 0; j <= numSegments; j++ ) {
            drawList.pushVertex( cosf( angle ) * radius * sinPhi + sphere.x(),
                                 sinf( angle ) * radius * sinPhi + sphere.y(),
                                 radius * cosPhi + sphere.z() );
            angle += stepTheta;
        }
        drawList.commitPrimitive();
        phi += stepPhi;
    }
}

static void DrawDebugIndexedShape( const Matrix4x4f_t &matrix,
                                   const uint32_t color,
                                   const Eigen::Vector4f *pVertices,
                                   const int32_t ( *pFaces )[4],
                                   const int32_t numFaces,
                                   DrawList &drawList,
                                   const int32_t mode ) {
    // FUN_009b9ac0
    Matrix4x4f_t transform = matrix;
    drawList.setActiveTransformMatrix( transform );
    drawList.setDiffuse( 0, ToDiffuse( color ) );

    auto pushVertex = [&]( const int32_t index ) {
        const Eigen::Vector4f &vertex = pVertices[index];
        drawList.pushVertex( vertex.x(), vertex.y(), vertex.z() );
    };

    for ( int32_t i = 0; i < numFaces; i++ ) {
        const int32_t *pFace = pFaces[i];
        drawList.setActiveMaterial( nullptr );
        if ( mode != 1 ) {
            if ( !drawList.beginPrimitive( PT_LineStrip, 5 ) ) {
                break;
            }
            pushVertex( pFace[0] );
            pushVertex( pFace[1] );
            pushVertex( pFace[3] );
            pushVertex( pFace[2] );
            pushVertex( pFace[0] );
        } else {
            if ( !drawList.beginPrimitive( PT_TriangleStrip, 4 ) ) {
                break;
            }
            for ( int32_t j = 0; j < 4; j++ ) {
                pushVertex( pFace[j] );
            }
        }
        drawList.commitPrimitive();
    }
    drawList.setIdentityTransform();
}

void DrawDebugArc( const Matrix4x4f_t &matrix,
                   const float radius,
                   const bool bSpokes,
                   const uint32_t color,
                   DrawList &drawList,
                   const float angleStart,
                   const float angleEnd ) {
    // FUN_009baa70
    static constexpr int32_t kMaxSegments = 24;

    const int32_t numSegments =
        ComputeDebugLOD( matrix.row( 3 ).transpose(), radius, kMaxSegments );

    Eigen::Vector3f points[kMaxSegments + 1];
    for ( int32_t i = 0; i <= numSegments; i++ ) {
        const float angle =
            static_cast<float>( i ) * ( ( angleEnd - angleStart ) /
                                        static_cast<float>( numSegments ) ) +
            angleStart;
        points[i] = Eigen::Vector3f( cosf( angle ) * radius, 0.0f,
                                     sinf( angle ) * radius );
    }

    Matrix4x4f_t transform = matrix;
    drawList.setActiveTransformMatrix( transform );
    drawList.setActiveMaterial( nullptr );
    drawList.setDiffuse( 0, ToDiffuse( color ) );
    if ( bSpokes && drawList.beginPrimitive( PT_Line, numSegments * 2 ) ) {
        for ( int32_t i = 0; i < numSegments; i++ ) {
            drawList.pushVertex( 0.0f, 0.0f, 0.0f );
            drawList.pushVertex( points[i].x(), points[i].y(), points[i].z() );
        }
        drawList.commitPrimitive();
    }
    if ( drawList.beginPrimitive( PT_LineStrip, numSegments + 1 ) ) {
        for ( int32_t i = 0; i <= numSegments; i++ ) {
            drawList.pushVertex( points[i].x(), points[i].y(), points[i].z() );
        }
        drawList.commitPrimitive();
    }
}

void DrawDebugBox( const Matrix4x4f_t &matrix,
                   const uint32_t color,
                   DrawList &drawList,
                   const int32_t mode ) {
    // FUN_009ba5c0
    DrawDebugIndexedShape( matrix, color, kUnitBoxVertices, kUnitBoxFaces, 6,
                           drawList, mode );
}

void DrawDebugSphereMarker( const Eigen::Vector4f &position,
                            const float radius,
                            const uint32_t color,
                            DrawList &drawList ) {
    // FUN_009baf10
    Eigen::Vector3f scale( radius, radius, radius );
    Matrix4x4f_t matrix = MakeScaleMatrix( scale );
    matrix.row( 3 ) = position.transpose();

    // FUN_009ba350
    DrawDebugIndexedShape( matrix, color, kUnitSphereVertices, kUnitSphereFaces,
                           24, drawList, 2 );
}

static void DrawDebugCylinder( const Eigen::Vector4f &center,
                               const float radius,
                               const float height,
                               const bool bSpokes,
                               const uint32_t color,
                               DrawList &drawList ) {
    // FUN_009ba700
    static constexpr int32_t kMaxSegments = 24;

    const int32_t numSegments = ComputeDebugLOD( center, radius, kMaxSegments );

    Eigen::Vector3f ring[kMaxSegments + 1];
    for ( int32_t i = 0; i <= numSegments; i++ ) {
        const float angle = static_cast<float>( i ) *
                            ( kTwoPI / static_cast<float>( numSegments ) );
        ring[i] =
            Eigen::Vector3f( cosf( angle ) * radius + center.x(), center.y(),
                             sinf( angle ) * radius + center.z() );
    }

    drawList.setIdentityTransform();
    drawList.setActiveMaterial( nullptr );
    drawList.setDiffuse( 0, ToDiffuse( color ) );
    if ( !bSpokes ) {
        if ( drawList.beginPrimitive( PT_Line, numSegments * 2 ) ) {
            for ( int32_t i = 0; i < numSegments; i++ ) {
                drawList.pushVertex( ring[i].x(), ring[i].y(), ring[i].z() );
                drawList.pushVertex( ring[i].x(), ring[i].y() + height,
                                     ring[i].z() );
            }
            drawList.commitPrimitive();
        }
    } else if ( drawList.beginPrimitive( PT_LineStrip, numSegments * 4 ) ) {
        for ( int32_t i = 0; i < numSegments; i++ ) {
            drawList.pushVertex( center.x(), center.y(), center.z() );
            drawList.pushVertex( ring[i].x(), ring[i].y(), ring[i].z() );
            drawList.pushVertex( ring[i].x(), ring[i].y() + height,
                                 ring[i].z() );
            drawList.pushVertex( center.x(), height + center.y(), center.z() );
        }
        drawList.commitPrimitive();
    }

    if ( drawList.beginPrimitive( PT_LineStrip, numSegments * 2 + 2 ) ) {
        for ( int32_t i = 0; i <= numSegments; i++ ) {
            drawList.pushVertex( ring[i].x(), ring[i].y(), ring[i].z() );
        }
        for ( int32_t i = 0; i <= numSegments; i++ ) {
            drawList.pushVertex( ring[i].x(), ring[i].y() + height,
                                 ring[i].z() );
        }
        drawList.commitPrimitive();
    }
}

AiDebugDraw::AiDebugDraw()
    : pDrawList( nullptr ),
      pDrawList2D( nullptr ),
      pDrawListBackground( nullptr ),
      bCreated( false ) {}

static DrawList *CreateDebugDrawList( const uint32_t numPrimitives,
                                      const uint32_t numVertices,
                                      const char *pName ) {
    DrawStreams streams;
    streams.Normals = '\0';
    streams.Diffuse = '\1';
    streams.Specular = '\0';
    streams.UVMap = '\0';
    streams.Tangent = '\0';
    streams.Binormal = '\0';
    streams.bIndex16 = '\1';

    DrawList *pList = new DrawList();
    if ( !pList->initialize( numPrimitives, numVertices, 0, true, &streams,
                             pName, false ) ) {
        pList->destroy();
        delete pList;
        return nullptr;
    }
    return pList;
}

static void DestroyDebugDrawList( DrawList *&pList ) {
    if ( pList != nullptr ) {
        pList->destroy();
        delete pList;
        pList = nullptr;
    }
}

void AiDebugDraw::createBackground() {
    // FUN_0096dbd0
    pDrawListBackground = CreateDebugDrawList(
        kMaxNumBackgroundPrimitives, kMaxNumBackgroundVertices, "AI_DBG_BK" );
}

void AiDebugDraw::releaseBackground() {
    DestroyDebugDrawList( pDrawListBackground );
}

static bool CreatePIDGraph( DebugGraph &graph,
                            DrawList *pList,
                            const float minY,
                            const float maxY ) {
    // FUN_00972720
    if ( !graph.create( 5, pList, minY, maxY, 1000, 20.0f ) ) {
        return false;
    }

    graph.getCurve( 0 ).setColor( 0xff, 0x00, 0x00, 0xff );
    graph.getCurve( 0 ).setName( "Proport. Error" );
    graph.getCurve( 1 ).setColor( 0x00, 0xff, 0x00, 0xff );
    graph.getCurve( 1 ).setName( "Output" );
    graph.getCurve( 2 ).setColor( 0x00, 0x00, 0xff, 0xff );
    graph.getCurve( 2 ).setName( "External Output" );
    graph.getCurve( 3 ).setColor( 0xff, 0x00, 0xff, 0xff );
    graph.getCurve( 3 ).setName( "Current Val" );
    graph.getCurve( 4 ).setColor( 0x00, 0x80, 0x80, 0xff );
    graph.getCurve( 4 ).setName( "Wanted Val" );
    graph.setAutoRange( true );
    graph.setShowNames( true );
    graph.setShowRange( true );
    graph.getCurve( 3 ).bVisible = false;
    graph.getCurve( 4 ).bVisible = false;
    return true;
}

static bool CreateSpeedGraph( DrawList *pList ) {
    if ( !gSpeedGraph.create( 3, pList, 0.0f, 100.0f, 500, 20.0f ) ) {
        return false;
    }

    gSpeedGraph.setBounds( 0.0f, 0.25f, 1.0f, 0.5f );
    gSpeedGraph.getCurve( 0 ).setName( "Spd Max" );
    gSpeedGraph.getCurve( 0 ).setColor( 0xff, 0x00, 0x00, 0x80 );
    gSpeedGraph.getCurve( 1 ).setName( "Spd Cur" );
    gSpeedGraph.getCurve( 1 ).setColor( 0x00, 0xff, 0x00, 0xff );
    gSpeedGraph.getCurve( 2 ).setName( "Spd Asked" );
    gSpeedGraph.getCurve( 2 ).setColor( 0x00, 0x00, 0xff, 0x80 );
    gSpeedGraph.setShowNames( true );
    gSpeedGraph.setShowRange( true );
    gSpeedGraph.setAutoScale( true );
    gSpeedGraph.setAutoRange( false );
    // TODO: FUN_006dd770 (DebugGraph::autoScale) is private; draw() runs it
    // every frame since bAutoScale is set.
    return true;
}

bool AiDebugDraw::create() {
    // FUN_009732a0
    if ( bCreated ) {
        return true;
    }

    pDrawList = CreateDebugDrawList( kMaxNumDebugPrimitives,
                                     kMaxNumDebugVertices, "AI_DBG" );
    if ( pDrawList == nullptr ) {
        return false;
    }

    pDrawList2D = CreateDebugDrawList( kMaxNumDebugPrimitives2D,
                                       kMaxNumDebugVertices2D, "AI_DBG_2D" );
    if ( pDrawList2D == nullptr ) {
        DestroyDebugDrawList( pDrawList );
        return false;
    }

    if ( !CreatePIDGraph( gPIDSpeedGraph, pDrawList2D, -10.0f, 10.0f ) ) {
        DestroyDebugDrawList( pDrawList2D );
        DestroyDebugDrawList( pDrawList );
        return false;
    }
    gPIDSpeedGraph.setBounds( 0.0f, 0.5f, 1.0f, 0.75f );

    if ( !CreatePIDGraph( gPIDSteerGraph, pDrawList2D, -1.0f, 1.0f ) ) {
        gPIDSpeedGraph.release();
        DestroyDebugDrawList( pDrawList2D );
        DestroyDebugDrawList( pDrawList );
        return false;
    }
    gPIDSteerGraph.setBounds( 0.0f, 0.75f, 1.0f, 1.0f );

    if ( !CreateSpeedGraph( pDrawList2D ) ) {
        gPIDSteerGraph.release();
        gPIDSpeedGraph.release();
        DestroyDebugDrawList( pDrawList2D );
        DestroyDebugDrawList( pDrawList );
        return false;
    }

    bCreated = true;
    return true;
}

void AiDebugDraw::release() {
    // FUN_00972670
    if ( !bCreated ) {
        return;
    }

    gSpeedGraph.release();
    gPIDSteerGraph.release();
    gPIDSpeedGraph.release();
    DestroyDebugDrawList( pDrawList2D );
    DestroyDebugDrawList( pDrawList );
    bCreated = false;
}

static void ClearStaleDebuggedVehicles() {
    // FUN_009746f0
    if ( gpDebuggedAiVehicle != nullptr &&
         gpDebuggedAiVehicle->getCarState() == nullptr ) {
        gpDebuggedAiVehicle = nullptr;
        gDebuggedAiVehicleIndex = -1;
        gDebuggedAiVehicleIndex2 = -1;
    }
    if ( gpDebuggedOpponent != nullptr &&
         gpDebuggedOpponent->getCarState() == nullptr ) {
        gpDebuggedOpponent = nullptr;
    }
    if ( gDebuggedAiVehicleIndex < 0 ) {
        return;
    }

    const int32_t numVehicles = gpAI->getAIPool()->getVehicles().Count;
    if ( numVehicles < 1 ) {
        gDebuggedAiVehicleIndex = 0;
        gpDebuggedAiVehicle = nullptr;
        gpPIDSpeedGraphSource = nullptr;
        gpPIDSteerGraphSource = nullptr;
        return;
    }
    if ( numVehicles <= gDebuggedAiVehicleIndex ) {
        gDebuggedAiVehicleIndex = numVehicles - 1;
    }
}

static void UpdateCameraOnTrafficCar() {
    // FUN_00973220
    AiVehicle *pVehicle = nullptr;
    if ( gToggleCameraOnTrafficCar && gpDebuggedAiVehicle != nullptr ) {
        pVehicle = gpDebuggedAiVehicle;
    } else if ( gbDebugFirstOpponent && gpDebuggedOpponent != nullptr ) {
        pVehicle = gpDebuggedOpponent;
    }
    if ( pVehicle == nullptr ) {
        return;
    }

    CarState *pTarget = gpCamera->getBoundCarState();
    CameraGame *pFocusCamera = gpCamera->findCameraWithMode( 2, pTarget );
    if ( gpCamera->getActiveVehicleCamera() != pFocusCamera ) {
        return;
    }

    PhysicsVehicle *pPhysicsVehicle =
        pVehicle->getCarState()->getPhysicsVehicle();
    const float *pFocusPosition =
        ( pPhysicsVehicle != nullptr )
            ? pPhysicsVehicle->getPhysicsMatrix().row( 3 ).data()
            : gpScenaric->getLocalPlayer().getLastPosition().data();
    static_cast<CameraFocusObject *>( pFocusCamera )
        ->setObjectPosition( pFocusPosition );
}

void SetDebuggedAiVehicle( AiVehicle *pVehicle ) {
    // FUN_00973970
    gpDebuggedAiVehicle = pVehicle;
    gpPIDSpeedGraphSource = nullptr;
    gpPIDSteerGraphSource = nullptr;
    if ( pVehicle == nullptr ) {
        return;
    }

    if ( pVehicle->getLocomotionType() == 0 ) {
        gpPIDSpeedGraphSource =
            &pVehicle->getTrafficLocomotion().getSpeedControl();
        gpPIDSteerGraphSource =
            &pVehicle->getTrafficLocomotion().getSteerControl();
    } else if ( pVehicle->getLocomotionType() == 1 ) {
        gpPIDSpeedGraphSource =
            &pVehicle->getOpponentLocomotion().getAccelControl();
        gpPIDSteerGraphSource =
            &pVehicle->getOpponentLocomotion().getSteerControl();
    }
    CarState *pCarState = pVehicle->getCarState();
    if ( gToggleCameraOnTrafficCar && pCarState != nullptr ) {
        if ( gpMngHud != nullptr ) {
            gpMngHud->FUN_006e6a00( pCarState );
        }
        if ( pCarState->getPhysicsVehicle() != nullptr ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: GSPhysics shown vehicle
        }
    }
}

void DrawAiVehicleDebug( AiVehicle *pVehicle, DrawList *pDrawList ) {
    // FUN_00925940
    pVehicle->debugDraw( pDrawList );
}

static void DrawAllAiVehiclesDebug( DrawList *pDrawList ) {
    // FUN_0091dfd0
    for ( AiVehicle *pVehicle : gpAI->getAIPool()->getVehicles() ) {
        DrawAiVehicleDebug( pVehicle, pDrawList );
    }
}

void DrawAiDebugCarBox( AiDebugDraw &debugDraw,
                        const Matrix4x4f_t &modelMatrix,
                        const uint32_t color ) {
    // FUN_00972cc0
    if ( debugDraw.pDrawList == nullptr ) {
        return;
    }

    Eigen::Vector3f scale( 2.0f, 2.0f, 5.0f );
    Matrix4x4f_t matrix = MakeScaleMatrix( scale ) * modelMatrix;
    matrix.row( 3 ) -= GetShiftWorldPosition().transpose();
    matrix.row( 3 ) += Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f ).transpose();
    DrawDebugBox( matrix, color, *debugDraw.pDrawList, 1 );
}

void DrawDebugText3D( const char *pText,
                      const Eigen::Vector4f &position,
                      const ColorRGBA &color ) {
    // FUN_009bace0
    CameraVehicle *pCamera = gpCamera->getActiveVehicleCamera();
    if ( pCamera == nullptr ) {
        return;
    }

    const Eigen::Vector4f viewPosition =
        TDUMat4x4MulVec( pCamera->getWorldToCam(), position );
    if ( viewPosition.z() >= 0.0f ) {
        return;
    }

    Frustum &frustum = gpRender->getRenderPass<eRenderPass::RP_Near>()
                           .pScene->getFrustumWrite();
    Eigen::Vector4f clipPosition =
        TDUMat4x4MulVec( frustum.getProjectionMatrix(), viewPosition );
    clipPosition /= clipPosition.w();
    if ( clipPosition.x() < -1.0f || 1.0f < clipPosition.x() ||
         clipPosition.y() < -1.0f || 1.0f < clipPosition.y() ) {
        return;
    }

    const int32_t x =
        static_cast<int32_t>( ( clipPosition.x() + 1.0f ) * 0.5f *
                              static_cast<float>( gWindowWidth ) );
    const int32_t y =
        static_cast<int32_t>( ( 1.0f - clipPosition.y() ) * 0.5f *
                              static_cast<float>( gWindowHeight ) );
    gEdDebugText.DrawColor = color;

    // The retail strtok call has its arguments swapped, so only the first
    // draw shows anything.
    if ( pText != nullptr && pText[0] != '\0' ) {
        gEdDebugText.drawTextAt( x, y, "%s", pText );
    }
}

static void AppendLaneFlags( char *pText, const int8_t flags ) {
    const uint8_t mask = ( flags == 0 ) ? 7 : static_cast<uint8_t>( flags );
    if ( mask & 1 ) {
        strcat( pText, "L" );
    }
    if ( mask & 2 ) {
        strcat( pText, "F" );
    }
    if ( mask & 4 ) {
        strcat( pText, "R" );
    }
}

static void AppendControlDevice( char *pText, const uint8_t device ) {
    switch ( device ) {
    case 0:
        strcat( pText, " no dev" );
        break;
    case 1:
        strcat( pText, " stop" );
        break;
    case 2:
        strcat( pText, " yield" );
        break;
    default:
        sprintf( pText + strlen( pText ), " lgt %d", device - 3 );
        break;
    }
}

static void AppendSpeedLimit( char *pText,
                              const AiRoadMap &roadMap,
                              const AiRoadMap::Entity &entity ) {
    static constexpr float kSpeedLimitScale = 1.0f; // DAT_00faa990
    static constexpr float kMsToKmh = 3.6f;         // DAT_00f41768

    int32_t index = entity.SpeedLimitIndice;
    if ( roadMap.getNumSpeedLimits() <= index ) {
        index = 0;
    }
    sprintf( pText + strlen( pText ), " %d",
             static_cast<int32_t>( roadMap.getSpeedLimit( index ) *
                                   kSpeedLimitScale * kMsToKmh ) );
}

static void BuildRoadEntityNames( const AiRoadMap::Road &road,
                                  char *pBeginText,
                                  char *pEndText ) {
    // FUN_00972200
    const AiRoadMap &roadMap = gpAI->getRoadMap();
    const AiRoadMap::RoadDetail *pDetail = road.pDetail;
    const AiRoadMap::Entity *pFirst = pDetail->pEntities[0];
    const AiRoadMap::Entity *pLast =
        pDetail->pEntities[pDetail->NumEntities - 1];
    const int32_t numLanes = pFirst->NumLeftLanes + pLast->NumRightLanes;

    sprintf( pBeginText, "[B] %s (",
             roadMap.getPoolNames() + pFirst->NameOffset );
    sprintf( pEndText, "[E] %s (", roadMap.getPoolNames() + pLast->NameOffset );

    for ( int32_t i = 0; i < pFirst->NumLeftLanes; i++ ) {
        if ( i > 0 ) {
            strcat( pBeginText, "." );
        }
        int32_t lane = pLast->NumRightLanes - i - 1;
        if ( lane < 0 || numLanes <= lane ) {
            lane = 0;
        }
        AppendLaneFlags( pBeginText, road.LaneFlags[lane] );
    }
    strcat( pBeginText, ")" );

    for ( int32_t i = 0; i < pLast->NumRightLanes; i++ ) {
        if ( i > 0 ) {
            strcat( pEndText, "." );
        }
        int32_t lane = pFirst->NumLeftLanes - i - 1 + pLast->NumRightLanes;
        if ( lane < 0 || numLanes <= lane ) {
            lane = 0;
        }
        AppendLaneFlags( pEndText, road.LaneFlags[lane] );
    }
    strcat( pEndText, ")" );

    AppendControlDevice( pBeginText, road.CtrlDeviceBegin );
    AppendControlDevice( pEndText, road.CtrlDeviceEnd );
    AppendSpeedLimit( pBeginText, roadMap, *pFirst );
    AppendSpeedLimit( pEndText, roadMap, *pLast );
}

static inline bool IsInsideBounds( const AiRoadMap::Vertex &position,
                                   const float minX,
                                   const float maxX,
                                   const float minZ,
                                   const float maxZ ) {
    return minX < position.X && position.X < maxX && minZ < position.Z &&
           position.Z < maxZ;
}

static void DrawRoadMap( AiDebugDraw &debugDraw,
                         const float minX,
                         const float maxX,
                         const float minZ,
                         const float maxZ ) {
    // FUN_00973ff0
    const Eigen::Vector4f shift = GetShiftWorldPosition();
    const AiRoadMap &roadMap = gpAI->getRoadMap();

    for ( int32_t i = 0; i < roadMap.getNumJunctions(); i++ ) {
        const AiRoadMap::Junction *pJunction =
            roadMap.getJunction( static_cast<uint32_t>( i + 1 ) );
        if ( pJunction == nullptr ) {
            continue;
        }
        const AiRoadMap::Vertex &position = pJunction->Position;
        if ( minX <= position.X && position.X <= maxX && minZ <= position.Z &&
             position.Z <= maxZ ) {
            const Eigen::Vector4f sphere(
                position.X - shift.x(), position.Y - shift.y(),
                position.Z - shift.z(), kJunctionRadius );
            DrawDebugSphere( sphere, 0xff0000ff, *debugDraw.pDrawList );
            if ( gDisplayJunctionsID ) {
                Eigen::Vector4f textPosition = position.toVector4();
                textPosition.y() += 5.0f + 5.0f;
                char text[256];
                sprintf( text, "%d", static_cast<int32_t>( pJunction->ID ) );
                DrawDebugText3D( text, textPosition,
                                 ColorRGBA( 0xff, 0x00, 0x00, 0xff ) );
            }
        }
    }

    for ( int32_t i = 0; i < roadMap.getNumRoads(); i++ ) {
        const AiRoadMap::Road &road = roadMap.getRoads()[i];
        const AiRoadMap::Junction *pBegin = road.pJunctionBegin;
        const AiRoadMap::Junction *pEnd = road.pJunctionEnd;
        if ( pBegin == nullptr || pEnd == nullptr ) {
            continue;
        }
        if ( !IsInsideBounds( pBegin->Position, minX, maxX, minZ, maxZ ) &&
             !IsInsideBounds( pEnd->Position, minX, maxX, minZ, maxZ ) ) {
            continue;
        }

        const Eigen::Vector4f up( 0.0f, 1.0f, 0.0f, 0.0f );
        const Eigen::Vector4f start = pBegin->Position.toVector4() + up - shift;
        const Eigen::Vector4f end = pEnd->Position.toVector4() + up - shift;
        DrawDebugLine( start, end, 0xffff0000, *debugDraw.pDrawList );
        if ( gDisplayEntitiesNames && road.pDetail != nullptr ) {
            char beginText[256];
            char endText[256];
            BuildRoadEntityNames( road, beginText, endText );

            const Eigen::Vector4f textOffset( 0.0f, 0.5f, 0.0f, 0.0f );
            const Eigen::Vector4f beginPosition =
                start * 0.9f + end * 0.1f + shift + textOffset;
            const Eigen::Vector4f endPosition =
                end * 0.9f + start * 0.1f + shift + textOffset;
            const ColorRGBA textColor( 0xff, 0xa2, 0x00, 0xff );
            DrawDebugText3D( beginText, beginPosition, textColor );
            DrawDebugText3D( endText, endPosition, textColor );
        }
    }
}

static uint32_t GetTrafficLightColor( const int32_t synchro ) {
    switch ( GetTrafficSignalState( synchro, gpAI->getSignalSyncTime() ) ) {
    case 4:
        return 0xff00ff00;
    case 1:
        return 0xff0000ff;
    case 2:
        return 0xff009dff;
    default:
        return 0xffffffff;
    }
}

static void DrawTrafficLights( AiDebugDraw &debugDraw,
                               const float minX,
                               const float maxX,
                               const float minZ,
                               const float maxZ ) {
    // FUN_00973d90
    static constexpr uint8_t kFirstSignalDevice = 3;

    const AiRoadMap &roadMap = gpAI->getRoadMap();
    for ( int32_t i = 0; i < roadMap.getNumRoads(); i++ ) {
        const AiRoadMap::Road &road = roadMap.getRoads()[i];
        const AiRoadMap::Junction *pBegin = road.pJunctionBegin;
        const AiRoadMap::Junction *pEnd = road.pJunctionEnd;

        if ( pBegin != nullptr &&
             IsInsideBounds( pBegin->Position, minX, maxX, minZ, maxZ ) &&
             road.CtrlDeviceBegin >= kFirstSignalDevice ) {
            const uint32_t color =
                GetTrafficLightColor( road.getSignalSynchro( false ) );
            const AiRoadMap::Entity *pEntity = road.pDetail->pEntities[0];
            const Eigen::Vector4f position =
                pEntity->getWaypoint( 0 ).toVector4() - GetShiftWorldPosition();
            DrawDebugSphereMarker( position, 1.0f, color,
                                   *debugDraw.pDrawList );
        }

        if ( pEnd != nullptr &&
             IsInsideBounds( pEnd->Position, minX, maxX, minZ, maxZ ) &&
             road.CtrlDeviceEnd >= kFirstSignalDevice ) {
            const uint32_t color =
                GetTrafficLightColor( road.getSignalSynchro( true ) );
            const AiRoadMap::RoadDetail *pDetail = road.pDetail;
            const AiRoadMap::Entity *pEntity =
                pDetail->pEntities[pDetail->NumEntities - 1];
            const Eigen::Vector4f position =
                pEntity->getWaypoint( pEntity->getNumWaypoints() - 1 )
                    .toVector4() -
                GetShiftWorldPosition();
            DrawDebugSphereMarker( position, 1.0f, color,
                                   *debugDraw.pDrawList );
        }
    }
}

static Matrix4x4f_t MakeSimBubbleBox( const float height,
                                      const Matrix4x4f_t &frame ) {
    Eigen::Vector3f scale( kSimBubbleBoxWidth, height, kSimBubbleBoxWidth );
    Matrix4x4f_t box = MakeScaleMatrix( scale );
    box( 3, 1 ) += height * 0.5f;
    return box * frame;
}

static void DrawSimBubble( const AiSimBubble &bubble, DrawList &drawList ) {
    // FUN_00919430
    static constexpr uint32_t kColorWeight = 0x7f00ffff;    // DAT_016a8ed8
    static constexpr uint32_t kColorAltWeight = 0x7f0000ff; // DAT_016a8edc
    static constexpr uint32_t kColorOutline = 0xff00ff00;   // DAT_016a8ed4

    const Eigen::Vector4f shift = GetShiftWorldPosition();
    const Eigen::Vector4f center = bubble.getPosition() - shift;
    DrawDebugCylinder( center, gAIConfig.SpawnDistances[1], kSimBubbleHeight,
                       true, 0xff0000ff, drawList );
    DrawDebugCylinder( center, bubble.getRadius(), kSimBubbleHeight, false,
                       0xff00ffff, drawList );
    DrawDebugCylinder( center, gAIConfig.SpawnDistances[2], kSimBubbleHeight,
                       false, 0xff00ff00, drawList );

    const float heightScale = 1.0f / bubble.getMaxWeight();
    for ( const AiSimBubble::Candidate &candidate : bubble.getCandidates() ) {
        const float weightHeight =
            candidate.Weight * kSimBubbleHeight * heightScale;
        const float altWeightHeight =
            candidate.AltWeight * kSimBubbleHeight * heightScale;

        Matrix4x4f_t frame;
        candidate.It.computeFrame( frame, 0 );
        frame.row( 3 ) -= shift.transpose();

        const Matrix4x4f_t weightBox = MakeSimBubbleBox( weightHeight, frame );
        const Matrix4x4f_t fullBox =
            MakeSimBubbleBox( kSimBubbleHeight, frame );
        const Matrix4x4f_t altWeightBox =
            MakeSimBubbleBox( altWeightHeight, frame );

        DrawDebugBox( weightBox, kColorWeight, drawList, 1 );
        DrawDebugBox( weightBox, kColorOutline, drawList, 0 );
        DrawDebugBox( altWeightBox, kColorAltWeight, drawList, 1 );
        DrawDebugBox( altWeightBox, kColorOutline, drawList, 0 );
        DrawDebugBox( fullBox, kColorOutline, drawList, 0 );
    }
}

static void DrawCullingShape( const AiCullingShape &shape,
                              const Matrix4x4f_t &matrix,
                              const bool bWireframe,
                              DrawList &drawList ) {
    // FUN_00920460
    const Eigen::Vector4f forward( 0.0f, 0.0f, shape.FovDistance, 0.0f );
    const Eigen::Vector4f left =
        Mat4x4fTransform( MakeRotationMatY( -shape.FovHalfAngle ), forward );
    Eigen::Vector4f right =
        Mat4x4fTransform( MakeRotationMatY( shape.FovHalfAngle ), forward );

    Matrix4x4f_t transform = matrix;
    drawList.setActiveTransformMatrix( transform );
    drawList.setActiveMaterial( nullptr );
    drawList.setDiffuse( 0, ColorRGBA( 0xffff0000u ) );
    if ( !bWireframe ) {
        drawList.beginPrimitive( PT_Triangle, 6 );
        drawList.pushVertex( 0.0f, 1.0f, 0.0f );
        drawList.pushVertex( left.x(), 1.0f, left.z() );
        drawList.pushVertex( right.x(), 1.0f, right.z() );
        drawList.pushVertex( 0.0f, 1.0f, 0.0f );
        drawList.pushVertex( -left.x(), 1.0f, -left.z() );
        right = -right;
    } else {
        drawList.beginPrimitive( PT_LineStrip, 7 );
        drawList.pushVertex( 0.0f, 1.0f, 0.0f );
        drawList.pushVertex( left.x(), 1.0f, left.z() );
        drawList.pushVertex( right.x(), 1.0f, right.z() );
        drawList.pushVertex( 0.0f, 1.0f, 0.0f );
        drawList.pushVertex( -left.x(), 1.0f, -left.z() );
        drawList.pushVertex( -right.x(), 1.0f, -right.z() );
        right = Eigen::Vector4f::Zero();
    }
    drawList.pushVertex( right.x(), 1.0f, right.z() );
    drawList.commitPrimitive();

    drawList.setDiffuse( 0, ColorRGBA( 0xff0000ffu ) );
    drawList.beginPrimitive( bWireframe ? PT_LineStrip : PT_Triangle, 0x30 );
    float angle = 0.0f;
    for ( int32_t i = 0; i < 16; i++ ) {
        const float nextAngle = angle + kCullingCircleStep;
        drawList.pushVertex( 0.0f, 1.0f, 0.0f );
        drawList.pushVertex( cosf( angle ) * shape.FirstRadius, 1.0f,
                             sinf( angle ) * shape.FirstRadius );
        drawList.pushVertex( cosf( nextAngle ) * shape.FirstRadius, 1.0f,
                             sinf( nextAngle ) * shape.FirstRadius );
        angle = nextAngle;
    }
    drawList.commitPrimitive();
}

static void DrawLocalPlayerCullingShapes( AiDebugDraw &debugDraw ) {
    // FUN_00974460
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar != nullptr ) {
        DrawCullingShape( gpAI->getCullingShape(), pLocalCar->getWorldMatrix(),
                          true, *debugDraw.pDrawList );
    }
}

static void DrawLocalPlayerRoadFrame( AiDebugDraw &debugDraw ) {
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr ) {
        return;
    }

    const Matrix4x4f_t &modelMatrix = pLocalCar->getWorldMatrix();
    AiRoadMap::Road *pRoad = gpAI->getRoadMap().findNearestRoad(
        modelMatrix.row( 3 ).transpose(), true );
    if ( pRoad == nullptr ) {
        return;
    }

    AiRoadIt it;
    Matrix4x4f_t frame = Matrix4x4f_t::Identity();
    ProjectOnRoadShape( modelMatrix, it, &frame, true, pRoad->getShape() );

    Eigen::Vector4f origin = frame.row( 3 ).transpose();
    origin -= GetShiftWorldPosition();
    origin.y() += kRoadFrameHeight;

    static constexpr uint32_t kAxisColors[3] = { 0xff0000ff, 0xff00ff00,
                                                 0xffff0000 };
    for ( int32_t axis = 0; axis < 3; axis++ ) {
        const Eigen::Vector4f end =
            origin + frame.row( axis ).transpose() * kRoadFrameAxisLength;
        DrawDebugLine( origin, end, kAxisColors[axis], *debugDraw.pDrawList );
    }
}

static void DrawDebuggedAiVehicleState( AiDebugDraw &debugDraw,
                                        const int32_t state,
                                        AiVehicle *pVehicle ) {
    // FUN_00974ca0
    if ( state == 4 ) {
        DrawLocalPlayerRoadFrame( debugDraw );
        return;
    }
    if ( pVehicle == nullptr ) {
        return;
    }

    const AiRoadIt position = pVehicle->getInfos().LastRoadPos;
    if ( position.pRoad == nullptr ) {
        return;
    }

    static constexpr uint32_t kColorFree = 0x7f00ff00;    // green
    static constexpr uint32_t kColorPartial = 0x7f00ffff; // yellow
    static constexpr uint32_t kColorBlocked = 0x7f0000ff; // red
    static constexpr float kLaneCheckDistance = 20.0f;    // DAT_00faa73c
    static constexpr float kRibbonHeight = 4.0f;          // DAT_00faa74c
    static constexpr float kMinLookSpeed = 1.0f;          // DAT_00faa730
    static constexpr float kMaxLookSpeed = 75.0f;         // DAT_00faa72c
    static constexpr float kMinLookDistance = 10.0f;      // DAT_00faa738
    static constexpr float kMaxLookDistance = 200.0f;     // DAT_00faa734

    DrawList &drawList = *debugDraw.pDrawList;
    const int32_t lane = pVehicle->computeCurrentLane();
    float ratioStart = 0.0f;
    float ratioEnd = 0.0f;
    position.getEntity()->pProfile->computeLaneWidthRatios(
        lane, position.bWay, ratioStart, ratioEnd );
    const Eigen::Vector4f shift = GetShiftWorldPosition();
    auto pushPoint = [&drawList, &shift]( const Eigen::Vector4f &point ) {
        const Eigen::Vector4f shifted = point - shift;
        drawList.pushVertex( shifted.x(), shifted.y(), shifted.z() );
    };

    if ( state == 1 ) {
        AiRoadIt back = position;
        AiRoadIt front = position;
        const bool bBackValid =
            back.advanceByDistance( -kLaneCheckDistance, true );
        const bool bFrontValid =
            front.advanceByDistance( kLaneCheckDistance, true );
        uint32_t color = kColorPartial;
        if ( pVehicle->isLaneFree( lane, position, 0.0f, -kLaneCheckDistance,
                                   kLaneCheckDistance ) ) {
            color = kColorFree;
        } else if ( bBackValid && bFrontValid ) {
            color = kColorBlocked;
        }

        const Eigen::Vector4f ribbonUp( 0.0f, kRibbonHeight, 0.0f, 0.0f );
        const Eigen::Vector4f laneStart =
            position.computeWidthPosition( ratioStart );
        const Eigen::Vector4f laneEnd =
            position.computeWidthPosition( ratioEnd );
        drawList.setActiveMaterial( nullptr );
        drawList.setIdentityTransform();
        drawList.setDiffuse( 0, ToDiffuse( kColorFree ) );
        drawList.beginPrimitive( PT_TriangleStrip, 4 );
        pushPoint( laneStart );
        pushPoint( laneEnd );
        pushPoint( laneStart + ribbonUp );
        pushPoint( laneEnd + ribbonUp );
        drawList.commitPrimitive();

        Eigen::Vector4f backStart = back.computeWidthPosition( ratioStart );
        Eigen::Vector4f backEnd = back.computeWidthPosition( ratioEnd );
        const Eigen::Vector4f frontStart =
            front.computeWidthPosition( ratioStart );
        const Eigen::Vector4f frontEnd = front.computeWidthPosition( ratioEnd );
        backStart.y() += kRoadFrameHeight;
        backEnd.y() += kRoadFrameHeight;
        drawList.setDiffuse( 0, ToDiffuse( color ) );
        drawList.beginPrimitive( PT_TriangleStrip, 6 );
        pushPoint( backStart );
        pushPoint( backEnd );
        pushPoint( laneStart );
        pushPoint( laneEnd );
        pushPoint( frontStart );
        pushPoint( frontEnd );
        drawList.commitPrimitive();
        return;
    }

    if ( state == 2 ) {
        if ( pVehicle->getNavigationType() != 0 ) {
            return;
        }

        int32_t targetLane = lane - 1;
        if ( targetLane < 0 ) {
            const AiRoadProfile *pProfile = position.getEntity()->pProfile;
            if ( pProfile->NumLeftLanes + pProfile->NumRightLanes < 2 ) {
                return;
            }
            targetLane = lane + 1;
        }

        const float lookDistance =
            std::clamp( ( pVehicle->getInfos().Speed - kMinLookSpeed ) *
                                ( kMaxLookDistance - kMinLookDistance ) /
                                ( kMaxLookSpeed - kMinLookSpeed ) +
                            kMinLookDistance,
                        kMinLookDistance, kMaxLookDistance );
        AiRoadIt ahead = position;
        const bool bAheadValid = ahead.advanceByDistance( lookDistance, true );
        float targetRatioStart = 0.0f;
        float targetRatioEnd = 0.0f;
        ahead.getEntity()->pProfile->computeLaneWidthRatios(
            targetLane, ahead.bWay, targetRatioStart, targetRatioEnd );
        const bool bGapFree =
            pVehicle->getTrafficNavigation().checkLaneChangeGap(
                lane, targetLane, position.bWay, lookDistance, &drawList );

        Eigen::Vector4f points[4] = {
            position.computeWidthPosition( ratioStart ),
            position.computeWidthPosition( ratioEnd ),
            ahead.computeWidthPosition( targetRatioStart ),
            ahead.computeWidthPosition( targetRatioEnd ),
        };
        for ( Eigen::Vector4f &point : points ) {
            point.y() += kRoadFrameHeight;
        }

        uint32_t color = kColorPartial;
        if ( bAheadValid ) {
            color = bGapFree ? kColorFree : kColorBlocked;
        }
        drawList.setActiveMaterial( nullptr );
        drawList.setIdentityTransform();
        drawList.setDiffuse( 0, ToDiffuse( color ) );
        drawList.beginPrimitive( PT_TriangleStrip, 4 );
        for ( const Eigen::Vector4f &point : points ) {
            pushPoint( point );
        }
        drawList.commitPrimitive();
        return;
    }

    if ( state == 3 && pVehicle->getNavigationType() == 0 ) {
        pVehicle->getTrafficNavigation().drawFollowing( &drawList );
    }
}

static void DrawAiMemoryPage() {
    // FUN_00971510
    const AiRoadMap &roadMap = gpAI->getRoadMap();
    AiPool *pPool = gpAI->getAIPool();

    gEdDebugText.drawText( "AI memory:" );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: GSAI memory master (GSAI+0x10e0) size and
                             // free size, debug allocations (DAT_010e73ec)
    gEdDebugText.drawText( "pool max %d", 0 );
    gEdDebugText.drawText( "pool used %d", 0 );
    gEdDebugText.drawText(
        "roadmap %d",
        static_cast<int32_t>(
            ( roadMap.getNumJunctions() + roadMap.getNumProfiles() ) * 4 +
            roadMap.getSizeRoadmap() + roadMap.getSizeDetails() ) );
    gEdDebugText.drawText( "debug %d", 0 );

    auto getCategoryCount = [pPool]( const eAiVehicleCategory category ) {
        return pPool
            ->getList( AiPool::AVL_FirstCategory +
                       static_cast<int32_t>( category ) )
            .Count;
    };
    gEdDebugText.drawText( "%d AI driven vehicles",
                           pPool->getVehicles().Count );
    gEdDebugText.drawText(
        "%d Traffic", getCategoryCount( eAiVehicleCategory::AVC_Traffic ) );
    gEdDebugText.drawText( "%d Police",
                           getCategoryCount( eAiVehicleCategory::AVC_Cop ) );
    gEdDebugText.drawText( "%d Police Dam",
                           getCategoryCount( eAiVehicleCategory::AVC_CopDam ) );
    gEdDebugText.drawText(
        "%d Opponent", getCategoryCount( eAiVehicleCategory::AVC_Opponent ) );
    gEdDebugText.drawText( "%d Parked",
                           getCategoryCount( eAiVehicleCategory::AVC_Parked ) );
    gEdDebugText.drawText(
        "%d Cinematic", getCategoryCount( eAiVehicleCategory::AVC_Cinematic ) );
    gEdDebugText.drawText( "%d Bot",
                           getCategoryCount( eAiVehicleCategory::AVC_Bot ) );
    gEdDebugText.drawText( "-----------------" );
    gEdDebugText.drawText( "%d Using a traffic rsc",
                           pPool->getList( AiPool::AVL_WithTemplate ).Count );
    gEdDebugText.drawText(
        "%d Using a brand car rsc",
        pPool->getList( AiPool::AVL_WithoutTemplate ).Count );
    gEdDebugText.drawText( "-----------------" );
    gEdDebugText.drawText( "%d Local Traffic",
                           pPool->getList( AiPool::AVL_LocalTraffic ).Count );
    gEdDebugText.drawText( "%d Distant Traffic",
                           pPool->getList( AiPool::AVL_DistantTraffic ).Count );

    const float numHits = static_cast<float>( gNumAiHits );
    gEdDebugText.drawText(
        "%d hits %.2f mean energy %.2f hits/minute", gNumAiHits,
        static_cast<double>( gAiHitEnergy / numHits ),
        static_cast<double>( numHits / gpTimer->GameTotalTime * 60.0f ) );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: "***RECORDING***" when GSAI+0x1388 +0x4
}

static void DrawAiOptimPage() {
    // FUN_0096d6d0
    if ( gpTimer->GameSpeed == 0.0f ) {
        gEdDebugText.drawText( gbForceParsingWhenPaused
                                   ? "Force Parsing is Active"
                                   : "Force Parsing is Inactive" );
        gEdDebugText.drawText( gbForceOptimWhenPaused
                                   ? "Force Optim is Active"
                                   : "Force Optim is Inactive" );
    }
    gAiPathOptimiser.drawStats();
}

static void DrawAiPolicePage() {
    // FUN_0096d660
    gEdDebugText.drawText( "Nb max Cop = %d", gAIConfig.MaxNumPoliceVehicle );
    gEdDebugText.drawText( "Nb max Dam = %d", gAIConfig.MaxNumPoliceDam );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: PoliceManager cop behavior tuning
                             // (gDisplayCopBehaviorParametersTuning)
}

static void DrawAiTemplatesPage( AiPool &pool ) {
    // FUN_0091c950
    const AiPoolTraffic &traffic = pool.getTraffic();
    gEdDebugText.drawText( "#   ID  L C #I #USE #TOT RATE" );
    for ( int32_t i = 0; i < traffic.getNumTotalTemplates(); i++ ) {
        const AiPoolTraffic::Template &entry = traffic.getTemplateAt( i );
        gEdDebugText.drawText(
            "%02d %4d %c %c %2d %4d %4d %0.2f", i, entry.IDCar,
            entry.isLoaded() ? 'X' : '-', entry.bMarkedForChange ? 'X' : '-',
            ( entry.pResource != nullptr ) ? entry.pResource->RefCount : 0,
            entry.NumUsed, entry.NumTotalUsed,
            static_cast<double>( entry.Frequency ) );
    }
    gEdDebugText.drawText( "L = loaded  C = marked for change" );
    gEdDebugText.drawText( "#I = nb instances  #USE = use count" );
    gEdDebugText.drawText( "#TOT = total use count  RATE = spawn rate" );
}

static void DrawAiSimBubblePage( const AiSimBubble &bubble ) {
    // FUN_00917650
    gEdDebugText.drawText( "%d possible insertion points",
                           bubble.getNumCandidates() );

    float totalWeight = 0.0f;
    for ( const AiSimBubble::Candidate &candidate : bubble.getCandidates() ) {
        totalWeight += candidate.Weight;
    }
    gEdDebugText.drawText( "Total proba: %.3f  visible: %.3f",
                           static_cast<double>( totalWeight ),
                           static_cast<double>( bubble.getAltWeightSum() ) );
    if ( bubble.getMaxWeight() < bubble.getMinWeight() ) {
        gEdDebugText.drawText( "Min/Max proba at an intersection: -/-" );
    } else {
        gEdDebugText.drawText( "Min/Max proba at an intersection: %.3f/%.3f",
                               static_cast<double>( bubble.getMinWeight() ),
                               static_cast<double>( bubble.getMaxWeight() ) );
    }

    AiPool *pPool = gpAI->getAIPool();
    const int32_t numTrafficLoco =
        pPool->getList( AiPool::AVL_FirstLocomotion ).Count;
    const int32_t numTraffic =
        pPool
            ->getList( AiPool::AVL_FirstCategory +
                       static_cast<int32_t>( eAiVehicleCategory::AVC_Traffic ) )
            .Count;
    gEdDebugText.drawText( "Current/Max (All Traffic Rsc): %d/%d",
                           pPool->getList( AiPool::AVL_WithTemplate ).Count,
                           gAIConfig.MaxNumVehicle );
    gEdDebugText.drawText(
        "Current/Max (Traffic): %d/%d  (%d police, %d other)", numTrafficLoco,
        gAIConfig.MaxNumTrafficVehicle, numTrafficLoco - numTraffic,
        numTraffic );
    gEdDebugText.drawText(
        "Current/Max (Police): %d/%d",
        pPool
            ->getList( AiPool::AVL_FirstCategory +
                       static_cast<int32_t>( eAiVehicleCategory::AVC_Cop ) )
            .Count,
        gAIConfig.MaxNumPoliceVehicle );
    gEdDebugText.drawText(
        "Current/Max (PoliceDam): %d/%d",
        pPool
            ->getList( AiPool::AVL_FirstCategory +
                       static_cast<int32_t>( eAiVehicleCategory::AVC_CopDam ) )
            .Count,
        gAIConfig.MaxNumPoliceDam );
    gEdDebugText.drawText(
        "Current/Max (Parked): %d/%d",
        pPool
            ->getList( AiPool::AVL_FirstCategory +
                       static_cast<int32_t>( eAiVehicleCategory::AVC_Parked ) )
            .Count,
        gAIConfig.MaxNumParkedVehicle );
    gEdDebugText.drawText( "%s", "" );
    gEdDebugText.drawText( "Actual/Wanted (Traffic): %d/%d",
                           bubble.getNumTraffic(), bubble.getTargetTraffic() );
    gEdDebugText.drawText( "Actual/Wanted (Visible Traffic): %d/%d",
                           bubble.getNumVisibleHits(),
                           bubble.getTargetVisible() );
    gEdDebugText.drawText( "%s", "" );
    gEdDebugText.drawText(
        "Total Length of Roads inside: %d",
        static_cast<int32_t>( bubble.getRoadLengthInside() ) );
    gEdDebugText.drawText(
        "Lane Length of Roads inside: %d",
        static_cast<int32_t>( bubble.getLaneLengthInside() ) );
    for ( int32_t i = 0; i < AiSimBubble::kNumZoneWeights; i++ ) {
        gEdDebugText.drawText(
            "Length Inside Zone %d: %d meters", i,
            static_cast<int32_t>( bubble.getZoneWeight( i ) ) );
    }
    gEdDebugText.drawText( "Online: no" );
    gEdDebugText.drawText( "----------------------------------" );

    static constexpr float kSecondsToMinutes = 1.0f / 60.0f; // FLOAT_00f41998
    static constexpr float kPercent = 100.0f;                // DAT_00f41708
    const AiSimBubble::StatsHistory &estimated =
        bubble.getEstimatedVisibility();
    gEdDebugText.drawText(
        "(tested %3.1f min.) inst.  1 min.  5 min.",
        static_cast<double>( static_cast<float>( estimated.NumSamples ) *
                             kSecondsToMinutes ) );
    gEdDebugText.drawText(
        "%s: %3d %%  %3d %%   %3d %%", "Estim. Traffic Visib.",
        static_cast<int32_t>( estimated.Instant * kPercent ),
        static_cast<int32_t>( estimated.ShortAverage * kPercent ),
        static_cast<int32_t>( estimated.LongAverage * kPercent ) );
    if ( !bubble.isOcclusionRaycastEnabled() ) {
        gEdDebugText.drawText( "No Real Visibility Computation" );
        return;
    }
    const AiSimBubble::StatsHistory &real = bubble.getRealVisibility();
    gEdDebugText.drawText(
        "%s: %3d %%  %3d %%   %3d %%", "Real Traffic Visib.   ",
        static_cast<int32_t>( real.Instant * kPercent ),
        static_cast<int32_t>( real.ShortAverage * kPercent ),
        static_cast<int32_t>( real.LongAverage * kPercent ) );
}

struct RemovedVehicleRecord {
    char Reason[AiVehicle::kRemovalReasonLength];
    int32_t CarStateIndex;
    int32_t NetID;
    float Lifetime;
    Matrix4x4f_t Matrix;
};

static constexpr int32_t kMaxRemovedVehicleRecords = 16;
static RemovedVehicleRecord
    gRemovedVehicles[kMaxRemovedVehicleRecords]; // DAT_01442620
static int32_t gNumRemovedVehicles = 0;          // DAT_010e73e4

void RecordRemovedAiVehicle( const AiVehicle *pVehicle, const char *pReason ) {
    // FUN_0096d270
    if ( gNumRemovedVehicles == kMaxRemovedVehicleRecords ) {
        std::copy( gRemovedVehicles + 1,
                   gRemovedVehicles + kMaxRemovedVehicleRecords,
                   gRemovedVehicles );
    } else {
        gNumRemovedVehicles++;
    }

    RemovedVehicleRecord &record = gRemovedVehicles[gNumRemovedVehicles - 1];
    CarState *pCarState = pVehicle->getCarState();
    if ( pCarState == nullptr ) {
        record.CarStateIndex = -pVehicle->getCarStateIndex();
        record.NetID = 0;
        record.Matrix = pVehicle->getInfos().GroundMatrix;
        record.Matrix.row( 3 ) = pVehicle->computeCenterPosition().transpose();
    } else {
        record.CarStateIndex = pCarState->getCarInstanceID();
        record.NetID = pCarState->getNetID();
        record.Matrix = pCarState->getWorldMatrix();
    }
    record.Lifetime = gpTimer->GameTotalTime -
                      ( pVehicle->getFadeInEndTime() - gAIConfig.FadeInTime );
    strncpy( record.Reason, pReason, AiVehicle::kRemovalReasonLength );
    record.Reason[AiVehicle::kRemovalReasonLength - 1] = '\0';
}

static void DrawRemovedVehiclesPage() {
    // FUN_0096d430
    for ( int32_t i = 0; i < gNumRemovedVehicles; i++ ) {
        const RemovedVehicleRecord &record = gRemovedVehicles[i];
        gEdDebugText.drawText( "%02d. CS%02d GO%04X %s (lifetime: %.2f s)", i,
                               record.CarStateIndex, record.NetID,
                               record.Reason,
                               static_cast<double>( record.Lifetime ) );
    }
}

static void DrawRemovedVehicles( AiDebugDraw &debugDraw ) {
    // FUN_00973c20
    static constexpr uint32_t kBoxColor = 0x50ff00ff;
    static constexpr float kLabelHeight = 2.5f; // DAT_00f416c0

    const Eigen::Vector4f labelOffset( 0.0f, kLabelHeight, 0.0f, 0.0f );
    for ( int32_t i = 0; i < gNumRemovedVehicles; i++ ) {
        const RemovedVehicleRecord &record = gRemovedVehicles[i];
        DrawAiDebugCarBox( debugDraw, record.Matrix, kBoxColor );

        char label[44];
        sprintf( label, "%02d", i );
        DrawDebugText3D( label,
                         record.Matrix.row( 3 ).transpose() + labelOffset,
                         ColorRGBA( 0xc8, 0x00, 0xc8, 0xff ) );
    }
}

static void DrawAiVehicleText( AiVehicle *pVehicle ) {
    // FUN_00926ae0
    CarState *pCarState = pVehicle->getCarState();
    if ( pCarState == nullptr ) {
        return;
    }

    const float instanciationTime =
        pVehicle->getFadeInEndTime() - gAIConfig.FadeInTime;
    gEdDebugText.drawText(
        "Instanciated at %.3f second (%.3f seconds ago)",
        static_cast<double>( instanciationTime ),
        static_cast<double>( gpTimer->GameTotalTime - instanciationTime ) );
    gEdDebugText.drawText( "%s", pVehicle->isPhysicsDriven() ? "Loco: physics"
                                                             : "Loco: manual" );

    const ObjectWorldLocation &location = pCarState->getWorldLocation();
    switch ( location.LocationType ) {
    case ObjectWorldLocation::OWL_Road: {
        const ObjectLocationRoad &road = location.LocationRoad;
        const char *pWay = location.bWay ? "B2E" : "E2B";
        AiRoadMap::Road *pRoad = gpAI->getRoadMap().getRoad( road.RoadID );
        if ( pRoad == nullptr ) {
            break;
        }
        const float length = pRoad->getShape()->TotalLength;
        gEdDebugText.drawText( "Loc: road %d %s U[%.2f %.2f] V[%.2f %.2f]",
                               road.RoadID, pWay,
                               static_cast<double>( length * road.FractionMin ),
                               static_cast<double>( length * road.FractionMax ),
                               static_cast<double>( road.LateralMin ),
                               static_cast<double>( road.LateralMax ) );
        break;
    }
    case ObjectWorldLocation::OWL_Junction: {
        const ObjectLocationOther &junction = location.LocationOther;
        gEdDebugText.drawText( "Loc: junction %d [%.2f %.2f]", junction.RoadID,
                               static_cast<double>( junction.PositionX ),
                               static_cast<double>( junction.PositionZ ) );
        break;
    }
    case ObjectWorldLocation::OWL_OffRoad:
        gEdDebugText.drawText( "%s",
                               location.bNearJunction
                                   ? "Loc: outside (maybe on junction sidewalk)"
                                   : "Loc: outside" );
        break;
    default:
        gEdDebugText.drawText( "Loc: unknown" );
        break;
    }

    VehicleBase *pVehicleBase = pCarState->getVehicleInstance();
    if ( pVehicleBase != nullptr ) {
        gEdDebugText.drawText(
            "Alpha: %.3f", static_cast<double>( pVehicleBase->getAlpha() ) );
    }
    if ( pVehicle->getBehaviour() != nullptr ) {
        pVehicle->getBehaviour()->debugText();
    }
    if ( pVehicle->getController() != nullptr ) {
        pVehicle->getController()->debugText();
    }
    if ( pVehicle->getNavigation() != nullptr ) {
        pVehicle->getNavigation()->debugText();
    }
    if ( pVehicle->getLocomotion() != nullptr ) {
        pVehicle->getLocomotion()->debugText();
    }
}

void SelectNextHandmadeJunction() {
    // FUN_00972fe0
    const AiRoadMap &roadMap = gpAI->getRoadMap();
    const AiRoadMap::Junction *pFirst = nullptr;
    const AiRoadMap::Junction *pNext = nullptr;
    int32_t index = -1;

    gNumHandmadeJunctions = 0;
    for ( int32_t i = 0; i < roadMap.getNumJunctions(); i++ ) {
        const AiRoadMap::Junction *pJunction =
            roadMap.getJunction( static_cast<uint32_t>( i + 1 ) );
        if ( pJunction == nullptr || pJunction->Flags == 0 ) {
            continue;
        }
        if ( pFirst == nullptr ) {
            pFirst = pJunction;
        }
        gNumHandmadeJunctions++;
        index++;
        if ( index == gHandmadeJunctionIndex + 1 ) {
            pNext = pJunction;
        }
    }

    if ( pNext != nullptr ) {
        gHandmadeJunctionIndex++;
    } else if ( pFirst != nullptr ) {
        gHandmadeJunctionIndex = 0;
        pNext = pFirst;
    } else {
        return;
    }

    gHandmadeJunctionID = pNext->ID;

    static constexpr float kCameraHeight = 100.0f; // DAT_00f41708
    CameraMouse *pFreeCamera = static_cast<CameraMouse *>(
        gpCamera->findCameraWithMode( 1, gpCamera->getBoundCarState() ) );
    pFreeCamera->setFollowPlayer( false );
    const Eigen::Vector4f target = pNext->Position.toVector4();
    Eigen::Vector4f position = target;
    position.y() += kCameraHeight;
    pFreeCamera->setPosition( position );
    pFreeCamera->setTarget( target );
    pFreeCamera->updateMatrix( position.head<3>(),
                               Eigen::Vector3f( 0.0f, 1.0f, 0.0f ),
                               target.head<3>() );
    gpCamera->requestCameraMode( 1, 0, 1.0f );
}

static void DrawHandmadeJunctionPage() {
    // FUN_00971430
    gEdDebugText.drawText( "Handmade junc %d / %d", gHandmadeJunctionIndex + 1,
                           gNumHandmadeJunctions );

    const AiRoadMap::Junction *pJunction =
        gpAI->getRoadMap().getJunction( gHandmadeJunctionID );
    if ( pJunction == nullptr ) {
        return;
    }

    static constexpr const char *kSplitTypeNames[5] = {
        "Not highway junction", "Highway split", "Highway merge",
        "Highway entry",        "Highway exit",
    };

    const uint32_t roadID =
        ( pJunction->NumRoads != 0 ) ? pJunction->pRoads[0]->ID : 0;
    AiRoadMap::JunctionSplit split;
    pJunction->classifySplit( split, roadID );
    gEdDebugText.drawText( "%s", kSplitTypeNames[split.Type] );
}

static void DrawAiDebugPages() {
    static constexpr int32_t kPageTop = 20;   // TODO: edDebug+0x78
    static constexpr int32_t kPageLeft = 400; // DAT_00faa75c

    gEdDebugText.setPosition( kPageLeft, kPageTop );
    gEdDebugText.setColor( DDC_White );

    switch ( gAiDebugPage ) {
    case 1:
        DrawAiMemoryPage();
        break;
    case 2:
        DrawAiOptimPage();
        break;
    case 3:
        DrawAiPolicePage();
        break;
    case 4:
        DrawAiTemplatesPage( *gpAI->getAIPool() );
        break;
    case 5:
        DrawAiSimBubblePage( gpAI->getSimBubble() );
        break;
    case 6:
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_0096d480)
        break;
    case 7:
        DrawRemovedVehiclesPage();
        break;
    default:
        break;
    }

    if ( gpDebuggedAiVehicle != nullptr ) {
        DrawAiVehicleText( gpDebuggedAiVehicle );
    }
    if ( gpDebuggedOpponent != nullptr ) {
        DrawAiVehicleText( gpDebuggedOpponent );
    }
    if ( gHandmadeJunctionID != 0 ) {
        DrawHandmadeJunctionPage();
    }
}

static void UpdatePIDGraph( DebugGraph &graph,
                            const PIDCtrl *pSource,
                            const float deltaTime,
                            const float time ) {
    // FUN_0096dc90
    if ( deltaTime <= 0.0f || pSource == nullptr || graph.isFrozen() ) {
        return;
    }

    if ( graph.getNumCurves() > 0 ) {
        graph.getCurve( 0 ).addPoint( time, pSource->getErrorProportional() );
    }
    if ( graph.getNumCurves() > 1 ) {
        graph.getCurve( 1 ).addPoint( time, pSource->getOutput() );
    }
    if ( graph.getNumCurves() > 3 ) {
        graph.getCurve( 3 ).addPoint( time, pSource->getCurrentValue() );
    }
    if ( graph.getNumCurves() > 4 ) {
        graph.getCurve( 4 ).addPoint( time, pSource->getWantedValue() );
    }
}

static void UpdateDebugGraphs( const float deltaTime, const float time ) {
    // FUN_00974490
    UpdatePIDGraph( gPIDSpeedGraph, gpPIDSpeedGraphSource, deltaTime, time );
    UpdatePIDGraph( gPIDSteerGraph, gpPIDSteerGraphSource, deltaTime, time );

    AiVehicle *pVehicle = gpDebuggedAiVehicle;
    if ( pVehicle == nullptr || deltaTime <= 0.0f ) {
        return;
    }

    const int32_t locoType = pVehicle->getLocomotionType();
    if ( locoType == 0 || locoType == 1 ) {
        const AiVehLocomotion &loco =
            ( locoType == 0 ) ? static_cast<const AiVehLocomotion &>(
                                    pVehicle->getTrafficLocomotion() )
                              : static_cast<const AiVehLocomotion &>(
                                    pVehicle->getOpponentLocomotion() );
        if ( !gPIDSpeedGraph.isFrozen() && gPIDSpeedGraph.getNumCurves() > 2 ) {
            gPIDSpeedGraph.getCurve( 2 ).addPoint( time,
                                                   loco.getPadThrottle() );
        }

        const float speedLimit = ( locoType == 0 )
                                     ? pVehicle->getTrafficNavigation()
                                           .getPath()
                                           .computeCurrentSpeedLimit()
                                     : pVehicle->getOpponentNavigation()
                                           .getPath()
                                           .computeCurrentSpeedLimit();
        if ( !gSpeedGraph.isFrozen() ) {
            if ( gSpeedGraph.getNumCurves() > 0 ) {
                gSpeedGraph.getCurve( 0 ).addPoint( time, speedLimit );
            }
            if ( gSpeedGraph.getNumCurves() > 1 ) {
                gSpeedGraph.getCurve( 1 ).addPoint(
                    time, pVehicle->getInfos().Speed );
            }
            if ( gSpeedGraph.getNumCurves() > 2 ) {
                gSpeedGraph.getCurve( 2 ).addPoint( time,
                                                    loco.getTargetSpeed() );
            }
        }
    }

    AiVehLocomotion *pLoco = pVehicle->getLocomotion();
    if ( pLoco != nullptr && !gPIDSteerGraph.isFrozen() &&
         gPIDSteerGraph.getNumCurves() > 2 ) {
        gPIDSteerGraph.getCurve( 2 ).addPoint( time, pLoco->getPadSteering() );
    }
}

void TickAiDebugOverlay( float deltaTime, float time ) {
    // FUN_00975a10
    AiDebugDraw &debugDraw = *gpAI->getDebugDraw();

    if ( gFlushInstanciation ) {
        gEdDebugText.setColor( DDC_Red );
        AiSpawnRequest &spawnRequest = gpAI->getAIPool()->getSpawnRequest();
        if ( spawnRequest.getState() == AiSpawnRequest::ASS_WaitResource ) {
            gFlushInstanciation = false;
            spawnRequest.flush();
            gEdDebugText.drawTextAt( 400, 500, "Flushing Instanciation" );
        } else {
            gEdDebugText.drawTextAt( 400, 500, "Waiting for Flush" );
        }
    }
    // TODO: personality rebinding from the edDebug menu (FUN_0096d740)
    if ( gbAiDebugPageOpen ) {
        DrawAiDebugPages();
    }

    if ( !gDistributionZoneDebugDraw ) {
        debugDraw.release();
    } else if ( !debugDraw.create() ) {
        gDistributionZoneDebugDraw = false;
    }

    ClearStaleDebuggedVehicles();
    UpdateCameraOnTrafficCar();

    DrawList *pBackground = debugDraw.pDrawListBackground;
    if ( pBackground != nullptr ) {
        pBackground->reset();
        if ( gbAiDebugPageOpen ) {
            pBackground->setIdentityTransform();
            pBackground->setActiveMaterial( nullptr );
            pBackground->setDiffuse( 0, ColorRGBA( 0x7f000000u ) );
            pBackground->beginPrimitive( PT_TriangleStrip, 4 );
            pBackground->pushVertex( 0.0f, 0.0f, 0.0f );
            pBackground->pushVertex( 1.0f, 0.0f, 0.0f );
            pBackground->pushVertex( 0.0f, 0.5f, 0.0f );
            pBackground->pushVertex( 1.0f, 0.5f, 0.0f );
            pBackground->commitPrimitive();
        }
    }

    // TODO: particle test (FUN_009729a0)
    if ( !debugDraw.bCreated ) {
        return;
    }

    UpdateDebugGraphs( deltaTime, time );

    if ( gDistributionZoneDebugDraw ) {
        debugDraw.pDrawList->reset();
        debugDraw.pDrawList2D->reset();
        debugDraw.pDrawList->setIdentityTransform();

        if ( gDisplayCullingShapes ) {
            DrawLocalPlayerCullingShapes( debugDraw );
        }

        CameraVehicle *pCamera = gpCamera->getActiveVehicleCamera();
        if ( pCamera == nullptr ) {
            return;
        }
        const Eigen::Vector3f cameraPosition = pCamera->getWorldPosition();
        const float minX = cameraPosition.x() - gBoundBoxWidth;
        const float maxX = cameraPosition.x() + gBoundBoxWidth;
        const float minZ = cameraPosition.z() - gBoundBoxWidth;
        const float maxZ = cameraPosition.z() + gBoundBoxWidth;

        if ( gDisplayRoadMap ) {
            DrawRoadMap( debugDraw, minX, maxX, minZ, maxZ );
        }
        if ( gProjectOnRoad >= 1 && gProjectOnRoad <= 4 ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_009749f0
        } else if ( gProjectOnRoad == 5 ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_00971760
        }
        if ( gDisplayJunctionDetail ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_009720c0
        }

        AiPool *pPool = gpAI->getAIPool();
        if ( !gbDebugFirstOpponent ) {
            if ( pPool->getVehicles().Count == 0 &&
                 gpDebuggedAiVehicle != nullptr ) {
                DrawAiVehicleDebug( gpDebuggedAiVehicle, debugDraw.pDrawList );
            } else {
                DrawAllAiVehiclesDebug( debugDraw.pDrawList );
            }
        }

        if ( gTogglePIDSteerGraph ) {
            gPIDSteerGraph.draw( time );
        }
        if ( gTogglePIDSpeedGraph ) {
            gPIDSpeedGraph.draw( time );
        }
        if ( gToggleSpeedGraph ) {
            gSpeedGraph.draw( time );
        }
        if ( gDisplayGameObjects ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_0096af00
        }
        if ( gDisplayRoadBuckets ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_00936030
        }
        if ( gDisplayLastSearch ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: GSAI+0x1130 vtable slot 1
        }
        if ( gDisplaySimBubble ) {
            DrawSimBubble( gpAI->getSimBubble(), *debugDraw.pDrawList );
        }
        if ( gDisplaySimGroups ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_00916530)
        }
        if ( gDisplayNetInterp ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_009219e0)
        }
        if ( gDisplayTrafficLights ) {
            DrawTrafficLights( debugDraw, minX, maxX, minZ, maxZ );
        }

        if ( gbDebugFirstOpponent ) {
            if ( gpDebuggedOpponent != nullptr ) {
                DrawAiVehicleDebug( gpDebuggedOpponent, debugDraw.pDrawList );
            } else {
                AiVehicleList &opponents =
                    pPool->getList( AiPool::AVL_FirstNavigation + 1 );
                if ( opponents.Count > 0 ) {
                    gpDebuggedOpponent = opponents.Items[0];
                }
            }
        }
        if ( gDebugAiVehicleState > 0 ) {
            DrawDebuggedAiVehicleState( debugDraw, gDebugAiVehicleState,
                                        gpDebuggedAiVehicle );
        }
        if ( gDisplayCollisionRisks ) {
            for ( AiVehicle *pVehicle :
                  pPool->getList( AiPool::AVL_FirstCategory +
                                  static_cast<int32_t>(
                                      eAiVehicleCategory::AVC_Traffic ) ) ) {
                if ( pVehicle != nullptr &&
                     pVehicle->getNavigationType() == 0 ) {
                    pVehicle->getTrafficNavigation().drawCollisionRisk(
                        debugDraw.pDrawList );
                }
            }
        }
        if ( gDisplayScenaricCarPos ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: iterate the streamed sectors
                                     // ET_CARP entities (DrawAiDebugCarBox)
        }
        if ( gDisplayRemovedVehicles ) {
            DrawRemovedVehicles( debugDraw );
        }
    }
    if ( gDisplayPedestrians ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_0092a480
    }
}

void SubmitAiDebugDrawLists() {
    // FUN_00973150
    AiDebugDraw &debugDraw = *gpAI->getDebugDraw();
    if ( debugDraw.bCreated && gDistributionZoneDebugDraw ) {
        gpRender->getRenderPass<eRenderPass::RP_Near>()
            .pScene->enqueueDynamicDrawList( debugDraw.pDrawList );
        gpRender->getRenderPass<eRenderPass::RP_2D>()
            .pScene->enqueueDynamicDrawList( debugDraw.pDrawList2D );
    }
    if ( debugDraw.pDrawListBackground != nullptr ) {
        gpRender->getRenderPass<eRenderPass::RP_2D>()
            .pScene->enqueueDynamicDrawList( debugDraw.pDrawListBackground );
    }
    // TODO: particle test draw (FUN_00972950)
}
