#include "shared.h"
#include "ai_road_shape.h"

#include "ai/gs_ai.h"
#include "game/game_object.h"

#include "ai/ai_pool.h"
#include "core/maths_helper.h"
#include "system/system_shared.h"

static constexpr float kMaxVerticalGap = 2.0f;          // DAT_00f416b0
static constexpr float kFarDistanceSq = 10000000000.0f; // DAT_00f41b80

uint32_t gRoadShapeUseCounter = 0;  // DAT_010e740c
bool gbUniformRoadFraction = false; // DAT_016a2c3b

AiRoadShape *AiRoadMap::Road::getShape() {
    // FUN_00932350
    if ( pDetail == nullptr ) {
        return nullptr;
    }

    if ( pShape == nullptr ) {
        pShape = gpAI->getAIPool()->getUnusedShape();
        pShape->init( this );
    }

    gRoadShapeUseCounter++;
    if ( gRoadShapeUseCounter == 0 ) {
        gRoadShapeUseCounter = 1;
    }
    pShape->LastUse = gRoadShapeUseCounter;
    return pShape;
}

void AiRoadShape::init( AiRoadMap::Road *pOwnerRoad ) {
    // FUN_0092d510
    pRoad = pOwnerRoad;
    NumVertex = 0;
    NumWaypoints = 0;
    NumCurves = 0;

    const AiRoadMap::RoadDetail *pDetail = pOwnerRoad->pDetail;
    for ( int32_t i = 0; i < pDetail->NumEntities; i++ ) {
        const AiRoadMap::Entity *pEntity = pDetail->pEntities[i];
        NumVertex += pEntity->NumVertex;
        NumWaypoints += pEntity->getNumWaypoints();
        NumCurves += pEntity->getNumCurves();
    }

    pFirstPhantom = nullptr;
    pLastPhantom = nullptr;
    NumPhantoms = 0;
    buildCurveList();
}

void AiRoadShape::clear() {
    // FUN_0092c9c0
    AiRoadBuckets &buckets = gpAI->getRoadBuckets();
    AiRoadBuckets::PhantomNode *pPhantom = pFirstPhantom;
    while ( pPhantom != nullptr ) {
        AiRoadBuckets::PhantomNode *pNext = pPhantom->pNext;
        buckets.freeNode( pPhantom );
        pPhantom = pNext;
    }
    pFirstPhantom = nullptr;
    pLastPhantom = nullptr;
    NumPhantoms = 0;

    CurveCalc *pCurve = pCurveList;
    while ( pCurve != nullptr ) {
        CurveCalc *pNext = pCurve->pNext;
        TestDrive::Free( pCurve );
        pCurve = pNext;
    }
    pCurveList = nullptr;
    pRoad->pShape = nullptr;
    pRoad = nullptr;
    LastUse = 0;
}

void AiRoadShape::buildCurveList() {
    // FUN_0092ceb0
    TotalLength = 0.0f;

    AiRoadIt it;
    it.pRoad = pRoad;
    it.bWay = true;
    it.T = 0.0f;

    CurveCalc *pPrevious = nullptr;
    CurveCalc *pCurve = nullptr;
    int32_t entityIndex = 0;
    int32_t curveIndex = 0;
    for ( ;; ) {
        it.IndEntity = entityIndex;
        it.IndWay = curveIndex;

        pCurve =
            static_cast<CurveCalc *>( TestDrive::Alloc( sizeof( CurveCalc ) ) );
        pCurve->IndEntity = entityIndex;
        pCurve->IndCurve = curveIndex;
        pCurve->Length = it.computeCurveLength( 0.0f, 1.0f );
        TotalLength += pCurve->Length;
        if ( pPrevious != nullptr ) {
            pPrevious->pNext = pCurve;
        } else {
            pCurveList = pCurve;
        }
        pPrevious = pCurve;

        curveIndex++;
        if ( curveIndex != it.getNumCurves() ) {
            continue;
        }
        if ( it.isAtRoadEnd() ) {
            break;
        }
        entityIndex++;
        curveIndex = 0;
    }
    pCurve->pNext = nullptr;
}

float AiRoadShape::computeRoadFraction( const AiRoadIt &it ) const {
    // FUN_0092c700
    const float t = it.bWay ? it.T : 1.0f - it.T;
    float distance = 0.0f;
    for ( const CurveCalc *pCurve = pCurveList; pCurve != nullptr;
          pCurve = pCurve->pNext ) {
        if ( pCurve->IndEntity == it.IndEntity &&
             pCurve->IndCurve == it.IndWay ) {
            return ( pCurve->Length * t + distance ) / TotalLength;
        }
        distance += pCurve->Length;
    }
    return 0.0f;
}

float AiRoadShape::computeUniformRoadFraction( const AiRoadIt &it ) const {
    // FUN_0092c690
    const float curveFraction = 1.0f / static_cast<float>( NumCurves );
    float fraction = 0.0f;
    for ( const CurveCalc *pCurve = pCurveList;; ) {
        if ( pCurve->IndEntity == it.IndEntity &&
             pCurve->IndCurve == it.IndWay ) {
            const float t = it.bWay ? it.T : 1.0f - it.T;
            const float roadFraction = t * curveFraction + fraction;
            return ( 1.0f < roadFraction ) ? 1.0f : roadFraction;
        }
        fraction += curveFraction;
        pCurve = pCurve->pNext;
        if ( pCurve == nullptr ) {
            return 1.0f;
        }
    }
}

void AiRoadShape::computeIt( const float fraction, AiRoadIt &outIt ) const {
    // FUN_0092c770
    const float distance = TotalLength * fraction;
    float covered = 0.0f;
    for ( const CurveCalc *pCurve = pCurveList; pCurve != nullptr;
          pCurve = pCurve->pNext ) {
        if ( pCurve->Length + covered < distance ) {
            covered += pCurve->Length;
            continue;
        }
        float t = ( distance - covered ) / pCurve->Length;
        if ( t < 0.0f ) {
            t = 0.0f;
        } else if ( 1.0f < t ) {
            t = 1.0f;
        }
        outIt.pRoad = pRoad;
        outIt.IndEntity = pCurve->IndEntity;
        outIt.IndWay = pCurve->IndCurve;
        outIt.bWay = true;
        outIt.T = t;
        return;
    }
}

void AiRoadShape::computeUniformIt( float fraction, AiRoadIt &outIt ) const {
    // FUN_0092ccc0
    static constexpr float kMaxCurveT = 0.999f; // DAT_00f41e54

    const float step = 1.0f / static_cast<float>( NumCurves );
    float covered = step;
    const CurveCalc *pCurve = pCurveList;
    while ( fraction > covered ) {
        pCurve = pCurve->pNext;
        covered += step;
        if ( pCurve == nullptr ) {
            const AiRoadMap::RoadDetail *pDetail = pRoad->pDetail;
            const int32_t lastEntity = pDetail->NumEntities - 1;
            outIt.pRoad = pRoad;
            outIt.IndEntity = lastEntity;
            outIt.IndWay =
                ( pDetail->pEntities[lastEntity]->NumVertex + 2 ) / 3 - 2;
            outIt.bWay = true;
            outIt.T = 1.0f;
            return;
        }
    }

    float t = ( fraction - ( covered - step ) ) / step;
    if ( 0.0f > t ) {
        t = 0.0f;
    } else if ( t > kMaxCurveT ) {
        t = 1.0f;
    }
    outIt.pRoad = pRoad;
    outIt.IndEntity = pCurve->IndEntity;
    outIt.IndWay = pCurve->IndCurve;
    outIt.bWay = true;
    outIt.T = t;
}

void AiRoadShape::computeLateralPosition( const float fraction,
                                          const float lateralRatio,
                                          Eigen::Vector4f &outPosition ) const {
    // FUN_0092cd90
    AiRoadIt it;
    computeIt( fraction, it );
    if ( it.pRoad == nullptr ) {
        return;
    }
    float lateral = 0.0f;
    float height = 0.0f;
    it.getEntity()->pProfile->computeWidthOffset( lateralRatio, lateral,
                                                  height );
    outPosition = it.computeOffsetPosition( lateral, height );
}

void AiRoadShape::addPhantom( GameObject *pObject,
                              const int32_t entityIndex,
                              const int32_t curveIndex,
                              const int32_t roadID ) {
    // FUN_0092c840
    if ( pObject == nullptr ) {
        return;
    }
    AiRoadBuckets::PhantomNode *pNode = gpAI->getRoadBuckets().allocNode();
    if ( pNode == nullptr ) {
        return;
    }
    pNode->RoadID = roadID;
    pNode->EntityIndex = entityIndex;
    pNode->CurveIndex = curveIndex;
    pNode->pObject = pObject;

    AiRoadBuckets::PhantomNode *pAfter = pLastPhantom;
    while ( pAfter != nullptr && ( entityIndex < pAfter->EntityIndex ||
                                   ( entityIndex == pAfter->EntityIndex &&
                                     curveIndex < pAfter->CurveIndex ) ) ) {
        pAfter = pAfter->pPrevious;
    }

    if ( pAfter == nullptr ) {
        pNode->pPrevious = nullptr;
        pNode->pNext = pFirstPhantom;
        if ( pFirstPhantom != nullptr ) {
            pFirstPhantom->pPrevious = pNode;
        } else {
            pLastPhantom = pNode;
        }
        pFirstPhantom = pNode;
    } else {
        pNode->pPrevious = pAfter;
        pNode->pNext = pAfter->pNext;
        if ( pAfter->pNext != nullptr ) {
            pAfter->pNext->pPrevious = pNode;
        } else {
            pLastPhantom = pNode;
        }
        pAfter->pNext = pNode;
    }
    NumPhantoms++;
}

void AiRoadShape::removePhantom( const GameObject *pObject,
                                 const int32_t entityIndex,
                                 const int32_t curveIndex ) {
    // FUN_0092c7f0
    for ( AiRoadBuckets::PhantomNode *pNode = pFirstPhantom; pNode != nullptr;
          pNode = pNode->pNext ) {
        if ( pNode->pObject->getID() != pObject->getID() ||
             pNode->EntityIndex != entityIndex ||
             pNode->CurveIndex != curveIndex ) {
            continue;
        }
        if ( pNode->pPrevious != nullptr ) {
            pNode->pPrevious->pNext = pNode->pNext;
        } else {
            pFirstPhantom = pNode->pNext;
        }
        if ( pNode->pNext != nullptr ) {
            pNode->pNext->pPrevious = pNode->pPrevious;
        } else {
            pLastPhantom = pNode->pPrevious;
        }
        NumPhantoms--;
        gpAI->getRoadBuckets().freeNode( pNode );
        return;
    }
}

float ComputeRoadDistance( const AiRoadShape *pShape,
                           const bool bCovered,
                           const AiRoadIt &it ) {
    // FUN_00934f40
    if ( pShape != nullptr ) {
        float fraction = gbUniformRoadFraction
                             ? pShape->computeUniformRoadFraction( it )
                             : pShape->computeRoadFraction( it );
        if ( it.bWay ) {
            fraction = 1.0f - fraction;
        }
        const float length = pShape->TotalLength;
        return bCovered ? length - length * fraction : length * fraction;
    }

    const bool bFromStart = bCovered ? it.bWay : !it.bWay;
    const Eigen::Vector4f position = it.computePosition( -1 );
    AiRoadVertexIt endIt;
    endIt.initAtRoadEnd( it.pRoad, bFromStart );
    return ( position - endIt.getWaypoint().toVector4() ).norm();
}

void ProjectOnRoadShape( const Matrix4x4f_t &frame,
                         AiRoadIt &io,
                         Matrix4x4f_t *pOutFrame,
                         const bool bBank,
                         const AiRoadShape *pShape ) {
    // FUN_0092d5a0
    AiRoadMap::Road *pRoad = pShape->pRoad;
    const Eigen::Vector4f point = frame.row( 3 ).transpose();

    AiRoadVertexIt vertexIt;
    vertexIt.pRoad = pRoad;
    vertexIt.pDetail = pRoad->pDetail;
    vertexIt.IndEntity = 0;
    vertexIt.IndVertex = 0;
    vertexIt.bWay = true;

    // Waypoints at the same height are preferred over the ones above/below
    // (e.g. bridges)
    AiRoadVertexIt nearestIt;
    nearestIt.pRoad = nullptr;
    AiRoadVertexIt nearestOtherLevelIt = vertexIt;
    float nearestDistanceSq = kFarDistanceSq;
    float nearestOtherLevelDistanceSq = kFarDistanceSq;
    do {
        const Eigen::Vector4f delta =
            vertexIt.getWaypoint().toVector4() - point;
        const float distanceSq = delta.squaredNorm();
        if ( kMaxVerticalGap <= std::abs( delta.y() ) ) {
            if ( distanceSq < nearestOtherLevelDistanceSq ) {
                nearestOtherLevelIt = vertexIt;
                nearestOtherLevelDistanceSq = distanceSq;
            }
        } else if ( distanceSq < nearestDistanceSq ) {
            nearestIt = vertexIt;
            nearestDistanceSq = distanceSq;
        }
    } while ( vertexIt.advance( false ) );

    if ( nearestIt.pRoad == nullptr ) {
        nearestIt = nearestOtherLevelIt;
        nearestOtherLevelIt = AiRoadVertexIt();
    }

    Eigen::Vector4f position;
    Eigen::Vector4f tangent;
    const bool bProjected =
        nearestIt.projectOnAdjacentCurves( point, position, tangent, io );

    AiRoadIt otherLevelIt;
    otherLevelIt.pRoad = nullptr;
    if ( nearestOtherLevelIt.pRoad != nullptr ) {
        Eigen::Vector4f otherLevelPosition;
        Eigen::Vector4f otherLevelTangent;
        if ( nearestOtherLevelIt.projectOnAdjacentCurves(
                 point, otherLevelPosition, otherLevelTangent,
                 otherLevelIt ) ) {
            if ( !bProjected || ( otherLevelPosition - point ).norm() <
                                    ( position - point ).norm() ) {
                position = otherLevelPosition;
                tangent = otherLevelTangent;
                io = otherLevelIt;
            }
        }
    }

    const Eigen::Vector4f direction = io.computeDirection();
    if ( direction.dot( frame.row( 2 ).transpose() ) < 0.0f ) {
        io.T = 1.0f - io.T;
        tangent *= -1.0f;
        io.bWay = !io.bWay;
    }

    if ( pOutFrame == nullptr ) {
        return;
    }

    Eigen::Vector4f side( tangent.z(), 0.0f, -tangent.x(), 0.0f );
    if ( bBank ) {
        float twist = io.computeTwist();
        if ( io.bWay ) {
            twist = -twist;
        }
        side.y() = side.norm() * twist;
    }
    side /= side.norm();

    const Eigen::Vector4f up = Vector4DCross( tangent, side );
    const Eigen::Vector4f right = Vector4DCross( up, tangent );
    pOutFrame->row( 0 ) = right.transpose();
    pOutFrame->row( 1 ) = up.transpose();
    pOutFrame->row( 2 ) = tangent.transpose();
    pOutFrame->row( 3 ) = position.transpose();
}
