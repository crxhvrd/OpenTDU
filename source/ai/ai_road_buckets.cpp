#include "shared.h"
#include "ai_road_buckets.h"

#include "ai/ai_road_map.h"
#include "ai/ai_road_shape.h"
#include "ai/gs_ai.h"
#include "game/game_object.h"

AiRoadBuckets::AiRoadBuckets()
    : pRoadMap( nullptr ),
      pPhantomManager( nullptr ),
      phantomNodes(),
      pFreePhantomNodes( nullptr ),
      numUsedPhantomNodes( 0 ),
      roadBuckets(),
      junctionBuckets() {}

AiRoadBuckets::~AiRoadBuckets() {}

void AiRoadBuckets::initialize( AiRoadMap *pMap,
                                RoadPhantomManager *pManager ) {
    // FUN_00935f70
    pPhantomManager = pManager;
    phantomNodes.assign( kNumPhantomNodes, PhantomNode{} );
    pFreePhantomNodes = nullptr;
    for ( int32_t i = kNumPhantomNodes - 1; i >= 0; i-- ) {
        phantomNodes[i].pNext = pFreePhantomNodes;
        pFreePhantomNodes = &phantomNodes[i];
    }
    numUsedPhantomNodes = 0;
    if ( pPhantomManager != nullptr ) {
        pPhantomManager->registerCallback( this );
    }
    pRoadMap = pMap;
    if ( gpAI != nullptr && gpAI->isRoadMapLoaded() ) {
        build();
    }
}

void AiRoadBuckets::terminate() {
    // FUN_00935e20 (inlined)
    if ( pPhantomManager != nullptr ) {
        pPhantomManager->unregisterCallback( this );
        pPhantomManager = nullptr;
    }
}

void AiRoadBuckets::build() {
    // FUN_00935eb0
    if ( pRoadMap == nullptr || !roadBuckets.empty() ) {
        return;
    }
    roadBuckets.assign( pRoadMap->getNumRoads(), Bucket{} );
    junctionBuckets.assign( pRoadMap->getNumJunctions(), Bucket{} );
}

AiRoadBuckets::Bucket *AiRoadBuckets::getBucket( std::vector<Bucket> &buckets,
                                                 const uint32_t id ) {
    // FUN_00935da0 / FUN_00935dc0
    if ( buckets.empty() ) {
        return nullptr;
    }
    int32_t index = static_cast<int32_t>( id ) - 1;
    if ( index < 0 || index >= static_cast<int32_t>( buckets.size() ) ) {
        index = 0;
    }
    return &buckets[index];
}

void AiRoadBuckets::insertObject( GameObject *pObject ) {
    // FUN_0096aa40
    const ObjectWorldLocation &location = pObject->getWorldLocation();
    Bucket *pBucket = nullptr;
    if ( location.LocationType == ObjectWorldLocation::OWL_Road ) {
        pBucket = getBucket( roadBuckets, location.LocationRoad.RoadID );
    } else if ( location.LocationType == ObjectWorldLocation::OWL_Junction ) {
        pBucket = getBucket( junctionBuckets, location.LocationOther.RoadID );
    }
    if ( pBucket == nullptr ) {
        return;
    }
    // FUN_00935d90
    pObject->pNextInBucket = pBucket->pFirstObject;
    pBucket->pFirstObject = pObject;
    pBucket->NumObjects++;
}

void AiRoadBuckets::removeObject( GameObject *pObject ) {
    // FUN_0096a210
    if ( gpAI == nullptr || !gpAI->isRoadMapLoaded() ) {
        return;
    }
    const ObjectWorldLocation &location = pObject->getWorldLocation();
    Bucket *pBucket = nullptr;
    if ( location.LocationType == ObjectWorldLocation::OWL_Road ) {
        pBucket = getBucket( roadBuckets, location.LocationRoad.RoadID );
    } else if ( location.LocationType == ObjectWorldLocation::OWL_Junction ) {
        pBucket = getBucket( junctionBuckets, location.LocationOther.RoadID );
    }
    if ( pBucket == nullptr ) {
        return;
    }

    // FUN_00935d50
    GameObject **ppLink = &pBucket->pFirstObject;
    while ( *ppLink != nullptr ) {
        if ( *ppLink == pObject ) {
            *ppLink = pObject->pNextInBucket;
            pObject->pNextInBucket = nullptr;
            pBucket->NumObjects--;
            return;
        }
        ppLink = &( *ppLink )->pNextInBucket;
    }
}

AiRoadBuckets::PhantomNode *AiRoadBuckets::allocNode() {
    if ( pFreePhantomNodes == nullptr ||
         numUsedPhantomNodes >= kNumPhantomNodes ) {
        return nullptr;
    }
    PhantomNode *pNode = pFreePhantomNodes;
    pFreePhantomNodes = pNode->pNext;
    numUsedPhantomNodes++;
    *pNode = PhantomNode{};
    return pNode;
}

void AiRoadBuckets::freeNode( PhantomNode *pNode ) {
    pNode->pPrevious = nullptr;
    pNode->pObject = nullptr;
    pNode->pNext = pFreePhantomNodes;
    pFreePhantomNodes = pNode;
    numUsedPhantomNodes--;
}

void AiRoadBuckets::onRoadPhantomAdd( const RoadPhantomEvt &evt ) {
    // FUN_009366a0
    if ( pRoadMap == nullptr ) {
        return;
    }
    AiRoadMap::Road *pRoad =
        pRoadMap->getRoad( static_cast<uint32_t>( evt.RoadID ) );
    if ( pRoad == nullptr ) {
        return;
    }
    AiRoadShape *pShape = pRoad->getShape();
    if ( pShape == nullptr ) {
        return;
    }
    pShape->addPhantom( static_cast<GameObject *>( evt.pObject ),
                        evt.EntityIndex, evt.CurveIndex, evt.RoadID );
}

void AiRoadBuckets::onRoadPhantomRemove( const RoadPhantomEvt &evt ) {
    // FUN_00936650
    if ( pRoadMap == nullptr ) {
        return;
    }
    AiRoadMap::Road *pRoad =
        pRoadMap->getRoad( static_cast<uint32_t>( evt.RoadID ) );
    if ( pRoad == nullptr ) {
        return;
    }
    AiRoadShape *pShape = pRoad->getShape();
    if ( pShape == nullptr ) {
        return;
    }
    pShape->removePhantom( static_cast<GameObject *>( evt.pObject ),
                           evt.EntityIndex, evt.CurveIndex );
}
