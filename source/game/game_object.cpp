#include "shared.h"
#include "game_object.h"

#include "ai/ai_pool.h"
#include "ai/ai_road_shape.h"
#include "ai/gs_ai.h"
#include "game/vehicle/car_state.h"
#include "render/camera/camera_base.h"

GameObjectDico gGameObjectDico; // DAT_01449428

static constexpr float kJunctionSearchRadiusSq = 10000.0f;
static constexpr float kJunctionOutlineDistance = 3.0f;
static constexpr float kMaxDropBelowRoad = -1.0f;

static Eigen::Vector4f kDefaultVelocity = Eigen::Vector4f::Zero();

void ObjectWorldLocation::computeWorldPosition(
    Eigen::Vector4f &outPosition ) const {
    // FUN_0096ae20
    AiRoadMap::Road *pRoad = gpAI->getRoadMap().getRoad( LocationRoad.RoadID );
    if ( pRoad == nullptr ) {
        return;
    }
    const float fraction = std::clamp(
        ( LocationRoad.FractionMin + LocationRoad.FractionMax ) * 0.5f, 0.0f,
        1.0f );
    const float lateral = std::clamp(
        ( LocationRoad.LateralMin + LocationRoad.LateralMax ) * 0.5f, 0.0f,
        1.0f );
    pRoad->getShape()->computeLateralPosition( fraction, lateral, outPosition );
}

GameObject::GameObject()
    : objectID( 0 ),
      parentID( 0 ),
      pNextDico( nullptr ),
      pNextInBucket( nullptr ),
      worldLocation(),
      netID( 0 ),
      pNetOwner( nullptr ) {}

GameObject::~GameObject() {}

bool GameObject::isInFrustum( Camera *param_2,
                              float inSphereRadius,
                              Eigen::Vector4f *pSpherePos ) {
    // FUN_0096abd0
    Eigen::Vector4f spherePosition = Eigen::Vector4f::Zero();
    float sphereRadius = 0.0f;
    if ( ( inSphereRadius < 0.0f ) || ( pSpherePos == nullptr ) ) {
        retrieveBoundingSphere( &spherePosition, &sphereRadius );
    } else {
        spherePosition = *pSpherePos;
        sphereRadius = inSphereRadius;
    }

    int32_t iVar1 = param_2->sphereInFrustum( &spherePosition, sphereRadius );
    return iVar1 != 0;
}

Eigen::Vector4f &GameObject::getLinearVelocity() {
    // FUN_00406f20
    return kDefaultVelocity;
}

void GameObject::setRoadLocation( const AiRoadIt &it, const bool bDebug ) {
    // FUN_0096b1b0
    const AiRoadShape *pShape = it.pRoad->getShape();
    Matrix4x4f_t box;
    retrieveBoundingBox( box );
    const Eigen::Vector4f center = it.computeOffsetPosition( 0.0f, 0.0f );

    Eigen::Vector4f direction = it.computeDirection();
    if ( !it.bWay ) {
        direction = -direction;
    }
    Eigen::Vector4f side = it.computeSideDirection();
    if ( ( side.array().square() <= 1.0e-10f ).all() ) {
        side = Eigen::Vector4f( direction.z(), 0.0f, -direction.x(), 0.0f );
    }
    // FUN_00406fa0 (side x direction)
    const Eigen::Vector3f up3 = side.head<3>().cross( direction.head<3>() );
    Eigen::Vector4f up( up3.x(), up3.y(), up3.z(), side.w() );
    if ( up.y() < 0.0f ) {
        side = -side;
        up = -up;
    }

    const float fraction = pShape->computeRoadFraction( it );
    const AiRoadProfile *pProfile = it.getEntity()->pProfile;
    const float halfInnerWidth = pProfile->InnerWidth * 0.5f;
    const Eigen::Vector4f toBox = box.row( 3 ).transpose() - center;
    const float lateralCenter =
        ( toBox.x() * side.x() + toBox.z() * side.z() ) / halfInnerWidth;

    Matrix4x4f_t roadFrame;
    roadFrame.row( 0 ) = ( side * halfInnerWidth ).transpose();
    roadFrame.row( 1 ) = ( direction * pShape->TotalLength ).transpose();
    roadFrame.row( 2 ) = up.transpose();
    roadFrame.row( 3 ) = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ).transpose();

    const Matrix4x4f_t invRoadFrame = roadFrame.inverse();

    box.row( 3 ) = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ).transpose();
    const Matrix4x4f_t boxInRoad = box * invRoadFrame;
    float minX = 1.0f;
    float maxX = -1.0f;
    float minY = 1.0f;

    float maxY = 0.0f;
    for ( int32_t i = 0; i < 8; i++ ) {
        const Eigen::RowVector4f corner( ( i & 1 ) ? 0.5f : -0.5f,
                                         ( i & 4 ) ? 0.5f : -0.5f,
                                         ( i & 2 ) ? 0.5f : -0.5f, 1.0f );
        const Eigen::RowVector4f point = corner * boxInRoad;
        minX = std::min( minX, point.x() );
        maxX = std::max( maxX, point.x() );
        minY = std::min( minY, point.y() );
        maxY = std::max( maxY, point.y() );
    }

    const float lateralLimit = pProfile->OuterWidth / pProfile->InnerWidth;
    const bool bRebucket =
        !( worldLocation.LocationType == ObjectWorldLocation::OWL_Road &&
           worldLocation.LocationRoad.RoadID == it.pRoad->ID );
    if ( bRebucket ) {
        gpAI->getRoadBuckets().removeObject( this );
    }
    worldLocation.LocationRoad.RoadID = it.pRoad->ID;
    worldLocation.LocationType = ObjectWorldLocation::OWL_Road;
    worldLocation.bWay = it.bWay ? 1 : 0;
    worldLocation.bNearJunction = 0;
    worldLocation.LocationRoad.FractionMin =
        std::clamp( fraction + minY, 0.0f, 1.0f );
    worldLocation.LocationRoad.FractionMax =
        std::clamp( fraction + maxY, 0.0f, 1.0f );
    worldLocation.LocationRoad.LateralMin =
        std::clamp( lateralCenter + minX, -lateralLimit, lateralLimit );
    worldLocation.LocationRoad.LateralMax =
        std::clamp( lateralCenter + maxX, -lateralLimit, lateralLimit );
    if ( bRebucket ) {
        gpAI->getRoadBuckets().insertObject( this );
    }

    const Eigen::RowVector4f velocity =
        getLinearVelocity().transpose() * invRoadFrame;
    worldLocation.VelocityX = velocity.y();
    worldLocation.VelocityZ = velocity.x();

    if ( bDebug ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: DEBUG (road frame axes)
    }
}

void GameObject::setJunctionLocation( const Matrix4x4f_t &worldMatrix,
                                      const AiRoadMap::Junction *pJunction ) {
    // FUN_0096ab20
    const bool bRebucket =
        !( worldLocation.LocationType == ObjectWorldLocation::OWL_Junction &&
           worldLocation.LocationOther.RoadID == pJunction->ID );
    if ( bRebucket ) {
        gpAI->getRoadBuckets().removeObject( this );
    }
    worldLocation.LocationOther.RoadID = pJunction->ID;
    worldLocation.LocationOther.PositionX =
        worldMatrix( 3, 0 ) - pJunction->Position.X;
    worldLocation.LocationOther.PositionZ =
        worldMatrix( 3, 2 ) - pJunction->Position.Z;
    worldLocation.LocationType = ObjectWorldLocation::OWL_Junction;
    worldLocation.bNearJunction = 0;
    if ( bRebucket ) {
        gpAI->getRoadBuckets().insertObject( this );
    }
}

GameObjectDico::GameObjectDico()
    : idCounter( 0 ), pFirstObject( nullptr ), numObjects( 0 ) {}

void GameObjectDico::registerObject( GameObject *pObject ) {
    // FUN_0084a5d0 (inlined)
    if ( pObject->objectID != 0 ) {
        return;
    }
    pObject->pNextDico = pFirstObject;
    pFirstObject = pObject;
    numObjects++;
    pObject->objectID = ++idCounter;
    pObject->worldLocation.LocationType = ObjectWorldLocation::OWL_None;
    pObject->worldLocation.bNearJunction = 0;
}

void GameObjectDico::unregisterObject( GameObject *pObject ) {
    // FUN_0084391b (inlined)
    if ( pObject->objectID == 0 ) {
        return;
    }
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    gpAI->getRoadBuckets().removeObject( pObject );
    pObject->worldLocation.LocationType = ObjectWorldLocation::OWL_None;
    pObject->worldLocation.bNearJunction = 0;

    GameObject **ppLink = &pFirstObject;
    while ( *ppLink != nullptr ) {
        if ( ( *ppLink )->objectID == pObject->objectID ) {
            *ppLink = pObject->pNextDico;
            numObjects--;
            break;
        }
        ppLink = &( *ppLink )->pNextDico;
    }
    pObject->pNextDico = nullptr;
    pObject->objectID = 0;
}

GameObject *GameObjectDico::findObject( const uint32_t id ) const {
    for ( GameObject *pObject = pFirstObject; pObject != nullptr;
          pObject = pObject->pNextDico ) {
        if ( pObject->objectID == id ) {
            return pObject;
        }
    }
    return nullptr;
}

void GameObjectDico::updateLocations( const float deltaTime,
                                      const bool bDebug ) {
    // FUN_0096c640
    if ( deltaTime <= 0.0f ) {
        return;
    }
    for ( GameObject *pObject = pFirstObject; pObject != nullptr;
          pObject = pObject->pNextDico ) {
        const CarState *pCar = ( pObject->getType() == 0 )
                                   ? static_cast<const CarState *>( pObject )
                                   : nullptr;
        if ( pCar != nullptr && pCar->getAIController() != nullptr ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: AI
            continue;
        }
        locate( pObject, nullptr, bDebug );
    }
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
}

void GameObjectDico::locate( GameObject *pObject,
                             AiRoadMap::Road *pRoadHint,
                             const bool bDebug ) {
    // FUN_0096b9b0
    AiRoadMap &roadMap = gpAI->getRoadMap();
    if ( pRoadHint == nullptr && pObject->parentID != 0 ) {
        const GameObject *pParent = findObject( pObject->parentID );
        if ( pParent != nullptr && pParent->worldLocation.LocationType ==
                                       ObjectWorldLocation::OWL_Road ) {
            pRoadHint =
                roadMap.getRoad( pParent->worldLocation.LocationRoad.RoadID );
        }
    }

    auto fitsRoad = [pObject]( const AiRoadIt &candidate,
                               const Matrix4x4f_t &frame,
                               float &outDistanceSq ) {
        const Eigen::Vector4f position = pObject->getWorldPosition();
        const Eigen::Vector4f onRoad = frame.row( 3 ).transpose();
        outDistanceSq = ( onRoad - position ).squaredNorm();
        const float width = candidate.getEntity()->pProfile->OuterWidth +
                            pObject->getDimensions().x();
        return outDistanceSq <= width * width * 0.25f &&
               position.y() - onRoad.y() > kMaxDropBelowRoad;
    };

    AiRoadIt it;
    bool bFound = false;
    if ( pRoadHint != nullptr ) {
        Matrix4x4f_t frame;
        ProjectOnRoadShape( pObject->getWorldMatrix(), it, &frame, false,
                            pRoadHint->getShape() );
        // NOTE: retail tests "T > 0 || 1 > T", always true but for NaN.
        float distanceSq = 0.0f;
        if ( ( it.T > 0.0f || 1.0f > it.T ) &&
             fitsRoad( it, frame, distanceSq ) ) {
            bFound = true;
        }
    }

    AiPool *pPool = gpAI->getAIPool();
    if ( !bFound ) {
        float bestDistanceSq = -1.0f;
        for ( int32_t s = 0; s < AiPool::kNumRoadShapes; s++ ) {
            const AiRoadShape &shape = pPool->getShape( s );
            if ( shape.LastUse == 0 ) {
                continue;
            }
            for ( const AiRoadBuckets::PhantomNode *pNode = shape.pFirstPhantom;
                  pNode != nullptr; pNode = pNode->pNext ) {
                if ( pNode->pObject != nullptr &&
                     pNode->pObject->getID() != pObject->getID() ) {
                    continue;
                }

                AiRoadMap::Road *pRoad =
                    roadMap.getRoad( static_cast<uint32_t>( pNode->RoadID ) );
                AiRoadShape *pRoadShape =
                    ( pRoad != nullptr && pRoad->pDetail != nullptr )
                        ? pRoad->getShape()
                        : nullptr;
                if ( pRoadShape == nullptr ) {
                    continue;
                }
                AiRoadIt candidate;
                Matrix4x4f_t frame;
                ProjectOnRoadShape( pObject->getWorldMatrix(), candidate,
                                    &frame, false, pRoadShape );
                if ( !( 0.0f < candidate.T && candidate.T < 1.0f ) ) {
                    continue;
                }
                float distanceSq = 0.0f;
                if ( !fitsRoad( candidate, frame, distanceSq ) ) {
                    continue;
                }
                bFound = true;
                if ( bestDistanceSq < 0.0f || distanceSq < bestDistanceSq ) {
                    bestDistanceSq = distanceSq;
                    it = candidate;
                }
            }
        }
    }

    if ( bFound ) {
        pObject->setRoadLocation( it, bDebug );
        return;
    }

    const Eigen::Vector4f position = pObject->getWorldPosition();
    bool bNearJunction = false;
    for ( int32_t s = 0; s < AiPool::kNumRoadShapes; s++ ) {
        const AiRoadShape &shape = pPool->getShape( s );
        if ( shape.LastUse == 0 ) {
            continue;
        }
        for ( const AiRoadMap::Junction *pJunction :
              { shape.pRoad->pJunctionEnd, shape.pRoad->pJunctionBegin } ) {
            if ( pJunction == nullptr ||
                 !( ( pJunction->Position.toVector4() - position )
                        .squaredNorm() < kJunctionSearchRadiusSq ) ) {
                continue;
            }
            if ( position.y() - pJunction->Position.Y > kMaxDropBelowRoad &&
                 pJunction->containsPoint(
                     Eigen::Vector2f( position.x(), position.z() ) ) ) {
                pObject->setJunctionLocation( pObject->getWorldMatrix(),
                                              pJunction );
                return;
            }
            float distance = 0.0f;
            if ( pJunction->findClosestOutlinePoint( position,
                                                     kJunctionOutlineDistance,
                                                     &distance, nullptr ) ) {
                bNearJunction = true;
            }
        }
    }

    gpAI->getRoadBuckets().removeObject( pObject );
    pObject->worldLocation.LocationType = ObjectWorldLocation::OWL_OffRoad;
    pObject->worldLocation.bNearJunction = bNearJunction ? 1 : 0;
    gpAI->getRoadBuckets().insertObject( pObject );
}
