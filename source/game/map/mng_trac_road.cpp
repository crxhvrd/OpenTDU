#include "shared.h"
#include "mng_trac_road.h"

#include "ai/ai_road_query.h"
#include "database/gs_database.h"
#include "game/scenaric/event_parameters.h"
#include "player_data/gs_playerdata.h"
#include "core/cubic_bezier.h"
#include "game/game_mechanism_player.h"
#include "game/gm_base.h"
#include "game/map/gm_map.h"
#include "game/map/mng_map_viewer.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/mng_spots.h"
#include "game/scenaric/spots.h"
#include "render/camera/camera_gps.h"
#include "render/camera/gs_camera.h"
#include "render/gs_render.h"
#include "render/render_scene.h"
#include "tdu_instance.h"

MngTracRoad *gpMngTracRoad = nullptr; // DAT_010e7cf8

static bool gbTraceRoad = true;           // DAT_00f945d8
static int32_t gTraceRoadDetailMode = 4;  // DAT_00f945dc (4 = no override)
static bool gbWireframeTraceRoad = false; // DAT_010e7cfc

static constexpr int32_t kGPSCameraMode = 3;
static constexpr int32_t kMaxNumPrimitives = 0x226;
static constexpr int32_t kMaxNumVertices = 0x1194;
static constexpr ColorRGBA kRibbonColor = 0x00ff3232u; // DAT_00fab8fc..904
static constexpr uint32_t kExtraChallengeRecordFlag = 0x10;
static constexpr uint8_t kSpotTypeChallenge = 7;

static constexpr float kMinRibbonWidth = 15.0f;       // DAT_00fab918
static constexpr float kMaxRibbonWidth = 150.0f;      // DAT_00fab920
static constexpr float kCloseRibbonWidth = 100.0f;    // DAT_00fab91c
static constexpr float kMinRibbonHeight = 2.0f;       // DAT_00fab90c
static constexpr float kMaxRibbonHeight = 261.0f;     // DAT_00fab908
static constexpr float kMinCameraDistance = 665.0f;   // DAT_00fab914
static constexpr float kMaxCameraDistance = 15000.0f; // DAT_00fab910
static constexpr float kViewBoxExtent = 999999.0f;
static constexpr float kMinRibbonDirectionSq = 0.01f;

static CameraGPS *FindGPSCamera() {
    return static_cast<CameraGPS *>( gpCamera->findCameraWithMode(
        kGPSCameraMode, gpCamera->getBoundCarState() ) );
}

static float LerpClamped( const float minValue,
                          const float maxValue,
                          const float offset,
                          const float range ) {
    const float value = ( ( maxValue - minValue ) * offset ) / range + minValue;
    const float upper = std::max( minValue, maxValue );
    const float lower = std::min( minValue, maxValue );
    return std::max( std::min( value, upper ), lower );
}

static Matrix4x4f_t ComputeWorldShiftMatrix() {
    Matrix4x4f_t matrix = Matrix4x4f_t::Identity();
    if ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() ) {
        const Eigen::Vector4f &shift = gpWorldStreaming->getPosition();
        matrix.row( 3 ) =
            Eigen::Vector4f( -shift.x(), -shift.y(), -shift.z(), 1.0f )
                .transpose();
    }
    return matrix;
}

MngTracRoad::MngTracRoad()
    : Manager(),
      StreamingCallback(),
      drawList(),
      numPoints( 0 ),
      pPoints( nullptr ),
      numPrimitivesUsed( 0 ),
      numVerticesUsed( 0 ),
      paths(),
      legLengths(),
      field_0x93f4( false ),
      detailMode( 0 ),
      width( 20.0f ),
      height( 5.0f ),
      viewBoxMin( Eigen::Vector4f::Zero() ),
      viewBoxMax( Eigen::Vector4f::Zero() ),
      alpha( 0xff ) {
    // FUN_00783a10
    gpMngTracRoad = this;
}

MngTracRoad::~MngTracRoad() {
    // FUN_00783990
    gpMngTracRoad = nullptr;
    for ( int32_t i = kMaxNumLegs - 1; i >= 0; i-- ) {
        paths[i].clear();
    }
}

bool MngTracRoad::initialize( TestDriveGameInstance * ) {
    // FUN_00783180
    drawList.initialize( kMaxNumPrimitives, kMaxNumVertices, 0, true, nullptr,
                         "MngTracRoad", false );
    pPoints = new Eigen::Vector4f[kMaxNumPoints];
    memset( legLengths, 0, sizeof( legLengths ) );
    return true; // TODO: Original p-code always return false
                 // (most likely a mistake made by the devs?)
}

void MngTracRoad::tick( float, float ) {
    // FUN_00784eb0
    drawList.reset();
    alpha = 0xff;

    const int32_t zoom = FindGPSCamera()->getZoom();
    if ( zoom == CameraGPS::GZL_Island ) {
        detailMode = 1;
        width = kMaxRibbonWidth;
    } else if ( zoom == CameraGPS::GZL_Far ) {
        detailMode = 2;
        width = kCloseRibbonWidth;
    } else {
        detailMode = 3;
        width = ( zoom == CameraGPS::GZL_Medium ) ? kCloseRibbonWidth
                                                  : kMinRibbonWidth;
    }

    CameraGame *pCamera = gpCamera->getActiveCamera();
    const float distance =
        ( pCamera->getWorldPosition() - pCamera->getTarget() ).norm();
    const float offset = distance - kMinCameraDistance;
    const float range = kMaxCameraDistance - kMinCameraDistance;
    width = LerpClamped( kMinRibbonWidth, kMaxRibbonWidth, offset, range );
    height = LerpClamped( kMinRibbonHeight, kMaxRibbonHeight, offset, range );
    if ( gTraceRoadDetailMode != 4 ) {
        detailMode = gTraceRoadDetailMode;
    }

    numPrimitivesUsed = 0;
    numVerticesUsed = 0;
    if ( !gbTraceRoad || !isActive() || gpScenaric->isICEnabled() ) {
        return;
    }

    numPoints = 0;
    computeViewBox();
    if ( getNumWaypoints() <= 0 ) {
        return;
    }
    for ( int32_t i = 0; i < getNumWaypoints() - 1; i++ ) {
        if ( i < kMaxNumLegs ) {
            drawLeg( paths[i] );
        }
    }
    if ( isLoop() && getNumWaypoints() - 1 < kMaxNumLegs ) {
        drawLeg( paths[getNumWaypoints() - 1] );
    }
}

void MngTracRoad::reset() {
    // FUN_00783170
}

void MngTracRoad::draw() {
    // FUN_00783850
    if ( gpTestDriveInstance->getActiveGameMode() == GM_Lobby ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: GMLobby
        return;
    }
    gpRender->getRenderPass<RP_Near>().pScene->enqueueDynamicDrawList(
        &drawList );
}

void MngTracRoad::pause() {
    // FUN_00783160
}

void MngTracRoad::terminate() {
    // FUN_00783130
    delete[] pPoints;
    pPoints = nullptr;
    drawList.destroy();
}

bool MngTracRoad::onWorldStreamed( Eigen::Vector4f *,
                                   Eigen::Vector4f *,
                                   Eigen::Vector4f *pShiftPosition ) {
    // FUN_00783410
    Matrix4x4f_t matrix = Matrix4x4f_t::Identity();
    matrix.row( 3 ) = -pShiftPosition->transpose();
    matrix( 3, 3 ) = 1.0f;
    if ( drawList.isInitialized() ) {
        for ( uint32_t i = 0; i < drawList.getNumPrimitives(); i++ ) {
            drawList.getMatrixAtIndex( i ) = matrix;
        }
    }
    return true;
}

void MngTracRoad::solveLegs( const bool bIgnoreOneWay ) {
    // FUN_00784e10
    const int32_t numLegWaypoints = getNumWaypoints();
    if ( !isActive() ) {
        for ( int32_t i = 0; i < numLegWaypoints; i++ ) {
            paths[i].clear();
        }
        return;
    }
    if ( numLegWaypoints <= 0 ) {
        return;
    }
    for ( int32_t i = 0; i < numLegWaypoints - 1; i++ ) {
        solveLeg( i, bIgnoreOneWay );
    }
    if ( isLoop() ) {
        solveLeg( numLegWaypoints - 1, bIgnoreOneWay );
    }
}

float MngTracRoad::computeRouteLength( const bool bLoop,
                                       const int32_t loopCheckpointIndex,
                                       const int32_t numLaps ) {
    // FUN_00783060
    if ( !bLoop ) {
        float length = 0.0f;
        for ( int32_t i = 0; i < getNumWaypoints() - 1; i++ ) {
            length += legLengths[i];
        }
        return length;
    }

    float lapLength = 0.0f;
    float leadLength = 0.0f;
    for ( int32_t i = 0; i < getNumWaypoints(); i++ ) {
        if ( i < loopCheckpointIndex - 1 ) {
            leadLength += legLengths[i];
        } else {
            lapLength += legLengths[i];
        }
    }
    return float( numLaps ) * lapLength + leadLength;
}

float ComputeEventRouteLength( EventParameters *pEventParameters,
                               MngTracRoad *pTracRoad ) {
    // FUN_00783370
    const bool bLoop = pEventParameters->getLoop().getValue() != 0;
    const int32_t loopCheckpointIndex =
        int32_t( pEventParameters->getLoopCheckpointIndex().getValue() );
    int32_t numLaps = int32_t( pEventParameters->getNumLaps().getValue() );
    if ( numLaps == 0 ) {
        numLaps = int32_t( pEventParameters->getNumOpponents().getValue() );
    }
    return pTracRoad->computeRouteLength( bLoop, loopCheckpointIndex, numLaps );
}

void FormatRouteDistance( const float distanceKm, char *pOutText ) {
    // FUN_00783310
    static constexpr uint64_t kMilesHashcode = 0x5f3563;
    static constexpr uint64_t kKilometersHashcode = 0x60bc03;
    static constexpr float kKilometersToMiles = 0.621504f;

    float distance = distanceKm;
    uint64_t unitHashcode = kKilometersHashcode;
    if ( gpPlayerData->isUsingImperialUnits() ) {
        distance *= kKilometersToMiles;
        unitHashcode = kMilesHashcode;
    }
    sprintf( pOutText, "%.1f %s", distance,
             gpDatabase->getStringByHashcode( 0, unitHashcode ) );
}

void MngTracRoad::solveLeg( const int32_t legIndex, const bool bIgnoreOneWay ) {
    // FUN_00784ce0
    AiRoadLocation *pWaypoint = getWaypoint( legIndex );
    pWaypoint->It.T = pWaypoint->T;

    AiPath &path = paths[legIndex];
    if ( legIndex == 0 ) {
        path.setStart( getWaypoint( 0 )->It, 0 );
    } else {
        const AiPath &previousPath = paths[legIndex - 1];
        path.setStart( previousPath.getEnd(), previousPath.getEndSide() );
    }

    const int32_t nextIndex = ( legIndex == getNumWaypoints() - 1 )
                                  ? getLoopWaypointCount() - 1
                                  : legIndex + 1;
    AiRoadLocation *pNextWaypoint = getWaypoint( nextIndex );
    pNextWaypoint->It.T = pNextWaypoint->T;
    path.setEnd( pNextWaypoint->It );
    path.setIgnoreOneWay( bIgnoreOneWay );
    if ( path.solve( legIndex != 0 ) == AiPathSolver::S_Success ) {
        legLengths[legIndex] = static_cast<float>( path.getTotalDistance() );
        path.computeBounds();
    }
}

void MngTracRoad::computeViewBox() {
    // FUN_00784190
    Eigen::Vector4f corners[4];
    ScreenToWorldGround( Eigen::Vector2f( 0.0f, 0.0f ), corners[0] );
    ScreenToWorldGround( Eigen::Vector2f( 0.0f, 1.0f ), corners[1] );
    ScreenToWorldGround( Eigen::Vector2f( 1.0f, 1.0f ), corners[2] );
    ScreenToWorldGround( Eigen::Vector2f( 1.0f, 0.0f ), corners[3] );

    viewBoxMin = Eigen::Vector4f( kViewBoxExtent, 0.0f, kViewBoxExtent, 1.0f );
    viewBoxMax =
        Eigen::Vector4f( -kViewBoxExtent, 0.0f, -kViewBoxExtent, 1.0f );
    for ( const Eigen::Vector4f &corner : corners ) {
        viewBoxMin.x() = std::min( viewBoxMin.x(), corner.x() );
        viewBoxMin.z() = std::min( viewBoxMin.z(), corner.z() );
        viewBoxMax.x() = std::max( viewBoxMax.x(), corner.x() );
        viewBoxMax.z() = std::max( viewBoxMax.z(), corner.z() );
    }
}

void MngTracRoad::drawLeg( const AiPath &path ) {
    // FUN_00784900
    if ( path.getStatus() != AiPathSolver::S_Success ) {
        return;
    }

    for ( const AiRouteElem *pElem = path.getHead(); pElem != nullptr;
          pElem = pElem->pNext ) {
        // FUN_007833d0
        if ( pElem->BoundsMinX > viewBoxMax.x() ||
             pElem->BoundsMinZ > viewBoxMax.z() ||
             viewBoxMin.x() > pElem->BoundsMaxX ||
             viewBoxMin.z() > pElem->BoundsMaxZ ) {
            continue;
        }

        AiRoadIt start;
        AiRoadIt end;
        pElem->From.unpack( start );
        pElem->To.unpack( end );
        drawRoadSpan( start, end );

        if ( pElem->pNext != nullptr ) {
            AiRoadIt nextStart;
            pElem->pNext->From.unpack( nextStart );
            drawJoin( end, nextStart );
        }
    }
}

void MngTracRoad::addSpanPoint( const AiRoadIt &it, const AiRoadIt &end ) {
    AiRoadIt curveEnd = it;
    curveEnd.T = 1.0f;
    if ( !it.isBefore( end ) ) {
        pPoints[numPoints++] = end.computePosition( -1 );
    } else if ( curveEnd.isBefore( end ) ) {
        pPoints[numPoints++] = curveEnd.computePosition( -1 );
    }
}

void MngTracRoad::drawRoadSpan( const AiRoadIt &start, const AiRoadIt &end ) {
    // FUN_00784520
    AiRoadIt it = start;
    drawList.setActiveMaterial( nullptr );
    numPoints = 0;

    if ( detailMode == 0 ) {
        pPoints[0] = it.computePosition( -1 );
        pPoints[1] = end.computePosition( -1 );
        numPoints = 2;
    } else if ( detailMode >= 1 && detailMode <= 3 ) {
        pPoints[numPoints++] = it.computePosition( -1 );
        it.T = 1.0f;
        addSpanPoint( it, end );

        const int32_t step = it.bWay ? 1 : -1;
        while ( it.isBefore( end ) && !it.isAtRoadEnd() ) {
            if ( detailMode == 1 ) {
                // Jump to the last curve of the entity
                if ( !it.isLastCurveInEntity() ) {
                    do {
                        it.IndWay += step;
                    } while ( !it.isLastCurveInEntity() );
                } else if ( !it.isAtRoadEnd() ) {
                    it.stepCurve( false );
                    while ( !it.isLastCurveInEntity() ) {
                        it.IndWay += step;
                    }
                }
            } else {
                it.stepCurve( false );
            }

            addSpanPoint( it, end );
            if ( numPoints >= kMaxNumPoints ) {
                return;
            }
        }
    }

    if ( numPoints >= 2 ) {
        const Eigen::Vector4f startDirection = start.computeDirection();
        const Eigen::Vector4f endDirection = end.computeDirection();
        buildRibbon( numPoints, width, &startDirection, &endDirection );
    }
}

void MngTracRoad::drawJoin( const AiRoadIt &from, const AiRoadIt &to ) {
    // FUN_00783f60
    numPoints = 0;
    const Eigen::Vector4f fromPosition = from.computePosition( -1 );
    const Eigen::Vector4f toPosition = to.computePosition( -1 );
    const Eigen::Vector4f fromDirection = from.computeDirection();
    const Eigen::Vector4f toDirection = to.computeDirection();
    const float handleLength = ( toPosition - fromPosition ).norm() * 0.5f;

    // FUN_0040a5c0
    CubicBezier curve;
    curve.ControlPoints[0] = fromPosition;
    curve.ControlPoints[1] = fromPosition - fromDirection * handleLength;
    curve.ControlPoints[2] = toPosition - toDirection * handleLength;
    curve.ControlPoints[3] = toPosition;

    int32_t numSamples = 2;
    switch ( detailMode ) {
    case 1:
        numSamples = 3;
        break;
    case 2:
        numSamples = 4;
        break;
    case 3:
        numSamples = 5;
        break;
    default:
        break;
    }
    const float lastSample = static_cast<float>( numSamples ) - 1.0f;
    for ( int32_t i = 0; i < numSamples; i++ ) {
        Eigen::Vector4f point =
            curve.evaluate( static_cast<float>( i ) / lastSample );
        point.w() = 1.0f;
        pPoints[numPoints++] = point;
    }

    if ( numPoints >= 2 ) {
        const Eigen::Vector4f startDirection = -from.computeDirection();
        const Eigen::Vector4f endDirection = to.computeDirection();
        buildRibbon( numPoints, width, &startDirection, &endDirection );
    }
}

void MngTracRoad::buildRibbon( const int32_t numRibbonPoints,
                               const float ribbonWidth,
                               const Eigen::Vector4f *pStartDirection,
                               const Eigen::Vector4f *pEndDirection ) {
    // FUN_007834a0
    numPrimitivesUsed++;
    numVerticesUsed += numRibbonPoints * 2;
    if ( numPrimitivesUsed > kMaxNumPrimitives ||
         numVerticesUsed > kMaxNumVertices ) {
        return;
    }

    const Eigen::Vector4f fallbackDirection( 1.0f, 0.0f, 0.0f, 0.0f );
    if ( numRibbonPoints < 2 || numRibbonPoints >= kMaxNumPoints ) {
        return;
    }

    const ePrimitiveType primitiveType =
        gbWireframeTraceRoad ? PT_LineStrip : PT_TriangleStrip;
    Matrix4x4f_t shiftMatrix = ComputeWorldShiftMatrix();
    drawList.setActiveTransformMatrix( shiftMatrix );
    drawList.beginPrimitive( primitiveType, numRibbonPoints * 2 );

    const float halfWidth = ribbonWidth * 0.5f;
    for ( int32_t i = 0; i < numRibbonPoints; i++ ) {
        drawList.setDiffuse(
            0, ColorRGBA( ( alpha << 24 ) | kRibbonColor.Color ) );

        Eigen::Vector4f direction;
        if ( i == 0 ) {
            direction = ( pStartDirection != nullptr )
                            ? *pStartDirection
                            : Eigen::Vector4f( pPoints[1] - pPoints[0] );
        } else if ( i == numRibbonPoints - 1 ) {
            direction = ( pEndDirection != nullptr )
                            ? *pEndDirection
                            : Eigen::Vector4f( pPoints[i] - pPoints[i - 1] );
        } else {
            direction = pPoints[i + 1] - pPoints[i - 1];
        }
        if ( direction.dot( direction ) <= kMinRibbonDirectionSq ) {
            direction = fallbackDirection;
        } else {
            direction.normalize();
        }

        // FUN_00406fa0 (up x direction)
        const Eigen::Vector4f side( direction.z(), 0.0f, -direction.x(), 0.0f );
        const Eigen::Vector4f left = pPoints[i] + side * -halfWidth;
        const Eigen::Vector4f right = pPoints[i] + side * halfWidth;
        drawList.pushVertex( left.x(), left.y() + height, left.z() );
        drawList.pushVertex( right.x(), right.y() + height, right.z() );
    }
    drawList.commitPrimitive();
}

MngTracRoadMap::MngTracRoadMap()
    : MngTracRoad(), numWaypoints( 0 ), pOwner( nullptr ), bLobby( false ) {
    // FUN_00783b80
}

MngTracRoadMap::~MngTracRoadMap() {}

AiRoadLocation *MngTracRoadMap::getWaypoint( const int32_t index ) {
    // FUN_00782fe0
    return pOwner->getRouteWaypoint( index );
}

void MngTracRoadMap::setNumWaypoints( const int32_t count ) {
    // FUN_0042c8a0
    numWaypoints = count;
}

bool MngTracRoadMap::isActive() const {
    // FUN_00783ac0
    if ( gpTestDriveInstance->getActiveGameMode() == GM_Lobby &&
         gpMngMapViewer != nullptr &&
         gpMngMapViewer->getState() == MngMapViewer::MS_Idle ) {
        return true;
    }
    if ( pOwner == nullptr ) {
        return false;
    }
    if ( FindGPSCamera()->getZoom() == CameraGPS::GZL_Space ||
         ( gMapActiveFilterMode & 1 ) == 0 ) {
        return false;
    }

    if ( pOwner->isChallengeInfoRequested() &&
         pOwner->isChallengeInfoFromCache() ) {
        const SpotMap *pSpot = pOwner->getSelectedSpot();
        if ( pSpot != nullptr && pSpot->Type == kSpotTypeChallenge ) {
            return GetSpotChallengeKind( pSpot ) != 1;
        }
    }

    const MapSpotRecord *pRecord = pOwner->getSelectedRecord();
    return pRecord != nullptr &&
           ( pRecord->Info.ChallengeFlags & kExtraChallengeRecordFlag ) != 0;
}

int32_t MngTracRoadMap::getNumWaypoints() const {
    // FUN_00783000
    return isActive() ? numWaypoints : 0;
}

bool MngTracRoadMap::isLoop() const {
    // FUN_00783230
    if ( gpTestDriveInstance->getActiveGameMode() == GM_Lobby ) {
        if ( bLobby ) {
            return gpScenaric->getSelectedEventParameters()
                       .getLoop()
                       .getValue() != 0;
        }
        if ( pOwner == nullptr ) {
            return false;
        }
    } else {
        const MapSpotRecord *pRecord = pOwner->getSelectedRecord();
        if ( pRecord != nullptr && ( pRecord->Info.ChallengeFlags &
                                     kExtraChallengeRecordFlag ) != 0 ) {
            return false;
        }
    }
    return pOwner->getChallengeInfo().getLoop().getValue() != 0;
}

int32_t MngTracRoadMap::getLoopWaypointCount() const {
    // FUN_00783210
    return int32_t(
        pOwner->getChallengeInfo().getLoopCheckpointIndex().getValue() );
}
