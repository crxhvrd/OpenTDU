#include "shared.h"
#include "ai_opt_path.h"

#include "ai/ai_config.h"
#include "ai/ai_road_shape.h"
#include "ai/ai_veh_nav_opponent.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "core/cubic_bezier.h"
#include "core/maths_helper.h"
#include "game/vehicle/car_state.h"
#include "player_data/saved_road_discovery.h"
#include "world/gs_world_streaming.h"

static constexpr float kMinCurvature = 0.001f;             // DAT_00f4167c
static constexpr float kCurvatureProbeDistance = 10.0f;    // DAT_00f4166c
static constexpr float kCurvatureRefreshWindow = 10.0f;    // DAT_00f4166c
static constexpr float kMinOptPositionComponent = 1.0e-5f; // DAT_00f41710
static constexpr float kMinElemSpacing = 1.1f;             // DAT_00f4174c
static constexpr float kSeedSnapDistance = 2.0f;
static constexpr float kSeedNextSnapDistance = 3.0f;        // DAT_00f416b4
static constexpr float kOneThird = 1.0f / 3.0f;             // DAT_00f41764
static constexpr float kNoWidthRatio = -1000.0f;            // DAT_00f419c4
static constexpr float kLaneWidthRatio = 0.5f;              // DAT_00f41674
static constexpr float kJunctionPointA = 0.33f;             // 0x3ea8f5c3
static constexpr float kJunctionPointB = 0.67f;             // 0x3f2b851f
static constexpr float kUTurnAngleMin = 175.0f;             // DAT_00f42150
static constexpr float kUTurnAngleMax = 185.0f;             // DAT_00f4214c
static constexpr float kJunctionThroughAngle = 0.1f;        // FLOAT_00f41668
static constexpr float kJunctionUTurnAngle = 3.0f;          // DAT_00f416b4
static constexpr float kJunctionCurveSpeed = 100.0f;        // 0x42c80000
static constexpr float kDriftHorizonFactor = 0.2f;          // DAT_00f41730
static constexpr float kQuarterPi = 0.785398185f;           // DAT_00f41cb0
static constexpr float kHalfPi = 1.57079637f;               // DAT_00f418a4
static constexpr float kRaisedSeparatorHeight = 0.4f;       // DAT_00f4175c
static constexpr float kJunctionEdgeSearchDistance = 50.0f; // DAT_00f41848
static constexpr int32_t kJunctionEdgeSearchSteps = 16;
static constexpr float kBikeLateralMarginFactor = 4.0f; // DAT_00f418d4
static constexpr float kUTurnLateralLimit = 0.01f;      // DAT_00f416d4
static constexpr float kSamePositionTolerance = 0.01f;  // 0x3c23d70a
static constexpr float kUTurnMaxWidth = 16.0f;          // DAT_00f418d8
static constexpr float kUTurnMinWidth = 8.0f;           // DAT_00f41ac4
static constexpr float kTurnAroundBrakingFactor = 1.2f; // DAT_00f41dc4
static constexpr float kTurnAroundMargin = 16.0f;       // DAT_00f418d8
static constexpr float kOppositeRoadAngle = 180.0f;     // DAT_00f417fc
static constexpr float kMaxRoadAngle = 360.0f;          // DAT_00f41800

static bool IsZeroPosition( const Eigen::Vector4f &position ) {
    const float threshold = kMinOptPositionComponent * kMinOptPositionComponent;
    return position.x() * position.x() <= threshold &&
           position.y() * position.y() <= threshold &&
           position.z() * position.z() <= threshold &&
           position.w() * position.w() <= threshold;
}

static float GetElemWidthRatio( const OptPathElem &elem ) {
    return ( elem.OptWidthRatio != OptPath::kUnsetWidthRatio )
               ? elem.OptWidthRatio
               : elem.WidthRatio;
}

static const Eigen::Vector4f &GetElemPosition( const OptPathElem &elem ) {
    return ( elem.OptWidthRatio != OptPath::kUnsetWidthRatio )
               ? elem.OptPosition
               : elem.Position;
}

static AiRoadMap::Junction *GetExitJunction( const AiRoadIt &it ) {
    return it.bWay ? it.pRoad->pJunctionEnd : it.pRoad->pJunctionBegin;
}

static AiRoadMap::Junction *GetEntryJunction( const AiRoadIt &it ) {
    return it.bWay ? it.pRoad->pJunctionBegin : it.pRoad->pJunctionEnd;
}

static float ComputeRoadDistance( const AiRoadIt &from, const AiRoadIt &to ) {
    // FUN_009352c0
    const AiRoadShape *pShape = from.pRoad->getShape();
    const float fractionFrom = pShape->computeRoadFraction( from );
    const float fractionTo = pShape->computeRoadFraction( to );
    return fabsf( fractionTo - fractionFrom ) * pShape->TotalLength;
}

static int32_t PickOppositeRoad( const AiRoadMap::Junction &junction,
                                 const int32_t fromIndex ) {
    // FUN_0092f610
    const float referenceAngle = junction.computeRoadAngle( fromIndex );
    int32_t bestIndex = -1;
    float bestScore = kMaxRoadAngle;
    for ( int32_t i = 0; i < junction.NumRoads; i++ ) {
        if ( !IsRoadAvailable( junction.getRoad( i )->ID ) ) {
            continue;
        }
        const float score =
            fabsf( kOppositeRoadAngle -
                   fabsf( junction.computeRoadAngle( i ) - referenceAngle ) );
        if ( score < bestScore ) {
            bestScore = score;
            bestIndex = i;
        }
    }
    return bestIndex;
}

static Eigen::Vector4f ScaleFlat( Eigen::Vector4f direction,
                                  const float length ) {
    const float height = direction.y();
    direction.y() = 0.0f;
    direction = Normalize( direction ) * length;
    direction.y() = height;
    return direction;
}

bool ComputeOwnSideCentreRatio( const AiRoadIt &it, float &outRatio ) {
    // FUN_00933cd0
    const AiRoadMap::RoadDetail *pDetail = it.pRoad->pDetail;
    const AiRoadMap::Entity *pFirst = pDetail->pEntities[0];
    const AiRoadMap::Entity *pLast =
        pDetail->pEntities[pDetail->NumEntities - 1];
    const float numOwnLanes = static_cast<float>(
        it.bWay ? pLast->NumRightLanes : pFirst->NumLeftLanes );
    if ( numOwnLanes == 0.0f ) {
        outRatio = 0.0f;
        return false;
    }

    const float numOtherLanes = static_cast<float>(
        it.bWay ? pFirst->NumLeftLanes : pLast->NumRightLanes );
    const float invNumLanes = 1.0f / ( numOtherLanes + numOwnLanes );
    float ratio =
        1.0f - invNumLanes * numOtherLanes * 2.0f - invNumLanes * numOwnLanes;
    if ( it.bWay ) {
        ratio *= -1.0f;
    }
    outRatio = ratio;
    return true;
}

bool ComputeWidthRatioAt( const AiRoadIt &it,
                          const Eigen::Vector4f &position,
                          float &outRatio ) {
    // FUN_00944630
    Eigen::Vector4f left = it.computeWidthPosition( -1.0f );
    const Eigen::Vector4f right = it.computeWidthPosition( 1.0f );
    left.y() = right.y();
    const float t = std::clamp( Project( position, left, right ), 0.0f, 1.0f );
    outRatio = t * 2.0f - 1.0f;
    return true;
}

bool IsBeforeInWay( AiRoadIt a, AiRoadIt b, const bool bWay ) {
    // FUN_00934440
    if ( a.pRoad != b.pRoad ) {
        return false;
    }
    a.setWay( bWay );
    b.setWay( bWay );
    return a.isBefore( b );
}

bool OptPathSituation::isBikeRiderDown() const {
    if ( !bBike ) {
        return false;
    }
    return pVehicle->getCarState()->getBody()->bHasBikeAvatar;
}

void OptPathSituation::refreshLast() {
    // FUN_00953930
    if ( pOptPath->getCount() == 0 ) {
        Last.Position = Eigen::Vector4f::Zero();
        Last.OptPosition = Eigen::Vector4f::Zero();
        Last.WidthRatio = OptPath::kUnsetWidthRatio;
        Last.OptWidthRatio = OptPath::kUnsetWidthRatio;
        Last.Type = OptPathElem::ET_Empty;
        return;
    }

    Last = pOptPath->back();
    LastWidthRatio = ( Last.Type == OptPathElem::ET_Empty )
                         ? kNoWidthRatio
                         : GetElemWidthRatio( Last );
}

void OptPathSituation::refreshLastPathPoint() {
    // FUN_00954780
    if ( pPath->getNumPoints() == 0 ) {
        LastPathPoint.Position = Eigen::Vector4f::Zero();
        LastPathPoint.WidthRatio = OptPath::kUnsetWidthRatio;
        LastPathPoint.Type = NavPath::PT_End;
        return;
    }

    LastPathPoint = pPath->back();
    if ( LastPathPoint.Type == NavPath::PT_End ) {
        LastPointWidthRatio = kNoWidthRatio;
        return;
    }
    ComputeWidthRatioAt( LastPathPoint.It, LastPathPoint.Position,
                         LastPointWidthRatio );
}

void OptPathSituation::checkLineMismatch() {
    // FUN_00953800
    if ( RouteFrom.pRoad == nullptr ) {
        return;
    }
    const eAiVehicleCategory category = pVehicle->getCategory();
    if ( category != eAiVehicleCategory::AVC_Cop &&
         category != eAiVehicleCategory::AVC_Bot ) {
        return;
    }

    AiVehController *pController = pVehicle->getController();
    const int32_t numLegs = pController->getCount();
    const int32_t numElems = pOptPath->getCount();
    for ( int32_t i = 0; i < numElems; i++ ) {
        const OptPathElem &elem = pOptPath->at( i );
        if ( elem.Type != OptPathElem::ET_Road || elem.RouteIndex < 0 ) {
            continue;
        }
        if ( elem.RouteIndex >= numLegs ) {
            return;
        }

        AiRoadIt from;
        AiRoadIt to;
        pController->getAt( elem.RouteIndex, from, to );
        if ( elem.It.pRoad != from.pRoad ) {
            bLineMismatch = true;
            return;
        }
        if ( elem.It.bWay != from.bWay && i + 1 < numElems &&
             pOptPath->at( i + 1 ).Marker == OptPathElem::EM_None ) {
            bLineMismatch = true;
            return;
        }
    }
}

const Eigen::Vector4f &
OptPath::GetEffectivePosition( const OptPathElem &elem ) {
    // FUN_00948340
    return IsZeroPosition( elem.OptPosition ) ? elem.Position
                                              : elem.OptPosition;
}

OptPath::OptPath()
    : idCounter( 0 ),
      elems(),
      dummyElem(),
      field_0x100( 0 ),
      field_0x104( -1 ),
      totalLength( 0.0f ),
      bBatchAppend( false ),
      lastSentId( 0 ),
      resampleCursor( 0 ),
      field_0x128( -1 ),
      pNav( nullptr ),
      pVehicle( nullptr ),
      pSituation( nullptr ),
      currentIndex( 0 ) {
    // FUN_0094a700
}

void OptPath::attach( AiVehNavOpponent *pOwnerNav,
                      AiVehicle *pOwnerVehicle,
                      OptPathSituation *pOwnerSituation ) {
    // FUN_009482d0
    elems.allocate( kCapacity );
    reset( false );
    pNav = pOwnerNav;
    pVehicle = pOwnerVehicle;
    pSituation = pOwnerSituation;
    reset( false );
}

void OptPath::release() {
    // FUN_00947cd0
    pNav = nullptr;
    pVehicle = nullptr;
    pSituation = nullptr;
    elems.Data.clear();
    elems.Count = 0;
    elems.Capacity = 0;
}

void OptPath::reset( const bool bFull ) {
    // FUN_00947d10
    currentIndex = 0;
    field_0x128 = -1;
    lastSentId = 0;
    resampleCursor = 0;
    if ( bFull ) {
        idCounter = 0;
        totalLength = 0.0f;
        bBatchAppend = false;
        elems.clear();
    }
}

OptPathElem &OptPath::front() {
    return ( elems.Count > 0 ) ? elems.at( 0 ) : dummyElem;
}

OptPathElem &OptPath::back() {
    // FUN_00409640
    return ( elems.Count > 0 ) ? elems.last() : dummyElem;
}

const OptPathElem &OptPath::front() const {
    return ( elems.Count > 0 ) ? elems.at( 0 ) : dummyElem;
}

const OptPathElem &OptPath::back() const {
    return ( elems.Count > 0 ) ? elems.last() : dummyElem;
}

float OptPath::popFront( int32_t numElems ) {
    // FUN_0040a390
    if ( numElems <= 0 ) {
        return 0.0f;
    }

    float removed = 0.0f;
    while ( numElems-- > 0 ) {
        removed += front().Len;
        if ( elems.Count > 0 ) {
            elems.popFront();
        }
    }
    removed += front().Len;
    front().Len = 0.0f;
    totalLength -= removed;
    return removed;
}

void OptPath::popBack() {
    // FUN_0040a330
    if ( elems.Count == 0 ) {
        return;
    }
    totalLength -= back().Len;
    elems.Count--;
    elems.Back--;
    if ( elems.Back < 0 ) {
        elems.Back = elems.Capacity - 1;
    }
}

void OptPath::pushRaw( OptPathElem &elem, const bool bForce ) {
    // FUN_00409690
    if ( elems.Count == elems.Capacity ) {
        return;
    }
    if ( elems.Count != 0 && !bForce ) {
        Eigen::Vector4f delta = back().Position - elem.Position;
        delta.y() = 0.0f;
        if ( 1.0f > delta.squaredNorm() ) {
            return;
        }
    }

    elem.Id = ++idCounter;
    elems.pushBack( elem );
    onElemAdded();
}

void OptPath::onElemAdded() {
    // FUN_004099a0
    OptPathElem &last = back();
    last.Curvature = 0.0f;
    last.Radius = -1.0f;
    last.field_0x28 = 0.0f;
    if ( elems.Count == 1 ) {
        last.Len = 0.0f;
        return;
    }

    last.Len = ( last.Position - elems.at( elems.Count - 2 ).Position ).norm();
    totalLength += last.Len;

    float distance = last.Len;
    int32_t index = elems.Count - 2;
    while ( true ) {
        const OptPathElem &elem = elems.at( index );
        if ( !( elem.Curvature > 0.0f ) && !bBatchAppend ) {
            recomputeRadius( index );
        }
        const float previousDistance = distance;
        distance += elem.Len;
        index--;
        if ( !( previousDistance < kCurvatureRefreshWindow ) || index < 0 ) {
            break;
        }
    }
}

void OptPath::recomputeRadius( const int32_t index ) {
    OptPathElem &elem = elems.at( index );
    elem.Curvature = computeCurvature( index, true );
    const float absCurvature = fabsf( elem.Curvature );
    elem.Radius =
        ( absCurvature < kMinCurvature ) ? -1.0f : 1.0f / absCurvature;
}

void OptPath::recomputeAllRadii() {
    // FUN_00409770
    for ( int32_t i = 0; i < elems.Count; i++ ) {
        recomputeRadius( i );
    }
}

float OptPath::computeCurvature( const int32_t index,
                                 const bool bSigned ) const {
    // FUN_00409b10
    if ( index < 1 || index > elems.Count - 2 ) {
        return 0.0f;
    }

    Eigen::Vector4f before;
    if ( !computeOffsetPosition( index, -kCurvatureProbeDistance, before ) ) {
        return 0.0f;
    }
    const Eigen::Vector4f &current = elems.at( index ).Position;
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

bool OptPath::computeOffsetPosition( int32_t index,
                                     const float distance,
                                     Eigen::Vector4f &outPosition ) const {
    // FUN_00409c80
    bool bBackward = false;
    bool bForward = false;
    if ( distance > 0.0f ) {
        if ( index + 1 >= elems.Count ) {
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
        if ( index < 0 || index >= elems.Count ) {
            outPosition.setZero();
            return false;
        }
        outPosition = elems.at( index ).Position;
    }

    const float absDistance = fabsf( distance );
    if ( bBackward ) {
        const OptPathElem *pFrom = &elems.at( index - 1 );
        const OptPathElem *pTo = &elems.at( index );
        float accumulated = pTo->Len;
        while ( absDistance > accumulated && index > 1 ) {
            index--;
            pFrom = &elems.at( index - 1 );
            pTo = &elems.at( index );
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

    const OptPathElem *pFrom = &elems.at( index );
    const OptPathElem *pTo = &elems.at( index + 1 );
    float accumulated = pTo->Len;
    while ( absDistance > accumulated && index + 2 < elems.Count ) {
        index++;
        pFrom = &elems.at( index );
        pTo = &elems.at( index + 1 );
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

void OptPath::push( OptPathElem &elem,
                    const bool bForce,
                    const int32_t routeIndex ) {
    // FUN_0094b310
    elem.OptPosition = Eigen::Vector4f::Zero();
    elem.OptWidthRatio = kUnsetWidthRatio;
    elem.RouteIndex = routeIndex;

    if ( elems.Count > 0 ) {
        const OptPathElem &tail = back();
        bool bCheckDistance = true;
        if ( elem.Type == OptPathElem::ET_Road ) {
            bCheckDistance = false;
            if ( tail.Type == OptPathElem::ET_Road ) {
                const bool bSameRoad = elem.It.pRoad == tail.It.pRoad;
                if ( !bSameRoad || elem.It.bWay == tail.It.bWay ) {
                    if ( bForce ) {
                        bCheckDistance = true;
                    } else {
                        if ( elem.It.isSamePosition( tail.It, 0.0f ) ) {
                            return;
                        }
                        if ( bSameRoad ) {
                            const AiRoadShape *pShape =
                                elem.It.pRoad->getShape();
                            const float fractionElem =
                                pShape->computeRoadFraction( elem.It );
                            const float fractionTail =
                                tail.It.pRoad->getShape()->computeRoadFraction(
                                    tail.It );
                            if ( fractionElem == fractionTail ) {
                                return;
                            }
                            if ( 1.0f / pShape->TotalLength * kMinElemSpacing >=
                                 fabsf( fractionTail - fractionElem ) ) {
                                return;
                            }
                        }
                    }
                }
            }
        }

        if ( bCheckDistance ) {
            Eigen::Vector4f delta = elem.Position - tail.Position;
            delta.y() = 0.0f;
            const float distanceSq = delta.squaredNorm();
            if ( 1.0f >= distanceSq && !bForce ) {
                return;
            }
            if ( kMinOptPositionComponent >= distanceSq ) {
                return;
            }
        }

        if ( back().Marker == OptPathElem::EM_End ) {
            back().Marker = OptPathElem::EM_None;
        }
    }

    elem.BasePosX = elem.Position.x();
    elem.BasePosZ = elem.Position.z();
    pushRaw( elem, bForce );

    if ( back().Type != OptPathElem::ET_Road || elems.Count <= 1 ) {
        return;
    }
    const int32_t count = elems.Count;
    if ( elems.at( count - 2 ).Type == OptPathElem::ET_Junction ) {
        for ( int32_t i = count - 4; i < count; i++ ) {
            updateElemFrame( i );
        }
        for ( int32_t i = count - 3; i < count; i++ ) {
            updateLateralLimits( i, false );
        }
    } else {
        updateElemFrame( count - 2 );
        updateElemFrame( count - 1 );
        updateLateralLimits( count - 1, false );
    }
}

static Eigen::Vector2f ComputeRoadFrame( const AiRoadIt &it ) {
    // FUN_00935320
    const Eigen::Vector4f side = it.computeSideDirection() * -1.0f;
    return Eigen::Vector2f( side.x(), side.z() );
}

static int32_t IntersectLinesXZ( const Eigen::Vector2f &startA,
                                 const Eigen::Vector2f &endA,
                                 const Eigen::Vector2f &startB,
                                 const Eigen::Vector2f &endB,
                                 float epsilon,
                                 Eigen::Vector2f &outPoint ) {
    // FUN_0090b720
    if ( 0.0f > epsilon ) {
        epsilon = kMinCurvature;
    }

    const Eigen::Vector2f deltaA = endA - startA;
    const float lengthA = deltaA.norm();
    const Eigen::Vector2f dirA = deltaA * ( 1.0f / lengthA );
    const Eigen::Vector2f deltaB = endB - startB;
    const float lengthB = deltaB.norm();
    const Eigen::Vector2f dirB = deltaB * ( 1.0f / lengthB );
    const Eigen::Vector2f offset = startB - startA;

    const float cross = dirA.x() * dirB.y() - dirA.y() * dirB.x();
    bool bCrossing = false;
    float t = 0.0f;
    float s = 0.0f;
    if ( fabsf( cross ) > epsilon ) {
        const float invCross = 1.0f / cross;
        t = ( offset.x() * dirB.y() - offset.y() * dirB.x() ) * invCross;
        s = ( offset.x() * dirA.y() - offset.y() * dirA.x() ) * invCross;
        bCrossing = true;
    }

    const Eigen::Vector2f offsetDir = offset * ( 1.0f / offset.norm() );
    const float collinear =
        fabsf( dirB.y() * offsetDir.x() - offsetDir.y() * dirB.x() );
    if ( epsilon >= collinear || !bCrossing ) {
        outPoint = Eigen::Vector2f::Zero();
        return 2;
    }

    outPoint = startA + dirA * t;
    return ( t >= 0.0f && lengthA >= t && s >= 0.0f && lengthB >= s ) ? 1 : 0;
}

static inline Eigen::Vector2f Normalize2D( const Eigen::Vector2f &vector ) {
    // FUN_0040a530
    return vector * ( 1.0f / vector.norm() );
}

void OptPath::updateElemFrame( const int32_t index ) {
    // FUN_0094aa50
    if ( elems.Count <= 1 || index < 0 ) {
        return;
    }

    OptPathElem &elem = elems.at( index );
    Eigen::Vector2f frame = Eigen::Vector2f::Zero();
    if ( elem.It.pRoad != nullptr && gAIConfig.bPathOptFixedNormals ) {
        frame = ComputeRoadFrame( elem.It );
    }
    elem.FrameX = frame.x();
    elem.FrameZ = frame.y();
    if ( frame.squaredNorm() != 0.0f ) {
        return;
    }

    const int32_t last = elems.Count - 1;
    if ( index > 0 && index < last ) {
        if ( elem.It.pRoad != nullptr ) {
            const Eigen::Vector4f direction = elem.It.computeDirection();
            frame = Eigen::Vector2f( direction.z(), -direction.x() );
        } else {
            const bool bPrevIsRoad =
                elems.at( index - 1 ).Type == OptPathElem::ET_Road;
            const OptPathElem &elemA =
                elems.at( bPrevIsRoad ? index - 1 : index - 2 );
            const OptPathElem &elemB =
                elems.at( bPrevIsRoad ? index + 2 : index + 1 );
            if ( elemA.It.pRoad == nullptr || elemB.It.pRoad == nullptr ) {
                return;
            }

            const Eigen::Vector2f startA( elemA.Position.x(),
                                          elemA.Position.z() );
            const Eigen::Vector2f endA = startA + ComputeRoadFrame( elemA.It );
            const Eigen::Vector2f startB( elemB.Position.x(),
                                          elemB.Position.z() );
            const Eigen::Vector2f frameB = ComputeRoadFrame( elemB.It );
            const Eigen::Vector2f endB = startB + frameB;
            Eigen::Vector2f centre;
            IntersectLinesXZ( startA, endA, startB, endB, -1.0f, centre );

            // Retail tests endB.x (not centre.x) along with centre.z
            if ( endB.x() == 0.0f && centre.y() == 0.0f ) {
                frame = Normalize2D( frameB );
            } else {
                frame = Normalize2D(
                    Eigen::Vector2f( elem.BasePosX, elem.BasePosZ ) - centre );
                const OptPathElem &previous = elems.at( index - 1 );
                const OptPathElem &next = elems.at( index + 1 );
                const float chordX = next.BasePosX - previous.BasePosX;
                const float chordZ = next.BasePosZ - previous.BasePosZ;
                const float angle =
                    fabsf( atan2f( chordX * frame.x() + chordZ * frame.y(),
                                   chordZ * frame.x() - chordX * frame.y() ) );
                if ( angle > kQuarterPi && angle > kHalfPi ) {
                    frame = -frame;
                }
            }
        }
    } else if ( index == 0 ) {
        const OptPathElem &next = elems.at( 1 );
        frame = Eigen::Vector2f( next.BasePosZ - elem.BasePosZ,
                                 -( next.BasePosX - elem.BasePosX ) );
    } else if ( index == last ) {
        const OptPathElem &previous = elems.at( index - 1 );
        frame = Eigen::Vector2f( elem.BasePosZ - previous.BasePosZ,
                                 -( elem.BasePosX - previous.BasePosX ) );
    }

    frame = Normalize2D( frame );
    elem.FrameX = frame.x();
    elem.FrameZ = frame.y();
}

static bool HasRaisedSeparator( const AiRoadProfile *pProfile ) {
    // FUN_0092e070
    for ( uint32_t i = 0; i < pProfile->NumPoints; i++ ) {
        const AiRoadProfile::Point &point = pProfile->Points[i];
        if ( ( point.Flags == 0 || point.Flags == 1 || point.Flags == 5 ) &&
             point.Y > kRaisedSeparatorHeight ) {
            return true;
        }
    }
    return false;
}

static float
ComputeWidthRatioXZ( const AiRoadIt &it, const float x, const float z ) {
    // FUN_00944570
    const Eigen::Vector4f left = it.computeWidthPosition( -1.0f );
    const Eigen::Vector4f right = it.computeWidthPosition( 1.0f );
    const float axisX = right.x() - left.x();
    const float axisZ = right.z() - left.z();
    float t = ( ( x - left.x() ) * axisX + ( z - left.z() ) * axisZ ) /
              ( axisX * axisX + axisZ * axisZ );
    if ( 0.0f > t ) {
        t = 0.0f;
    } else if ( t > 1.0f ) {
        t = 1.0f;
    }
    return t * 2.0f - 1.0f;
}

static float DistanceXZ( const Eigen::Vector4f &a, const Eigen::Vector4f &b ) {
    const float deltaX = a.x() - b.x();
    const float deltaZ = a.z() - b.z();
    return sqrtf( deltaZ * deltaZ + deltaX * deltaX );
}

static bool JunctionContainsPoint( const AiRoadMap::Junction *pJunction,
                                   const Eigen::Vector2f &pointXZ,
                                   const int32_t side ) {
    // FUN_0092fe60
    if ( side == -1 ) {
        return pJunction->containsPoint( pointXZ );
    }

    const AiRoadMap::JunctionDetail *pDetail =
        static_cast<const AiRoadMap::JunctionDetail *>( pJunction->pDetail );
    if ( pDetail == nullptr ) {
        return false;
    }
    const float *pPoints = reinterpret_cast<const float *>( pDetail + 1 );
    int32_t first = 0;
    int32_t numPoints = pDetail->field_0x8;
    if ( side != 0 && pDetail->field_0x9 > 0 ) {
        first = pDetail->field_0x8;
        numPoints = pDetail->field_0x9;
    }

    bool bInside = false;
    for ( int32_t i = 0, j = numPoints - 1; i < numPoints; j = i++ ) {
        const float xi = pPoints[( first + i ) * 2];
        const float zi = pPoints[( first + i ) * 2 + 1];
        const float xj = pPoints[( first + j ) * 2];
        const float zj = pPoints[( first + j ) * 2 + 1];
        if ( ( zi > pointXZ.y() ) != ( zj > pointXZ.y() ) &&
             pointXZ.x() <
                 ( xj - xi ) * ( pointXZ.y() - zi ) / ( zj - zi ) + xi ) {
            bInside = !bInside;
        }
    }
    return bInside;
}

static Eigen::Vector2f GetJunctionPoint( const AiRoadMap::Junction *pJunction,
                                         const int32_t index ) {
    const AiRoadMap::JunctionDetail *pDetail =
        static_cast<const AiRoadMap::JunctionDetail *>( pJunction->pDetail );
    const float *pPoints = reinterpret_cast<const float *>( pDetail + 1 );
    return Eigen::Vector2f( pPoints[index * 2], pPoints[index * 2 + 1] );
}

static void GetJunctionMouthCorners( const AiRoadMap::Junction *pJunction,
                                     const int32_t side,
                                     const bool bFirstMouth,
                                     Eigen::Vector2f &outCornerA,
                                     Eigen::Vector2f &outCornerB ) {
    // FUN_0092e670 (first mouth) / FUN_0092e6e0 (second mouth)
    const AiRoadMap::JunctionDetail *pDetail =
        static_cast<const AiRoadMap::JunctionDetail *>( pJunction->pDetail );
    const int32_t first = pDetail->field_0x8 + pDetail->field_0x9 - side * 4 +
                          ( bFirstMouth ? 8 : 6 );
    outCornerA = GetJunctionPoint( pJunction, first );
    outCornerB = GetJunctionPoint( pJunction, first + 1 );
}

static Eigen::Vector4f
GetJunctionMouthCentre( const AiRoadMap::Junction *pJunction,
                        const int32_t side,
                        const bool bFirstMouth ) {
    Eigen::Vector2f cornerA;
    Eigen::Vector2f cornerB;
    GetJunctionMouthCorners( pJunction, side, bFirstMouth, cornerA, cornerB );
    const Eigen::Vector2f centre = ( cornerB - cornerA ) * 0.5f + cornerA;
    return Eigen::Vector4f( centre.x(), pJunction->Position.Y, centre.y(),
                            1.0f );
}

static Eigen::Vector2f SearchJunctionEdge( const AiRoadMap::Junction *pJunction,
                                           const Eigen::Vector2f &base,
                                           const Eigen::Vector2f &axis,
                                           const int32_t side ) {
    Eigen::Vector2f edge = base;
    float distance = kJunctionEdgeSearchDistance;
    float step = kJunctionEdgeSearchDistance;
    for ( int32_t i = 0; i < kJunctionEdgeSearchSteps; i++ ) {
        step *= 0.5f;
        const Eigen::Vector2f point = base + axis * distance;
        if ( JunctionContainsPoint( pJunction, point, side ) ) {
            distance += step;
            edge = point;
        } else {
            distance -= step;
        }
    }
    return edge;
}

static void ShrinkLimit( Eigen::Vector2f &limit,
                         const Eigen::Vector2f &centre,
                         const float halfWidth,
                         const bool bKeepHalfWidth ) {
    const Eigen::Vector2f offset = limit - centre;
    const float length = offset.norm();
    if ( length < halfWidth ) {
        return;
    }
    const float distance = bKeepHalfWidth ? halfWidth : length - halfWidth;
    limit = centre + Normalize2D( offset ) * distance;
}

static void UpdateJunctionLateralLimits( OptPathElem &elem,
                                         const float halfWidth,
                                         const bool bJunctionUTurn ) {
    const AiRoadMap::Junction *pJunction = elem.pJunction;
    if ( pJunction == nullptr ) {
        return;
    }

    const Eigen::Vector2f base( elem.BasePosX, elem.BasePosZ );
    const Eigen::Vector2f frame( elem.FrameX, elem.FrameZ );
    Eigen::Vector2f left = base;
    Eigen::Vector2f right = base;
    if ( pJunction->Flags == 0 || pJunction->Flags == 2 ) {
        const int32_t side = ( pJunction->Flags == 2 )
                                 ? pJunction->computeSideOfLine( base )
                                 : -1;
        right = SearchJunctionEdge( pJunction, base, frame, side );
        left = SearchJunctionEdge( pJunction, base, -frame, side );
        const Eigen::Vector2f centre = ( right + left ) * 0.5f;
        ShrinkLimit( left, centre, halfWidth, false );
        ShrinkLimit( right, centre, halfWidth, false );
    } else if ( pJunction->Flags == 1 ) {
        const int32_t side = pJunction->computeSideOfLine( base );
        Eigen::Vector2f firstA;
        Eigen::Vector2f firstB;
        Eigen::Vector2f secondA;
        Eigen::Vector2f secondB;
        GetJunctionMouthCorners( pJunction, side, true, firstA, firstB );
        GetJunctionMouthCorners( pJunction, side, false, secondA, secondB );
        const Eigen::Vector2f firstMid = ( firstB - firstA ) * 0.5f + firstA;
        const Eigen::Vector2f secondMid =
            ( secondB - secondA ) * 0.5f + secondA;
        if ( ( base - firstMid ).squaredNorm() >
             ( base - secondMid ).squaredNorm() ) {
            right = secondB;
            left = secondA;
        } else {
            right = firstB;
            left = firstA;
        }
        const Eigen::Vector2f centre = ( right + left ) * 0.5f;
        ShrinkLimit( left, centre, halfWidth, bJunctionUTurn );
        ShrinkLimit( right, centre, halfWidth, bJunctionUTurn );
    }

    elem.LateralLimits[0] = left.x();
    elem.LateralLimits[1] = left.y();
    elem.LateralLimits[2] = right.x();
    elem.LateralLimits[3] = right.y();

    const Eigen::Vector2f axis = right - left;
    const float t = ( base - left ).dot( axis ) / axis.squaredNorm();
    if ( 0.0f > t ) {
        elem.BasePosX = left.x();
        elem.BasePosZ = left.y();
    } else if ( t > 1.0f ) {
        elem.BasePosX = right.x();
        elem.BasePosZ = right.y();
    }
}

void OptPath::updateLateralLimits( const int32_t index, const bool bWiden ) {
    // FUN_00948f50
    if ( index < 0 ) {
        return;
    }

    OptPathElem &elem = elems.at( index );
    const bool bJunctionUTurn = elem.Marker == OptPathElem::EM_JunctionUTurn;
    float halfWidth = pVehicle->getCarState()->getDimensions().x() * 0.5f;
    if ( pSituation->bBike ) {
        halfWidth *= kBikeLateralMarginFactor;
    }
    const float widthFactor = pVehicle->getDPC().RoadWidthFactor;

    if ( elem.It.pRoad == nullptr ) {
        UpdateJunctionLateralLimits( elem, halfWidth, bJunctionUTurn );
        return;
    }

    float distance = kUTurnLateralLimit;
    if ( elem.Marker != OptPathElem::EM_BeforeUTurn &&
         elem.Marker != OptPathElem::EM_AfterUTurn ) {
        const AiRoadProfile *pProfile = elem.It.getEntity()->pProfile;
        if ( HasRaisedSeparator( pProfile ) ) {
            halfWidth *= 2.0f;
            if ( index > 0 ) {
                const OptPathElem &previous = elems.at( index - 1 );
                if ( previous.Type == OptPathElem::ET_Road &&
                     previous.It.pRoad != nullptr &&
                     previous.It.pRoad == elem.It.pRoad &&
                     !HasRaisedSeparator(
                         previous.It.getEntity()->pProfile ) ) {
                    updateLateralLimits( index - 1, true );
                }
            }
        } else if ( bWiden ) {
            halfWidth *= 2.0f;
        }

        if ( elem.It.pRoad->isOncomingOvertakeForbidden() ) {
            const float ratio =
                ComputeWidthRatioXZ( elem.It, elem.BasePosX, elem.BasePosZ );
            const Eigen::Vector4f position =
                elem.It.computeWidthPosition( ratio );
            const Eigen::Vector4f edge =
                elem.It.computeWidthPosition( ( ratio > 0.0f ) ? 1.0f : -1.0f );
            distance =
                ( DistanceXZ( edge, position ) - halfWidth ) * widthFactor;
        } else if ( pVehicle->getDPC().DPCPattern.bRespectWay ) {
            const Eigen::Vector4f centre = elem.It.computeWidthPosition( 0.0f );
            const Eigen::Vector4f edge =
                elem.It.computeWidthPosition( elem.It.bWay ? 1.0f : -1.0f );
            distance =
                ( DistanceXZ( edge, centre ) * 0.5f - halfWidth ) * widthFactor;
        } else {
            distance =
                ( pProfile->InnerWidth * 0.5f - halfWidth ) * widthFactor;
        }
    }

    elem.LateralLimits[0] = elem.BasePosX - elem.FrameX * distance;
    elem.LateralLimits[1] = elem.BasePosZ - elem.FrameZ * distance;
    elem.LateralLimits[2] = elem.BasePosX + elem.FrameX * distance;
    elem.LateralLimits[3] = elem.BasePosZ + elem.FrameZ * distance;
}

void OptPath::seed( const AiRoadIt &pos,
                    const int32_t routeIndex,
                    const float widthRatio,
                    const bool bForce ) {
    // FUN_0094cd40
    if ( pos.pRoad == nullptr ) {
        return;
    }

    if ( elems.Count != 0 && back().Type == OptPathElem::ET_Road &&
         back().It.pRoad->ID == pos.pRoad->ID &&
         back().It.isSamePosition( pos, 0.0f ) && !bForce ) {
        return;
    }

    AiRoadIt it = pos;
    if ( !bForce ) {
        AiVehController *pController = pVehicle->getController();
        AiRoadIt legFrom;
        AiRoadIt legTo;
        AiRoadIt nextFrom;
        AiRoadIt nextTo;
        if ( pController->getCount() != 0 ) {
            pController->getAt( routeIndex, legFrom, legTo );
        }
        if ( pController->getCount() > routeIndex + 1 ) {
            pController->getAt( routeIndex + 1, nextFrom, nextTo );
        }

        const float fractionPos =
            pos.pRoad->getShape()->computeRoadFraction( it );
        const float invLength = 1.0f / pos.pRoad->getShape()->TotalLength;

        bool bTryNext = true;
        if ( legTo.pRoad == pos.pRoad && it.bWay == legTo.bWay &&
             it.isBefore( legTo ) ) {
            const float fraction =
                legTo.pRoad->getShape()->computeRoadFraction( legTo );
            if ( invLength * kSeedSnapDistance >
                 fabsf( fraction - fractionPos ) ) {
                it = legTo;
            }
            bTryNext = ( fraction == -1.0f );
        }
        if ( bTryNext && nextTo.pRoad == pos.pRoad && it.bWay == nextTo.bWay &&
             it.isBefore( nextTo ) ) {
            const float fraction =
                nextTo.pRoad->getShape()->computeRoadFraction( nextTo );
            if ( invLength * kSeedNextSnapDistance >
                 fabsf( fraction - fractionPos ) ) {
                it = nextTo;
            }
        }
    }

    float lateral = 0.0f;
    float height = 0.0f;
    it.getEntity()->pProfile->computeWidthOffset( widthRatio, lateral, height );
    OptPathElem elem;
    elem.Position = it.computeOffsetPosition( lateral, height );
    elem.It = it;
    elem.WidthRatio = widthRatio;
    elem.Type = OptPathElem::ET_Road;
    push( elem, bForce, routeIndex );
}

void OptPath::pushUTurn( const AiRoadIt &it,
                         const int32_t routeIndexA,
                         const int32_t routeIndexB,
                         const float widthRatioA,
                         const float widthRatioB ) {
    // FUN_0094cb10
    if ( it.pRoad == nullptr ) {
        return;
    }

    const AiRoadProfile *pProfile = it.getEntity()->pProfile;
    float lateral = 0.0f;
    float height = 0.0f;

    OptPathElem before;
    pProfile->computeWidthOffset( widthRatioA, lateral, height );
    before.Position = it.computeOffsetPosition( lateral, height );
    before.It = it;
    before.WidthRatio = widthRatioA;
    before.Type = OptPathElem::ET_Road;
    before.Marker = OptPathElem::EM_BeforeUTurn;
    push( before, true, routeIndexA );

    OptPathElem after;
    pProfile->computeWidthOffset( widthRatioB, lateral, height );
    after.Position = it.computeOffsetPosition( lateral, height );
    after.It = it;
    after.It.setWay( !it.bWay );
    after.WidthRatio = widthRatioB;
    after.Type = OptPathElem::ET_Road;
    after.Marker = OptPathElem::EM_AfterUTurn;
    push( after, true, routeIndexB );
}

bool OptPath::appendRoadStretch( const AiRoadIt &from,
                                 const AiRoadIt &to,
                                 const int32_t routeIndex,
                                 const float widthRatioFrom,
                                 const float widthRatioTo ) {
    // FUN_0094d3b0
    if ( from.pRoad->ID != to.pRoad->ID || from.bWay != to.bWay ) {
        return false;
    }
    if ( from.isSamePosition( to, 0.0f ) ) {
        return true;
    }
    if ( isFull() ) {
        return false;
    }

    const AiRoadShape *pShape = from.pRoad->getShape();
    const float invLength = 1.0f / pShape->TotalLength;
    const float fractionFrom = pShape->computeRoadFraction( from );
    const float fractionTo = to.pRoad->getShape()->computeRoadFraction( to );
    const float span = fabsf( fractionTo - fractionFrom );
    if ( invLength * kMinElemSpacing > span ) {
        return false;
    }

    int32_t numSteps = static_cast<int32_t>(
        span / ( gAIConfig.PathOptTrackDistance * invLength ) );
    if ( numSteps <= 0 ) {
        numSteps = 1;
    }
    const float widthStep =
        ( widthRatioTo - widthRatioFrom ) / static_cast<float>( numSteps );
    float fractionStep = span / static_cast<float>( numSteps );
    if ( fractionFrom > fractionTo ) {
        fractionStep = -fractionStep;
    }
    const float snapDistance = fabsf( fractionStep ) * 0.5f;

    for ( int32_t i = 1; !isFull(); i++ ) {
        const float fraction =
            fractionFrom + static_cast<float>( i ) * fractionStep;
        AiRoadIt it;
        float widthRatio;
        if ( snapDistance > fabsf( fractionTo - fraction ) ) {
            it = to;
            widthRatio = widthRatioTo;
        } else {
            pShape->computeIt( fraction, it );
            it.setWay( from.bWay );
            widthRatio = widthRatioFrom + static_cast<float>( i ) * widthStep;
        }
        seed( it, routeIndex, widthRatio, false );
        if ( i == numSteps ) {
            break;
        }
    }
    return back().It.isSamePosition( to, 0.0f );
}

void OptPath::onRoutePopped() {
    // FUN_00948160
    for ( int32_t i = 0; i < elems.Count; i++ ) {
        OptPathElem &elem = elems.at( i );
        if ( elem.RouteIndex >= 0 ) {
            elem.RouteIndex--;
        }
    }
}

void OptPath::clearOptimisation( const int32_t first, const int32_t last ) {
    // FUN_00948260
    if ( first < 0 || first > elems.Count || last < 0 || last > elems.Count ||
         first > last ) {
        return;
    }
    for ( int32_t i = first; i <= last; i++ ) {
        elems.at( i ).OptPosition = Eigen::Vector4f::Zero();
        elems.at( i ).OptWidthRatio = kUnsetWidthRatio;
    }
}

int32_t OptPath::findLastOnSameRoad( const int32_t start,
                                     const float maxDistance,
                                     float *pOutDistance ) const {
    // FUN_00947d90
    const bool bLimited = maxDistance >= 0.0f;
    const AiRoadMap::Road *pRoad = elems.at( start ).It.pRoad;
    int32_t last = start;
    float distance = 0.0f;
    for ( int32_t i = start + 1; i < elems.Count; i++ ) {
        const OptPathElem &elem = elems.at( i );
        if ( elem.Type != OptPathElem::ET_Road ||
             elem.It.pRoad->ID != pRoad->ID ) {
            break;
        }
        distance += elem.Len;
        last = i;
        if ( bLimited && distance >= maxDistance ) {
            break;
        }
    }
    if ( pOutDistance != nullptr ) {
        *pOutDistance = distance;
    }
    return last;
}

void OptPath::setLateralTarget( const int32_t first,
                                const int32_t last,
                                const float widthRatio ) {
    // FUN_0094a4a0
    for ( int32_t i = first; i <= last; i++ ) {
        OptPathElem &elem = elems.at( i );
        if ( elem.Type != OptPathElem::ET_Road ) {
            return;
        }
        float lateral = 0.0f;
        float height = 0.0f;
        elem.It.getEntity()->pProfile->computeWidthOffset( widthRatio, lateral,
                                                           height );
        elem.OptWidthRatio = widthRatio;
        elem.OptPosition = elem.It.computeOffsetPosition( lateral, height );
    }
}

void OptPath::driftBackToLine() {
    // FUN_0094a190
    const int32_t count = elems.Count;
    if ( currentIndex <= 0 || currentIndex + 1 >= count || count < 3 ) {
        return;
    }

    const float horizon =
        pSituation->CurSpeed * pSituation->LineDistance * kDriftHorizonFactor;
    float distance = 0.0f;
    int32_t last = currentIndex + 1;
    for ( int32_t i = currentIndex + 1; i < count; i++ ) {
        last = i;
        distance += elems.at( i ).Len;
        if ( distance >= horizon ) {
            break;
        }
    }
    const int32_t numSteps = last - currentIndex;
    const float distanceStep = pSituation->LineDistance / distance;

    const Eigen::Vector4f &previous = elems.at( currentIndex - 1 ).Position;
    const Eigen::Vector4f segment =
        elems.at( currentIndex ).Position - previous;
    const Eigen::Vector4f car = pSituation->Position - previous;
    const float side = -atan2f( car.z() * segment.x() - car.x() * segment.z(),
                                car.x() * segment.x() + car.z() * segment.z() );
    const bool bLeft = side > 0.0f;

    for ( int32_t i = 0; i <= currentIndex; i++ ) {
        applyDriftOffset( i, pSituation->LineDistance, bLeft );
    }
    for ( int32_t step = 1; step <= numSteps; step++ ) {
        const float offset = computeDriftOffset( step, distanceStep, bLeft );
        if ( offset == 0.0f ) {
            return;
        }
        applyDriftOffset( currentIndex + step, offset, bLeft );
    }
}

void OptPath::applyDriftOffset( const int32_t index,
                                const float distance,
                                const bool bLeft ) {
    // FUN_00949e10
    OptPathElem &elem = elems.at( index );
    if ( elem.Type == OptPathElem::ET_Empty ||
         elem.Type == OptPathElem::ET_None ) {
        return;
    }

    const float baseX = elem.Position.x();
    const float baseZ = elem.Position.z();
    const float limitX = bLeft ? elem.LateralLimits[2] : elem.LateralLimits[0];
    const float limitZ = bLeft ? elem.LateralLimits[3] : elem.LateralLimits[1];
    const float limitDistance =
        sqrtf( ( limitX - baseX ) * ( limitX - baseX ) +
               ( limitZ - baseZ ) * ( limitZ - baseZ ) );
    if ( distance > limitDistance ) {
        elem.OptPosition.x() = limitX;
        elem.OptPosition.z() = limitZ;
    } else if ( bLeft ) {
        elem.OptPosition.x() = elem.FrameX * distance + baseX;
        elem.OptPosition.z() = elem.FrameZ * distance + baseZ;
    } else {
        elem.OptPosition.x() = baseX - elem.FrameX * distance;
        elem.OptPosition.z() = baseZ - elem.FrameZ * distance;
    }
    elem.OptPosition.y() = elem.Position.y();
    elem.OptPosition.w() = 1.0f;

    if ( elem.Type == OptPathElem::ET_Road ) {
        ComputeWidthRatioAt( elem.It, elem.OptPosition, elem.OptWidthRatio );
    } else {
        elem.OptWidthRatio = kUnsetWidthRatio;
    }
}

float OptPath::computeDriftOffset( const int32_t step,
                                   const float distanceStep,
                                   const bool bLeft ) const {
    // FUN_00949f90
    const OptPathElem &previous = elems.at( currentIndex + step - 1 );
    const OptPathElem &elem = elems.at( currentIndex + step );

    Eigen::Vector4f delta = previous.OptPosition - previous.Position;
    delta.y() = 0.0f;
    float offset = sqrtf( delta.dot( delta ) ) - elem.Len * distanceStep;
    if ( 0.0f > offset ) {
        offset = 0.0f;
    }
    if ( elem.Type != OptPathElem::ET_Road ||
         previous.Type != OptPathElem::ET_Road ) {
        return offset;
    }

    const Eigen::Vector4f target =
        elem.It.computeWidthPosition( previous.OptWidthRatio );
    const Eigen::Vector4f segment = elem.Position - previous.Position;
    const Eigen::Vector4f toTarget = target - previous.Position;
    const float side =
        -atan2f( toTarget.z() * segment.x() - segment.z() * toTarget.x(),
                 toTarget.z() * segment.z() + toTarget.x() * segment.x() );
    if ( bLeft != ( side > 0.0f ) ) {
        return 0.0f;
    }

    Eigen::Vector4f toElem = target - elem.Position;
    toElem.y() = 0.0f;
    const float targetDistance = sqrtf( toElem.dot( toElem ) );
    return ( offset > targetDistance ) ? targetDistance : offset;
}

void OptPath::blendLateral( const int32_t first,
                            const int32_t last,
                            const float widthFirst,
                            const float widthLast ) {
    // FUN_0094c0f0
    static constexpr float kFirstBlend = 0.33f;  // DAT_00f41a0c
    static constexpr float kSecondBlend = 0.67f; // DAT_00f420ec

    if ( widthFirst == kUnsetWidthRatio && widthLast == kUnsetWidthRatio ) {
        for ( int32_t i = 0; i < 4; i++ ) {
            elems.at( first + i ).OptWidthRatio = kUnsetWidthRatio;
            elems.at( first + i ).OptPosition = Eigen::Vector4f::Zero();
        }
        return;
    }

    OptPathElem &elemFirst = elems.at( first );
    OptPathElem &elemLast = elems.at( last );
    OptPathElem &middle1 = elems.at( first + 1 );
    OptPathElem &middle2 = elems.at( last - 1 );
    elemFirst.OptWidthRatio = widthFirst;
    elemFirst.OptPosition = elemFirst.It.computeWidthPosition(
        std::clamp( widthFirst, -1.0f, 1.0f ) );
    elemLast.OptWidthRatio = widthLast;
    elemLast.OptPosition = elemLast.It.computeWidthPosition(
        std::clamp( widthLast, -1.0f, 1.0f ) );
    if ( middle1.Marker == OptPathElem::EM_JunctionUTurn ) {
        return;
    }

    const float handleLength =
        ( elemFirst.OptPosition - elemLast.OptPosition ).norm() * kOneThird;
    const Eigen::Vector4f directionFirst =
        Normalize( elemFirst.It.computeDirection() );
    const Eigen::Vector4f directionLast =
        Normalize( elemLast.It.computeDirection() * -1.0f );
    CubicBezier curve;
    curve.ControlPoints[0] = elemFirst.OptPosition;
    curve.ControlPoints[1] =
        elemFirst.OptPosition + directionFirst * handleLength;
    curve.ControlPoints[2] =
        elemLast.OptPosition + directionLast * handleLength;
    curve.ControlPoints[3] = elemLast.OptPosition;
    middle1.OptWidthRatio = 0.0f;
    middle1.OptPosition = curve.evaluate( kFirstBlend );
    middle2.OptWidthRatio = 0.0f;
    middle2.OptPosition = curve.evaluate( kSecondBlend );
}

bool OptPath::isCarBeforeRoadElem( const int32_t index ) const {
    // FUN_009489e0
    const OptPathElem &elem = elems.at( index );
    const AiRoadIt &carIt = pSituation->RoadIt;
    if ( carIt.pRoad != elem.It.pRoad ) {
        return false;
    }
    if ( carIt.bWay == elem.It.bWay ) {
        return carIt.isBefore( elem.It );
    }
    if ( elem.Marker == OptPathElem::EM_AfterUTurn ) {
        return isCarInsideUTurn( index );
    }
    if ( elem.Marker == OptPathElem::EM_BeforeUTurn ) {
        return false;
    }

    AiRoadIt carInElemWay = carIt;
    carInElemWay.setWay( elem.It.bWay );
    return carInElemWay.isBefore( elem.It ) ||
           carInElemWay.isSamePosition( elem.It, 0.0f );
}

bool OptPath::isCarInsideUTurn( const int32_t index ) const {
    // FUN_00948910
    const AiRoadIt &previousIt = elems.at( index - 1 ).It;
    const AiRoadIt &currentIt = elems.at( index ).It;
    const AiRoadIt &carIt = pSituation->RoadIt;
    return carIt.pRoad == currentIt.pRoad &&
           IsBeforeInWay( previousIt, carIt, previousIt.bWay ) &&
           IsBeforeInWay( carIt, currentIt, currentIt.bWay );
}

bool OptPath::isCarBeforeElemXZ( const int32_t index ) const {
    // FUN_00948b00
    Eigen::Vector4f car = pSituation->Position;
    Eigen::Vector4f a = elems.at( index - 1 ).Position;
    Eigen::Vector4f b = elems.at( index ).Position;
    car.y() = 0.0f;
    a.y() = 0.0f;
    b.y() = 0.0f;
    return 1.0f > Project( car, a, b );
}

void OptPath::computeLineDistance( float *pOutDistance ) const {
    // FUN_00948bf0
    if ( currentIndex > elems.Count || currentIndex < 0 ) {
        *pOutDistance = 0.0f;
        return;
    }

    const Eigen::Vector4f &car = pSituation->Position;
    const Eigen::Vector4f &b = elems.at( currentIndex ).Position;
    if ( currentIndex == 0 ) {
        *pOutDistance = ( car - b ).norm();
        return;
    }
    const Eigen::Vector4f &a = elems.at( currentIndex - 1 ).Position;
    const float t = Project( car, a, b );
    *pOutDistance = ( car - ( a + ( b - a ) * t ) ).norm();
}

void OptPath::updateCurrentIndex( float *pOutLineDistance ) {
    // FUN_00948d60
    const int32_t count = elems.Count;
    if ( count <= 2 ) {
        currentIndex = count - 1;
        computeLineDistance( pOutLineDistance );
        return;
    }
    if ( pSituation->isBikeRiderDown() ) {
        return;
    }

    int32_t found = -1;
    for ( int32_t i = 1; i < count; i++ ) {
        bool bFound = false;
        if ( pSituation->isOnRoad() ) {
            const OptPathElem &previous = elems.at( i - 1 );
            const OptPathElem &current = elems.at( i );
            if ( previous.Type == OptPathElem::ET_Road &&
                 current.Type == OptPathElem::ET_Road &&
                 current.It.pRoad == previous.It.pRoad ) {
                bFound = isCarBeforeRoadElem( i );
            }
        } else {
            bFound = isCarBeforeElemXZ( i );
        }
        if ( bFound ) {
            found = i;
            break;
        }
    }

    if ( found < 0 ) {
        const OptPathElem &last = elems.at( count - 1 );
        const AiRoadIt &carIt = pSituation->RoadIt;
        if ( last.Type == OptPathElem::ET_Road &&
             last.It.pRoad == carIt.pRoad && last.It.bWay == carIt.bWay &&
             carIt.isBefore( last.It ) ) {
            found = count - 1;
        } else {
            found = ( currentIndex > 0 ) ? currentIndex : count - 1;
        }
    }
    currentIndex = found;

    while ( currentIndex > 2 &&
            elems.at( currentIndex ).Type == OptPathElem::ET_Road &&
            pSituation->isOnRoad() ) {
        popFront( 1 );
        if ( resampleCursor > 0 ) {
            resampleCursor--;
        }
        currentIndex--;
    }
    computeLineDistance( pOutLineDistance );
}

bool OptPath::getNeighbourKey( const OptSpanKeys &keys,
                               Eigen::Vector2f &outKey,
                               const bool bNext ) const {
    // FUN_0094aed0
    const int32_t reference = bNext ? keys.IndexB : keys.IndexA;
    const int32_t neighbour = bNext ? keys.IndexB + 1 : keys.IndexA - 1;
    if ( neighbour < 0 || neighbour >= elems.Count ) {
        return false;
    }

    const OptPathElem &neighbourElem = elems.at( neighbour );
    const OptPathElem &referenceElem = elems.at( reference );
    if ( neighbourElem.Type == OptPathElem::ET_Road ) {
        if ( neighbourElem.It.pRoad->ID != referenceElem.It.pRoad->ID ||
             neighbourElem.It.bWay != referenceElem.It.bWay ) {
            return false;
        }
        outKey.x() = neighbourElem.It.pRoad->getShape()->computeRoadFraction(
            neighbourElem.It );
        outKey.y() = GetElemWidthRatio( neighbourElem );
        return true;
    }
    if ( !bNext ) {
        return false;
    }
    outKey = keys.KeyB;
    return true;
}

void OptPath::computeSpanKeys( OptSpanKeys &keys ) const {
    // FUN_0094b6c0
    const OptPathElem &elemA = elems.at( keys.IndexA );
    const OptPathElem &elemB = elems.at( keys.IndexB );
    const AiRoadShape *pShape = elemA.It.pRoad->getShape();
    const float scale = elemA.It.getEntity()->pProfile->InnerWidth *
                        kLaneWidthRatio / pShape->TotalLength;

    Eigen::Vector2f key0;
    Eigen::Vector2f key3;
    const bool bPrevious = getNeighbourKey( keys, key0, false );
    const bool bNext = getNeighbourKey( keys, key3, true );
    if ( bPrevious && bNext ) {
        const Eigen::Vector2f p0( key0.x(), key0.y() * scale );
        const Eigen::Vector2f p1( keys.KeyA.x(), keys.KeyA.y() * scale );
        const Eigen::Vector2f p2( keys.KeyB.x(), keys.KeyB.y() * scale );
        const Eigen::Vector2f p3( key3.x(), key3.y() * scale );
        const float length01 = ( p1 - p0 ).norm();
        const float length12 = ( p2 - p1 ).norm();
        const float length23 = ( p3 - p2 ).norm();
        const float handle =
            std::min( { std::min( length01, 1.0f ), std::min( length12, 1.0f ),
                        std::min( length23, 1.0f ) } ) *
            kOneThird;
        const Eigen::Vector2f tangent1 = p2 - p0;
        const Eigen::Vector2f tangent2 = p3 - p1;
        const Eigen::Vector2f handle1 =
            p1 + tangent1.normalized() * handle *
                     ( tangent1.norm() / ( length01 + length12 ) );
        const Eigen::Vector2f handle2 =
            p2 - tangent2.normalized() * handle *
                     ( tangent2.norm() / ( length12 + length23 ) );
        keys.Handle1 = Eigen::Vector2f( handle1.x(), handle1.y() / scale );
        keys.Handle2 = Eigen::Vector2f( handle2.x(), handle2.y() / scale );
    } else {
        OptSpanHandles handles;
        handles.IndexA = keys.IndexA;
        handles.IndexB = keys.IndexB;
        handles.PositionA = GetEffectivePosition( elemA );
        handles.PositionB = GetEffectivePosition( elemB );
        computeSpanHandles( handles );

        for ( int32_t i = 0; i < 2; i++ ) {
            const Eigen::Vector4f &handle =
                ( i == 0 ) ? handles.Handle1 : handles.Handle2;
            Matrix4x4f_t frame = Matrix4x4f_t::Identity();
            frame.row( 3 ) = handle.transpose();
            AiRoadIt it;
            it.pRoad = nullptr;
            ProjectOnRoadShape( frame, it, nullptr, false, pShape );
            it.setWay( elemA.It.bWay );

            Eigen::Vector2f &key = ( i == 0 ) ? keys.Handle1 : keys.Handle2;
            key.x() = it.pRoad->getShape()->computeRoadFraction( it );
            ComputeWidthRatioAt( it, handle, key.y() );
        }
    }

    keys.KeyA.x() = std::clamp( keys.KeyA.x(), 0.0f, 1.0f );
    keys.KeyB.x() = std::clamp( keys.KeyB.x(), 0.0f, 1.0f );
    if ( elemA.It.bWay ) {
        keys.Handle2.x() = std::min( keys.Handle2.x(), keys.KeyB.x() );
        if ( keys.KeyA.x() > keys.Handle1.x() ) {
            keys.Handle1.x() = keys.KeyA.x();
        } else if ( keys.Handle1.x() > keys.Handle2.x() ) {
            keys.Handle1.x() = keys.Handle2.x();
        }
    } else {
        if ( keys.Handle2.x() > keys.Handle1.x() ) {
            keys.Handle1.x() = keys.Handle2.x();
        } else if ( keys.Handle1.x() > keys.KeyA.x() ) {
            keys.Handle1.x() = keys.KeyA.x();
        }
        keys.Handle2.x() = std::max( keys.Handle2.x(), keys.KeyB.x() );
    }
}

Eigen::Vector2f OptPath::getNeighbourPositionXZ( const OptSpanHandles &handles,
                                                 const bool bNext ) const {
    // FUN_00948380
    const int32_t reference = bNext ? handles.IndexB : handles.IndexA;
    const int32_t neighbour = bNext ? handles.IndexB + 1 : handles.IndexA - 1;
    Eigen::Vector4f position;
    if ( neighbour >= 0 && neighbour < elems.Count ) {
        position = GetEffectivePosition( elems.at( neighbour ) );
    } else if ( bNext ) {
        position = GetEffectivePosition( elems.at( reference ) );
    } else {
        const Eigen::Vector4f forward =
            pVehicle->getInfos().GroundMatrix.row( 2 ).transpose();
        position = GetEffectivePosition( elems.at( reference ) ) - forward;
    }
    return Eigen::Vector2f( position.x(), position.z() );
}

void OptPath::computeSpanHandles( OptSpanHandles &handles ) const {
    // FUN_00948490
    const Eigen::Vector2f p1( handles.PositionA.x(), handles.PositionA.z() );
    const Eigen::Vector2f p2( handles.PositionB.x(), handles.PositionB.z() );
    const Eigen::Vector2f p0 = getNeighbourPositionXZ( handles, false );
    const Eigen::Vector2f p3 = getNeighbourPositionXZ( handles, true );

    const float length01 = ( p1 - p0 ).norm();
    const float length12 = ( p2 - p1 ).norm();
    const float length23 = ( p3 - p2 ).norm();
    const float handle =
        std::min( { length01, length12, length23 } ) * kOneThird;
    const Eigen::Vector2f tangent1 = p2 - p0;
    const Eigen::Vector2f tangent2 = p3 - p1;
    const Eigen::Vector2f handle1 =
        p1 + tangent1.normalized() * handle *
                 ( tangent1.norm() / ( length01 + length12 ) );
    const Eigen::Vector2f handle2 =
        p2 - tangent2.normalized() * handle *
                 ( tangent2.norm() / ( length12 + length23 ) );
    handles.Handle1 = Eigen::Vector4f( handle1.x(), handles.PositionA.y(),
                                       handle1.y(), 0.0f );
    handles.Handle2 = Eigen::Vector4f( handle2.x(), handles.PositionB.y(),
                                       handle2.y(), 0.0f );
}

OptPathBuilder::OptPathBuilder()
    : localSlot(),
      solverSlot( -1 ),
      optimiserIteration( -1 ),
      pLine( nullptr ),
      pPath( nullptr ),
      pNav( nullptr ),
      pVehicle( nullptr ),
      halfWidth( 0.0f ),
      pSituation( nullptr ) {}

void OptPathBuilder::attach( AiVehNavOpponent *pOwnerNav,
                             AiVehicle *pOwnerVehicle,
                             OptPath *pOwnerLine,
                             NavPathOpponent *pOwnerPath,
                             OptPathSituation *pOwnerSituation ) {
    // FUN_009448a0
    pNav = pOwnerNav;
    pVehicle = pOwnerVehicle;
    pLine = pOwnerLine;
    pPath = pOwnerPath;
    pSituation = pOwnerSituation;
    solverSlot =
        ( gpAiPathOptimiser != nullptr ) ? gpAiPathOptimiser->allocSlot() : -1;
    localSlot = AiPathOptimiserSlot();
    optimiserIteration = -1;
    reset( false );
}

void OptPathBuilder::detach() {
    // FUN_00944790
    if ( gpAiPathOptimiser != nullptr && solverSlot >= 0 ) {
        gpAiPathOptimiser->freeSlot( solverSlot );
    }
    localSlot.clear();
    pNav = nullptr;
    pLine = nullptr;
    solverSlot = -1;
}

void OptPathBuilder::reset( const bool bFull ) {
    // FUN_00944810
    if ( gpAiPathOptimiser != nullptr && solverSlot >= 0 ) {
        gpAiPathOptimiser->resetSlot( solverSlot );
    }
    localSlot.clear();
    if ( pLine != nullptr ) {
        pLine->setResampleCursor( 0 );
        pLine->setLastSentId( 0 );
        pLine->setField0x128( -1 );
    }
    if ( bFull ) {
        pNav = nullptr;
        pVehicle = nullptr;
        pLine = nullptr;
        pPath = nullptr;
        pSituation = nullptr;
    }
}

static Eigen::Vector4f GetShiftWorldPosition() {
    if ( gpWorldStreaming->isEnabled() ) {
        return gpWorldStreaming->getPosition();
    }
    return Eigen::Vector4f::Zero();
}

void OptPathBuilder::buildSlot( AiPathOptimiserSlot &slot ) const {
    // FUN_00944130
    slot.clear();
    slot.Origin = GetShiftWorldPosition();
    const float originX = slot.Origin.x();
    const float originZ = slot.Origin.z();
    slot.NumSegments = std::clamp( pLine->getCount() - 1, 0,
                                   AiPathOptimiserSlot::kMaxSegments );
    if ( slot.NumSegments == 0 ) {
        return;
    }

    for ( int32_t i = 0; i <= slot.NumSegments; i++ ) {
        const OptPathElem &elem = pLine->at( i );
        AiPathOptimiserSlot::CrossSection &crossSection = slot.CrossSections[i];
        crossSection.ID = elem.Id;
        crossSection.BaseX = elem.BasePosX - originX;
        crossSection.BaseZ = elem.BasePosZ - originZ;
        crossSection.LeftX = elem.LateralLimits[0] - originX;
        crossSection.LeftZ = elem.LateralLimits[1] - originZ;
        crossSection.RightX = elem.LateralLimits[2] - originX;
        crossSection.RightZ = elem.LateralLimits[3] - originZ;

        AiPathOptimiserSlot::Point &point = slot.Points[i];
        point.X = elem.Position.x() - originX;
        point.Z = elem.Position.z() - originZ;
        point.ForceZ = 0.0f;
        point.ForceX = 0.0f;
    }

    AiPathOptimiserSlot::CrossSection &first = slot.CrossSections[0];
    first.BaseX = first.LeftX = first.RightX = slot.Points[0].X;
    first.BaseZ = first.LeftZ = first.RightZ = slot.Points[0].Z;
    slot.updateSegments();
}

void OptPathBuilder::applyResult( const AiPathOptimiserSlot &slot,
                                  const int32_t start ) {
    // FUN_00944d30
    const int32_t count = pLine->getCount();
    if ( count < 2 || slot.NumSegments < 1 ) {
        return;
    }

    const int32_t frontId = pLine->at( 0 ).Id;
    int32_t first = -1;
    for ( int32_t i = 0; i <= slot.NumSegments; i++ ) {
        if ( slot.CrossSections[i].ID == frontId ) {
            first = i;
            break;
        }
    }
    if ( first == -1 ) {
        return;
    }

    const int32_t numElems = count - first;
    for ( int32_t i = start; i < numElems; i++ ) {
        if ( i >= AiPathOptimiserSlot::kMaxPoints || i < 0 ) {
            continue;
        }
        OptPathElem &elem = pLine->at( i );
        const AiPathOptimiserSlot::Point &point = slot.Points[first + i];
        elem.Position.x() = slot.Origin.x() + point.X;
        elem.Position.z() = point.Z + slot.Origin.z();
        if ( elem.Type == OptPathElem::ET_Road &&
             !gbPathOptIgnoreConstraints ) {
            ComputeWidthRatioAt( elem.It, elem.Position, elem.WidthRatio );
        } else {
            elem.WidthRatio = OptPath::kUnsetWidthRatio;
        }
    }
}

void OptPathBuilder::pullOptimiserResult() {
    // FUN_00944f20
    if ( gpAiPathOptimiser != nullptr && solverSlot >= 0 ) {
        const AiPathOptimiserSlot &slot =
            gpAiPathOptimiser->lockSlotForRead( solverSlot );
        if ( slot.NumSegments >= 2 ) {
            applyResult( slot, 0 );
        }
        slot.copyTo( localSlot );
        optimiserIteration = slot.Iteration;
        gpAiPathOptimiser->unlock();
    }

    if ( pSituation->pOptPath->getCount() != 0 ) {
        pLine->updateCurrentIndex( &pSituation->LineDistance );
    }
    pSituation->refreshLast();
}

void OptPathBuilder::submitToOptimiser() {
    // FUN_009446c0
    halfWidth = pVehicle->getCarState()->getDimensions().x() * 0.5f;
    if ( gpAiPathOptimiser == nullptr || solverSlot < 0 ) {
        return;
    }

    AiPathOptimiserSlot &slot =
        gpAiPathOptimiser->lockSlotForWrite( solverSlot );
    buildSlot( slot );
    slot.Iteration = optimiserIteration;
    if ( pNav->isGridRevHold() ) {
        slot.FirstMovable = 1;
    } else {
        slot.FirstMovable =
            pLine->getCurrentIndex() + ( gbPathOptFreeNearCar ? 1 : 3 );
    }
    gpAiPathOptimiser->unlock();
}

void OptPathBuilder::fillOnly() {
    // FUN_00947be0
    if ( !gbDisablePathOpt ) {
        pullOptimiserResult();
        extendFull();
        submitToOptimiser();
    }
    pLine->setLastSentId( pLine->back().Id );
    resample();
}

void OptPathBuilder::optimise() {
    // FUN_00947c50
    if ( !gbDisablePathOpt ) {
        pullOptimiserResult();
        extendSimple();
        submitToOptimiser();
    }
    pLine->setLastSentId( pLine->back().Id );
    resample();
}

float OptPathBuilder::computeDefaultWidthRatio( const AiRoadIt &it ) const {
    float ratio = 0.0f;
    if ( pVehicle->getDPC().DPCPattern.bRespectWay ) {
        ComputeOwnSideCentreRatio( it, ratio );
    }
    return ratio;
}

float OptPathBuilder::computeWidthRatio( Eigen::Vector4f *pOutPosition,
                                         const AiRoadIt &it ) {
    // FUN_00944370
    pSituation->refreshLast();
    const OptPathElem &last = pSituation->Last;
    if ( pOutPosition != nullptr ) {
        *pOutPosition = OptPath::GetEffectivePosition( last );
        if ( it.pRoad->isOncomingOvertakeForbidden() ) {
            const AiRoadMap::Junction *pJunction = GetExitJunction( last.It );
            const int32_t side = pJunction->computeSideOfLine(
                Eigen::Vector2f( pOutPosition->x(), pOutPosition->z() ) );
            const Eigen::Vector4f mouth =
                it.computeWidthPosition( kLaneWidthRatio );
            return ( pJunction->computeSideOfLine(
                         Eigen::Vector2f( mouth.x(), mouth.z() ) ) == side )
                       ? kLaneWidthRatio
                       : -kLaneWidthRatio;
        }
        return computeDefaultWidthRatio( it );
    }
    if ( last.It.pRoad->isOncomingOvertakeForbidden() ) {
        return ( last.WidthRatio > 0.0f ) ? kLaneWidthRatio : -kLaneWidthRatio;
    }
    return computeDefaultWidthRatio( last.It );
}

bool OptPathBuilder::isCarInJunctionOf( const AiRoadIt &from ) const {
    // FUN_009438c0
    const AiRoadMap::Junction *pJunction = nullptr;
    if ( pSituation->isInJunction() ) {
        pJunction = gpAI->getRoadMap().getJunction( pSituation->LocationID );
    }
    return pJunction == GetEntryJunction( from );
}

void OptPathBuilder::computeUTurnWidthRatios( const AiRoadIt &it,
                                              float &outWidthRatioFrom,
                                              float &outWidthRatioTo,
                                              float *pOutWidth ) const {
    // FUN_00953c30
    const AiRoadProfile *pProfile = it.getEntity()->pProfile;
    const float carLength = pVehicle->getCarState()->getDimensions().z();
    const float ratio = HasRaisedSeparator( pProfile )
                            ? carLength / ( pProfile->InnerWidth * 0.5f )
                            : carLength / pProfile->InnerWidth;
    const float lastWidthRatio = pSituation->Last.WidthRatio;
    if ( it.pRoad->isOncomingOvertakeForbidden() ) {
        if ( lastWidthRatio > 0.0f ) {
            if ( lastWidthRatio >= kLaneWidthRatio ) {
                outWidthRatioFrom = 1.0f - ratio;
                outWidthRatioTo = ratio;
            } else {
                outWidthRatioFrom = ratio;
                outWidthRatioTo = 1.0f - ratio;
            }
        } else if ( lastWidthRatio >= -kLaneWidthRatio ) {
            outWidthRatioFrom = -ratio;
            outWidthRatioTo = ratio - 1.0f;
        } else {
            outWidthRatioFrom = ratio - 1.0f;
            outWidthRatioTo = -ratio;
        }
    } else if ( lastWidthRatio >= 0.0f ) {
        outWidthRatioFrom = 1.0f - ratio;
        outWidthRatioTo = ratio - 1.0f;
    } else {
        outWidthRatioFrom = ratio - 1.0f;
        outWidthRatioTo = 1.0f - ratio;
    }

    const float halfWidth = pProfile->InnerWidth * 0.5f;
    float width = fabsf( outWidthRatioTo - outWidthRatioFrom ) * halfWidth;
    if ( width > kUTurnMaxWidth ) {
        const float span = kUTurnMinWidth / halfWidth * 2.0f;
        outWidthRatioTo = ( outWidthRatioTo > outWidthRatioFrom )
                              ? outWidthRatioFrom + span
                              : outWidthRatioFrom - span;
        width = kUTurnMaxWidth;
    }
    if ( pOutWidth != nullptr ) {
        *pOutWidth = width;
    }
}

void OptPathBuilder::pickNextRoadEntry( AiRoadIt &outEntry, float &outLength ) {
    // FUN_00944c30
    pSituation->refreshLast();
    const AiRoadIt &last = pSituation->Last.It;
    const AiRoadMap::Junction *pJunction = GetExitJunction( last );
    if ( pJunction == nullptr ) {
        return;
    }
    const int32_t fromIndex = pJunction->getRoadIndex( last.pRoad );
    AiRoadMap::Road *pRoad =
        pJunction->getRoad( PickOppositeRoad( *pJunction, fromIndex ) );
    outEntry.setRoadEntry( pRoad, pRoad->pJunctionEnd != pJunction );
    outLength = pRoad->getShape()->TotalLength;
}

void OptPathBuilder::appendTurnAround( const AiRoadIt &last,
                                       const int32_t routeIndex ) {
    // FUN_009469a0 (inlined at 0x00946e24)
    AiRoadIt exit;
    exit.setRoadExit( last.pRoad, last.bWay );
    float distance = ComputeRoadDistance( last, exit );
    float neededDistance =
        pVehicle->computeBrakingDistance( pSituation->CurSpeed, 0.0f, 0.0f ) *
            kTurnAroundBrakingFactor +
        kTurnAroundMargin;
    int32_t junctionRouteIndex = routeIndex;
    float widthRatioFrom = 0.0f;
    float widthRatioTo = 0.0f;
    float width = 0.0f;
    computeUTurnWidthRatios( last, widthRatioFrom, widthRatioTo, &width );
    bool bNarrow = width < kUTurnMinWidth;
    float widthRatio = computeWidthRatio( nullptr, last );

    if ( neededDistance <= distance && !bNarrow ) {
        pLine->appendRoadStretch( last, exit, routeIndex, widthRatio,
                                  widthRatioFrom );
        return;
    }
    if ( !pLine->appendRoadStretch( last, exit, routeIndex, widthRatio,
                                    widthRatio ) ) {
        return;
    }

    // Retail loop is only bounded by the line capacity
    for ( int32_t i = 0;
          ( neededDistance > distance || bNarrow ) && i < OptPath::kCapacity;
          i++ ) {
        junctionRouteIndex++;
        neededDistance -= distance;
        AiRoadIt entry;
        pickNextRoadEntry( entry, distance );
        if ( entry.pRoad == nullptr ) {
            return;
        }

        Eigen::Vector4f exitPosition;
        widthRatio = computeWidthRatio( &exitPosition, entry );
        if ( !appendJunction( last, exitPosition, entry, junctionRouteIndex,
                              widthRatio ) ) {
            pSituation->refreshLast();
            return;
        }
        computeUTurnWidthRatios( entry, widthRatioFrom, widthRatioTo, &width );
        bNarrow = width < kUTurnMinWidth;
        pSituation->refreshLast();

        AiRoadIt entryExit;
        entryExit.setRoadExit( entry.pRoad, entry.bWay );
        if ( !pLine->appendRoadStretch( last, entryExit, routeIndex, widthRatio,
                                        widthRatioFrom ) ) {
            pSituation->refreshLast();
            return;
        }
        if ( distance > neededDistance ) {
            return;
        }
    }
}

bool OptPathBuilder::extendAlongRoad( const AiRoadIt &from,
                                      const AiRoadIt &to,
                                      const int32_t routeIndex,
                                      const float widthRatio ) {
    // FUN_009467d0
    const float invLength = 1.0f / from.pRoad->getShape()->TotalLength;
    const float fractionFrom =
        from.pRoad->getShape()->computeRoadFraction( from );
    const float fractionTo = to.pRoad->getShape()->computeRoadFraction( to );
    if ( fabsf( fractionTo - fractionFrom ) > invLength * kMinElemSpacing ) {
        pLine->appendRoadStretch( from, to, routeIndex, widthRatio,
                                  widthRatio );
    } else {
        bool bWasSent = false;
        if ( pLine->getCount() != 0 ) {
            const OptPathElem &tail = pLine->back();
            if ( tail.Type == OptPathElem::ET_Road &&
                 tail.It.pRoad == to.pRoad && tail.It.bWay == to.bWay ) {
                bWasSent = tail.Id == pLine->getLastSentId();
                pLine->popBack();
            }
        }
        pLine->seed( to, routeIndex, widthRatio, true );
        if ( bWasSent ) {
            pLine->setLastSentId( pLine->back().Id );
        }
    }

    const AiRoadIt &tailIt = pLine->back().It;
    return tailIt.isSamePosition( to, 0.0f ) || to.isBefore( tailIt );
}

bool OptPathBuilder::finishRoadAndCross( const AiRoadIt &last,
                                         const AiRoadIt &nextFrom,
                                         const int32_t routeIndex ) {
    // FUN_00946630
    const float nextWidthRatio = computeDefaultWidthRatio( nextFrom );
    const OptPathElem &lastElem = pSituation->Last;
    const bool bOptimised = !IsZeroPosition( lastElem.OptPosition );
    Eigen::Vector4f position =
        bOptimised ? lastElem.OptPosition : lastElem.Position;
    const float widthRatio =
        bOptimised ? lastElem.OptWidthRatio : lastElem.WidthRatio;

    AiRoadIt exit;
    exit.setRoadExit( last.pRoad, last.bWay );
    if ( !last.isSamePosition( exit, 0.0f ) ) {
        pLine->seed( exit, pSituation->Last.RouteIndex, widthRatio, true );
        pSituation->refreshLast();
        position = pSituation->Last.Position;
    }

    const AiRoadIt lastIt = pSituation->Last.It;
    const bool bResult = appendJunction( lastIt, position, nextFrom, routeIndex,
                                         nextWidthRatio );
    pSituation->refreshLast();
    return bResult;
}

bool OptPathBuilder::appendJunction( const AiRoadIt &last,
                                     const Eigen::Vector4f &startPosition,
                                     const AiRoadIt &next,
                                     const int32_t routeIndex,
                                     const float widthRatio ) {
    // FUN_00944f90
    if ( last.pRoad == nullptr || next.pRoad == nullptr ) {
        return false;
    }
    if ( pLine->getCount() + 3 >= pLine->getCapacity() ) {
        return false;
    }
    if ( pLine->getCount() == 0 ) {
        float startWidthRatio = 0.0f;
        ComputeWidthRatioAt( last, startPosition, startWidthRatio );
        pLine->seed( last, 0, startWidthRatio, false );
    }
    if ( pLine->back().Type != OptPathElem::ET_Road ) {
        while ( pLine->getCount() != 0 &&
                pLine->back().Type != OptPathElem::ET_Road ) {
            pLine->popBack();
        }
        return false;
    }

    const AiRoadIt tailIt = pLine->back().It;
    AiRoadMap::Junction *pJunction = GetExitJunction( tailIt );
    if ( pJunction == nullptr || tailIt.pRoad == nullptr ) {
        return false;
    }

    const float turnAngle =
        pJunction->computeRoadAngle( pJunction->getRoadIndex( next.pRoad ) ) -
        pJunction->computeRoadAngle( pJunction->getRoadIndex( tailIt.pRoad ) );
    const Eigen::Vector4f start = pLine->back().Position;
    float lateral = 0.0f;
    float height = 0.0f;
    next.getEntity()->pProfile->computeWidthOffset( widthRatio, lateral,
                                                    height );
    const Eigen::Vector4f end = next.computeOffsetPosition( lateral, height );

    Eigen::Vector4f pointA;
    Eigen::Vector4f pointB;
    bool bJunctionUTurn = false;
    bool bMouthPoints = false;
    if ( pJunction->Flags == 1 ) {
        const Eigen::Vector4f directionLast = last.computeDirection();
        const Eigen::Vector4f directionNext = next.computeDirection();
        const float angle =
            fabsf( atan2f( directionNext.z() * directionLast.x() -
                               directionLast.z() * directionNext.x(),
                           directionNext.z() * directionLast.z() +
                               directionNext.x() * directionLast.x() ) );
        if ( angle < kJunctionThroughAngle ) {
            const int32_t startSide = pJunction->computeSideOfLine(
                Eigen::Vector2f( start.x(), start.z() ) );
            const Eigen::Vector4f firstMid =
                GetJunctionMouthCentre( pJunction, startSide, true );
            const Eigen::Vector4f secondMid =
                GetJunctionMouthCentre( pJunction, startSide, false );
            if ( ( secondMid - startPosition ).norm() >
                 ( firstMid - startPosition ).norm() ) {
                pointA = firstMid;
                pointB = secondMid;
            } else {
                const int32_t endSide = pJunction->computeSideOfLine(
                    Eigen::Vector2f( end.x(), end.z() ) );
                if ( endSide == startSide ) {
                    pointA = secondMid;
                    pointB = firstMid;
                } else {
                    pointA =
                        GetJunctionMouthCentre( pJunction, endSide, false );
                    pointB = GetJunctionMouthCentre( pJunction, endSide, true );
                }
            }
            bMouthPoints = true;
        } else if ( angle > kJunctionUTurnAngle ) {
            bJunctionUTurn = true;
            pointA = GetJunctionMouthCentre(
                pJunction,
                pJunction->computeSideOfLine(
                    Eigen::Vector2f( start.x(), start.z() ) ),
                false );
            pointB = GetJunctionMouthCentre(
                pJunction,
                pJunction->computeSideOfLine(
                    Eigen::Vector2f( end.x(), end.z() ) ),
                false );
            bMouthPoints = true;
        }
    }
    if ( !bMouthPoints && pJunction->Flags != 0 && pJunction->Flags != 2 ) {
        OTDU_UNIMPLEMENTED_ONCE;
    }
    if ( !bMouthPoints ) {
        const bool bStraight =
            pJunction->Flags == 2 || ( fabsf( turnAngle ) > kUTurnAngleMin &&
                                       fabsf( turnAngle ) < kUTurnAngleMax );
        if ( bStraight ) {
            pointA = start + ( end - start ) * kJunctionPointA;
            pointB = start + ( end - start ) * kJunctionPointB;
        } else {
            const float handleLength = ( end - start ).norm() * kOneThird;
            const Eigen::Vector4f directionIn =
                ScaleFlat( tailIt.computeDirection(), handleLength );
            const Eigen::Vector4f directionOut =
                ScaleFlat( next.computeDirection() * -1.0f, handleLength );
            CubicBezier curve;
            curve.ControlPoints[0] = start;
            curve.ControlPoints[1] = start + directionIn;
            curve.ControlPoints[2] = end + directionOut;
            curve.ControlPoints[3] = end;
            pointA = curve.evaluate( kJunctionPointA );
            pointB = curve.evaluate( kJunctionPointB );
        }
    }

    OptPathElem elem;
    elem.Type = OptPathElem::ET_Junction;
    if ( bJunctionUTurn ) {
        elem.Marker = OptPathElem::EM_JunctionUTurn;
    }
    elem.Position = Eigen::Vector4f( pointA.x(), pointA.y(), pointA.z(), 1.0f );
    elem.pJunction = pJunction;
    elem.JunctionLocalX = pointA.x() - pJunction->Position.X;
    elem.JunctionLocalZ = pointA.z() - pJunction->Position.Z;
    pLine->push( elem, true, -1 );

    const AiRoadMap::Junction *pSecondJunction = ( pLine->getCount() > 0 )
                                                     ? pLine->back().pJunction
                                                     : GetExitJunction( last );
    elem.Position = Eigen::Vector4f( pointB.x(), pointB.y(), pointB.z(), 1.0f );
    elem.pJunction = pSecondJunction;
    elem.JunctionLocalX = pointB.x() - pSecondJunction->Position.X;
    elem.JunctionLocalZ = pointB.z() - pSecondJunction->Position.Z;
    pLine->push( elem, true, -1 );

    pLine->seed( next, routeIndex, widthRatio, true );
    return true;
}

void OptPathBuilder::extendSimple() {
    // FUN_00947630
    AiVehController *pController = pVehicle->getController();
    const int32_t numLegs = pController->getCount();
    pSituation->refreshLast();
    if ( pLine->getCount() != 0 ) {
        pLine->updateCurrentIndex( &pSituation->LineDistance );
    }
    int32_t leg = pSituation->Last.RouteIndex;
    if ( leg < 0 ) {
        return;
    }

    pLine->setBatchAppend( true );
    while ( leg < numLegs ) {
        pSituation->refreshLast();
        AiRoadIt from;
        AiRoadIt to;
        pController->getAt( leg, from, to );
        const AiRoadIt &last = pSituation->Last.It;
        // Retail spins forever here
        if ( pSituation->Last.Type != OptPathElem::ET_Road ) {
            break;
        }
        if ( pSituation->Last.RouteIndex != leg || to.pRoad != last.pRoad ) {
            break;
        }

        if ( to.bWay != last.bWay ) {
            if ( pVehicle->getBehaviourType() != 0 || pLine->isFull() ) {
                break;
            }
            AiRoadIt reversed = last;
            reversed.flip();
            const float widthRatio =
                ( pSituation->LastWidthRatio > 0.0f ) ? -1.0f : 1.0f;
            pLine->seed( reversed, leg, widthRatio, true );
            pSituation->refreshLast();
        }

        if ( !last.isSamePosition( to, kSamePositionTolerance ) &&
             !to.isBefore( last ) ) {
            const float widthRatio = computeWidthRatio( nullptr, last );
            if ( !extendAlongRoad( last, to, leg, widthRatio ) ) {
                pSituation->refreshLast();
                break;
            }
            pSituation->refreshLast();
            continue;
        }

        const int32_t next = leg + 1;
        if ( next >= numLegs ) {
            break;
        }
        AiRoadIt nextFrom;
        AiRoadIt nextTo;
        pController->getAt( next, nextFrom, nextTo );
        if ( nextFrom.pRoad == nullptr || nextTo.pRoad == nullptr ) {
            break;
        }
        if ( to.pRoad->ID == nextFrom.pRoad->ID && to.bWay != nextFrom.bWay ) {
            if ( pLine->isFull() ) {
                break;
            }
            AiRoadIt reversed = last;
            reversed.flip();
            const float widthRatio =
                ( pSituation->LastWidthRatio > 0.0f ) ? -1.0f : 1.0f;
            pLine->seed( reversed, next, widthRatio, true );
            pSituation->refreshLast();
            leg = next;
            continue;
        }

        Eigen::Vector4f exitPosition;
        const float widthRatio = computeWidthRatio( &exitPosition, nextFrom );
        if ( !appendJunction( last, exitPosition, nextFrom, next,
                              widthRatio ) ) {
            pSituation->refreshLast();
            break;
        }
        leg = next;
    }
    pLine->setBatchAppend( false );
    pLine->recomputeAllRadii();
    if ( pLine->getCount() != 0 ) {
        pLine->updateCurrentIndex( &pSituation->LineDistance );
    }
    pSituation->refreshLast();
    pSituation->checkLineMismatch();
}

float OptPathBuilder::computeRoadSideWidthRatio( const AiRoadIt &it ) const {
    // FUN_00943850
    const AiRoadProfile *pProfile = it.getEntity()->pProfile;
    const float ratio = pVehicle->getCarState()->getDimensions().x() /
                        ( pProfile->InnerWidth * 0.5f );
    return it.bWay ? 1.0f - ratio : ratio - 1.0f;
}

void OptPathBuilder::extendAlongRoadSide() {
    // FUN_00947130
    AiVehController *pController = pVehicle->getController();
    const int32_t numLegs = pController->getCount();
    pSituation->refreshLast();
    if ( pLine->getCount() != 0 ) {
        pLine->updateCurrentIndex( &pSituation->LineDistance );
    }
    int32_t leg = pSituation->Last.RouteIndex;
    if ( leg < 0 ) {
        return;
    }

    pLine->setBatchAppend( true );
    while ( leg < numLegs ) {
        pSituation->refreshLast();
        AiRoadIt from;
        AiRoadIt to;
        pController->getAt( leg, from, to );
        const AiRoadIt &last = pSituation->Last.It;
        // Retail spins forever here
        if ( pSituation->Last.Type != OptPathElem::ET_Road ) {
            break;
        }
        if ( pSituation->Last.RouteIndex != leg || to.pRoad != last.pRoad ) {
            break;
        }

        if ( to.bWay != last.bWay ) {
            if ( pVehicle->getBehaviourType() != 0 || pLine->isFull() ) {
                break;
            }
            AiRoadIt reversed = last;
            reversed.flip();
            const float widthRatio =
                ( pSituation->LastWidthRatio > 0.0f ) ? -1.0f : 1.0f;
            pLine->seed( reversed, leg, widthRatio, true );
            pSituation->refreshLast();
        }

        if ( !last.isSamePosition( to, kSamePositionTolerance ) &&
             !to.isBefore( last ) ) {
            const float widthRatio = computeRoadSideWidthRatio( last );
            if ( !extendAlongRoad( last, to, leg, widthRatio ) ) {
                pSituation->refreshLast();
                break;
            }
            pSituation->refreshLast();
            continue;
        }

        const int32_t next = leg + 1;
        if ( next >= numLegs ) {
            break;
        }
        AiRoadIt nextFrom;
        AiRoadIt nextTo;
        pController->getAt( next, nextFrom, nextTo );
        if ( nextFrom.pRoad == nullptr || nextTo.pRoad == nullptr ) {
            break;
        }
        if ( to.pRoad->ID == nextFrom.pRoad->ID && to.bWay != nextFrom.bWay ) {
            if ( pLine->isFull() ) {
                break;
            }
            AiRoadIt reversed = last;
            reversed.flip();
            const float widthRatio =
                ( pSituation->LastWidthRatio > 0.0f ) ? -1.0f : 1.0f;
            pLine->seed( reversed, next, widthRatio, true );
            pSituation->refreshLast();
            leg = next;
            continue;
        }

        const float widthRatio = computeRoadSideWidthRatio( nextFrom );
        const Eigen::Vector4f exitPosition =
            OptPath::GetEffectivePosition( pSituation->Last );
        if ( !appendJunction( last, exitPosition, nextFrom, next,
                              widthRatio ) ) {
            pSituation->refreshLast();
            break;
        }
        leg = next;
    }
    pLine->setBatchAppend( false );
    pLine->recomputeAllRadii();
    if ( pLine->getCount() != 0 ) {
        pLine->updateCurrentIndex( &pSituation->LineDistance );
    }
    pSituation->refreshLast();
    pSituation->checkLineMismatch();
}

void OptPathBuilder::extendFull() {
    // FUN_009469a0
    AiVehController *pController = pVehicle->getController();
    const int32_t numLegs = pController->getCount();
    pSituation->refreshLast();
    if ( pLine->getCount() != 0 ) {
        pLine->updateCurrentIndex( &pSituation->LineDistance );
    }
    int32_t leg = std::max( pSituation->Last.RouteIndex, 0 );

    pLine->setBatchAppend( true );
    while ( leg < numLegs ) {
        pSituation->refreshLast();
        AiRoadIt from;
        AiRoadIt to;
        pController->getAt( leg, from, to );
        // Retail spins forever on the two breaks below
        if ( pSituation->Last.Type != OptPathElem::ET_Road ) {
            break;
        }
        const AiRoadIt &last = pSituation->Last.It;

        if ( pSituation->Last.RouteIndex != leg ) {
            if ( numLegs == 0 || leg != 0 ) {
                break;
            }
            if ( pSituation->Last.RouteIndex != -1 ) {
                break;
            }
            if ( GetExitJunction( last ) == GetEntryJunction( from ) &&
                 last.pRoad != from.pRoad ) {
                if ( !finishRoadAndCross( last, from, 0 ) ) {
                    break;
                }
            } else {
                if ( !isCarInJunctionOf( from ) || pLine->isFull() ) {
                    break;
                }
                pLine->seed( from, 0, computeDefaultWidthRatio( from ), true );
                pSituation->refreshLast();
            }
            continue;
        }

        if ( to.pRoad != last.pRoad ) {
            if ( GetExitJunction( last ) == GetEntryJunction( from ) &&
                 last.pRoad != from.pRoad ) {
                if ( !finishRoadAndCross( last, from, leg ) ) {
                    break;
                }
                continue;
            }

            pSituation->checkLineMismatch();
            pLine->reset( true );
            reset( false );
            pSituation->refreshLast();
            pPath->reset( true );
            pLine->setField0x128( -1 );
            pLine->setResampleCursor( 0 );
            pSituation->refreshLastPathPoint();
            pLine->seed( from, 0, computeDefaultWidthRatio( from ), true );
            pSituation->LastValidRoadIt = from;
            pSituation->refreshLast();
        } else if ( to.bWay != last.bWay ) {
            appendTurnAround( last, leg );
            break;
        }

        if ( !last.isSamePosition( to, kSamePositionTolerance ) &&
             !to.isBefore( last ) ) {
            const float widthRatio = computeWidthRatio( nullptr, last );
            if ( !extendAlongRoad( last, to, leg, widthRatio ) ) {
                pSituation->refreshLast();
                break;
            }
            continue;
        }

        const int32_t next = leg + 1;
        if ( next >= numLegs ) {
            OptPathElem &tail = pLine->back();
            if ( tail.Marker == OptPathElem::EM_None ) {
                tail.Marker = OptPathElem::EM_End;
            }
            break;
        }
        AiRoadIt nextFrom;
        AiRoadIt nextTo;
        pController->getAt( next, nextFrom, nextTo );
        if ( nextFrom.pRoad == nullptr || nextTo.pRoad == nullptr ) {
            break;
        }
        if ( to.pRoad->ID == nextFrom.pRoad->ID && to.bWay != nextFrom.bWay ) {
            if ( pLine->getCount() + 1 >= OptPath::kCapacity ) {
                break;
            }
            float widthRatioFrom = 0.0f;
            float widthRatioTo = 0.0f;
            computeUTurnWidthRatios( last, widthRatioFrom, widthRatioTo,
                                     nullptr );
            pLine->popBack();
            pLine->pushUTurn( to, leg, next, widthRatioFrom, widthRatioTo );
            pSituation->refreshLast();
            break;
        }

        Eigen::Vector4f exitPosition;
        const float widthRatio = computeWidthRatio( &exitPosition, nextFrom );
        if ( !appendJunction( last, exitPosition, nextFrom, next,
                              widthRatio ) ) {
            pSituation->refreshLast();
            break;
        }
        leg = next;
    }
    pLine->setBatchAppend( false );
    pLine->recomputeAllRadii();
    if ( pLine->getCount() != 0 ) {
        pLine->updateCurrentIndex( &pSituation->LineDistance );
    }
    pSituation->refreshLast();
    pSituation->checkLineMismatch();
}

void OptPathBuilder::resample() {
    // FUN_00945b00
    if ( pPath->isFull() ) {
        return;
    }

    pPath->setBatchAppend( true );
    if ( pSituation->pOptPath->getCount() != 0 ) {
        pSituation->pOptPath->updateCurrentIndex( &pSituation->LineDistance );
    }
    pSituation->refreshLast();
    pSituation->refreshLastPathPoint();

    for ( int32_t i = pLine->getResampleCursor(); i < pLine->getCount() - 1;
          i++ ) {
        OptPathElem &elemA = pLine->at( i );
        OptPathElem &elemB = pLine->at( i + 1 );
        if ( elemA.Type == OptPathElem::ET_Road &&
             elemB.Type == OptPathElem::ET_Road ) {
            pLine->setResampleCursor( i + 1 );
        }
        const Eigen::Vector4f positionA = GetElemPosition( elemA );
        const float widthA = GetElemWidthRatio( elemA );
        Eigen::Vector4f positionB = GetElemPosition( elemB );
        float widthB = GetElemWidthRatio( elemB );

        if ( elemA.Type == OptPathElem::ET_Road &&
             elemB.Type == OptPathElem::ET_Road &&
             elemA.It.pRoad == elemB.It.pRoad ) {
            if ( elemA.It.bWay != elemB.It.bWay ) {
                if ( !pPath->appendUTurn( elemA.It, elemA.WidthRatio,
                                          elemB.WidthRatio ) ) {
                    pLine->setResampleCursor( i );
                    pSituation->refreshLastPathPoint();
                }
                break;
            }
            if ( elemA.Marker == OptPathElem::EM_AfterUTurn &&
                 i > pLine->getCurrentIndex() ) {
                pLine->setResampleCursor( i );
                pSituation->refreshLastPathPoint();
                break;
            }

            OptSpanKeys keys;
            keys.IndexA = i;
            keys.IndexB = i + 1;
            keys.KeyA = Eigen::Vector2f(
                elemA.It.pRoad->getShape()->computeRoadFraction( elemA.It ),
                widthA );
            keys.KeyB = Eigen::Vector2f(
                elemB.It.pRoad->getShape()->computeRoadFraction( elemB.It ),
                widthB );
            pLine->computeSpanKeys( keys );
            if ( !pPath->appendRoadBezier(
                     elemA.It, elemB.It, keys.KeyA, keys.Handle1, keys.Handle2,
                     keys.KeyB, elemA.Position, elemB.Position ) ) {
                pLine->setResampleCursor( i );
                pSituation->refreshLastPathPoint();
                break;
            }
            elemB.TurningSpeed =
                pVehicle->computeTurningSpeed( pPath->back().Radius );
            pSituation->refreshLastPathPoint();
        } else if ( elemA.Type != OptPathElem::ET_Junction &&
                    elemB.Type != OptPathElem::ET_Junction &&
                    elemA.It.pRoad == elemB.It.pRoad ) {
            // Nothing to emit (e.g. end of the line)
        } else {
            const AiRoadMap::Junction *pJunction = nullptr;
            if ( elemA.Type == OptPathElem::ET_Junction ) {
                pJunction = elemA.pJunction;
            } else if ( elemA.OptWidthRatio > OptPath::kUnsetWidthRatio ) {
                const OptPathElem &elemC = pLine->at( i + 3 );
                pLine->blendLateral( i, i + 3, elemA.OptWidthRatio,
                                     GetElemWidthRatio( elemC ) );
                positionB = elemB.OptPosition;
                widthB = elemB.OptWidthRatio;
            }
            if ( elemB.Type == OptPathElem::ET_Junction ) {
                pJunction = elemB.pJunction;
            }
            if ( pJunction == nullptr ) {
                pJunction = elemA.It.bWay ? elemA.It.pRoad->pJunctionEnd
                                          : elemA.It.pRoad->pJunctionBegin;
            }

            OptSpanHandles handles;
            handles.IndexA = i;
            handles.IndexB = i + 1;
            handles.PositionA = positionA;
            handles.PositionB = positionB;
            pLine->computeSpanHandles( handles );
            if ( !pPath->appendJunctionCurve( pJunction, positionA,
                                              handles.Handle1, handles.Handle2,
                                              positionB ) ) {
                while ( pPath->getNumPoints() != 0 &&
                        pPath->back().Type == NavPath::PT_Junction ) {
                    pPath->popBack();
                }
                break;
            }
            elemB.TurningSpeed =
                pVehicle->computeTurningSpeed( pPath->back().Radius );
            if ( elemB.Type == OptPathElem::ET_Road ) {
                while ( pPath->getNumPoints() != 0 &&
                        pPath->back().Type == NavPath::PT_Junction ) {
                    pPath->popBack();
                    pPath->pushRoadPoint( elemB.It, widthB, kJunctionCurveSpeed,
                                          true );
                }
                pLine->setResampleCursor( i + 1 );
            }
        }
        if ( pPath->isFull() ) {
            break;
        }
    }
    pPath->setBatchAppend( false );
    pPath->recomputeAllRadii();
    pSituation->refreshLastPathPoint();
}
