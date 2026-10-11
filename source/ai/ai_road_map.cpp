#include "shared.h"
#include "ai_road_map.h"

#include "ai/ai_road_shape.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_road_discovery.h"
#include "system/system_shared.h"

static constexpr uint32_t kDensityZoneHackRoadID = 0x552;
static constexpr float kNearestRoadSearchExtent = 500.0f; // DAT_00faa994
static constexpr float kFarDistanceSq = 10000000000.0f;   // DAT_00f41b80
static constexpr int32_t kMaxNearestRoadClusters = 9;
static constexpr int32_t kMaxNearestRoadCandidates = 8;

static bool IsRoadEntityAccessible( const AiRoadMap::Road &road,
                                    const int32_t entityIndex ) {
    const SavedRoadDiscovery *pDiscovery =
        gpPlayerData->getLoadedSave()->getRoadDiscovery();
    return !pDiscovery->isRoadLocked( road.ID, entityIndex ) &&
           IsRoadAvailable( road.ID );
}

int32_t AiRoadMap::Junction::getRoadIndex( const Road *pRoad ) const {
    // FUN_0092dfd0
    if ( pRoad == nullptr ) {
        return -1;
    }
    for ( int32_t i = 0; i < NumRoads; i++ ) {
        if ( pRoads[i]->ID == pRoad->ID ) {
            return i;
        }
    }
    return 0;
}

int32_t AiRoadMap::Junction::getRoadIndex( const Road *pRoad,
                                           const int32_t occurrence ) const {
    // FUN_0092df90
    if ( pRoad == nullptr ) {
        return -1;
    }
    int32_t numFound = 0;
    for ( int32_t i = 0; i < NumRoads; i++ ) {
        if ( pRoads[i]->ID != pRoad->ID ) {
            continue;
        }
        if ( numFound == occurrence ) {
            return i;
        }
        numFound++;
    }
    return 0;
}

AiRoadMap::Junction *
AiRoadMap::Junction::getNeighbor( const int32_t index ) const {
    // FUN_0092e030
    const Road *pRoad = getRoad( index );
    if ( pRoad->pJunctionBegin == this ) {
        return pRoad->pJunctionEnd;
    }
    return ( pRoad->pJunctionEnd == this ) ? pRoad->pJunctionBegin : nullptr;
}

float AiRoadMap::Junction::computeRoadAngle( const int32_t index ) const {
    // FUN_0092e890
    static constexpr float kSlotAngles[4] = { 0.0f, 30.0f, 45.0f, 60.0f };

    const Road *pRoad = getRoad( index );
    int32_t slot = 0;
    if ( pRoad->pJunctionBegin != nullptr && pRoad->pJunctionBegin->ID == ID ) {
        slot = pRoad->AngleSlotBegin;
    } else if ( pRoad->pJunctionEnd != nullptr &&
                pRoad->pJunctionEnd->ID == ID ) {
        slot = pRoad->AngleSlotEnd;
    } else {
        return 0.0f;
    }
    return static_cast<float>( slot / 4 ) * 90.0f + kSlotAngles[slot & 3];
}

bool AiRoadMap::Junction::crossesCircle( const Eigen::Vector2f &centreXZ,
                                         const float radiusSq ) const {
    // FUN_0092fcc0
    const JunctionDetail *pJunctionDetail =
        static_cast<const JunctionDetail *>( pDetail );
    const float *pPoints =
        reinterpret_cast<const float *>( pJunctionDetail + 1 );
    const int32_t numPoints =
        pJunctionDetail->field_0x8 + pJunctionDetail->field_0x9;
    bool bAnyInside = false;
    bool bAnyOutside = false;
    for ( int32_t i = 0; i < numPoints; i++ ) {
        const Eigen::Vector2f point( pPoints[i * 2], pPoints[i * 2 + 1] );
        const float distanceSq = ( point - centreXZ ).squaredNorm();
        if ( distanceSq <= radiusSq ) {
            bAnyInside = true;
        } else {
            bAnyOutside = true;
        }
    }
    return bAnyInside && bAnyOutside;
}

int32_t AiRoadMap::Junction::pickStraightestRoad( const int32_t fromIndex,
                                                  float *pOutAngle ) const {
    // FUN_0092f6c0
    const float referenceAngle = computeRoadAngle( fromIndex );
    int32_t bestIndex = -1;
    float bestScore = -1.0f;
    float bestAngle = 0.0f;
    for ( int32_t i = 0; i < NumRoads; i++ ) {
        if ( i == fromIndex ) {
            continue;
        }
        float angle = computeRoadAngle( i ) - referenceAngle;
        if ( angle < 0.0f ) {
            angle += 360.0f;
        }
        const float score = fabsf( angle - 180.0f );
        if ( bestScore < 0.0f || score < bestScore ) {
            bestScore = score;
            bestAngle = angle;
            bestIndex = i;
        }
    }
    if ( pOutAngle != nullptr ) {
        *pOutAngle = bestAngle;
    }
    return bestIndex;
}

uint8_t AiRoadMap::Road::getNumLanesAt( const bool bEnd ) const {
    // FUN_0092e830
    return bEnd ? pDetail->pEntities[pDetail->NumEntities - 1]->NumRightLanes
                : pDetail->pEntities[0]->NumLeftLanes;
}

static float WrapAngle180( float angle ) {
    if ( -180.0f > angle ) {
        angle += 360.0f;
    }
    if ( angle > 180.0f ) {
        angle -= 360.0f;
    }
    return angle;
}

void AiRoadMap::Junction::classifySplit( JunctionSplit &outSplit,
                                         const uint32_t roadID ) const {
    // FUN_0092f310
    static constexpr float kSymmetryEpsilon = 0.001f; // DAT_00f4167c
    static constexpr float kMaxPairAngle = 45.0f;     // DAT_00f41808

    outSplit.Type = JunctionSplit::JST_None;
    if ( NumRoads != 3 ) {
        return;
    }

    int32_t numInbound = 0;
    int32_t numOutbound = 0;
    int32_t inbound[2] = { 0, 0 };
    int32_t outbound[2] = { 0, 0 };
    for ( int32_t i = 0; i < 3; i++ ) {
        const Road *pRoad = pRoads[i];
        if ( roadID == pRoad->ID ) {
            outSplit.RoadIndex = i;
        }
        const int32_t numRightLanes = pRoad->getNumLanesAt( true );
        const int32_t numLeftLanes = pRoad->getNumLanesAt( false );
        if ( numRightLanes > 0 && numLeftLanes > 0 ) {
            return;
        }

        const bool bBegin = ( pRoad->pJunctionBegin == this );
        const bool bInbound =
            bBegin ? ( numLeftLanes > 0 ) : ( numRightLanes > 0 );
        const uint8_t device =
            bBegin ? pRoad->CtrlDeviceBegin : pRoad->CtrlDeviceEnd;
        if ( device >= 3 || device == 1 ) {
            return;
        }

        if ( bInbound ) {
            inbound[( ++numInbound == 1 ) ? 0 : 1] = i;
        } else {
            outbound[( ++numOutbound == 1 ) ? 0 : 1] = i;
        }
    }

    int32_t pairA;
    int32_t pairB;
    if ( numInbound == 2 ) {
        outSplit.SingleIndex = outbound[0];
        pairA = inbound[0];
        pairB = inbound[1];
    } else if ( numOutbound == 2 ) {
        outSplit.SingleIndex = inbound[0];
        pairA = outbound[0];
        pairB = outbound[1];
    } else {
        return;
    }

    const float angleA = computeRoadAngle( pairA );
    const float angleB = computeRoadAngle( pairB );
    const float angleSingle = computeRoadAngle( outSplit.SingleIndex );
    const float pairDelta = WrapAngle180( angleB - angleA );
    const float deltaA = WrapAngle180( angleA - angleSingle );
    const float deltaB = WrapAngle180( angleSingle - angleB );

    const bool bSymmetric = fabsf( deltaB - deltaA ) < kSymmetryEpsilon;
    if ( !bSymmetric && fabsf( pairDelta ) > kMaxPairAngle ) {
        return;
    }

    const float side = ( numInbound == 2 ) ? -pairDelta : pairDelta;
    if ( side >= 0.0f ) {
        outSplit.PairIndexA = pairB;
        outSplit.PairIndexB = pairA;
    } else {
        outSplit.PairIndexA = pairA;
        outSplit.PairIndexB = pairB;
    }

    if ( bSymmetric ) {
        outSplit.Type = ( numInbound == 2 ) ? JunctionSplit::JST_SymmetricMerge
                                            : JunctionSplit::JST_SymmetricSplit;
    } else {
        outSplit.Type = ( numInbound != 2 ) ? JunctionSplit::JST_Split
                                            : JunctionSplit::JST_Merge;
    }
}

bool AiRoadMap::Junction::canTurnInto( const int32_t fromIndex,
                                       const int32_t toIndex ) const {
    // FUN_00932100
    const int32_t turnMask = computeTurnType( fromIndex, toIndex, nullptr );
    const Road *pRoad = getRoad( fromIndex );
    if ( pRoad == nullptr ) {
        return false;
    }

    const bool bTowardEnd = ( pRoad->pJunctionBegin != this );
    const int32_t numLanes = pRoad->getNumLanesAt( bTowardEnd );
    const int32_t numRightLanes = pRoad->getNumLanesAt( true );
    const int32_t numLeftLanes = pRoad->getNumLanesAt( false );
    for ( int32_t lane = 0; lane < numLanes; lane++ ) {
        int32_t index = bTowardEnd ? numLeftLanes - lane + numRightLanes - 1
                                   : numRightLanes - lane - 1;
        if ( index < 0 || index >= numLeftLanes + numRightLanes ) {
            index = 0;
        }
        int32_t laneFlags = static_cast<uint8_t>( pRoad->LaneFlags[index] );
        if ( laneFlags == 0 ) {
            laneFlags = 7;
        }
        if ( ( laneFlags & turnMask ) == turnMask ) {
            return true;
        }
    }
    return false;
}

int32_t AiRoadMap::Junction::computeTurnType( const int32_t fromIndex,
                                              const int32_t toIndex,
                                              float *pOutAngle ) const {
    // FUN_00931840
    static constexpr float kStraightTolerance = 5.0f; // DAT_00f41750
    static constexpr float kTurnAngleMin = 145.0f;    // DAT_00f41e68
    static constexpr float kTurnAngleMax = 215.0f;    // DAT_00f41e64

    float straightAngle = 0.0f;
    const int32_t straightIndex =
        pickStraightestRoad( fromIndex, &straightAngle );
    const float straightDeviation = fabsf( straightAngle - 180.0f );

    float angle = computeRoadAngle( toIndex ) - computeRoadAngle( fromIndex );
    if ( angle < 0.0f ) {
        angle += 360.0f;
    }
    if ( pOutAngle != nullptr ) {
        *pOutAngle = angle;
    }

    if ( straightDeviation <= kStraightTolerance ) {
        if ( toIndex == straightIndex ) {
            return 2;
        }
        if ( angle == 0.0f ) {
            return 0;
        }
        return ( angle > straightAngle ) ? 1 : 4;
    }

    if ( angle == 0.0f ) {
        return 0;
    }
    if ( angle <= kTurnAngleMin ) {
        return 4;
    }
    return ( angle > kTurnAngleMax ) ? 1 : 2;
}

int32_t
AiRoadMap::Junction::computeSideOfLine( const Eigen::Vector2f &pointXZ ) const {
    // FUN_0092f570
    if ( Flags == 0 ) {
        return 0;
    }

    Eigen::Vector2f lineStart = Eigen::Vector2f::Zero();
    Eigen::Vector2f lineEnd = Eigen::Vector2f::Zero();
    const JunctionDetail *pJunctionDetail =
        static_cast<const JunctionDetail *>( pDetail );
    if ( pJunctionDetail->field_0xa >= 2 ) {
        const float *pLine = reinterpret_cast<const float *>(
            reinterpret_cast<const uint8_t *>( pJunctionDetail + 1 ) +
            ( pJunctionDetail->field_0x8 + pJunctionDetail->field_0x9 ) * 8 );
        lineStart = Eigen::Vector2f( pLine[0], pLine[1] );
        lineEnd = Eigen::Vector2f( pLine[2], pLine[3] );
    }

    const float cross =
        ( pointXZ.y() - lineStart.y() ) * ( lineEnd.x() - lineStart.x() ) -
        ( pointXZ.x() - lineStart.x() ) * ( lineEnd.y() - lineStart.y() );
    return ( cross >= 0.0f ) ? 0 : 1;
}

bool AiRoadMap::Junction::containsPoint(
    const Eigen::Vector2f &pointXZ ) const {
    // FUN_0092fe60
    const JunctionDetail *pJunctionDetail =
        static_cast<const JunctionDetail *>( pDetail );
    if ( pJunctionDetail == nullptr ) {
        return false;
    }
    const float *pPoints =
        reinterpret_cast<const float *>( pJunctionDetail + 1 );
    int32_t first = 0;
    int32_t numPoints = pJunctionDetail->field_0x8;
    if ( Flags == 2 && computeSideOfLine( pointXZ ) != 0 &&
         pJunctionDetail->field_0x9 > 0 ) {
        first = pJunctionDetail->field_0x8;
        numPoints = pJunctionDetail->field_0x9;
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

bool AiRoadMap::Junction::findClosestOutlinePoint(
    const Eigen::Vector4f &position,
    const float maxDistance,
    float *pOutDistance,
    Eigen::Vector4f *pOutPoint ) const {
    // FUN_0092f770
    const JunctionDetail *pJunctionDetail =
        static_cast<const JunctionDetail *>( pDetail );
    float bestDistanceSq = maxDistance * maxDistance;
    bool bFound = false;
    Eigen::Vector2f bestPoint = Eigen::Vector2f::Zero();
    if ( pJunctionDetail != nullptr ) {
        const float *pPoints =
            reinterpret_cast<const float *>( pJunctionDetail + 1 );
        const int32_t numPoints =
            pJunctionDetail->field_0x8 + pJunctionDetail->field_0x9;
        const Eigen::Vector2f point( position.x(), position.z() );
        for ( int32_t i = 1; i < numPoints; i++ ) {
            const Eigen::Vector2f a( pPoints[( i - 1 ) * 2],
                                     pPoints[( i - 1 ) * 2 + 1] );
            const Eigen::Vector2f b( pPoints[i * 2], pPoints[i * 2 + 1] );
            const Eigen::Vector2f segment = b - a;
            const float lengthSq = segment.squaredNorm();
            if ( lengthSq <= 0.0f ) {
                continue;
            }
            const float t = ( point - a ).dot( segment ) / lengthSq;
            if ( t < 0.0f || t > 1.0f ) {
                continue;
            }
            const Eigen::Vector2f closest = a + segment * t;
            const float distanceSq = ( point - closest ).squaredNorm();
            if ( distanceSq < bestDistanceSq ) {
                bestDistanceSq = distanceSq;
                bestPoint = closest;
                bFound = true;
            }
        }
    }
    if ( pOutDistance != nullptr ) {
        *pOutDistance = std::sqrt( bestDistanceSq );
    }
    if ( bFound && pOutPoint != nullptr ) {
        *pOutPoint =
            Eigen::Vector4f( bestPoint.x(), Position.Y, bestPoint.y(), 1.0f );
    }
    return bFound;
}

AiRoadMap::AiRoadMap()
    : bSelfAllocated( false ),
      bDetailInstalled( false ),
      NumTrafficSignalTypes( 0 ),
      NumSpeedLimits( 0 ),
      pArraySpeedLimit( nullptr ),
      NumJunction( 0 ),
      ppJunctions( nullptr ),
      NumRoads( 0 ),
      pRoads( nullptr ),
      NumProfile( 0 ),
      ppProfile( nullptr ),
      SizePoolNames( 0 ),
      pPoolNames( nullptr ),
      pEntityArray( nullptr ),
      SizeRoadmap( 0 ),
      SizeDetails( 0 ) {}

AiRoadMap::~AiRoadMap() {
    if ( bSelfAllocated && pArraySpeedLimit != nullptr ) {
        TestDrive::Free( pArraySpeedLimit );
    }
    if ( ppJunctions != nullptr ) {
        TestDrive::Free( ppJunctions );
    }
    if ( ppProfile != nullptr ) {
        TestDrive::Free( ppProfile );
    }
}

void AiRoadMap::reset() {
    // FUN_00969060 (inlined)
    bSelfAllocated = false;
    NumJunction = 0;
    NumRoads = 0;
    NumProfile = 0;
    ppJunctions = nullptr;
    pRoads = nullptr;
    ppProfile = nullptr;
}

void AiRoadMap::load( void *pStream, const uint32_t streamSize ) {
    // FUN_0092ef00
    uint8_t *pFile = static_cast<uint8_t *>( pStream );
    FileHeader *pHeader = reinterpret_cast<FileHeader *>( pFile );

    SizeRoadmap = streamSize;
    NumJunction = pHeader->NumJunction;
    NumRoads = pHeader->NumRoads;
    NumProfile = pHeader->NumProfile;
    uint8_t *pProfileData =
        reinterpret_cast<uint8_t *>( &pHeader->ProfilesOffset ) +
        pHeader->ProfilesOffset;
    const int32_t poolNamesOffset = pHeader->PoolNamesOffset;
    const int32_t entityArrayOffset = pHeader->EntityArrayOffset;
    NumTrafficSignalTypes = pHeader->NumTrafficSignalTypes;
    NumSpeedLimits = pHeader->NumSpeedLimits;

    float *pSpeedLimits = reinterpret_cast<float *>( pHeader + 1 );
    if ( !bSelfAllocated ) {
        pArraySpeedLimit = pSpeedLimits;
    } else {
        pArraySpeedLimit = static_cast<float *>(
            TestDrive::Alloc( NumSpeedLimits * sizeof( float ) ) );
        memcpy( pArraySpeedLimit, pSpeedLimits,
                NumSpeedLimits * sizeof( float ) );
    }

    uint8_t *pData = AlignToPointerSlot(
        reinterpret_cast<uint8_t *>( pSpeedLimits + NumSpeedLimits ) );
    ppJunctions = static_cast<Junction **>(
        TestDrive::Alloc( NumJunction * sizeof( Junction * ) ) );
    for ( int32_t i = 0; i < NumJunction; i++ ) {
        ppJunctions[i] = reinterpret_cast<Junction *>( pData );
        pData += Junction::GetSize( ppJunctions[i]->NumRoads );
    }
    pRoads = reinterpret_cast<Road *>( pData );

    ppProfile = static_cast<AiRoadProfile **>(
        TestDrive::Alloc( NumProfile * sizeof( AiRoadProfile * ) ) );
    for ( int32_t i = 0; i < NumProfile; i++ ) {
        ppProfile[i] = reinterpret_cast<AiRoadProfile *>( pProfileData );
        pProfileData += ppProfile[i]->getSize();
    }

    pPoolNames = reinterpret_cast<char *>(
        reinterpret_cast<uint8_t *>( &pHeader->PoolNamesOffset + 1 ) +
        poolNamesOffset );
    pEntityArray = reinterpret_cast<EntityArray *>(
        reinterpret_cast<uint8_t *>( &pHeader->EntityArrayOffset + 1 ) +
        entityArrayOffset );

    for ( int32_t i = 0; i < NumRoads; i++ ) {
        Road &road = pRoads[i];
        const uint32_t junctionBeginID =
            PointerSlotToValue( road.pJunctionBegin );
        const uint32_t junctionEndID = PointerSlotToValue( road.pJunctionEnd );
        if ( junctionBeginID != 0u ) {
            road.pJunctionBegin = getJunction( junctionBeginID );
        }
        if ( junctionEndID != 0u ) {
            road.pJunctionEnd = getJunction( junctionEndID );
        }
    }

    for ( int32_t i = 0; i < NumJunction; i++ ) {
        Junction *pJunction = ppJunctions[i];
        for ( int32_t j = 0; j < pJunction->NumRoads; j++ ) {
            const uint32_t roadID = PointerSlotToValue( pJunction->pRoads[j] );
            if ( roadID != 0u ) {
                pJunction->pRoads[j] = getRoad( roadID );
            }
        }
    }
}

void AiRoadMap::installDetails( void *pStream, const uint32_t streamSize ) {
    // FUN_00931590
    SizeDetails = streamSize;

    DetailsFileHeader *pHeader =
        reinterpret_cast<DetailsFileHeader *>( pStream );
    uint8_t *pData =
        AlignToPointerSlot( reinterpret_cast<uint8_t *>( pHeader + 1 ) );
    for ( int32_t i = pHeader->NumRoadDetails; i > 0; i-- ) {
        RoadDetail *pDetail = reinterpret_cast<RoadDetail *>( pData );
        for ( int32_t j = 0; j < pDetail->NumEntities; j++ ) {
            Entity *pEntity = reinterpret_cast<Entity *>(
                pData + PointerSlotToValue( pDetail->pEntities[j] ) );
            pDetail->pEntities[j] = pEntity;
            pEntity->pProfile =
                getProfile( PointerSlotToValue( pEntity->pProfile ) );
            pEntity->Flags = 0;
        }

        Road *pRoad = getRoad( pDetail->RoadID );
        if ( pRoad != nullptr ) {
            pRoad->pDetail = pDetail;
        }

        pData = pDetail->pEntities[pDetail->NumEntities - 1]->getEnd();
    }

    const int32_t numJunctionDetails = *reinterpret_cast<int32_t *>( pData );
    pData += sizeof( int32_t );
    for ( int32_t i = numJunctionDetails; i > 0; i-- ) {
        JunctionDetail *pJunctionDetail =
            reinterpret_cast<JunctionDetail *>( pData );
        Junction *pJunction = getJunction( pJunctionDetail->JunctionID );
        if ( pJunction != nullptr ) {
            pJunction->pDetail = pJunctionDetail;
        }
        pData += pJunctionDetail->getSize();
    }

    installEntityArray();
    bDetailInstalled = true;

    Road *pHackRoad = getRoad( kDensityZoneHackRoadID );
    if ( pHackRoad != nullptr ) {
        RoadDetail *pDetail = pHackRoad->pDetail;
        for ( int32_t i = 0; i < pDetail->NumEntities; i++ ) {
            pDetail->pEntities[i]->DensityZoneIndice = 1;
        }
    }
}

void AiRoadMap::installEntityArray() {
    // FUN_0092f280
    EntityArray *pArray = pEntityArray;
    const int32_t numClusters = pArray->NumRows * pArray->NumColumns;
    for ( int32_t i = 0; i < numClusters; i++ ) {
        EntityCluster *pCluster = reinterpret_cast<EntityCluster *>(
            reinterpret_cast<uint8_t *>( pArray ) +
            PointerSlotToValue( pArray->pClusters[i] ) );
        pArray->pClusters[i] = pCluster;
        for ( uint32_t j = 0; j < pCluster->NumEntities; j++ ) {
            EntityRef &ref = pCluster->Refs[j];
            ref.pRoad = getRoad( PointerSlotToValue( ref.pRoad ) );
        }
    }
}

int32_t AiRoadMap::EntityArray::gatherClusters(
    EntityCluster **ppOutClusters,
    const int32_t maxClusters,
    const Eigen::Vector4f &boundsMin,
    const Eigen::Vector4f &boundsMax ) const {
    // FUN_0092e520
    auto ToCellIndex = []( const float coordinate, const float cellSizeInv,
                           const int32_t lastIndex ) {
        const int32_t index = static_cast<int32_t>( coordinate * cellSizeInv );
        if ( index < 0 ) {
            return 0;
        }
        return ( index <= lastIndex ) ? index : lastIndex;
    };

    const int32_t lastColumn = NumColumns - 1;
    const int32_t lastRow = NumRows - 1;
    const int32_t firstColumn =
        ToCellIndex( boundsMin.x(), 1.0f / ClusterWidth, lastColumn );
    const int32_t firstRow =
        ToCellIndex( boundsMin.z(), 1.0f / ClusterHeight, lastRow );
    const int32_t endColumn =
        ToCellIndex( boundsMax.x(), 1.0f / ClusterWidth, lastColumn );
    const int32_t endRow =
        ToCellIndex( boundsMax.z(), 1.0f / ClusterHeight, lastRow );

    int32_t numClusters = 0;
    for ( int32_t column = firstColumn; column <= endColumn; column++ ) {
        for ( int32_t row = firstRow; row <= endRow; row++ ) {
            EntityCluster *pCluster = pClusters[NumRows * column + row];
            if ( pCluster->NumEntities == 0 ) {
                continue;
            }
            if ( numClusters == maxClusters ) {
                return numClusters;
            }
            ppOutClusters[numClusters++] = pCluster;
        }
    }
    return numClusters;
}

AiRoadMap::Road *
AiRoadMap::findNearestRoad( const Eigen::Vector4f &position,
                            const bool bIgnoreDiscovery ) const {
    // FUN_00933570
    struct Candidate {
        Road *pRoad;
        uint32_t EntityIndex;
        float WaypointDistanceSq;
        float ProjectedDistanceSq;
        Candidate *pNext;
    };

    const float searchDistanceSq =
        kNearestRoadSearchExtent * kNearestRoadSearchExtent;
    const Eigen::Vector4f searchExtent( kNearestRoadSearchExtent, 0.0f,
                                        kNearestRoadSearchExtent, 0.0f );
    EntityCluster *pClusters[kMaxNearestRoadClusters];
    const int32_t numClusters = pEntityArray->gatherClusters(
        pClusters, kMaxNearestRoadClusters, position - searchExtent,
        position + searchExtent );

    Candidate candidates[kMaxNearestRoadCandidates];
    int32_t numCandidates = 0;
    Candidate *pFirstCandidate = nullptr;
    for ( int32_t i = 0; i < numClusters; i++ ) {
        const EntityCluster *pCluster = pClusters[i];
        for ( uint32_t j = 0; j < pCluster->NumEntities; j++ ) {
            const EntityRef &ref = pCluster->Refs[j];
            if ( !bIgnoreDiscovery &&
                 !IsRoadEntityAccessible( *ref.pRoad, ref.EntityIndex ) ) {
                continue;
            }

            AiRoadVertexIt vertexIt;
            vertexIt.pRoad = ref.pRoad;
            vertexIt.pDetail = ref.pRoad->pDetail;
            vertexIt.IndEntity = ref.EntityIndex;
            vertexIt.IndVertex = 0;
            vertexIt.bWay = true;
            float distanceSq = searchDistanceSq;
            do {
                if ( vertexIt.IndEntity !=
                     static_cast<int32_t>( ref.EntityIndex ) ) {
                    break;
                }
                const float waypointDistanceSq =
                    ( vertexIt.getWaypoint().toVector4() - position )
                        .squaredNorm();
                if ( waypointDistanceSq < distanceSq ) {
                    distanceSq = waypointDistanceSq;
                }
            } while ( vertexIt.advance( false ) );

            if ( !( distanceSq < searchDistanceSq ) ) {
                continue;
            }

            Candidate *pCandidate = nullptr;
            Candidate *pPrevious = nullptr;
            bool bAlreadyCloser = false;
            for ( Candidate *pIt = pFirstCandidate; pIt != nullptr;
                  pIt = pIt->pNext ) {
                if ( pIt->pRoad == ref.pRoad ) {
                    if ( pIt->WaypointDistanceSq < distanceSq ) {
                        bAlreadyCloser = true;
                    } else if ( pPrevious == nullptr ) {
                        pFirstCandidate = pIt->pNext;
                        pCandidate = pIt;
                    } else {
                        pPrevious->pNext = pIt->pNext;
                        pCandidate = pIt;
                    }
                    break;
                }
                pPrevious = pIt;
            }
            if ( bAlreadyCloser ) {
                continue;
            }

            if ( pCandidate == nullptr ) {
                if ( numCandidates < kMaxNearestRoadCandidates ) {
                    pCandidate = &candidates[numCandidates++];
                } else {
                    if ( pFirstCandidate->WaypointDistanceSq <= distanceSq ) {
                        continue;
                    }
                    pCandidate = pFirstCandidate;
                    pFirstCandidate = pFirstCandidate->pNext;
                }
            }

            Candidate *pInsertAfter = nullptr;
            Candidate *pInsertBefore = pFirstCandidate;
            while ( pInsertBefore != nullptr &&
                    distanceSq < pInsertBefore->WaypointDistanceSq ) {
                pInsertAfter = pInsertBefore;
                pInsertBefore = pInsertBefore->pNext;
            }
            pCandidate->pRoad = ref.pRoad;
            pCandidate->EntityIndex = ref.EntityIndex;
            pCandidate->WaypointDistanceSq = distanceSq;
            pCandidate->ProjectedDistanceSq = 0.0f;
            pCandidate->pNext = pInsertBefore;
            if ( pInsertAfter != nullptr ) {
                pInsertAfter->pNext = pCandidate;
            } else {
                pFirstCandidate = pCandidate;
            }
        }
    }

    Matrix4x4f_t frame = Matrix4x4f_t::Identity();
    frame.row( 3 ) = position.transpose();
    if ( numCandidates == 0 ) {
        return findNearestRoadExhaustive( frame, bIgnoreDiscovery );
    }

    uint32_t nearestRoadID = pFirstCandidate->pRoad->ID;
    float nearestDistanceSq = searchDistanceSq;
    for ( Candidate *pCandidate = pFirstCandidate; pCandidate != nullptr;
          pCandidate = pCandidate->pNext ) {
        AiRoadShape *pShape = pCandidate->pRoad->getShape();

        AiRoadIt roadIt;
        Matrix4x4f_t projectedFrame;
        ProjectOnRoadShape( frame, roadIt, &projectedFrame, false, pShape );

        const float distanceSq =
            ( projectedFrame.row( 3 ).transpose() - position ).squaredNorm();
        pCandidate->ProjectedDistanceSq = distanceSq;
        if ( distanceSq < nearestDistanceSq ) {
            nearestRoadID = pCandidate->pRoad->ID;
            nearestDistanceSq = distanceSq;
        }
    }
    return &pRoads[nearestRoadID - 1];
}

AiRoadMap::Road *
AiRoadMap::findNearestRoadExhaustive( const Matrix4x4f_t &frame,
                                      const bool bIgnoreDiscovery ) const {
    // FUN_00931390
    const Eigen::Vector4f position = frame.row( 3 ).transpose();
    float nearestDistanceSq = kFarDistanceSq;
    int32_t nearestRoadIndex = 0;
    for ( int32_t i = 0; i < NumRoads; i++ ) {
        Road &road = pRoads[i];
        if ( road.pDetail == nullptr ) {
            continue;
        }

        if ( !bIgnoreDiscovery ) {
            if ( !IsRoadAvailable( road.ID ) ) {
                continue;
            }

            const SavedRoadDiscovery *pDiscovery =
                gpPlayerData->getLoadedSave()->getRoadDiscovery();
            bool bDiscovered = false;
            for ( int32_t j = 0; j < road.pDetail->NumEntities; j++ ) {
                if ( !pDiscovery->isRoadLocked( road.ID, j ) ) {
                    bDiscovered = true;
                    break;
                }
            }
            if ( !bDiscovered ) {
                continue;
            }
        }

        AiRoadVertexIt vertexIt;
        vertexIt.pRoad = &road;
        vertexIt.pDetail = road.pDetail;
        vertexIt.IndEntity = 0;
        vertexIt.IndVertex = 0;
        vertexIt.bWay = true;
        do {
            const float distanceSq =
                ( vertexIt.getWaypoint().toVector4() - position ).squaredNorm();
            if ( distanceSq < nearestDistanceSq ) {
                nearestDistanceSq = distanceSq;
                nearestRoadIndex = i;
            }
        } while ( vertexIt.advance( false ) );
    }
    return &pRoads[nearestRoadIndex];
}

AiRoadMap::Road *
AiRoadMap::findNearestRoadInRadius( const Eigen::Vector4f &position,
                                    const float radius ) const {
    // FUN_00931170
    float nearestDistanceSq = kFarDistanceSq;
    Road *pNearestRoad = nullptr;
    for ( int32_t i = 0; i < NumRoads; i++ ) {
        Road &road = pRoads[i];
        if ( road.pDetail == nullptr ) {
            continue;
        }

        const float reach = static_cast<float>( road.Distance ) + radius;
        const float startDistanceSq =
            ( road.pDetail->pEntities[0]->FirstCV1.toVector4() - position )
                .squaredNorm();
        if ( reach * reach < startDistanceSq ) {
            continue;
        }

        AiRoadVertexIt vertexIt;
        vertexIt.pRoad = &road;
        vertexIt.pDetail = road.pDetail;
        vertexIt.IndEntity = 0;
        vertexIt.IndVertex = 0;
        vertexIt.bWay = true;
        do {
            const float distanceSq =
                ( vertexIt.getWaypoint().toVector4() - position ).squaredNorm();
            if ( distanceSq < nearestDistanceSq ) {
                nearestDistanceSq = distanceSq;
                pNearestRoad = &road;
            }
        } while ( vertexIt.advance( false ) );
    }
    return pNearestRoad;
}
