#include "shared.h"
#include "ai_nav_path.h"

#include "ai/ai_road_shape.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "core/cubic_bezier.h"
#include "core/maths_helper.h"
#include "game/game_object.h"
#include "game/vehicle/car_state.h"

static constexpr int32_t kPointCapacity = 0x20;
static constexpr int32_t kSpanCapacity = 0x20;
static constexpr int32_t kJunctionSpanMinFreePoints = 8;
static constexpr int32_t kRoadSpanMinFreePoints = 4;
static constexpr int32_t kJunctionSpanNumPoints = 8;
static constexpr int32_t kLaneChangeSpanNumPoints = 10;
static constexpr float kPointsPerCurve = 7.0f;           // DAT_00f41844
static constexpr float kInvPointsPerCurve = 1.0f / 7.0f; // DAT_00f416cc
static constexpr float kOneThird = 1.0f / 3.0f;          // DAT_00f41764
static constexpr float kMinTruncatedRemainder = 1.0f;    // FLOAT_00f4165c
static constexpr float kJunctionPointHeight = 0.2f;      // DAT_00f41730
static constexpr float kJunctionPointMarker = -1024.0f;  // DAT_00f417f8
static constexpr float kDefaultRoadSpeedLimit = 10.0f;   // DAT_00f4166c
static constexpr float kNoSpeedLimit = 10000.0f;         // DAT_00f416c4
static constexpr float kCurvatureProbeDistance = 10.0f;  // DAT_00faa788
static constexpr float kCurvatureRefreshWindow = 10.0f;  // DAT_00f4166c
static constexpr float kMinCurvature = 0.001f;           // DAT_00f4167c
static constexpr float kMinDirectionSq = 0.001f;         // DAT_00f4167c
static constexpr float kMinTangentLength = 1.0e-5f;      // DAT_00f41710
static constexpr float kGravity = 9.81f;                 // DAT_00f419bc
static constexpr float kCrestSpeedLimitScale = 1.0f;     // DAT_00faa824
static constexpr float kMinLookAhead = 1.0f;             // DAT_00faa79c
static constexpr float kLookAheadTime = 0.5f;            // DAT_00faa7a0
static constexpr float kTrafficBrakeMarginMax = 10.0f;   // DAT_00faa78c
static constexpr float kTrafficBrakeMarginScale = 1.0f;  // DAT_00faa790
static constexpr float kOtherBrakeMarginMax = 10.0f;     // DAT_00faa794
static constexpr float kOtherBrakeMarginScale = 1.0f;    // DAT_00faa798
static constexpr float kProjectionSnapMin = -0.01f;      // DAT_00f41f60
static constexpr float kProjectionSnapMax = 1.01f;       // DAT_00f41d38
static constexpr float kMaxOffPathDistanceSq = 100.0f;   // DAT_00f41708
static constexpr float kMaxJunctionFraction = 0.999f;    // DAT_00f41e54

static float SnapProjection( float t ) {
    if ( t >= kProjectionSnapMin && t < 0.0f ) {
        t = 0.0f;
    } else if ( t <= kProjectionSnapMax && t > 1.0f ) {
        t = 1.0f;
    }
    return t;
}

bool ComputeCircle( Eigen::Vector2f p1,
                    Eigen::Vector2f p2,
                    Eigen::Vector2f p3,
                    Eigen::Vector2f &outCenter,
                    float &outRadius ) {
    // FUN_0090a020
    static constexpr float kEpsilon = 1.0e-4f; // DAT_00f41760

    const Eigen::Vector2f in1 = p1;
    const Eigen::Vector2f in2 = p2;
    const Eigen::Vector2f in3 = p3;

    if ( fabsf( p1.x() - p2.x() ) < kEpsilon ||
         fabsf( p1.y() - p2.y() ) < kEpsilon ) {
        std::swap( p1, p3 );
    }
    if ( fabsf( p2.x() - p3.x() ) < kEpsilon ) {
        std::swap( p1, p2 );
    }

    bool bValid = true;
    float slopeA = 0.0f;
    float slopeB = 0.0f;
    if ( fabsf( p2.x() - p1.x() ) >= kEpsilon ) {
        slopeA = ( p2.y() - p1.y() ) / ( p2.x() - p1.x() );
    } else {
        bValid = false;
    }
    if ( fabsf( p2.x() - p3.x() ) >= kEpsilon ) {
        slopeB = ( p3.y() - p2.y() ) / ( p3.x() - p2.x() );
    } else {
        bValid = false;
    }
    if ( fabsf( slopeA ) < kEpsilon && slopeB < kEpsilon ) {
        bValid = false;
    }

    if ( fabsf( slopeA - slopeB ) < kEpsilon || !bValid ) {
        outCenter = ( in1 + in2 + in3 ) * ( 1.0f / 3.0f );
        outRadius = std::max(
            { in1.squaredNorm(), in2.squaredNorm(), in3.squaredNorm() } );
        return false;
    }

    const float centerX =
        ( slopeA * slopeB * ( p1.y() - p3.y() ) + slopeB * ( p1.x() + p2.x() ) -
          slopeA * ( p2.x() + p3.x() ) ) /
        ( ( slopeB - slopeA ) * 2.0f );
    float centerY;
    if ( fabsf( slopeA ) > kEpsilon ) {
        centerY = ( p1.y() + p2.y() ) * 0.5f -
                  ( centerX - ( p1.x() + p2.x() ) * 0.5f ) / slopeA;
    } else {
        centerY = ( p2.y() + p3.y() ) * 0.5f -
                  ( centerX - ( p2.x() + p3.x() ) * 0.5f ) / slopeB;
    }
    outCenter = Eigen::Vector2f( centerX, centerY );
    outRadius = ( p1 - outCenter ).norm();
    return true;
}

static void PushSpan( NavRing<NavPath::Span> &spans,
                      const NavPath::Span &span ) {
    // FUN_0040a750
    if ( spans.Count < spans.Capacity ) {
        spans.pushBack( span );
    }
}

NavPath::NavPath()
    : pointIdCounter( 0 ),
      points(),
      dummyPoint(),
      totalLength( 0.0f ),
      bBatchAppend( false ),
      pVehicle( nullptr ),
      nearestIndex( 0 ),
      progress( 0.0f ),
      spans() {
    // FUN_0095efd0
}

void NavPath::initialize( AiVehicle *pOwner ) {
    // FUN_0095e120
    points.allocate( kPointCapacity );
    reset( false );
    pVehicle = pOwner;
    reset( false );
}

void NavPath::attach( AiVehicle *pOwner ) {
    // FUN_00940590
    initialize( pOwner );
    spans.allocate( kSpanCapacity );
}

void NavPath::release() {
    // FUN_00940540
    spans.Data.clear();
    spans.Count = 0;
    spans.Capacity = 0;
    points.Data.clear();
    points.Count = 0;
    points.Capacity = 0;
}

void NavPath::reset( const bool bFull ) {
    // FUN_009404e0
    nearestIndex = 0;
    progress = 0.0f;
    if ( bFull ) {
        pointIdCounter = 0;
        totalLength = 0.0f;
        bBatchAppend = false;
        points.clear();
        spans.clear();
    }
}

NavPath::Point &NavPath::current() {
    // FUN_0040a7c0
    return points.at( nearestIndex );
}

const NavPath::Point &NavPath::current() const {
    // FUN_0040a7c0
    return points.at( nearestIndex );
}

bool NavPath::hasRoom() const {
    // FUN_0040a650
    return points.Capacity >= points.Count + kJunctionSpanMinFreePoints &&
           spans.Count != spans.Capacity;
}

NavPath::Point &NavPath::front() {
    return ( points.Count > 0 ) ? points.at( 0 ) : dummyPoint;
}

NavPath::Point &NavPath::back() {
    return ( points.Count > 0 ) ? points.last() : dummyPoint;
}

const NavPath::Point &NavPath::front() const {
    return ( points.Count > 0 ) ? points.at( 0 ) : dummyPoint;
}

const NavPath::Point &NavPath::back() const {
    return ( points.Count > 0 ) ? points.last() : dummyPoint;
}

void NavPath::push( Point &point, const bool bForce ) {
    // FUN_004082c0
    if ( points.Count == points.Capacity ) {
        return;
    }
    if ( points.Count != 0 && !bForce ) {
        Eigen::Vector4f delta = back().Position - point.Position;
        delta.y() = 0.0f;
        if ( Dot4( delta, delta ) < 1.0f ) {
            return;
        }
    }

    point.Id = ++pointIdCounter;
    points.pushBack( point );
    onPointAdded();
}

void NavPath::onPointAdded() {
    // FUN_00408f40
    Point &last = back();
    last.Curvature = 0.0f;
    last.Radius = -1.0f;
    last.field_0x28 = 0.0f;
    if ( points.Count == 1 ) {
        last.Len = 0.0f;
        return;
    }

    last.Len =
        ( last.Position - points.at( points.Count - 2 ).Position ).norm();
    totalLength += last.Len;

    float distance = last.Len;
    int32_t index = points.Count - 2;
    while ( true ) {
        const Point &point = points.at( index );
        if ( !( point.Curvature > 0.0f ) && !bBatchAppend ) {
            recomputeRadius( index );
        }
        const float previousDistance = distance;
        distance += point.Len;
        index--;
        if ( !( previousDistance < kCurvatureRefreshWindow ) || index < 0 ) {
            break;
        }
    }
}

float NavPath::popFront( int32_t numPoints ) {
    // FUN_00940360
    const float removed = popFrontPoints( numPoints );
    if ( points.Count == 0 ) {
        spans.clear();
        return removed;
    }

    const int32_t firstId = front().Id;
    while ( spans.Count != 0 ) {
        Span &span = spans.at( 0 );
        if ( span.LastPointIndex >= firstId ) {
            span.SampleFirst = span.SampleLast - span.LastPointIndex + firstId;
            break;
        }
        spans.popFront();
    }
    return removed;
}

float NavPath::popFrontPoints( int32_t numPoints ) {
    // FUN_00408aa0
    if ( numPoints <= 0 ) {
        return 0.0f;
    }

    float removed = 0.0f;
    while ( numPoints-- > 0 ) {
        removed += front().Len;
        if ( points.Count > 0 ) {
            points.popFront();
        }
    }
    removed += front().Len;
    front().Len = 0.0f;
    totalLength -= removed;
    return removed;
}

int32_t NavPath::getNextSpanFirstIndex() const {
    return ( ( spans.Count != 0 ) ? spans.last().LastPointIndex
                                  : pointIdCounter ) +
           1;
}

bool NavPath::generatePoints( const bool bKeepIds, const Span &span ) {
    // FUN_009405e0
    if ( !bKeepIds ) {
        pointIdCounter = span.SampleFirst + span.FirstPointIndex - 1;
    }

    const int32_t numPointsBefore = points.Count;
    float junctionHeight = 0.0f;
    if ( span.Type == PT_Junction ) {
        if ( points.Capacity < points.Count + kJunctionSpanMinFreePoints ||
             spans.Count == spans.Capacity ) {
            return false;
        }
        junctionHeight = span.pJunction->Position.Y + kJunctionPointHeight;
    }

    CubicBezier curve;
    for ( int32_t i = 0; i < 4; i++ ) {
        curve.ControlPoints[i] =
            Eigen::Vector4f( span.Keys[i].x(), 0.0f, span.Keys[i].y(), 1.0f );
    }

    const int32_t numSamples = span.LastPointIndex - span.FirstPointIndex + 1;
    const float sampleOffset = ( numPointsBefore != 0 )
                                   ? 1.0f / static_cast<float>( numSamples )
                                   : 0.0f;
    const int32_t divisor = ( numSamples > 1 ) ? numSamples - 1 : numSamples;

    bBatchAppend = true;
    for ( int32_t i = span.SampleFirst; i <= span.SampleLast; i++ ) {
        const float s = static_cast<float>( i ) /
                            static_cast<float>( divisor ) *
                            ( 1.0f - sampleOffset ) +
                        sampleOffset;
        const Eigen::Vector4f key = curve.evaluate( s );

        Point point;
        if ( span.Type == PT_Junction ) {
            point.JunctionLocalX = key.x();
            point.JunctionLocalZ = key.z();
            point.Type = PT_Junction;
            point.pJunction = span.pJunction;
            point.Position =
                Eigen::Vector4f( key.x(), junctionHeight, key.z(), 1.0f );
            point.WidthRatio = kJunctionPointMarker;
        } else if ( span.Type == PT_Road ) {
            point.Type = PT_Road;
            const AiRoadShape *pShape = span.pRoad->getShape();
            pShape->computeUniformIt( key.x(), point.It );
            const bool bWay = span.Keys[3].x() >= span.Keys[0].x();
            if ( bWay != point.It.bWay ) {
                point.It.T = 1.0f - point.It.T;
            }
            point.It.bWay = bWay;
            point.JunctionLocalX = key.z();
            point.Position = point.It.computeBankedWidthPosition( key.z() );
        }

        point.RoadSpeedLimit = ( span.SpeedLimit >= 0.0f )
                                   ? span.SpeedLimit
                                   : kDefaultRoadSpeedLimit;
        push( point, true );
    }
    bBatchAppend = false;

    recomputeAllRadii();
    for ( int32_t i = 0; i < points.Count; i++ ) {
        points.at( i ).CrestSpeedLimit =
            computeCrestSpeedLimit( i ) * kCrestSpeedLimitScale;
    }
    back().RoadSpeedLimit = span.EndSpeedLimit;
    return true;
}

bool NavPath::appendSpan( const AiRoadIt &from,
                          const AiRoadIt &to,
                          float widthRatioFrom,
                          float widthRatioTo,
                          AiRoadIt *pOutEnd,
                          const float speedLimit ) {
    // FUN_00940d60
    if ( std::isnan( widthRatioFrom ) ) {
        widthRatioFrom = 0.0f;
    }
    if ( std::isnan( widthRatioTo ) ) {
        widthRatioTo = 0.0f;
    }

    const AiRoadShape *pShape = from.pRoad->getShape();
    const float fractionFrom = pShape->computeUniformRoadFraction( from );
    float fractionTo = pShape->computeUniformRoadFraction( to );

    const int32_t numFreePoints = points.Capacity - points.Count;
    const float curveStep = 1.0f / static_cast<float>( pShape->NumCurves );
    const float maxSpan =
        static_cast<float>( numFreePoints ) * kInvPointsPerCurve * curveStep;
    const float spanLength = fabsf( fractionTo - fractionFrom );

    int32_t numPoints;
    if ( spanLength > maxSpan ) {
        numPoints = numFreePoints;
        float truncatedFraction;
        float remainder;
        if ( fractionTo >= fractionFrom ) {
            truncatedFraction = maxSpan + fractionFrom;
            remainder = fractionTo - truncatedFraction;
        } else {
            truncatedFraction = fractionFrom - maxSpan;
            remainder = truncatedFraction - fractionTo;
        }
        if ( remainder * pShape->TotalLength > kMinTruncatedRemainder ) {
            fractionTo = truncatedFraction;
        }
    } else {
        numPoints = static_cast<int32_t>(
            ceil( spanLength / curveStep * kPointsPerCurve ) );
        if ( numPoints == 1 ) {
            numPoints = 2;
        }
    }

    if ( numPoints <= 1 || spans.Count == spans.Capacity ||
         numFreePoints < kRoadSpanMinFreePoints ) {
        if ( pOutEnd != nullptr ) {
            *pOutEnd = from;
        }
        return false;
    }

    if ( pOutEnd != nullptr ) {
        if ( maxSpan >= spanLength ) {
            *pOutEnd = to;
        } else {
            pShape->computeUniformIt( fractionTo, *pOutEnd );
        }
        if ( from.bWay != pOutEnd->bWay ) {
            pOutEnd->T = 1.0f - pOutEnd->T;
        }
        pOutEnd->bWay = from.bWay;
    }

    Span span;
    span.FirstPointIndex = getNextSpanFirstIndex();
    span.LastPointIndex = span.FirstPointIndex + numPoints - 1;
    span.SampleFirst = 0;
    span.SampleLast = numPoints - 1;
    span.Type = PT_Road;
    span.pRoad = from.pRoad;
    span.pJunction = nullptr;
    span.Keys[0] = Eigen::Vector2f( fractionFrom, widthRatioFrom );
    span.Keys[1] = Eigen::Vector2f(
        fractionFrom * 2.0f * kOneThird + fractionTo * kOneThird,
        widthRatioFrom * 2.0f * kOneThird + widthRatioTo * kOneThird );
    span.Keys[2] = Eigen::Vector2f(
        fractionFrom * kOneThird + fractionTo * 2.0f * kOneThird,
        widthRatioFrom * kOneThird + widthRatioTo * 2.0f * kOneThird );
    span.Keys[3] = Eigen::Vector2f( fractionTo, widthRatioTo );
    span.SpeedLimit = speedLimit;
    span.EndSpeedLimit = speedLimit;
    if ( !generatePoints( false, span ) ) {
        return false;
    }
    PushSpan( spans, span );
    return true;
}

bool NavPath::appendJunctionSpan( const AiRoadIt &from,
                                  const float widthRatioFrom,
                                  const AiRoadIt &to,
                                  const float widthRatioTo,
                                  const float speedLimit ) {
    // FUN_009410e0
    const Eigen::Vector4f start = from.computeWidthPosition( widthRatioFrom );
    const Eigen::Vector4f end = to.computeWidthPosition( widthRatioTo );
    const Eigen::Vector4f startDirection = from.computeDirection();
    const Eigen::Vector4f endDirection = to.computeDirection() * -1.0f;

    const float handleLength = ( start - end ).norm() * 0.5f;
    const Eigen::Vector4f startHandle =
        start + startDirection / startDirection.norm() * handleLength;
    const Eigen::Vector4f endHandle =
        end + endDirection / endDirection.norm() * handleLength;

    Span span;
    span.FirstPointIndex = getNextSpanFirstIndex();
    span.LastPointIndex = span.FirstPointIndex + kJunctionSpanNumPoints - 1;
    span.SampleFirst = 0;
    span.SampleLast = kJunctionSpanNumPoints - 1;
    span.Type = PT_Junction;
    span.pRoad = nullptr;
    span.pJunction =
        from.bWay ? from.pRoad->pJunctionEnd : from.pRoad->pJunctionBegin;
    span.Keys[0] = Eigen::Vector2f( start.x(), start.z() );
    span.Keys[1] = Eigen::Vector2f( startHandle.x(), startHandle.z() );
    span.Keys[2] = Eigen::Vector2f( endHandle.x(), endHandle.z() );
    span.Keys[3] = Eigen::Vector2f( end.x(), end.z() );
    span.SpeedLimit = speedLimit;
    span.EndSpeedLimit = speedLimit;
    if ( !generatePoints( true, span ) ) {
        return false;
    }
    PushSpan( spans, span );
    return true;
}

bool NavPath::appendLaneChangeSpan( const AiRoadIt &from,
                                    const AiRoadIt &to,
                                    float widthRatioFrom,
                                    float widthRatioTo,
                                    const float speedLimit,
                                    const float handleDistance ) {
    // FUN_00941370
    if ( std::isnan( widthRatioFrom ) ) {
        widthRatioFrom = 0.0f;
    }
    if ( std::isnan( widthRatioTo ) ) {
        widthRatioTo = 0.0f;
    }

    const AiRoadShape *pShape = from.pRoad->getShape();
    const float fractionFrom = pShape->computeUniformRoadFraction( from );
    const float fractionTo = pShape->computeUniformRoadFraction( to );
    float handle = ( fractionTo - fractionFrom ) * 0.5f;
    const float maxHandle = handleDistance / pShape->TotalLength;
    if ( handle > maxHandle ) {
        handle = maxHandle;
    }

    Span span;
    span.FirstPointIndex = getNextSpanFirstIndex();
    span.LastPointIndex = span.FirstPointIndex + kLaneChangeSpanNumPoints - 1;
    span.SampleFirst = 0;
    span.SampleLast = kLaneChangeSpanNumPoints - 1;
    span.Type = PT_Road;
    span.pRoad = from.pRoad;
    span.pJunction = nullptr;
    span.Keys[0] = Eigen::Vector2f( fractionFrom, widthRatioFrom );
    span.Keys[1] = Eigen::Vector2f( fractionFrom + handle, widthRatioFrom );
    span.Keys[2] = Eigen::Vector2f( fractionTo - handle, widthRatioTo );
    span.Keys[3] = Eigen::Vector2f( fractionTo, widthRatioTo );
    span.SpeedLimit = speedLimit;
    span.EndSpeedLimit = speedLimit;
    if ( !generatePoints( false, span ) ) {
        return false;
    }
    PushSpan( spans, span );
    return true;
}

bool NavPath::appendToRoadExit( const AiRoadIt &from,
                                AiRoadIt *pOutEnd,
                                const float widthRatio,
                                const float speedLimit ) {
    // FUN_009414e0
    AiRoadIt exit;
    exit.setRoadExit( from.pRoad, from.bWay );
    return appendSpan( from, exit, widthRatio, widthRatio, pOutEnd,
                       speedLimit );
}

void NavPath::recomputeRadius( const int32_t index ) {
    // FUN_004090b0
    Point &point = points.at( index );
    point.Curvature = computeCurvature( index, true );
    const float absCurvature = fabsf( point.Curvature );
    point.Radius =
        ( absCurvature < kMinCurvature ) ? -1.0f : 1.0f / absCurvature;
}

void NavPath::recomputeAllRadii() {
    // FUN_00409810
    for ( int32_t i = 0; i < points.Count; i++ ) {
        recomputeRadius( i );
    }
}

float NavPath::computeCurvature( const int32_t index,
                                 const bool bSigned ) const {
    // FUN_004088a0
    if ( index < 1 || index > points.Count - 2 ) {
        return 0.0f;
    }

    Eigen::Vector4f before;
    if ( !computeOffsetPosition( index, -kCurvatureProbeDistance, before ) ) {
        return 0.0f;
    }
    const Eigen::Vector4f &current = points.at( index ).Position;
    Eigen::Vector4f after;
    if ( !computeOffsetPosition( index, kCurvatureProbeDistance, after ) ) {
        return 0.0f;
    }

    Eigen::Vector2f center;
    float radius;
    ComputeCircle( Eigen::Vector2f( before.x(), before.z() ),
                   Eigen::Vector2f( current.x(), current.z() ),
                   Eigen::Vector2f( after.x(), after.z() ), center, radius );
    const float curvature = 1.0f / radius;
    if ( !bSigned ) {
        return curvature;
    }

    const float side =
        ( before.x() - center.x() ) * ( after.z() - center.y() ) -
        ( before.z() - center.y() ) * ( after.x() - center.x() );
    return ( side < 0.0f ) ? -curvature : curvature;
}

bool NavPath::computeOffsetPosition( int32_t index,
                                     const float distance,
                                     Eigen::Vector4f &outPosition ) const {
    // FUN_00408c20
    bool bBackward = false;
    bool bForward = false;
    if ( distance > 0.0f ) {
        if ( index + 1 >= points.Count ) {
            outPosition.setZero();
            return false;
        }
        bForward = true;
    } else if ( distance < 0.0f ) {
        if ( index <= 0 ) {
            outPosition.setZero();
            return false;
        }
        bBackward = true;
    } else {
        if ( index < 0 || index >= points.Count ) {
            outPosition.setZero();
            return false;
        }
        outPosition = points.at( index ).Position;
    }

    const float absDistance = fabsf( distance );
    if ( bBackward ) {
        const Point *pFrom = &points.at( index - 1 );
        const Point *pTo = &points.at( index );
        float accumulated = pTo->Len;
        while ( absDistance > accumulated && index > 1 ) {
            index--;
            pFrom = &points.at( index - 1 );
            pTo = &points.at( index );
            accumulated += pTo->Len;
        }
        const float t = ( accumulated - absDistance ) / pTo->Len;
        outPosition = pTo->Position * t + pFrom->Position * ( 1.0f - t );
        outPosition.w() = 1.0f;
        return true;
    }

    if ( !bForward ) {
        return false;
    }

    const Point *pFrom = &points.at( index );
    const Point *pTo = &points.at( index + 1 );
    float accumulated = pTo->Len;
    while ( absDistance > accumulated && index + 2 < points.Count ) {
        index++;
        pFrom = &points.at( index );
        pTo = &points.at( index + 1 );
        accumulated += pTo->Len;
    }
    if ( absDistance > accumulated ) {
        return false;
    }

    const float t = ( pTo->Len - ( accumulated - absDistance ) ) / pTo->Len;
    outPosition = pTo->Position * t + pFrom->Position * ( 1.0f - t );
    outPosition.w() = 1.0f;
    return true;
}

Eigen::Vector4f NavPath::computeTangent( const int32_t index ) const {
    // FUN_004084a0
    const Eigen::Vector4f kDefaultTangent( 1.0f, 0.0f, 0.0f, 0.0f );
    if ( points.Count <= 1 || index >= points.Count ) {
        return kDefaultTangent;
    }

    Eigen::Vector4f tangent;
    float length;
    if ( index <= 0 ) {
        tangent = points.at( index + 1 ).Position - points.at( index ).Position;
        length = points.at( index + 1 ).Len;
    } else if ( index + 1 < points.Count ) {
        tangent =
            points.at( index + 1 ).Position - points.at( index - 1 ).Position;
        length = points.at( index ).Len + points.at( index + 1 ).Len;
    } else {
        tangent = points.at( index ).Position - points.at( index - 1 ).Position;
        length = points.at( index ).Len;
    }

    if ( length <= kMinTangentLength ) {
        return kDefaultTangent;
    }
    return tangent / length;
}

float NavPath::computeCrestSpeedLimit( const int32_t index ) const {
    // FUN_0095e4d0
    if ( index + 1 >= points.Count ) {
        return -1.0f;
    }

    const Eigen::Vector4f &from = points.at( index ).Position;
    const Eigen::Vector4f &to = points.at( index + 1 ).Position;
    const Eigen::Vector4f tangent = computeTangent( index );
    if ( tangent.norm() <= 1.0e-4f ) {
        return -1.0f;
    }

    Eigen::Vector4f tangentFlat = tangent;
    tangentFlat.y() = 0.0f;
    const float pitch = atan2f( tangent.y(), tangentFlat.norm() );

    const float drop = to.y() - from.y();
    Eigen::Vector4f delta = to - from;
    delta.y() = 0.0f;
    const float horizontal = delta.norm();

    const float rise = horizontal * tanf( pitch );
    if ( rise == drop ) {
        return -1.0f;
    }
    const float speedSq = horizontal * kGravity / ( rise - drop );
    if ( speedSq < 1.0f ) {
        return -1.0f;
    }
    return sqrtf( speedSq ) / cosf( pitch );
}

bool NavPath::locate( const float distance,
                      const Point *&pOutA,
                      const Point *&pOutB,
                      float &outT ) const {
    // FUN_00408a10
    float accumulated = 0.0f;
    for ( int32_t i = 1; i < points.Count; i++ ) {
        const float next = accumulated + points.at( i ).Len;
        if ( next >= distance ) {
            pOutA = &points.at( i - 1 );
            pOutB = &points.at( i );
            outT = ( distance - accumulated ) / ( next - accumulated );
            return true;
        }
        accumulated = next;
    }
    return false;
}

Eigen::Vector4f NavPath::computePositionAt( const float distance ) const {
    // FUN_004083a0
    const Point *pA = nullptr;
    const Point *pB = nullptr;
    float t = 0.0f;
    if ( !locate( distance, pA, pB, t ) ) {
        return back().Position;
    }
    return pB->Position * t + pA->Position * ( 1.0f - t );
}

void NavPath::computePositionAndTwistAt( const float distance,
                                         Eigen::Vector4f &outPosition,
                                         float &outTwist ) const {
    // FUN_0095e1e0
    const Point *pA = nullptr;
    const Point *pB = nullptr;
    float t = 0.0f;
    if ( !locate( distance, pA, pB, t ) ) {
        outPosition = back().Position;
        outTwist = 0.0f;
        return;
    }

    outPosition = pB->Position * t + pA->Position * ( 1.0f - t );
    const float twistA = ( pA->Type == PT_Road ) ? pA->It.computeTwist() : 0.0f;
    const float twistB = ( pB->Type == PT_Road ) ? pB->It.computeTwist() : 0.0f;
    outTwist = ( 1.0f - t ) * twistA + t * twistB;
}

float NavPath::computeLookAhead() const {
    // FUN_0095e330
    return std::max( kMinLookAhead,
                     pVehicle->getInfos().Speed * kLookAheadTime );
}

float NavPath::computePointSpeed( const Point &point ) const {
    float turningSpeed = pVehicle->computeTurningSpeed( point.Radius );
    if ( turningSpeed < 0.0f ) {
        turningSpeed = kNoSpeedLimit;
    }
    const float crestSpeed = ( point.CrestSpeedLimit < 0.0f )
                                 ? kNoSpeedLimit
                                 : point.CrestSpeedLimit;
    const float roadSpeed =
        ( point.RoadSpeedLimit < 0.0f ) ? kNoSpeedLimit : point.RoadSpeedLimit;
    return std::min( { turningSpeed, crestSpeed, roadSpeed } );
}

float NavPath::computeCurrentSpeedLimit() const {
    // FUN_0095e6f0
    if ( points.Count <= 0 ) {
        return 0.0f;
    }

    const int32_t index =
        ( nearestIndex > 0 ) ? nearestIndex - 1 : nearestIndex;
    const float speed = computePointSpeed( points.at( index ) );
    if ( speed >= kNoSpeedLimit || speed < 0.0f ) {
        return pVehicle->getInfos().MaxSpeed;
    }
    return speed;
}

void NavPath::computeSpeedProfile( int32_t &outCount,
                                   SpeedSample *pOutSamples,
                                   const int32_t maxSamples ) const {
    // FUN_0095e790
    outCount = 0;
    float distance = -progress;
    for ( int32_t i = 0; i < points.Count && outCount < maxSamples; i++ ) {
        const Point &point = points.at( i );
        distance += point.Len;
        pOutSamples[outCount].Distance = distance;

        const float speed = computePointSpeed( point );
        pOutSamples[outCount].Speed = ( speed < kNoSpeedLimit ) ? speed : -1.0f;
        outCount++;
    }
}

bool NavPath::computeTarget( Eigen::Vector4f &outDirection,
                             float &outSpeed,
                             float maxSpeed,
                             const float blend ) const {
    // FUN_0095f760
    const AiVehicleInfo &infos = pVehicle->getInfos();
    if ( maxSpeed < 0.0f ) {
        maxSpeed = infos.MaxSpeed;
    }

    if ( points.Count <= 0 ) {
        outDirection = infos.GroundMatrix.row( 2 ).transpose();
        outSpeed = 0.0f;
        return false;
    }

    Eigen::Vector4f direction =
        computePositionAt( progress + computeLookAhead() ) -
        infos.GroundMatrix.row( 3 ).transpose();
    direction.w() = 0.0f;
    if ( Dot4( direction, direction ) < kMinDirectionSq ||
         std::isnan( direction.x() ) ) {
        outDirection = infos.GroundMatrix.row( 2 ).transpose();
        outSpeed = 0.0f;
        return false;
    }
    outDirection = direction;

    int32_t numSamples = 0;
    SpeedSample samples[kPointCapacity];
    computeSpeedProfile( numSamples, samples, kPointCapacity );
    const float slope = ( infos.LastRoadPos.pRoad != nullptr )
                            ? infos.LastRoadPos.computeSlope()
                            : 0.0f;

    float referenceSpeed;
    float marginScale;
    float marginMax;
    if ( pVehicle->getLocomotionType() == 0 ) {
        marginScale = kTrafficBrakeMarginScale;
        marginMax = kTrafficBrakeMarginMax;
        referenceSpeed = std::max( computeCurrentSpeedLimit(), infos.Speed );
        const float doubleSpeed = infos.Speed * 2.0f;
        if ( referenceSpeed > doubleSpeed ) {
            referenceSpeed = std::max( doubleSpeed, infos.MaxSpeed * 0.1f );
        }
    } else {
        marginScale = kOtherBrakeMarginScale;
        marginMax = kOtherBrakeMarginMax;
        referenceSpeed = std::max( infos.MaxSpeed * 0.1f, infos.Speed );
    }

    int32_t bestIndex = -1;
    float bestMargin = kNoSpeedLimit;
    float bestSpeed = 0.0f;
    for ( int32_t i = 0; i < numSamples; i++ ) {
        const float speed = samples[i].Speed;
        const float distance = samples[i].Distance;
        if ( speed < 0.0f || !( referenceSpeed > speed ) ||
             !( distance > 0.0f ) ) {
            continue;
        }

        const float margin = distance - pVehicle->computeBrakingDistance(
                                            referenceSpeed, speed, slope ) *
                                            marginScale;
        if ( bestMargin > margin ||
             ( marginMax >= margin && bestSpeed > speed ) ) {
            bestIndex = i;
            bestMargin = margin;
            bestSpeed = speed;
        }
    }

    float speed;
    if ( numSamples > 0 && samples[0].Distance <= 0.0f ) {
        speed = ( samples[0].Speed < 0.0f ) ? infos.MaxSpeed : samples[0].Speed;
    } else {
        speed = computeCurrentSpeedLimit();
    }
    if ( bestIndex >= 0 && marginMax >= bestMargin && bestSpeed >= 0.0f ) {
        speed = bestSpeed;
    }

    if ( speed > maxSpeed ) {
        const float coef = std::clamp( blend, 0.0f, 1.0f );
        outSpeed = ( 1.0f - coef ) * speed + coef * maxSpeed;
    } else {
        outSpeed = speed;
    }
    return true;
}

void NavPath::updateProgress( const Matrix4x4f_t &groundMatrix ) {
    // FUN_00960d20
    if ( points.Count == 0 ) {
        return;
    }

    const Eigen::Vector4f position = groundMatrix.row( 3 ).transpose();

    int32_t nearest = nearestIndex;
    float bestDistanceSq =
        ( position - points.at( nearest ).Position ).squaredNorm();
    for ( int32_t i = 0; i < points.Count; i++ ) {
        const float distanceSq =
            ( position - points.at( i ).Position ).squaredNorm();
        if ( distanceSq <= bestDistanceSq ) {
            bestDistanceSq = distanceSq;
            nearest = i;
        }
    }
    const Point &nearestPoint = points.at( nearest );

    float tPrevious = -1.0f;
    float tNext = -1.0f;
    Eigen::Vector4f residualPrevious = Eigen::Vector4f::Zero();
    Eigen::Vector4f residualNext = Eigen::Vector4f::Zero();
    if ( nearest > 0 ) {
        const Eigen::Vector4f &a = points.at( nearest - 1 ).Position;
        const Eigen::Vector4f &b = nearestPoint.Position;
        tPrevious = SnapProjection( Project( position, a, b ) );
        residualPrevious = position - ( a + ( b - a ) * tPrevious );
    }
    if ( nearest + 1 < points.Count ) {
        const Eigen::Vector4f &a = nearestPoint.Position;
        const Eigen::Vector4f &b = points.at( nearest + 1 ).Position;
        tNext = SnapProjection( Project( position, a, b ) );
        residualNext = position - ( a + ( b - a ) * tNext );
    }

    AiVehicle *pOwner = pVehicle;
    CarState *pCarState = pOwner->getCarState();

    int32_t segmentIndex;
    float t;
    bool bOnSegment = true;
    if ( tNext >= 0.0f && tNext <= 1.0f ) {
        segmentIndex = nearest + 1;
        t = tNext;
        progress = points.at( nearest + 1 ).Len * tNext;
    } else if ( tPrevious >= 0.0f && tPrevious < 1.0f ) {
        segmentIndex = nearest;
        t = tPrevious;
        progress = -( 1.0f - tPrevious ) * nearestPoint.Len;
    } else {
        bOnSegment = false;
        segmentIndex = ( nearest == 0 && points.Count > 1 ) ? 1 : nearest;
        t = 0.0f;
        progress = 0.0f;
        if ( bestDistanceSq > 1.0f || nearestPoint.Type != PT_Road ) {
            gGameObjectDico.locate( pCarState, nearestPoint.It.pRoad, false );
        } else {
            pCarState->setRoadLocation( nearestPoint.It, false );
            pOwner->getInfos().LastRoadPos = nearestPoint.It;
        }
    }

    for ( int32_t i = 0; i <= nearest; i++ ) {
        progress += points.at( i ).Len;
    }
    if ( std::isnan( progress ) ) {
        progress = 0.0f;
    }
    if ( !bOnSegment ) {
        return;
    }

    float residualSq = bestDistanceSq;
    if ( nearest > 0 ) {
        residualSq = ( nearest + 1 < points.Count )
                         ? std::min( residualNext.squaredNorm(),
                                     residualPrevious.squaredNorm() )
                         : residualPrevious.squaredNorm();
    } else if ( nearest + 1 < points.Count ) {
        residualSq = residualNext.squaredNorm();
    }

    const Point &a =
        points.at( ( segmentIndex > 0 ) ? segmentIndex - 1 : segmentIndex );
    const Point &b = points.at( segmentIndex );
    if ( residualSq > kMaxOffPathDistanceSq ) {
        pCarState->setOffRoadLocation();
    } else if ( a.Type == PT_Road && b.Type == PT_Road ) {
        const AiRoadIt it = AiRoadIt::Lerp( a.It, b.It, t );
        pCarState->setRoadLocation( it, false );
        pOwner->getInfos().LastRoadPos = it;
    } else if ( a.Type == PT_Road && b.Type == PT_Junction ) {
        AiRoadIt exit = a.It;
        exit.setRoadExit( a.It.pRoad, a.It.bWay );
        const float roadFraction =
            std::clamp( ( exit.computeOffsetPosition( 0.0f, 0.0f ) -
                          a.It.computeOffsetPosition( 0.0f, 0.0f ) )
                                .norm() /
                            b.Len,
                        0.0f, 1.0f );
        if ( t > roadFraction || roadFraction < 1.0e-4f ) {
            pCarState->setJunctionLocation( groundMatrix, b.pJunction );
        } else {
            // Original pcode quirk: lerps from the road exit back to the point
            const AiRoadIt it = AiRoadIt::Lerp(
                exit, a.It, std::clamp( t / roadFraction, 0.0f, 1.0f ) );
            pCarState->setRoadLocation( it, false );
            pOwner->getInfos().LastRoadPos = it;
        }
    } else if ( a.Type == PT_Junction && b.Type == PT_Road ) {
        AiRoadIt entry = b.It;
        entry.setRoadEntry( b.It.pRoad, b.It.bWay );
        const float junctionFraction =
            std::clamp( ( b.Len - ( entry.computeOffsetPosition( 0.0f, 0.0f ) -
                                    b.It.computeOffsetPosition( 0.0f, 0.0f ) )
                                      .norm() ) /
                            b.Len,
                        0.0f, 1.0f );
        if ( junctionFraction > t || junctionFraction > kMaxJunctionFraction ) {
            pCarState->setJunctionLocation( groundMatrix, a.pJunction );
        } else {
            // Original pcode bug: t - r / (1 - r), not (t - r) / (1 - r)
            const AiRoadIt it = AiRoadIt::Lerp(
                entry, b.It,
                std::clamp( t - junctionFraction / ( 1.0f - junctionFraction ),
                            0.0f, 1.0f ) );
            pCarState->setRoadLocation( it, false );
            pOwner->getInfos().LastRoadPos = it;
        }
    } else if ( a.Type == PT_Junction && b.Type == PT_Junction ) {
        pCarState->setJunctionLocation( groundMatrix, a.pJunction );
    } else {
        pCarState->setOffRoadLocation();
    }

    if ( segmentIndex != nearestIndex ) {
        progress -= popFront( segmentIndex - 1 );
        nearestIndex = 1;
        if ( std::isnan( progress ) ) {
            progress = 0.0f;
        }
    }
}

static constexpr int32_t kBendCapacity = 0x20;
static constexpr float kOpponentBendCoeffMin = 1.0f;      // DAT_00f7a0fc
static constexpr float kOpponentBendCoeffMax = 2.0f;      // DAT_00f7a100
static constexpr float kOpponentBendMaxAngle = 0.314159f; // DAT_00f7a104
static constexpr float kCrestBendMinAngle = 0.19634955f;  // DAT_00faa81c
static constexpr float kBendCurvatureEpsilon = 0.001f;    // DAT_00faa810
static constexpr float kBendMeanTolerance = 1.0f;         // DAT_00faa818
static constexpr float kBendDeltaTolerance = 1.0f;        // DAT_00faa814
static constexpr float kMinBendAngle = 0.0f;              // DAT_016a2c34
static constexpr float kUTurnSpeed = 5.0f;                // DAT_00f41750
static constexpr float kUTurnHandleLength = 8.0f;         // DAT_00f41ac4
static constexpr float kPointsPerMetre = 0.909091f;       // DAT_00f417e4
static constexpr int32_t kMaxUTurnPoints = 16;
static constexpr float kStraightBend = 0.02f;            // DAT_00f417f0
static constexpr float kMediumBend = 0.4f;               // DAT_00f4175c
static constexpr float kCurveSpeedLimit = 100.0f;        // DAT_00f41708
static constexpr float kBezierDegenerateRatio = 1000.0f; // DAT_00f41718
static constexpr float kRoadPointSpeedLimitScale = 1.0f; // DAT_00faa990
static constexpr float kMinElemSpacing = 1.1f;           // DAT_00f4174c

float ComputeBezierBend( const CubicBezier &curve ) {
    // FUN_0095e9c0
    const Eigen::Vector4f &p0 = curve.ControlPoints[0];
    const Eigen::Vector4f &p1 = curve.ControlPoints[1];
    const Eigen::Vector4f &p2 = curve.ControlPoints[2];
    const Eigen::Vector4f &p3 = curve.ControlPoints[3];
    const Eigen::Vector4f chord = p3 - p0;
    const Eigen::Vector4f foot1 = p0 + chord * Project( p1, p0, p3 );
    const Eigen::Vector4f foot2 = p0 + chord * Project( p2, p0, p3 );
    const float lengthA = ( foot1 - p0 ).norm();
    const float lengthB = ( foot2 - foot1 ).norm();
    const float lengthC = ( p3 - foot2 ).norm();
    const float deviation1 = ( p1 - foot1 ).norm();
    const float deviation2 = ( p2 - foot2 ).norm();

    auto ratio = []( const float deviation, const float length ) {
        return ( length != 0.0f ) ? deviation / length : kBezierDegenerateRatio;
    };
    const float bend1 = ( deviation1 != 0.0f )
                            ? std::max( ratio( deviation1, lengthA ),
                                        ratio( deviation1, lengthB ) )
                            : 0.0f;
    const float bend2 = ( deviation2 != 0.0f )
                            ? std::max( ratio( deviation2, lengthB ),
                                        ratio( deviation2, lengthC ) )
                            : 0.0f;
    return bend1 + bend2;
}

NavPathOpponent::NavPathOpponent()
    : NavPath(), bends(), field_0x108( 0 ), field_0x10c( -1 ) {
    // FUN_00409460
}

void NavPathOpponent::initialize( AiVehicle *pOwner ) {
    // FUN_009cdcf0
    bends.allocate( kBendCapacity );
    NavPath::initialize( pOwner );
}

void NavPathOpponent::release() {
    // FUN_009cdca0
    bends.Data.clear();
    bends.Count = 0;
    bends.Capacity = 0;
    points.Data.clear();
    points.Count = 0;
    points.Capacity = 0;
}

void NavPathOpponent::reset( const bool bFull ) {
    // FUN_009cdd40
    if ( bFull ) {
        bends.clear();
    }
    nearestIndex = 0;
    progress = 0.0f;
    if ( bFull ) {
        pointIdCounter = 0;
        totalLength = 0.0f;
        bBatchAppend = false;
        points.clear();
    }
}

void NavPathOpponent::popBack() {
    if ( points.Count == 0 ) {
        return;
    }
    totalLength -= back().Len;
    points.Count--;
    points.Back--;
    if ( points.Back < 0 ) {
        points.Back = points.Capacity - 1;
    }
}

void NavPathOpponent::pushRoadPoint( const AiRoadIt &it,
                                     const float widthRatio,
                                     float speedLimit,
                                     const bool bForce ) {
    // FUN_0095fb50
    if ( isFull() ) {
        return;
    }

    const AiRoadIt *pPointIt = &it;
    AiRoadIt routeFrom0;
    AiRoadIt routeTo0;
    AiRoadIt routeFrom1;
    AiRoadIt routeTo1;
    if ( !bForce ) {
        if ( points.Count > 0 && back().Type == PT_Road &&
             back().It.isSamePosition( it, 0.0f ) ) {
            return;
        }

        const float tolerance = 1.0f / it.pRoad->getShape()->TotalLength;
        AiVehController *pController = pVehicle->getController();
        if ( pController->getCount() != 0 ) {
            pController->getAt( 0, routeFrom0, routeTo0 );
        }
        if ( pController->getCount() > 1 ) {
            pController->getAt( 1, routeFrom1, routeTo1 );
        }

        auto isNear = [&]( const AiRoadIt &routeIt ) {
            if ( it.pRoad != routeIt.pRoad || it.bWay != routeIt.bWay ) {
                return false;
            }
            const float fraction =
                it.pRoad->getShape()->computeRoadFraction( it );
            const float routeFraction =
                routeIt.pRoad->getShape()->computeRoadFraction( routeIt );
            return tolerance > fabsf( fraction - routeFraction );
        };
        if ( isNear( routeTo0 ) ) {
            routeTo0.setWay( it.bWay );
            pPointIt = &routeTo0;
        } else if ( routeTo1.pRoad != nullptr && isNear( routeTo1 ) ) {
            routeTo1.setWay( it.bWay );
            pPointIt = &routeTo1;
        }
    }

    float lateral = 0.0f;
    float height = 0.0f;
    pPointIt->getEntity()->pProfile->computeWidthOffset( widthRatio, lateral,
                                                         height );
    Point point;
    point.Position = pPointIt->computeOffsetPosition( lateral, height );
    point.It = *pPointIt;
    point.Type = PT_Road;
    point.WidthRatio = widthRatio;
    if ( speedLimit < 0.0f ) {
        const AiRoadMap &roadMap = gpAI->getRoadMap();
        int32_t index = it.getEntity()->SpeedLimitIndice;
        if ( index < 0 || index >= roadMap.getNumSpeedLimits() ) {
            index = 0;
        }
        speedLimit = roadMap.getSpeedLimit( index ) * kRoadPointSpeedLimitScale;
    }
    point.RoadSpeedLimit = speedLimit;
    push( point, bForce );
}

bool NavPathOpponent::appendUTurn( const AiRoadIt &it,
                                   const float widthFrom,
                                   const float widthTo ) {
    // FUN_00960050
    back().RoadSpeedLimit = kUTurnSpeed;

    const Eigen::Vector4f start =
        it.computeWidthPosition( std::clamp( widthFrom, -1.0f, 1.0f ) );
    const Eigen::Vector4f end =
        it.computeWidthPosition( std::clamp( widthTo, -1.0f, 1.0f ) );
    Eigen::Vector4f direction = it.computeDirection();
    direction /= direction.norm();

    CubicBezier curve;
    curve.ControlPoints[0] = start;
    curve.ControlPoints[1] = start + direction * kUTurnHandleLength;
    curve.ControlPoints[2] = end + direction * kUTurnHandleLength;
    curve.ControlPoints[3] = end;
    const int32_t numPoints = std::clamp(
        static_cast<int32_t>( curve.computeLength( 1.0f ) * kPointsPerMetre ),
        1, kMaxUTurnPoints );
    if ( points.Count + numPoints >= points.Capacity ) {
        return false;
    }

    const AiRoadShape *pShape = it.pRoad->getShape();
    AiRoadIt projection;
    for ( int32_t i = 1; i <= numPoints; i++ ) {
        const float s =
            static_cast<float>( i ) / static_cast<float>( numPoints );
        Matrix4x4f_t frame = Matrix4x4f_t::Identity();
        const Eigen::Vector4f position = curve.evaluate( s );
        frame.row( 3 ) = position.transpose();
        ProjectOnRoadShape( frame, projection, nullptr, false, pShape );
        projection.setWay( ( s > 0.5f ) ? !it.bWay : it.bWay );

        const Eigen::Vector4f left = projection.computeWidthPosition( -1.0f );
        const Eigen::Vector4f right = projection.computeWidthPosition( 1.0f );
        const float width =
            std::clamp( Project( position, left, right ), 0.0f, 1.0f ) * 2.0f -
            1.0f;
        if ( i == numPoints ) {
            projection = it;
            projection.flip();
        }
        pushRoadPoint( projection, width, kUTurnSpeed, true );
    }
    return true;
}

bool NavPathOpponent::appendRoadInterval( const AiRoadIt &from,
                                          const AiRoadIt &to,
                                          const float widthRatioFrom,
                                          const float widthRatioTo,
                                          const float speedLimit,
                                          AiRoadIt *pOutEnd ) {
    // FUN_00960a20
    if ( from.pRoad->ID != to.pRoad->ID || from.bWay != to.bWay ||
         from.isSamePosition( to, 0.0f ) ) {
        if ( pOutEnd != nullptr ) {
            *pOutEnd = from;
        }
        return false;
    }

    const AiRoadShape *pShape = from.pRoad->getShape();
    const float invLength = 1.0f / pShape->TotalLength;
    const float fractionFrom = pShape->computeRoadFraction( from );
    const float fractionTo = to.pRoad->getShape()->computeRoadFraction( to );
    const float span = fabsf( fractionTo - fractionFrom );
    if ( invLength > span ) {
        return false;
    }

    const int32_t numSteps = std::clamp(
        static_cast<int32_t>( span / ( invLength * kMinElemSpacing ) ), 1, 8 );
    const float widthStep =
        ( widthRatioTo - widthRatioFrom ) / static_cast<float>( numSteps );
    float fractionStep = span / static_cast<float>( numSteps );
    if ( fractionFrom > fractionTo ) {
        fractionStep = -fractionStep;
    }
    const float snapDistance = fabsf( fractionStep ) * 0.5f;

    AiRoadIt it;
    for ( int32_t i = 1; !isFull(); i++ ) {
        float widthRatio = widthRatioFrom + static_cast<float>( i ) * widthStep;
        const float fraction =
            fractionFrom + static_cast<float>( i ) * fractionStep;
        if ( snapDistance > fabsf( fractionTo - fraction ) ) {
            it = to;
            widthRatio = widthRatioTo;
        } else {
            from.pRoad->getShape()->computeIt( fraction, it );
            it.setWay( from.bWay );
        }
        if ( i == numSteps ) {
            pushRoadPoint( it, widthRatio, speedLimit, true );
            break;
        }
        pushRoadPoint( it, widthRatio, speedLimit, false );
    }

    if ( pOutEnd != nullptr ) {
        *pOutEnd = it;
    }
    return !back().It.isSamePosition( from, 0.0f );
}

bool NavPathOpponent::appendRoadBezier( const AiRoadIt &itA,
                                        const AiRoadIt &itB,
                                        Eigen::Vector2f key0,
                                        Eigen::Vector2f key1,
                                        Eigen::Vector2f key2,
                                        Eigen::Vector2f key3,
                                        const Eigen::Vector4f &positionA,
                                        const Eigen::Vector4f &positionB ) {
    // FUN_009605d0
    if ( isFull() ) {
        return false;
    }
    for ( Eigen::Vector2f *pKey : { &key0, &key1, &key2, &key3 } ) {
        pKey->x() = std::clamp( pKey->x(), 0.0f, 1.0f );
    }

    const AiRoadShape *pShape = itA.pRoad->getShape();
    CubicBezier keyCurve;
    keyCurve.ControlPoints[0] =
        Eigen::Vector4f( key0.x(), 0.0f, key0.y(), 0.0f );
    keyCurve.ControlPoints[1] =
        Eigen::Vector4f( key1.x(), 0.0f, key1.y(), 0.0f );
    keyCurve.ControlPoints[2] =
        Eigen::Vector4f( key2.x(), 0.0f, key2.y(), 0.0f );
    keyCurve.ControlPoints[3] =
        Eigen::Vector4f( key3.x(), 0.0f, key3.y(), 0.0f );

    CubicBezier worldCurve;
    worldCurve.ControlPoints[0] = positionA;
    pShape->computeLateralPosition( key1.x(), key1.y(),
                                    worldCurve.ControlPoints[1] );
    pShape->computeLateralPosition( key2.x(), key2.y(),
                                    worldCurve.ControlPoints[2] );
    worldCurve.ControlPoints[3] = positionB;
    const float bend = ComputeBezierBend( worldCurve );
    int32_t numPoints = 4;
    if ( bend <= kStraightBend ) {
        numPoints = 1;
    } else if ( bend < kMediumBend ) {
        numPoints = 3;
    }
    if ( points.Count + numPoints > points.Capacity ) {
        return false;
    }

    for ( int32_t i = 1; i <= numPoints; i++ ) {
        const float s = std::min(
            static_cast<float>( i ) / static_cast<float>( numPoints ), 1.0f );
        const Eigen::Vector4f key = keyCurve.evaluate( s );
        AiRoadIt it;
        pShape->computeIt( key.x(), it );
        it.setWay( itA.bWay );
        if ( i == numPoints ) {
            it = itB;
        }
        pushRoadPoint( it, key.z(), kCurveSpeedLimit, true );
    }
    return true;
}

bool NavPathOpponent::appendJunctionCurve( const AiRoadMap::Junction *pJunction,
                                           const Eigen::Vector4f &p0,
                                           const Eigen::Vector4f &p1,
                                           const Eigen::Vector4f &p2,
                                           const Eigen::Vector4f &p3 ) {
    // FUN_0095f5a0
    CubicBezier curve;
    curve.ControlPoints[0] = p0;
    curve.ControlPoints[1] = p1;
    curve.ControlPoints[2] = p2;
    curve.ControlPoints[3] = p3;
    const float length = curve.computeLength( 1.0f );
    int32_t numPoints = 1;
    if ( ComputeBezierBend( curve ) > kStraightBend ) {
        numPoints = std::clamp(
            static_cast<int32_t>( length * kPointsPerMetre ), 1, 2 );
    }
    if ( points.Count + numPoints >= points.Capacity ) {
        return false;
    }

    for ( int32_t i = 1; i <= numPoints; i++ ) {
        Point point;
        point.Position = curve.evaluate( static_cast<float>( i ) /
                                         static_cast<float>( numPoints ) );
        point.Position.w() = 1.0f;
        point.Type = ( pJunction != nullptr ) ? PT_Junction : PT_End;
        point.pJunction = pJunction;
        point.WidthRatio = kJunctionPointMarker;
        point.RoadSpeedLimit = kCurveSpeedLimit;
        push( point, true );
    }
    return true;
}

void NavPathOpponent::rebuildBends() const {
    // FUN_009cddf0
    struct Sample {
        int32_t Index;
        float Curvature;
        int32_t Group;
    };

    bends.clear();
    const int32_t numPoints = points.Count;
    if ( numPoints == 0 ) {
        return;
    }

    Sample samples[kPointCapacity];
    int32_t group = 0;
    int32_t groupSize = 0;
    float mean = 0.0f;
    for ( int32_t i = 0; i < numPoints; i++ ) {
        Sample &sample = samples[i];
        sample.Index = i;
        sample.Curvature = points.at( i ).Curvature;
        const float curvature = sample.Curvature;

        bool bJoin = false;
        if ( groupSize > 0 ) {
            const bool bSameSign = ( kBendCurvatureEpsilon >= curvature &&
                                     kBendCurvatureEpsilon >= mean ) ||
                                   ( curvature >= -kBendCurvatureEpsilon &&
                                     mean >= -kBendCurvatureEpsilon );
            const float delta = fabsf( curvature - mean );
            bJoin =
                bSameSign && ( fabsf( mean ) * kBendMeanTolerance >= delta ||
                               kBendDeltaTolerance >= delta );
            if ( !bJoin ) {
                group++;
            }
        }

        sample.Group = group;
        if ( bJoin ) {
            mean = ( static_cast<float>( groupSize ) * mean + curvature ) /
                   static_cast<float>( groupSize + 1 );
            groupSize++;
        } else {
            groupSize = 1;
            mean = curvature;
        }
    }

    int32_t first = 0;
    int32_t next = 0;
    while ( first < numPoints ) {
        const int32_t currentGroup = samples[first].Group;
        while ( next < numPoints && samples[next].Group == currentGroup ) {
            next++;
        }
        const int32_t last = std::min( numPoints - 1, next );

        int32_t peak = first;
        float peakCurvature = fabsf( points.at( first ).Curvature );
        for ( int32_t i = first + 1; i <= last; i++ ) {
            const float curvature = fabsf( points.at( i ).Curvature );
            if ( curvature > peakCurvature ) {
                peakCurvature = curvature;
                peak = i;
            }
        }
        peakCurvature = std::max( peakCurvature, kMinCurvature );

        const Eigen::Vector4f tangentFirst = computeTangent( first );
        const Eigen::Vector4f tangentLast = computeTangent( last );
        const float angle = -atan2f( tangentLast.z() * tangentFirst.x() -
                                         tangentFirst.z() * tangentLast.x(),
                                     tangentLast.z() * tangentFirst.z() +
                                         tangentLast.x() * tangentFirst.x() );
        if ( fabsf( angle ) > kMinBendAngle && bends.Count < bends.Capacity ) {
            Bend bend;
            bend.Radius = 1.0f / peakCurvature;
            bend.Angle = angle;
            bend.FirstIndex = first;
            bend.PeakIndex = peak;
            bend.LastIndex = last;
            bends.pushBack( bend );
        }
        first = next;
    }
}

void NavPathOpponent::computeSpeedProfile( int32_t &outCount,
                                           SpeedSample *pOutSamples,
                                           const int32_t maxSamples ) const {
    // FUN_009ce120
    outCount = 0;
    float distance = progress;
    rebuildBends();

    for ( int32_t bendIndex = 0; bendIndex < bends.Count; bendIndex++ ) {
        const Bend &bend = bends.at( bendIndex );
        for ( int32_t i = bend.FirstIndex + 1; i <= bend.LastIndex; i++ ) {
            if ( outCount >= maxSamples ) {
                return;
            }

            const float angle = fabsf( bend.Angle );
            float coef = 1.0f;
            if ( kOpponentBendMaxAngle > angle ) {
                coef = ( 1.0f - angle / kOpponentBendMaxAngle ) *
                           ( kOpponentBendCoeffMax - kOpponentBendCoeffMin ) +
                       kOpponentBendCoeffMin;
            }

            const Point &point = points.at( i );
            SpeedSample &sample = pOutSamples[outCount];
            if ( i > nearestIndex ) {
                distance += point.Len;
                sample.Distance = distance;
            } else {
                sample.Distance = 0.0f;
            }

            sample.Speed = pVehicle->computeTurningSpeed( point.Radius ) * coef;
            if ( angle > kCrestBendMinAngle && point.CrestSpeedLimit > 0.0f &&
                 sample.Speed > point.CrestSpeedLimit ) {
                sample.Speed = point.CrestSpeedLimit;
            }
            outCount++;
        }
    }
}
