#include "shared.h"
#include "ai_nav_path.h"

#include "ai/ai_road_shape.h"
#include "ai/ai_vehicle.h"
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

static bool ComputeCircle( Eigen::Vector2f p1,
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
            point.field_0x60 = kJunctionPointMarker;
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
