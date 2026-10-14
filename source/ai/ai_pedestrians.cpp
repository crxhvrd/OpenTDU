#include "shared.h"
#include "ai_pedestrians.h"

#include "ai/ai_config.h"
#include "core/maths_helper.h"
#include "game/scenaric/entity/base_entity.h"
#include "game/scenaric/entity/entity_factory.h"
#include "game/scenaric/entity/entity_pedestrian.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/mng_actor.h"
#include "render/camera/camera_vehicle.h"
#include "render/camera/gs_camera.h"
#include "world/gs_world.h"
#include "world/sector_manager.h"
#include "world/streaming_manager.h"

#include <cfloat>

static constexpr uint32_t kRandomMultiplier = 0x7ff8a3ed;
static constexpr uint32_t kRandomIncrement = 0x2aa01d31;
static constexpr uint32_t kRandomHalfRange = 0x7fffffff;
static constexpr float kRandomToUnit = 2.3283064e-10f; // DAT_00f418e4
static constexpr float kVisibilityRadius = 0.5f;
static constexpr float kCenterLineTolerance = 0.1f; // DAT_00f41668
static constexpr float kEdgeEpsilon = 1e-05f;       // DAT_00f41710

static const Eigen::Vector4f kUpVector( 0.0f, 1.0f, 0.0f, 0.0f );

static inline Eigen::Vector4f GetQuadPoint( const AiPedestrianNavQuad &quad,
                                            const int32_t index ) {
    return Eigen::Vector4f( quad.Points[index][0], quad.Points[index][1],
                            quad.Points[index][2], quad.Points[index][3] );
}

static bool IsPointInPolygonXZ( const AiPedestrianNavQuad &quad,
                                const int32_t numPoints,
                                const Eigen::Vector4f &point ) {
    // FUN_0040b220
    bool bInside = false;
    for ( int32_t i = 0, j = numPoints - 1; i < numPoints; j = i++ ) {
        const float xi = quad.Points[i][0];
        const float zi = quad.Points[i][2];
        const float xj = quad.Points[j][0];
        const float zj = quad.Points[j][2];

        const float lhs = ( xj - xi ) * ( point.z() - zi );
        const float rhs = ( point.x() - xi ) * ( zj - zi );
        if ( point.z() < zj ) {
            if ( zi <= point.z() && rhs < lhs ) {
                bInside = !bInside;
            }
        } else if ( point.z() < zi && lhs < rhs ) {
            bInside = !bInside;
        }
    }
    return bInside;
}

static bool IntersectSegmentsXZ( const Eigen::Vector4f &startA,
                                 const Eigen::Vector4f &endA,
                                 const Eigen::Vector4f &startB,
                                 const Eigen::Vector4f &endB,
                                 Eigen::Vector4f &outIntersection ) {
    // FUN_0040afe0
    const Eigen::Vector4f deltaA = endA - startA;
    const Eigen::Vector4f deltaB = endB - startB;
    const Eigen::Vector4f offset = startB - startA;

    const float denominator = deltaB.x() * deltaA.z() - deltaB.z() * deltaA.x();
    if ( denominator * denominator <=
         deltaB.dot( deltaB ) * deltaA.dot( deltaA ) * FLT_EPSILON ) {
        return false;
    }

    const float invDenominator = 1.0f / denominator;
    const float ratioA =
        ( offset.z() * deltaB.x() - deltaB.z() * offset.x() ) * invDenominator;
    const float ratioB =
        ( offset.z() * deltaA.x() - offset.x() * deltaA.z() ) * invDenominator;
    if ( ratioA < 0.0f || 1.0f < ratioA || ratioB < 0.0f || 1.0f < ratioB ) {
        return false;
    }

    outIntersection = startA + deltaA * ratioA;
    return true;
}

static Eigen::Vector4f GetQuadEdgeDirection( const AiPedestrianNavQuad &quad ) {
    // FUN_00928f30
    return NormalizeVector4( GetQuadPoint( quad, 1 ) -
                             GetQuadPoint( quad, 0 ) );
}

static Eigen::Vector4f ComputeQuadCenter( const AiPedestrianNavQuad &quad ) {
    // FUN_00928c60
    Eigen::Vector4f center =
        ( Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ) + GetQuadPoint( quad, 0 ) +
          GetQuadPoint( quad, 1 ) + GetQuadPoint( quad, 2 ) +
          GetQuadPoint( quad, 3 ) ) /
        4.0f;
    center.w() = 1.0f;
    return center;
}

static bool FindCrossedQuadEdge( const AiPedestrianNavQuad &quad,
                                 const Eigen::Vector4f &from,
                                 const Eigen::Vector4f &to,
                                 Eigen::Vector4f &outNormal,
                                 Eigen::Vector4f &outEdgeDirection ) {
    // FUN_00928f90
    const float epsilonSquared = kEdgeEpsilon * kEdgeEpsilon;

    int32_t previous = 3;
    for ( int32_t current = 0; current < 4; current++ ) {
        const Eigen::Vector4f edgeStart = GetQuadPoint( quad, previous );
        const Eigen::Vector4f edgeEnd = GetQuadPoint( quad, current );
        const Eigen::Vector4f delta = edgeEnd - edgeStart;
        const bool bDegenerate = ( delta.array() * delta.array() <=
                                   Eigen::Array4f::Constant( epsilonSquared ) )
                                     .all();

        Eigen::Vector4f intersection;
        if ( !bDegenerate && IntersectSegmentsXZ( edgeStart, edgeEnd, from, to,
                                                  intersection ) ) {
            outEdgeDirection = NormalizeVector4( delta );
            outNormal = Vector4DCross( outEdgeDirection, kUpVector );
            return true;
        }
        previous = current;
    }
    return false;
}

static int CompareDisplayPriority( const void *pLeft, const void *pRight ) {
    // FUN_00929470
    const AiPedestrian *pA = *static_cast<AiPedestrian *const *>( pLeft );
    const AiPedestrian *pB = *static_cast<AiPedestrian *const *>( pRight );

    const bool bDisplayedA = ( pA->Flags & AiPedestrian::PF_Displayed ) != 0;
    const bool bDisplayedB = ( pB->Flags & AiPedestrian::PF_Displayed ) != 0;
    if ( bDisplayedA ) {
        if ( !bDisplayedB ) {
            return -1;
        }
    } else if ( bDisplayedB ) {
        return 1;
    }
    return static_cast<int>( pA->CameraDistance - pB->CameraDistance );
}

static CameraGame *GetCullingCamera() {
    // Inlined in FUN_00929d40
    CarState *pTarget = gpCamera->getBoundCarState();
    int32_t mode = gpCamera->getActiveTransitionIndex();
    if ( gpCamera->isLookingBack() ) {
        CameraGame *pCamera = gpCamera->findCameraWithMode( mode, pTarget );
        mode = 0;
        if ( pCamera != nullptr && ( pCamera->getFlags() >> 0x11 & 1 ) == 0 ) {
            mode = static_cast<CameraVehicle *>( pCamera )->getBackivewType();
        }
    }

    CameraGame *pCamera = gpCamera->findCameraWithMode( mode, pTarget );
    return ( pCamera != nullptr ) ? pCamera : gpCamera->getDefaultCamera();
}

AiPedestrianNavSlot::AiPedestrianNavSlot()
    : pOwner( nullptr ),
      NumQuads( 0 ),
      pQuads( nullptr ),
      NumCircles( 0 ),
      pCircles( nullptr ),
      GridSize( 0 ),
      CellSize( 0 ),
      pCells( &EmptyCell ),
      EmptyCell{ 0, 0, 0, 0 },
      pBlob( nullptr ) {}

void AiPedestrianNavSlot::reset() {
    // FUN_00928b10
    pOwner = nullptr;
    pQuads = nullptr;
    pCircles = nullptr;
    NumQuads = 0;
    NumCircles = 0;
}

void AiPedestrianNavSlot::bind( const uint8_t *pNavBlob, Sector *pSector ) {
    // FUN_00928b30
    pOwner = pSector;
    pBlob = pNavBlob;

    NumQuads = *reinterpret_cast<const uint32_t *>( pNavBlob + 0x20 );
    pQuads = reinterpret_cast<const AiPedestrianNavQuad *>( pNavBlob + 0x30 );

    const uint8_t *pCircleSection =
        pNavBlob + 0x30 + NumQuads * sizeof( AiPedestrianNavQuad );
    NumCircles = *reinterpret_cast<const uint32_t *>( pCircleSection );
    pCircles = reinterpret_cast<const AiPedestrianNavCircle *>( pCircleSection +
                                                                0x10 );

    const uint8_t *pGridSection =
        pCircleSection + 0x10 + NumCircles * sizeof( AiPedestrianNavCircle );
    GridSize = *reinterpret_cast<const int32_t *>( pGridSection );
    CellSize = *reinterpret_cast<const int32_t *>( pGridSection + 4 );
    pCells =
        reinterpret_cast<const AiPedestrianNavCell *>( pGridSection + 0x10 );
}

const AiPedestrianNavCell &
AiPedestrianNavSlot::getCell( const Eigen::Vector4f &position ) const {
    // FUN_00928cd0
    if ( CellSize <= 0 ) {
        return EmptyCell;
    }

    const float localX = ( kMaxLocalCoordinate > position.x() )
                             ? position.x()
                             : kMaxLocalCoordinate;
    const float localZ = ( kMaxLocalCoordinate > position.z() )
                             ? position.z()
                             : kMaxLocalCoordinate;
    const int32_t cellX =
        static_cast<int32_t>( localX / static_cast<float>( CellSize ) );
    const int32_t cellZ =
        static_cast<int32_t>( localZ / static_cast<float>( CellSize ) );

    if ( cellX < 0 || cellZ < 0 || cellX >= GridSize || cellZ >= GridSize ) {
        return EmptyCell;
    }
    return pCells[cellZ * GridSize + cellX];
}

const AiPedestrianNavQuad *
AiPedestrianNavSlot::findQuad( const AiPedestrianNavCell &cell,
                               const Eigen::Vector4f &position ) const {
    // FUN_00928c10
    const uint32_t *pQuadIndexes =
        reinterpret_cast<const uint32_t *>( pBlob + cell.QuadIndexesOffset );
    for ( uint32_t i = 0; i < cell.NumQuads; i++ ) {
        const AiPedestrianNavQuad &quad = pQuads[pQuadIndexes[i]];
        if ( IsPointInPolygonXZ( quad, 4, position ) ) {
            return &quad;
        }
    }
    return nullptr;
}

const AiPedestrianNavQuad *
AiPedestrianNavSlot::findQuad( const Eigen::Vector4f &position ) const {
    // FUN_00928e30
    return findQuad( getCell( position ), position );
}

void AiPedestrianPool::initialize() {
    // FUN_0040ad90
    for ( AiPedestrian &pedestrian : Pedestrians ) {
        pedestrian.pSlot = nullptr;
        pedestrian.pNextSlot = nullptr;
        pedestrian.Transform = Matrix4x4f_t::Identity();
        pedestrian.SectorOrigin = Eigen::Vector4f::Zero();
        pedestrian.NextTransform = Matrix4x4f_t::Identity();
        pedestrian.NextSectorOrigin = Eigen::Vector4f::Zero();
        pedestrian.Direction = Eigen::Vector4f::Zero();
        pedestrian.Heading = 0.0f;
        pedestrian.Speed = -1.0f;
        pedestrian.Avoidance = Eigen::Vector4f::Zero();
        pedestrian.DeltaTime = 0.0f;
        pedestrian.Flags = 0;
        pedestrian.CameraDistance = 0.0f;
        pedestrian.bWasMoving = 0;
        pedestrian.bDirty = false;
        pedestrian.DisplaySlot = -1;
    }

    for ( int32_t i = 0; i < kCapacity; i++ ) {
        pActive[i] = nullptr;
        pFree[i] = &Pedestrians[i];
        bUsed[i] = false;
    }
    NumActive = 0;
}

AiPedestrian *AiPedestrianPool::allocate() {
    // FUN_0040ae30
    if ( kCapacity - NumActive <= 0 ) {
        return nullptr;
    }

    AiPedestrian *pPedestrian = pFree[kCapacity - 1 - NumActive];
    pFree[kCapacity - 1 - NumActive] = nullptr;
    pActive[NumActive] = pPedestrian;
    bUsed[pPedestrian - Pedestrians] = true;
    NumActive++;
    return pPedestrian;
}

void AiPedestrianPool::release( AiPedestrian *pPedestrian ) {
    // FUN_0040ae90
    if ( pPedestrian < Pedestrians || pPedestrian >= Pedestrians + kCapacity ) {
        return;
    }

    for ( int32_t i = 0; i < NumActive; i++ ) {
        if ( pActive[i] != pPedestrian ) {
            continue;
        }

        pFree[kCapacity - NumActive] = pPedestrian;
        for ( int32_t j = i + 1; j < NumActive; j++ ) {
            pActive[j - 1] = pActive[j];
        }
        pActive[NumActive - 1] = nullptr;
        bUsed[pPedestrian - Pedestrians] = false;
        NumActive--;
        return;
    }
}

void AiPedestrianList::reset() {
    pHead = nullptr;
    pTail = nullptr;
    pCursor = nullptr;
    Count = 0;
    pNodes = nullptr;
    pFreeNodes = nullptr;
    Capacity = 0;
    NumUsedNodes = 0;
}

void AiPedestrianList::allocate( const int32_t capacity ) {
    // FUN_0092ad90
    pNodes = new AiPedestrianListNode[capacity];
    for ( int32_t i = 0; i < capacity; i++ ) {
        pNodes[i].pData = nullptr;
        pNodes[i].pPrevious = nullptr;
        pNodes[i].pNext = ( i + 1 < capacity ) ? &pNodes[i + 1] : nullptr;
    }
    pFreeNodes = pNodes;
    Capacity = capacity;
    NumUsedNodes = 0;

    pHead = nullptr;
    pTail = nullptr;
    pCursor = nullptr;
    Count = 0;
}

void AiPedestrianList::release() {
    // FUN_0092ae90
    if ( pHead != nullptr ) {
        clear();
    }

    delete[] pNodes;
    reset();
}

AiPedestrianListNode *AiPedestrianList::pushBack() {
    // FUN_0040a220
    if ( NumUsedNodes >= Capacity ) {
        return nullptr;
    }

    NumUsedNodes++;
    AiPedestrianListNode *pNode = pFreeNodes;
    pFreeNodes = pNode->pNext;
    pNode->pPrevious = nullptr;
    pNode->pNext = nullptr;

    if ( pHead == nullptr ) {
        pHead = pNode;
    } else {
        pTail->pNext = pNode;
        pNode->pPrevious = pTail;
    }
    pTail = pNode;
    pCursor = pNode;
    Count++;
    return pNode;
}

void AiPedestrianList::removeCursor() {
    // FUN_0040ad40
    AiPedestrianListNode *pNode = pCursor;
    AiPedestrianListNode *pPrevious = pNode->pPrevious;
    AiPedestrianListNode *pNext = pNode->pNext;
    if ( pPrevious != nullptr ) {
        pPrevious->pNext = pNext;
    }
    if ( pNext != nullptr ) {
        pNext->pPrevious = pPrevious;
    }
    if ( pHead == pNode ) {
        pHead = pNext;
    }
    if ( pTail == pNode ) {
        pTail = pPrevious;
    }
    pCursor = ( pNext != nullptr ) ? pNext : pPrevious;
    Count--;

    NumUsedNodes--;
    pNode->pNext = pFreeNodes;
    pFreeNodes = pNode;
}

void AiPedestrianList::clear() {
    // FUN_0040af30
    pCursor = pHead;
    while ( pHead != nullptr ) {
        removeCursor();
    }
}

AiPedestrianManager::AiPedestrianManager()
    : pActorManager( nullptr ),
      bEnabled( true ),
      navSlots(),
      pDisplayEntities{},
      pSlotPedestrians{},
      randomSeed( 0x3039 ),
      bAreaRemovalPending( false ),
      pPendingRemovals{},
      numVisible( 0 ),
      numDisplayed( 0 ) {
    // FUN_0092a3d0
    pool.initialize();
    list.reset();
}

void AiPedestrianManager::initialize() {
    // FUN_0092a3a0
    list.allocate( AiPedestrianPool::kCapacity );
    for ( AiPedestrianNavSlot &slot : navSlots ) {
        if ( slot.pOwner != nullptr ) {
            spawnArea( slot );
        }
    }
}

void AiPedestrianManager::terminate() {
    // FUN_00969c40 (inlined)
    removePendingAreas();
    removeArea( nullptr );
    list.release();
}

void AiPedestrianManager::registerArea( const uint8_t *pNavBlob,
                                        Sector *pSector ) {
    // FUN_0092a350
    if ( pNavBlob == nullptr ) {
        return;
    }

    AiPedestrianNavSlot *pSlot = findNavSlot( nullptr );
    OTDU_ASSERT( pSlot != nullptr );
    if ( pSlot == nullptr ) {
        return;
    }

    pSlot->bind( pNavBlob, pSector );
    if ( list.pNodes != nullptr ) {
        spawnArea( *pSlot );
    }
}

void AiPedestrianManager::removeArea( const AiPedestrianNavSlot *pSlot ) {
    // FUN_00929530
    AiPedestrian *pRemoved[AiPedestrianPool::kCapacity];
    int32_t numRemoved = 0;
    for ( AiPedestrianListNode *pNode = list.pTail; pNode != nullptr;
          pNode = pNode->pPrevious ) {
        if ( pSlot == nullptr || pNode->pData->pSlot == pSlot ) {
            pRemoved[numRemoved++] = pNode->pData;
        }
    }

    for ( int32_t i = 0; i < numRemoved; i++ ) {
        AiPedestrianListNode *pNode = list.pHead;
        while ( pNode != nullptr && pNode->pData != pRemoved[i] ) {
            pNode = pNode->pNext;
        }
        if ( pNode == nullptr ) {
            continue;
        }

        list.pCursor = pNode;
        list.removeCursor();
        pool.release( pRemoved[i] );
    }
}

void AiPedestrianManager::removePendingAreas() {
    // FUN_00929cf0
    for ( AiPedestrianNavSlot *&pSlot : pPendingRemovals ) {
        if ( pSlot != nullptr ) {
            removeArea( pSlot );
            pSlot = nullptr;
        }
    }
}

void AiPedestrianManager::update( const float deltaTime ) {
    // FUN_0092ab70
    for ( AiPedestrianListNode *pNode = list.pHead; pNode != nullptr;
          pNode = pNode->pNext ) {
        AiPedestrian &pedestrian = *pNode->pData;
        if ( pedestrian.pSlot == nullptr ) {
            pedestrian.Flags |= AiPedestrian::PF_Stopped;
            continue;
        }

        updateSpeed( pedestrian, deltaTime );
        updateAvoidance( pedestrian, deltaTime );
        updateNavigation( pedestrian );
        commit( pedestrian );
    }
}

void AiPedestrianManager::updateDisplay() {
    // FUN_00929d40
    CameraGame *pCamera = GetCullingCamera();

    for ( AiPedestrian *&pSlotPedestrian : pSlotPedestrians ) {
        pSlotPedestrian = nullptr;
    }

    AiPedestrian *pVisible[AiPedestrianPool::kCapacity];
    int32_t numInView = 0;
    for ( AiPedestrianListNode *pNode = list.pHead; pNode != nullptr;
          pNode = pNode->pNext ) {
        if ( TestVisibility( *pNode->pData, pCamera ) ) {
            pVisible[numInView++] = pNode->pData;
        }
    }

    if ( numInView < 1 ) {
        numVisible = 0;
        numDisplayed = 0;
        return;
    }

    std::qsort( pVisible, numInView, sizeof( AiPedestrian * ),
                CompareDisplayPriority );

    const uint32_t maxDisplayed = gAIConfig.PedMaxToDisplay;
    const float step =
        static_cast<float>( maxDisplayed ) / static_cast<float>( numInView );
    float progress = 0.0f;
    float lastDisplayedProgress = -1.0f;
    uint32_t displayed = 0;
    AiPedestrian *pDisplayed[kMaxNumDisplayed];
    for ( int32_t i = 0; i < numInView; i++ ) {
        AiPedestrian &pedestrian = *pVisible[i];
        progress += step;

        if ( displayed >= maxDisplayed - 1 ) {
            pedestrian.Flags |= AiPedestrian::PF_OverBudget;
            continue;
        }

        if ( ( pedestrian.Flags & AiPedestrian::PF_OverBudget ) != 0 ) {
            continue;
        }
        if ( static_cast<int32_t>( progress ) ==
                 static_cast<int32_t>( lastDisplayedProgress ) &&
             ( pedestrian.Flags & AiPedestrian::PF_Displayed ) == 0 ) {
            continue;
        }

        pedestrian.Flags |= AiPedestrian::PF_Displayed;

        pDisplayed[displayed] = &pedestrian;

        lastDisplayedProgress = progress;
        displayed++;
    }

    numVisible = numInView;
    numDisplayed = static_cast<int32_t>( displayed );
    assignDisplaySlots( pDisplayed, displayed, maxDisplayed );
}

static bool HasPedestrianModel( Entity *pEntity ) {
    return pEntity != nullptr &&
           static_cast<EntityPedestrian *>( pEntity )->hasModel();
}

void AiPedestrianManager::assignDisplaySlots( AiPedestrian **ppDisplayed,
                                              const uint32_t numToDisplay,
                                              const uint32_t maxDisplayed ) {
    bool bUsed[kMaxNumDisplayed] = {};
    int32_t slots[kMaxNumDisplayed];
    for ( uint32_t i = 0; i < numToDisplay; i++ ) {
        const int32_t slot = ppDisplayed[i]->DisplaySlot;
        slots[i] = -1;
        if ( slot >= 0 && static_cast<uint32_t>( slot ) < maxDisplayed &&
             !bUsed[slot] && HasPedestrianModel( pDisplayEntities[slot] ) ) {
            slots[i] = slot;
            bUsed[slot] = true;
        }
    }

    for ( uint32_t i = 0; i < numToDisplay; i++ ) {
        if ( slots[i] >= 0 ) {
            continue;
        }

        const int32_t previous = ppDisplayed[i]->DisplaySlot;
        int32_t slot = -1;
        for ( int32_t pass = 0; pass < 3 && slot < 0; pass++ ) {
            for ( uint32_t j = 0; j < maxDisplayed; j++ ) {
                if ( bUsed[j] ) {
                    continue;
                }
                const bool bSameBody =
                    previous < 0 || ( j & 1 ) == ( previous & 1u );
                const bool bHasModel =
                    HasPedestrianModel( pDisplayEntities[j] );
                if ( ( pass == 0 && bSameBody && bHasModel ) ||
                     ( pass == 1 && bHasModel ) || pass == 2 ) {
                    slot = static_cast<int32_t>( j );
                    break;
                }
            }
        }

        slots[i] = slot;
        if ( slot >= 0 ) {
            bUsed[slot] = true;
            ppDisplayed[i]->DisplaySlot = slot;
        }
    }

    for ( uint32_t i = 0; i < numToDisplay; i++ ) {
        if ( slots[i] < 0 ) {
            continue;
        }

        AiPedestrian &pedestrian = *ppDisplayed[i];
        pSlotPedestrians[slots[i]] = &pedestrian;

        Entity *pEntity = pDisplayEntities[slots[i]];
        if ( pEntity == nullptr ) {
            continue;
        }
        pEntity->setTileVisible( true );

        Matrix4x4f_t identity = Matrix4x4f_t::Identity();
        pEntity->setTransform( identity, true );

        Matrix4x4f_t transform = pedestrian.Transform;
        transform.row( 3 ) += pedestrian.SectorOrigin.transpose();
        pEntity->propagateTransform( transform, true );
    }

    for ( uint32_t i = 0; i < maxDisplayed; i++ ) {
        if ( !bUsed[i] && pDisplayEntities[i] != nullptr ) {
            pDisplayEntities[i]->setTileVisible( false );
        }
    }
}

void AiPedestrianManager::instanciateEntities( MngActor *pMngActor ) {
    // FUN_0092ac70
    pActorManager = pMngActor;

    for ( uint32_t i = 0; i < gAIConfig.PedMaxToDisplay; i++ ) {
        EntityPedestrian *pTemplate = static_cast<EntityPedestrian *>(
            gpScenaric->getPedestrianBank().getFirst( eEntityType::ET_PTDR ) );
        if ( pTemplate == nullptr ) {
            pDisplayEntities[i] = nullptr;
            continue;
        }

        pTemplate->setType( ( i & 1 ) == 0 ? 1 : 0 );
        pTemplate->setSubType( 1 );

        Entity *pEntity = pMngActor->instanciateActor( pTemplate );
        pDisplayEntities[i] = pEntity;
        if ( pEntity != nullptr ) {
            pEntity->setTileVisible( false );
        }
    }
}

void AiPedestrianManager::uninstanciateEntities() {
    // FUN_0092ac20
    for ( uint32_t i = 0; i < gAIConfig.PedMaxToDisplay; i++ ) {
        MngActor::Uninstanciate( pActorManager, pDisplayEntities[i] );
        pDisplayEntities[i] = nullptr;
    }
    pActorManager = nullptr;
}

void AiPedestrianManager::enable() {
    // FUN_006c9c20
    bEnabled = true;
}

void AiPedestrianManager::disable() {
    // FUN_006c9bf0
    bEnabled = false;
    for ( uint32_t i = 0; i < gAIConfig.PedMaxToDisplay; i++ ) {
        if ( pDisplayEntities[i] != nullptr ) {
            pDisplayEntities[i]->setTileVisible( false );
        }
    }
}

AiPedestrianNavSlot *AiPedestrianManager::findNavSlot( const Sector *pSector ) {
    for ( AiPedestrianNavSlot &slot : navSlots ) {
        if ( slot.pOwner == pSector ) {
            return &slot;
        }
    }
    return nullptr;
}

uint32_t AiPedestrianManager::nextRandom() {
    randomSeed = randomSeed * kRandomMultiplier + kRandomIncrement;
    return randomSeed;
}

void AiPedestrianManager::spawnArea( AiPedestrianNavSlot &slot ) {
    // FUN_0092a0f0
    const uint32_t numSpawns = std::min( slot.NumQuads, kMaxNumSpawnPerArea );
    const float step =
        static_cast<float>( slot.NumQuads ) / static_cast<float>( numSpawns );

    float quadProgress = 0.0f;
    for ( uint32_t i = 0; i < numSpawns; i++, quadProgress += step ) {
        AiPedestrian *pPedestrian = pool.allocate();
        if ( pPedestrian == nullptr ) {
            continue;
        }

        AiPedestrianListNode *pNode = list.pushBack();
        if ( pNode != nullptr ) {
            pNode->pData = pPedestrian;
        }

        const AiPedestrianNavQuad &quad =
            slot.pQuads[static_cast<int32_t>( quadProgress )];
        const Eigen::Vector4f center = ComputeQuadCenter( quad );
        const Eigen::Vector4f direction = GetQuadEdgeDirection( quad );
        const float heading =
            ( nextRandom() < kRandomHalfRange ) ? -1.0f : 1.0f;

        Matrix4x4f_t transform = Matrix4x4f_t::Identity();
        transform.row( 3 ) = center.transpose();
        InitializePedestrian( *pPedestrian, &slot, transform );
        SetDirection( *pPedestrian, direction, heading );
    }
}

void AiPedestrianManager::updateSpeed( AiPedestrian &pedestrian,
                                       const float deltaTime ) {
    // FUN_00929fa0
    const float maxSpeed = gAIConfig.PedMaxSpeed;
    const uint32_t random = nextRandom();

    float speed = pedestrian.Speed;
    if ( speed < 0.0f ) {
        speed = static_cast<float>( random ) * kRandomToUnit * maxSpeed;
    } else {
        speed += ( random < kRandomHalfRange ) ? deltaTime : -deltaTime;
        if ( speed < 0.0f ) {
            speed = 0.0f;
        } else if ( maxSpeed < speed ) {
            speed = maxSpeed;
        }
    }

    if ( pedestrian.bWasMoving == 0 && gAIConfig.PedMinSpeed < speed &&
         nextRandom() < kRandomHalfRange ) {
        const AiPedestrianNavQuad *pQuad =
            pedestrian.pSlot->findQuad( pedestrian.getPosition() );
        if ( pQuad != nullptr ) {
            SetDirection( pedestrian, GetQuadEdgeDirection( *pQuad ),
                          0.0f - pedestrian.Heading );
        }
    }

    pedestrian.Speed = speed;
    pedestrian.DeltaTime = deltaTime;
    pedestrian.bDirty = true;
}

void AiPedestrianManager::updateAvoidance( AiPedestrian &pedestrian,
                                           const float deltaTime ) {
    // FUN_0092a4e0
    const Eigen::Vector4f side =
        NormalizeVector4( Vector4DCross( kUpVector, pedestrian.Direction ) );
    const float detectionRadius = gAIConfig.PedCollideSphereDetectionRadius;
    const float detectionRadiusSquared = detectionRadius * detectionRadius;

    Eigen::Vector4f avoidance = Eigen::Vector4f::Zero();
    uint32_t numNeighbours = 0;
    for ( AiPedestrianListNode *pNode = list.pHead; pNode != nullptr;
          pNode = pNode->pNext ) {
        AiPedestrian &other = *pNode->pData;
        if ( &other == &pedestrian ) {
            continue;
        }

        refresh( pedestrian );
        const Eigen::Vector4f nextPosition = pedestrian.getNextPosition();
        refresh( other );
        const Eigen::Vector4f delta = other.getNextPosition() - nextPosition;
        const float distanceSquared = delta.dot( delta );
        if ( distanceSquared < detectionRadiusSquared ) {
            numNeighbours++;
            avoidance +=
                side * ( 1.0f - distanceSquared / detectionRadiusSquared );
        }
    }

    refresh( pedestrian );
    if ( pedestrian.pNextSlot != nullptr ) {
        refresh( pedestrian );
    }

    if ( numNeighbours > 1 ) {
        avoidance /= static_cast<float>( numNeighbours );
    } else if ( numNeighbours == 0 ) {
        const Eigen::Vector4f position = pedestrian.getPosition();
        const AiPedestrianNavQuad *pQuad =
            pedestrian.pSlot->findQuad( position );
        if ( pQuad != nullptr ) {
            const Eigen::Vector4f normal = NormalizeVector4(
                Vector4DCross( GetQuadEdgeDirection( *pQuad ), kUpVector ) );
            const float lateralOffset =
                ( position - ComputeQuadCenter( *pQuad ) ).dot( normal );
            if ( kCenterLineTolerance < lateralOffset ||
                 lateralOffset < -kCenterLineTolerance ) {
                avoidance =
                    normal *
                    ( ( lateralOffset < 0.0f ) ? deltaTime : 0.0f - deltaTime );
            }
        }
    }

    pedestrian.Avoidance = avoidance;
    pedestrian.bDirty = true;
}

void AiPedestrianManager::updateNavigation( AiPedestrian &pedestrian ) {
    // FUN_0092a8c0
    const Eigen::Vector4f position = pedestrian.getPosition();
    const AiPedestrianNavQuad *pQuad = pedestrian.pSlot->findQuad( position );
    if ( pQuad == nullptr ) {
        pedestrian.Speed = 0.0f;
        pedestrian.DeltaTime = 0.0f;
        pedestrian.Avoidance = Eigen::Vector4f::Zero();
        pedestrian.Flags |= AiPedestrian::PF_Stopped;
        pedestrian.bDirty = true;
        return;
    }

    SetDirection( pedestrian, GetQuadEdgeDirection( *pQuad ),
                  pedestrian.Heading );

    refresh( pedestrian );
    const Eigen::Vector4f nextPosition = pedestrian.getNextPosition();
    refresh( pedestrian );

    const AiPedestrianNavQuad *pNextQuad =
        pedestrian.pNextSlot->findQuad( nextPosition );
    if ( pNextQuad != nullptr ) {
        pedestrian.Flags &= ~AiPedestrian::PF_Stopped;
    } else {
        Eigen::Vector4f edgeNormal;
        Eigen::Vector4f edgeDirection;
        if ( FindCrossedQuadEdge( *pQuad, position, nextPosition, edgeNormal,
                                  edgeDirection ) ) {
            SetDirection( pedestrian, GetQuadEdgeDirection( *pQuad ),
                          0.0f - pedestrian.Heading );
        }

        pedestrian.Avoidance = Eigen::Vector4f::Zero();
        pedestrian.Flags |= AiPedestrian::PF_Stopped;
        pedestrian.Speed = 0.0f;
        pedestrian.DeltaTime = 0.0f;
        pedestrian.bDirty = true;
        pNextQuad = pQuad;
    }

    snapToQuad( pedestrian, *pNextQuad );
}

void AiPedestrianManager::integrate( AiPedestrian &pedestrian,
                                     const bool bLookAhead ) {
    // FUN_0092b4e0
    const Eigen::Vector4f velocity =
        pedestrian.Direction * pedestrian.Speed + pedestrian.Avoidance;

    Eigen::Vector4f nextPosition = pedestrian.getPosition();
    if ( gAIConfig.PedMinSpeed <= pedestrian.Speed ) {
        nextPosition += velocity * pedestrian.DeltaTime;
        if ( bLookAhead ) {
            nextPosition +=
                NormalizeVector4( velocity ) * gAIConfig.PedCollideSphereRadius;
        }
    }

    pedestrian.NextTransform = Matrix4x4f_t::Identity();
    MatrixRotateY(
        pedestrian.NextTransform,
        std::atan2( pedestrian.Direction.x(), pedestrian.Direction.z() ) );
    pedestrian.NextTransform.row( 3 ) = nextPosition.transpose();

    const Eigen::Vector4f worldPosition =
        nextPosition + pedestrian.SectorOrigin;
    Sector *pSector =
        gpWorld->getStreamingManager()->sectorManager.getSectorAtPosition(
            worldPosition );
    if ( pSector != nullptr && pSector != pedestrian.pSlot->pOwner ) {
        pedestrian.pNextSlot = findNavSlot( pSector );
        pedestrian.NextSectorOrigin.x() =
            static_cast<float>( pSector->offsetX );
        pedestrian.NextSectorOrigin.z() =
            static_cast<float>( pSector->offsetY );
        pedestrian.NextTransform.row( 3 ) =
            ( worldPosition - pedestrian.NextSectorOrigin ).transpose();
    } else {
        pedestrian.pNextSlot = pedestrian.pSlot;
        pedestrian.NextSectorOrigin = pedestrian.SectorOrigin;
    }

    pedestrian.bDirty = false;
}

void AiPedestrianManager::refresh( AiPedestrian &pedestrian ) {
    if ( pedestrian.bDirty ) {
        integrate( pedestrian, true );
    }
}

void AiPedestrianManager::commit( AiPedestrian &pedestrian ) {
    // FUN_0092b7c0
    integrate( pedestrian, false );
    refresh( pedestrian );

    if ( pedestrian.pNextSlot != nullptr ) {
        refresh( pedestrian );
        pedestrian.Transform = pedestrian.NextTransform;
        pedestrian.pSlot = pedestrian.pNextSlot;
        refresh( pedestrian );
        pedestrian.SectorOrigin = pedestrian.NextSectorOrigin;
    }

    pedestrian.bWasMoving = ( gAIConfig.PedMinSpeed < pedestrian.Speed );
}

void AiPedestrianManager::InitializePedestrian(
    AiPedestrian &pedestrian,
    AiPedestrianNavSlot *pSlot,
    const Matrix4x4f_t &transform ) {
    // FUN_0092b020
    pedestrian.Transform = transform;
    pedestrian.NextTransform = transform;
    pedestrian.pSlot = pSlot;
    pedestrian.SectorOrigin.x() = static_cast<float>( pSlot->pOwner->offsetX );
    pedestrian.SectorOrigin.z() = static_cast<float>( pSlot->pOwner->offsetY );
    pedestrian.pNextSlot = pSlot;
    pedestrian.NextSectorOrigin = pedestrian.SectorOrigin;
    pedestrian.Speed = -1.0f;
    pedestrian.bDirty = false;
    pedestrian.DisplaySlot = -1;
}

void AiPedestrianManager::SetDirection( AiPedestrian &pedestrian,
                                        const Eigen::Vector4f &direction,
                                        const float heading ) {
    // FUN_0092b0e0
    pedestrian.Direction = NormalizeVector4( direction ) * heading;
    pedestrian.Heading = heading;
    pedestrian.bDirty = true;
}

bool AiPedestrianManager::snapToQuad( AiPedestrian &pedestrian,
                                      const AiPedestrianNavQuad &quad ) {
    // FUN_0092b870
    const Eigen::Vector4f origin = GetQuadPoint( quad, 0 );
    Eigen::Vector4f normal = Vector4DCross( GetQuadPoint( quad, 1 ) - origin,
                                            GetQuadPoint( quad, 3 ) - origin );
    normal /= std::sqrt( normal.dot( normal ) );
    const float planeDistance = 0.0f - normal.dot( origin );

    refresh( pedestrian );
    const Eigen::Vector4f nextPosition = pedestrian.getNextPosition();

    const float denominator = kUpVector.dot( normal );
    if ( kEdgeEpsilon > std::abs( denominator ) ) {
        return false;
    }

    const float distance = ( nextPosition.dot( normal ) + planeDistance ) *
                           ( -1.0f / denominator );
    pedestrian.Transform( 3, 1 ) = ( nextPosition + kUpVector * distance ).y();
    return true;
}

bool AiPedestrianManager::TestVisibility( AiPedestrian &pedestrian,
                                          CameraGame *pCamera ) {
    // FUN_0092b160
    Eigen::Vector4f center = pedestrian.getPosition() + pedestrian.SectorOrigin;
    if ( pCamera->sphereInFrustum( &center, kVisibilityRadius ) != 0 ) {
        const float distance =
            ( center - *pCamera->getWorldPositionRW() ).norm();
        if ( distance < gAIConfig.PedMaxLength2Player ) {
            pedestrian.Flags |= AiPedestrian::PF_InView;
            pedestrian.CameraDistance = distance;
            return true;
        }
    }

    pedestrian.Flags &=
        ~( AiPedestrian::PF_InView | AiPedestrian::PF_Displayed |
           AiPedestrian::PF_OverBudget );
    return false;
}
