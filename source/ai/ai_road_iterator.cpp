#include "shared.h"
#include "ai_road_iterator.h"

#include "ai/ai_road_shape.h"
#include "core/cubic_bezier.h"
#include "core/maths_helper.h"

static constexpr int32_t kCurveProjectionIterations = 10;
static constexpr float kCurveProjectionEpsilon = 0.01f;
static constexpr float kCurveProjectionStartT = 0.5f; // FLOAT_00f41674
static constexpr int32_t kCurveLengthRombergRows = 3;
static constexpr float kMinAdvanceDistance = 0.001f; // FLOAT_00f4167c

static constexpr int32_t kRoadCenterLane = -1;
static constexpr float kRoadCenterHeight = 0.2f; // DAT_00f41730

void AiRoadProfile::computeLaneOffset( const bool bWay,
                                       const int32_t lane,
                                       float &outLateral,
                                       float &outHeight ) const {
    // FUN_0092e190
    auto IsLaneMatch = [&]( const int32_t pointIndex, int32_t &laneIndex ) {
        const uint32_t flags = Points[pointIndex].Flags;
        if ( flags != 2 && flags != 3 ) {
            return false;
        }
        laneIndex++;
        if ( laneIndex != lane ) {
            return false;
        }
        outLateral = ( Points[pointIndex].X + Points[pointIndex + 1].X ) * 0.5f;
        outHeight = ( Points[pointIndex].Y + Points[pointIndex + 1].Y ) * 0.5f;
        return true;
    };

    int32_t laneIndex = -1;
    const int32_t lastSegment = static_cast<int32_t>( NumPoints ) - 2;
    if ( bWay ) {
        for ( int32_t i = lastSegment; i >= 0; i-- ) {
            if ( IsLaneMatch( i, laneIndex ) ) {
                return;
            }
        }
    } else {
        for ( int32_t i = 0; i <= lastSegment; i++ ) {
            if ( IsLaneMatch( i, laneIndex ) ) {
                return;
            }
        }
    }
    outLateral = 0.0f;
    outHeight = 0.0f;
}

void AiRoadProfile::computeWidthOffset( float widthRatio,
                                        float &outLateral,
                                        float &outHeight ) const {
    // FUN_0092ea20
    if ( widthRatio < -1.0f ) {
        widthRatio = -1.0f;
    } else if ( 1.0f < widthRatio ) {
        widthRatio = 1.0f;
    }

    const float lateral = InnerWidth * widthRatio * 0.5f;
    const int32_t numSegments = static_cast<int32_t>( NumPoints ) - 1;
    for ( int32_t i = 0; i < numSegments; i++ ) {
        const Point &start = Points[i];
        const Point &end = Points[i + 1];
        if ( start.X <= lateral && lateral <= end.X ) {
            const float ratio = ( lateral - start.X ) / ( end.X - start.X );
            outLateral = end.X * ratio + ( 1.0f - ratio ) * start.X;
            outHeight = start.Y * ( 1.0f - ratio ) + end.Y * ratio;
            return;
        }
    }
    outLateral = 0.0f;
    outHeight = 0.0f;
}

Eigen::Vector4f AiRoadMap::Entity::computePosition( const int32_t curveIndex,
                                                    const float t ) const {
    // FUN_009308f0
    const float u = 1.0f - t;
    const float middle = t * 3.0f * u;
    const Curve &curve = Curves[curveIndex];
    Eigen::Vector4f position =
        getWaypoint( curveIndex ).toVector4() * ( u * u * u ) +
        curve.CV2.toVector4() * ( middle * u ) +
        curve.CV3.toVector4() * ( middle * t ) +
        curve.CV4.toVector4() * ( t * t * t );
    position.w() = 1.0f;
    return position;
}

Eigen::Vector4f
AiRoadMap::Entity::computeCurveMidpoint( const int32_t curveIndex ) const {
    // FUN_00930a00
    const Curve &curve = Curves[curveIndex];
    Eigen::Vector4f position = getWaypoint( curveIndex ).toVector4() * 0.125f +
                               curve.CV2.toVector4() * 0.375f +
                               curve.CV3.toVector4() * 0.375f +
                               curve.CV4.toVector4() * 0.125f;
    position.w() = 1.0f;
    return position;
}

Eigen::Vector4f
AiRoadMap::Entity::computeOffsetPosition( const int32_t curveIndex,
                                          const float t,
                                          const float lateral,
                                          const float height ) const {
    // FUN_00931c20
    Eigen::Vector4f position = computePosition( curveIndex, t );
    Eigen::Vector4f tangent = computeTangent( curveIndex, t );
    tangent.y() = 0.0f;
    tangent /= std::sqrt( tangent.dot( tangent ) );
    position += Eigen::Vector4f( -tangent.z() * lateral, 0.0f,
                                 tangent.x() * lateral, 0.0f ) +
                Eigen::Vector4f( 0.0f, height, 0.0f, 0.0f );
    return position;
}

Eigen::Vector4f AiRoadMap::Entity::computeTangent( const int32_t curveIndex,
                                                   const float t ) const {
    // FUN_00930b90
    const float u = 1.0f - t;
    const float weight0 = u * u * 3.0f;
    const float weight1 = u * t * 6.0f;
    const float weight2 = t * t * 3.0f;

    const Curve &curve = Curves[curveIndex];
    const Eigen::Vector4f p0 = getWaypoint( curveIndex ).toVector4();
    const Eigen::Vector4f p1 = curve.CV2.toVector4();
    const Eigen::Vector4f p2 = curve.CV3.toVector4();
    const Eigen::Vector4f p3 = curve.CV4.toVector4();
    return ( p1 - p0 ) * weight0 + ( p2 - p1 ) * weight1 +
           ( p3 - p2 ) * weight2;
}

Eigen::Vector4f AiRoadMap::Entity::computeDirection( const int32_t curveIndex,
                                                     const float t ) const {
    // FUN_009320b0
    const Eigen::Vector4f tangent = computeTangent( curveIndex, t );
    return tangent / std::sqrt( tangent.dot( tangent ) );
}

bool AiRoadIt::projectOnCurve( float &outT,
                               Eigen::Vector4f &outPosition,
                               Eigen::Vector4f &outTangent,
                               const Eigen::Vector4f &point ) const {
    // FUN_00934c10
    const AiRoadMap::Entity *pEntity = getEntity();
    const AiRoadMap::Curve &curve = pEntity->Curves[IndWay];

    CubicBezier bezier;
    if ( bWay ) {
        bezier.ControlPoints[0] = pEntity->getWaypoint( IndWay ).toVector4();
        bezier.ControlPoints[1] = curve.CV2.toVector4();
        bezier.ControlPoints[2] = curve.CV3.toVector4();
        bezier.ControlPoints[3] = curve.CV4.toVector4();
    } else {
        bezier.ControlPoints[0] = curve.CV4.toVector4();
        bezier.ControlPoints[1] = curve.CV3.toVector4();
        bezier.ControlPoints[2] = curve.CV2.toVector4();
        bezier.ControlPoints[3] = pEntity->getWaypoint( IndWay ).toVector4();
    }

    const QuadraticBezier derivative = bezier.computeDerivative();
    const bool bResult = bezier.findClosestPointXZ(
        derivative, point, outT, outPosition, kCurveProjectionIterations,
        kCurveProjectionEpsilon, kCurveProjectionStartT );
    outPosition = bezier.evaluate( outT );
    outPosition.w() = 1.0f;
    outTangent = bezier.evaluateTangent( outT );
    return bResult;
}

Eigen::Vector4f AiRoadIt::computeDirection() const {
    // FUN_009347e0
    const AiRoadMap::Entity *pEntity = getEntity();
    if ( bWay ) {
        return pEntity->computeDirection( IndWay, T );
    }
    return pEntity->computeDirection( IndWay, 1.0f - T ) * -1.0f;
}

bool AiRoadVertexIt::advance( const bool bReverse ) {
    // FUN_009355c0
    const bool bForward = bReverse ? !bWay : bWay;
    if ( bForward ) {
        if ( IndVertex + 1 < getNumWaypoints( IndEntity ) ) {
            IndVertex++;
            return true;
        }
        if ( IndEntity + 1 < pDetail->NumEntities ) {
            IndEntity++;
            IndVertex = 0;
            return true;
        }
    } else {
        if ( 0 < IndVertex ) {
            IndVertex--;
            return true;
        }
        if ( 0 < IndEntity ) {
            IndEntity--;
            IndVertex = getNumWaypoints( IndEntity ) - 1;
            return true;
        }
    }
    return false;
}

bool AiRoadVertexIt::isLast() const {
    // FUN_009359e0
    if ( !bWay ) {
        return IndVertex == 0 && IndEntity == 0;
    }
    return IndVertex + 1 == getNumWaypoints( IndEntity ) &&
           IndEntity + 1 == pDetail->NumEntities;
}

bool AiRoadVertexIt::isFirst() const {
    // FUN_00935a40
    if ( bWay ) {
        return IndVertex == 0 && IndEntity == 0;
    }
    return IndVertex + 1 == getNumWaypoints( IndEntity ) &&
           IndEntity + 1 == pDetail->NumEntities;
}

bool AiRoadVertexIt::isEntityEnd() const {
    // FUN_00935670
    if ( !bWay ) {
        return IndVertex == 0;
    }
    return IndVertex + 1 == getNumWaypoints( IndEntity );
}

bool AiRoadVertexIt::isEntityStart() const {
    // FUN_009356b0
    if ( bWay ) {
        return IndVertex == 0;
    }
    return IndVertex + 1 == getNumWaypoints( IndEntity );
}

void AiRoadVertexIt::makeCurveIt( const bool bNext, AiRoadIt &outIt ) const {
    // FUN_009356f0
    if ( pRoad == nullptr ) {
        return;
    }

    outIt.pRoad = pRoad;
    outIt.bWay = bWay;
    outIt.T = 0.0f;
    if ( bWay ) {
        if ( !bNext ) {
            if ( isEntityStart() ) {
                outIt.IndEntity = IndEntity - 1;
                outIt.IndWay = getNumWaypoints( IndEntity - 1 ) - 2;
            } else {
                outIt.IndEntity = IndEntity;
                outIt.IndWay = IndVertex - 1;
            }
        } else if ( isEntityEnd() ) {
            outIt.IndEntity = IndEntity + 1;
            outIt.IndWay = 0;
        } else {
            outIt.IndEntity = IndEntity;
            outIt.IndWay = IndVertex;
        }
    } else {
        if ( !bNext ) {
            if ( isEntityStart() ) {
                outIt.IndEntity = IndEntity + 1;
                outIt.IndWay = 0;
            } else {
                outIt.IndEntity = IndEntity;
                outIt.IndWay = IndVertex;
            }
        } else if ( isEntityEnd() ) {
            outIt.IndEntity = IndEntity - 1;
            outIt.IndWay = getNumWaypoints( IndEntity - 1 ) - 2;
        } else {
            outIt.IndEntity = IndEntity;
            outIt.IndWay = IndVertex - 1;
        }
    }
}

void AiRoadVertexIt::computeHandle( const bool bReverse,
                                    Eigen::Vector4f &outHandle ) const {
    // FUN_009358f0
    const bool bForward = bReverse ? !bWay : bWay;
    const AiRoadMap::Entity *pEntity = getEntity( IndEntity );
    if ( bForward ) {
        if ( IndVertex + 1 < pEntity->getNumWaypoints() ) {
            outHandle = pEntity->Curves[IndVertex].CV2.toVector4();
            return;
        }
        outHandle = pEntity->Curves[IndVertex - 1].CV3.toVector4();
    } else {
        if ( 0 < IndVertex ) {
            outHandle = pEntity->Curves[IndVertex - 1].CV3.toVector4();
            return;
        }
        outHandle = pEntity->Curves[IndVertex].CV2.toVector4();
    }

    const Eigen::Vector4f waypoint =
        pEntity->getWaypoint( IndVertex ).toVector4();
    outHandle = ( outHandle - waypoint ) * -1.0f + waypoint;
}

void AiRoadVertexIt::computeTangent( Eigen::Vector4f &outTangent ) const {
    // FUN_00935ab0
    computeHandle( false, outTangent );
    outTangent -= getWaypoint().toVector4();
    outTangent /= std::sqrt( outTangent.dot( outTangent ) );
}

bool AiRoadVertexIt::projectOnAdjacentCurves( const Eigen::Vector4f &point,
                                              Eigen::Vector4f &outPosition,
                                              Eigen::Vector4f &outTangent,
                                              AiRoadIt &outIt ) const {
    // FUN_0092d310
    float t = 0.0f;
    AiRoadIt nextIt;
    AiRoadIt previousIt;
    bool bProjectedOnNext = false;
    bool bProjectedOnPrevious = false;

    if ( !isLast() ) {
        makeCurveIt( true, nextIt );
        bProjectedOnNext =
            nextIt.projectOnCurve( t, outPosition, outTangent, point );
        if ( bProjectedOnNext ) {
            outIt = nextIt;
        }
    }

    if ( !bProjectedOnNext && !isFirst() ) {
        makeCurveIt( false, previousIt );
        bProjectedOnPrevious =
            previousIt.projectOnCurve( t, outPosition, outTangent, point );
        if ( bProjectedOnPrevious ) {
            outIt = previousIt;
        }
    }

    if ( bProjectedOnNext || bProjectedOnPrevious ) {
        if ( t < 0.0f ) {
            t = 0.0f;
        } else if ( 1.0f < t ) {
            t = 1.0f;
        }
        outIt.T = t;
        return true;
    }

    outPosition = getWaypoint().toVector4();
    computeTangent( outTangent );
    if ( isLast() ) {
        outIt = previousIt;
        t = 1.0f;
    } else {
        outIt = nextIt;
        t = 0.0f;
    }
    outIt.T = t;
    return false;
}

float AiRoadIt::computeSpeed( const float t ) const {
    // FUN_00934240
    const Eigen::Vector4f tangent = getEntity()->computeTangent( IndWay, t );
    return std::sqrt( tangent.dot( tangent ) );
}

float AiRoadIt::computeCurveLength( const float startT,
                                    const float endT ) const {
    // FUN_0092ca30
    float step = endT - startT;
    float previousRow[kCurveLengthRombergRows];
    float row[kCurveLengthRombergRows];
    previousRow[0] =
        ( computeSpeed( startT ) + computeSpeed( endT ) ) * step * 0.5f;

    int32_t numSamples = 1;
    for ( int32_t rowSize = 2; rowSize <= kCurveLengthRombergRows; rowSize++ ) {
        float sum = 0.0f;
        for ( int32_t i = 1; i <= numSamples; i++ ) {
            sum += computeSpeed( startT +
                                 ( static_cast<float>( i ) - 0.5f ) * step );
        }
        row[0] = ( sum * step + previousRow[0] ) * 0.5f;

        int32_t power = 4;
        for ( int32_t j = 1; j < rowSize; j++ ) {
            row[j] = ( static_cast<float>( power ) * row[j - 1] -
                       previousRow[j - 1] ) /
                     static_cast<float>( power - 1 );
            power <<= 2;
        }
        for ( int32_t j = 0; j < rowSize; j++ ) {
            previousRow[j] = row[j];
        }

        numSamples *= 2;
        step *= 0.5f;
    }
    return previousRow[kCurveLengthRombergRows - 1];
}

static int32_t GetLastCurveIndex( const AiRoadMap::Road *pRoad ) {
    const AiRoadMap::RoadDetail *pDetail = pRoad->pDetail;
    return pDetail->pEntities[pDetail->NumEntities - 1]->getNumCurves() - 1;
}

void AiRoadIt::setRoadEntry( AiRoadMap::Road *pOwnerRoad, const bool bNewWay ) {
    // FUN_009345d0
    pRoad = pOwnerRoad;
    bWay = bNewWay;
    T = 0.0f;
    if ( bNewWay ) {
        IndEntity = 0;
        IndWay = 0;
    } else {
        IndEntity = pOwnerRoad->pDetail->NumEntities - 1;
        IndWay = GetLastCurveIndex( pOwnerRoad );
    }
}

void AiRoadIt::setRoadExit( AiRoadMap::Road *pOwnerRoad, const bool bNewWay ) {
    // FUN_00934560
    pRoad = pOwnerRoad;
    bWay = bNewWay;
    T = 1.0f;
    if ( !bNewWay ) {
        IndEntity = 0;
        IndWay = 0;
    } else {
        IndEntity = pOwnerRoad->pDetail->NumEntities - 1;
        IndWay = GetLastCurveIndex( pOwnerRoad );
    }
}

bool AiRoadIt::isBefore( const AiRoadIt &other ) const {
    // FUN_00933df0
    if ( pRoad != other.pRoad || bWay != other.bWay ) {
        return false;
    }
    if ( IndEntity != other.IndEntity ) {
        return ( IndEntity < other.IndEntity ) == bWay;
    }
    if ( IndWay != other.IndWay ) {
        return ( IndWay < other.IndWay ) == bWay;
    }
    return other.T > T;
}

bool AiRoadIt::isSamePosition( const AiRoadIt &other,
                               const float tolerance ) const {
    // FUN_00933d80
    if ( pRoad == nullptr || other.pRoad == nullptr ||
         pRoad->ID != other.pRoad->ID || IndEntity != other.IndEntity ||
         IndWay != other.IndWay ) {
        return false;
    }
    const float difference = ( bWay == other.bWay )
                                 ? std::fabs( T - other.T )
                                 : std::fabs( 1.0f - ( T + other.T ) );
    return difference <= tolerance;
}

bool AiRoadIt::isLastCurveInEntity() const {
    // FUN_009344e0
    if ( !bWay ) {
        return IndWay == 0;
    }
    return IndWay + 1 == getNumCurves();
}

bool AiRoadIt::isFirstCurveInEntity() const {
    // FUN_00934520
    if ( bWay ) {
        return IndWay == 0;
    }
    return IndWay + 1 == getNumCurves();
}

bool AiRoadIt::isAtRoadEnd() const {
    // FUN_00934b30
    if ( !isLastCurveInEntity() ) {
        return false;
    }
    if ( !bWay ) {
        return IndEntity == 0;
    }
    return IndEntity + 1 == pRoad->pDetail->NumEntities;
}

bool AiRoadIt::isAtRoadStart() const {
    // FUN_00934ba0
    if ( !isFirstCurveInEntity() ) {
        return false;
    }
    if ( bWay ) {
        return IndEntity == 0;
    }
    return IndEntity + 1 == pRoad->pDetail->NumEntities;
}

bool AiRoadIt::stepCurve( const bool bBackward ) {
    // FUN_00934e90
    if ( !bBackward ) {
        if ( !isLastCurveInEntity() ) {
            IndWay += bWay ? 1 : -1;
            return true;
        }
        if ( isAtRoadEnd() ) {
            return false;
        }
    } else {
        if ( !isFirstCurveInEntity() ) {
            IndWay += bWay ? -1 : 1;
            return true;
        }
        if ( isAtRoadStart() ) {
            return false;
        }
    }

    if ( bWay != bBackward ) {
        IndEntity++;
        IndWay = 0;
    } else {
        IndEntity--;
        IndWay = getNumCurves() - 1;
    }
    return true;
}

bool AiRoadIt::advanceByDistance( float distance, const bool bEnsureShape ) {
    // FUN_00935360
    bool bBackward = false;
    if ( distance < 0.0f ) {
        distance = 0.0f - distance;
        bBackward = true;
    }
    if ( distance < kMinAdvanceDistance ) {
        return true;
    }

    AiRoadShape *pShape = nullptr;
    if ( bEnsureShape && pRoad != nullptr ) {
        pShape = pRoad->getShape();
    }

    const bool bForward = !bBackward;
    const float startDistance = ComputeRoadDistance( pShape, bForward, *this );
    const float startT = T;
    T = bBackward ? 0.0f : 1.0f;
    const float curveDistance =
        ComputeRoadDistance( pShape, bForward, *this ) - startDistance;
    if ( distance <= curveDistance ) {
        T = bBackward
                ? startT - ( startT / curveDistance ) * distance
                : ( ( 1.0f - startT ) / curveDistance ) * distance + startT;
        return true;
    }
    if ( curveDistance < 0.0f ) {
        T = startT;
        return false;
    }

    float previousDistance = curveDistance;
    float travelledDistance = 0.0f;
    for ( ;; ) {
        if ( !stepCurve( bBackward ) ) {
            return false;
        }
        travelledDistance =
            ComputeRoadDistance( pShape, bForward, *this ) - startDistance;
        if ( distance <= travelledDistance ) {
            break;
        }
        previousDistance = travelledDistance;
    }

    const float ratio = ( distance - previousDistance ) /
                        ( travelledDistance - previousDistance );
    T = bBackward ? 1.0f - ratio : ratio;
    return true;
}

void AiRoadVertexIt::initAtRoadEnd( AiRoadMap::Road *pOwnerRoad,
                                    const bool bStart ) {
    // FUN_00935860
    pRoad = pOwnerRoad;
    pDetail = pOwnerRoad->pDetail;
    bWay = bStart;
    if ( bStart ) {
        IndEntity = 0;
        IndVertex = 0;
    } else {
        IndEntity = pDetail->NumEntities - 1;
        IndVertex = getNumWaypoints( IndEntity ) - 1;
    }
}

Eigen::Vector4f AiRoadIt::computePosition( const int32_t lane ) const {
    // FUN_00934a60
    if ( pRoad == nullptr ) {
        return Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    }

    const AiRoadMap::Entity *pEntity = getEntity();
    float lateral = 0.0f;
    float height = kRoadCenterHeight;
    if ( lane != kRoadCenterLane ) {
        pEntity->pProfile->computeLaneOffset( bWay, lane, lateral, height );
    }
    const float t = bWay ? T : 1.0f - T;
    return pEntity->computeOffsetPosition( IndWay, t, lateral, height );
}

Eigen::Vector4f AiRoadIt::computeWidthPosition( const float widthRatio ) const {
    // FUN_009349b0
    if ( pRoad == nullptr ) {
        return Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    }

    const AiRoadMap::Entity *pEntity = getEntity();
    float lateral = 0.0f;
    float height = 0.0f;
    pEntity->pProfile->computeWidthOffset( widthRatio, lateral, height );
    const float t = bWay ? T : 1.0f - T;
    return pEntity->computeOffsetPosition( IndWay, t, lateral, height );
}

Eigen::Vector4f AiRoadIt::computeOffsetPosition( const float lateral,
                                                 const float height ) const {
    // FUN_00934840
    return getEntity()->computeOffsetPosition( IndWay, bWay ? T : 1.0f - T,
                                               lateral, height );
}

float AiRoadIt::computeAngleAtJunction(
    const AiRoadMap::Junction *pJunction ) const {
    // FUN_00932c60
    static constexpr float kSlotAngles[4] = { 0.0f, 30.0f, 45.0f, 60.0f };
    if ( pRoad == nullptr ) {
        return 0.0f;
    }

    int32_t slot = 0;
    if ( pRoad->pJunctionBegin == pJunction ) {
        if ( pRoad->pJunctionEnd == pJunction ) {
            const float toEnd = ComputeRoadDistance( nullptr, bWay, *this );
            const float toStart = ComputeRoadDistance( nullptr, !bWay, *this );
            slot = ( toStart > toEnd ) ? pRoad->AngleSlotBegin
                                       : pRoad->AngleSlotEnd;
        } else {
            slot = pRoad->AngleSlotBegin;
        }
    } else if ( pRoad->pJunctionEnd == pJunction ) {
        slot = pRoad->AngleSlotEnd;
    } else {
        return 0.0f;
    }
    return static_cast<float>( slot / 4 ) * 90.0f + kSlotAngles[slot & 3];
}

Eigen::Vector4f AiRoadIt::computeSideDirection() const {
    // FUN_00934dd0
    const Eigen::Vector4f direction = computeDirection();
    Eigen::Vector4f side( -direction.z(), 0.0f, direction.x(), 0.0f );
    return side / side.norm();
}

Eigen::Vector4f AiRoadIt::computeStartDirection() const {
    // FUN_00934370
    const AiRoadMap::Entity *pEntity = getEntity();
    const AiRoadMap::Curve &curve = pEntity->Curves[IndWay];
    const Eigen::Vector4f direction =
        bWay ? Eigen::Vector4f( curve.CV2.toVector4() -
                                pEntity->getWaypoint( IndWay ).toVector4() )
             : Eigen::Vector4f( curve.CV3.toVector4() - curve.CV4.toVector4() );
    return direction / std::sqrt( direction.dot( direction ) );
}

bool AiRoadIt::computeFrame( Matrix4x4f_t &outFrame,
                             const int32_t lane ) const {
    // FUN_009350b0
    outFrame = Matrix4x4f_t::Identity();

    Eigen::Vector4f direction;
    if ( T == 0.0f ) {
        direction = computeStartDirection();
    } else {
        direction = computeDirection();
        direction /= std::sqrt( direction.dot( direction ) );
    }

    Eigen::Vector4f right( direction.z(), 0.0f, -direction.x(), 0.0f );
    const float rightLengthSq = right.dot( right );
    if ( rightLengthSq <= 0.0f ) {
        return false;
    }
    right /= std::sqrt( rightLengthSq );

    const Eigen::Vector4f up = Vector4DCross( direction, right );
    outFrame.row( 0 ) = Vector4DCross( up, direction ).transpose();
    outFrame.row( 1 ) = up.transpose();
    outFrame.row( 2 ) = direction.transpose();
    outFrame.row( 3 ) = computePosition( lane ).transpose();
    return true;
}
