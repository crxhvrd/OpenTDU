#include "shared.h"
#include "ai_path.h"

#include "ai/ai_road_shape.h"
#include "ai/gs_ai.h"
#include "player_data/saved_road_discovery.h"

static constexpr int32_t kMaxSolverIterations = 1000000;
static constexpr int32_t kSplitJunctionFlags = 2;
static constexpr float kMinPathT = 0.001f;              // DAT_00f4167c
static constexpr float kMaxPathT = 0.999f;              // DAT_00f41e54
static constexpr float kRoadEndT = 0.99f;               // DAT_00f41a44
static constexpr float kRoadStartT = 0.01f;             // DAT_00f416d4
static constexpr float kCurvatureScale = 1.0f / 510.0f; // DAT_00f41d18
static constexpr float kReversePenalty = 250.0f;        // DAT_00f41824

static AiRoadMap &GetRoadMap() { return gpAI->getRoadMap(); }

static bool IsJunctionWaypoint( const int32_t waypoint ) {
    return waypoint > 0;
}

static float ComputeCurvatureFactor( const AiRoadMap::Road *pRoad ) {
    return static_cast<float>( pRoad->Curvature ) * kCurvatureScale + 1.0f;
}

static float ComputeTravelledFraction( const AiRoadShape *pShape,
                                       const AiRoadIt &it ) {
    float fraction = gbUniformRoadFraction
                         ? pShape->computeUniformRoadFraction( it )
                         : pShape->computeRoadFraction( it );
    if ( it.bWay ) {
        fraction = 1.0f - fraction;
    }
    return fraction;
}

static const AiRoadMap::Entity *GetFirstEntity( const AiRoadMap::Road *pRoad ) {
    return pRoad->pDetail->pEntities[0];
}

static const AiRoadMap::Entity *GetLastEntity( const AiRoadMap::Road *pRoad ) {
    return pRoad->pDetail->pEntities[pRoad->pDetail->NumEntities - 1];
}

static bool IsSplitJunction( const AiRoadMap::Junction *pJunction ) {
    return pJunction != nullptr && pJunction->Flags == kSplitJunctionFlags;
}

void AiCompressedRoadIt::pack( const AiRoadIt &it ) {
    // FUN_009655e0
    const bool bAtExtremity = ( it.isAtRoadEnd() && it.T >= kRoadEndT ) ||
                              ( it.isAtRoadStart() && it.T <= kRoadStartT );
    RoadInfos =
        ( it.bWay ? 1u : 0u ) | ( bAtExtremity ? 0u : 2u ) |
        ( ( it.pRoad->ID << 2 ) & 0xfffcu ) |
        ( static_cast<uint32_t>( static_cast<uint8_t>( it.IndEntity ) )
          << 16 ) |
        ( static_cast<uint32_t>( static_cast<uint8_t>( it.IndWay ) ) << 24 );
    Curve = it.T;
}

void AiCompressedRoadIt::unpack( AiRoadIt &outIt ) const {
    // FUN_009652d0
    outIt.pRoad = GetRoadMap().getRoad( ( RoadInfos >> 2 ) & 0x3fffu );
    outIt.IndEntity = static_cast<int32_t>( ( RoadInfos >> 16 ) & 0xffu );
    outIt.IndWay = static_cast<int32_t>( RoadInfos >> 24 );
    outIt.bWay = ( RoadInfos & 1u ) != 0;
    outIt.T = Curve;
}

AiPathPool::AiPathPool() : bInitialized( false ) {}

AiPathPool::~AiPathPool() {}

void AiPathPool::initialize( const int32_t numNodes,
                             const int32_t numRouteElems ) {
    // FUN_00967280
    nodes.assign( numNodes, AiPathNode{} );
    freeNodes.initialize( numNodes );
    for ( AiPathNode &node : nodes ) {
        freeNodes.push( &node );
    }

    routeElems.assign( numRouteElems, AiRouteElem{} );
    freeRouteElems.initialize( numRouteElems );
    for ( AiRouteElem &elem : routeElems ) {
        freeRouteElems.push( &elem );
    }
    bInitialized = true;
}

AiPathNode *AiPathPool::allocNode() {
    // FUN_00966c70
    return freeNodes.pop();
}

void AiPathPool::freeNode( AiPathNode *pNode ) {
    // FUN_00966c20
    memset( pNode, 0, sizeof( AiPathNode ) );
    freeNodes.push( pNode );
}

AiRouteElem *AiPathPool::allocRouteElem() {
    // FUN_00966bd0
    return freeRouteElems.pop();
}

void AiPathPool::freeRouteElem( AiRouteElem *pElem ) {
    // FUN_00967190
    pElem->reset();
    freeRouteElems.push( pElem );
}

void AiPathEndpoint::buildLinks( const bool bIgnoreOneWay,
                                 const bool bReversePenalty,
                                 const bool bAllowReverseStart ) {
    // FUN_009684c0
    AiRoadMap::Road *pRoad = It.pRoad;
    AiRoadMap::Junction *pAhead =
        It.bWay ? pRoad->pJunctionEnd : pRoad->pJunctionBegin;
    AiRoadMap::Junction *pBehind =
        It.bWay ? pRoad->pJunctionBegin : pRoad->pJunctionEnd;

    const float curvatureFactor = ComputeCurvatureFactor( pRoad );
    const AiRoadShape *pShape = pRoad->getShape();
    const float fraction = ComputeTravelledFraction( pShape, It );
    const float covered = pShape->TotalLength * fraction * curvatureFactor;
    float remaining = pShape->TotalLength * curvatureFactor - covered;
    if ( bReversePenalty ) {
        remaining += kReversePenalty;
    }

    int32_t costToA = static_cast<int32_t>( covered );
    int32_t costFromA = costToA;
    int32_t costToB = static_cast<int32_t>( remaining );
    int32_t costFromB = costToB;
    if ( bReversePenalty && !bAllowReverseStart ) {
        costToB = -1;
        costFromB = -1;
    }

    if ( !bIgnoreOneWay && pAhead != pBehind ) {
        const uint8_t numBackwardLanes =
            It.bWay ? GetFirstEntity( pRoad )->NumLeftLanes
                    : GetLastEntity( pRoad )->NumRightLanes;
        if ( numBackwardLanes == 0 ) {
            costToB = -1;
            costFromA = -1;
        }
        const uint8_t numForwardLanes =
            It.bWay ? GetLastEntity( pRoad )->NumRightLanes
                    : GetFirstEntity( pRoad )->NumLeftLanes;
        if ( numForwardLanes == 0 ) {
            costToA = -1;
            costFromB = -1;
        }
    }

    if ( pAhead == pBehind ) {
        WaypointA = pAhead->ID;
        WaypointB = pAhead->ID;
        CostToA = costToA;
        CostToB = costToB;
        CostFromA = costFromA;
        CostFromB = costFromB;
        NumExtraA = 0;
        NumExtraB = 0;
        IndexA = pAhead->getRoadIndex( pRoad, 0 );
        IndexB = pAhead->getRoadIndex( pRoad, 1 );
    } else if ( pAhead == nullptr ) {
        if ( pBehind != nullptr ) {
            WaypointA = pBehind->ID;
            WaypointB = 0;
            CostToA = costToB;
            CostFromA = costFromB;
            CostToB = -1;
            CostFromB = -1;
            NumExtraA = 1;
            NumExtraB = 0;
            IndexA = pBehind->NumNeighbors;
            IndexB = 0;
        }
    } else if ( pBehind == nullptr ) {
        WaypointA = pAhead->ID;
        WaypointB = 0;
        CostToA = costToA;
        CostFromA = costFromA;
        CostToB = -1;
        CostFromB = -1;
        NumExtraA = 1;
        NumExtraB = 0;
        IndexA = pAhead->NumNeighbors;
        IndexB = 0;
    } else {
        WaypointA = pAhead->ID;
        WaypointB = pBehind->ID;
        CostToA = costToA;
        CostToB = costToB;
        CostFromA = costFromA;
        CostFromB = costFromB;
        NumExtraA = 0;
        NumExtraB = 0;
        IndexA = pAhead->getRoadIndex( pRoad );
        IndexB = pBehind->getRoadIndex( pRoad );
    }
}

void AiPathEndpoint::buildFloodLinks( const bool bBehind, const bool bAhead ) {
    // FUN_00966980
    AiRoadMap::Road *pRoad = It.pRoad;
    AiRoadMap::Junction *pAhead =
        It.bWay ? pRoad->pJunctionEnd : pRoad->pJunctionBegin;
    AiRoadMap::Junction *pBehind =
        It.bWay ? pRoad->pJunctionBegin : pRoad->pJunctionEnd;

    const AiRoadShape *pShape = pRoad->getShape();
    const float fraction = ComputeTravelledFraction( pShape, It );
    const float covered = pShape->TotalLength * fraction;
    const float remaining = pShape->TotalLength - covered;

    if ( pAhead == pBehind ) {
        WaypointA = pAhead->ID;
        WaypointB = pAhead->ID;
        CostToB = static_cast<int32_t>( remaining );
        NumExtraA = 0;
        NumExtraB = 0;
        CostToA = static_cast<int32_t>( covered );
        IndexA = pAhead->getRoadIndex( pRoad, 0 );
        IndexB = pAhead->getRoadIndex( pRoad, 1 );
    } else if ( pAhead == nullptr ) {
        if ( pBehind != nullptr ) {
            WaypointA = pBehind->ID;
            WaypointB = 0;
            CostToA = bBehind ? static_cast<int32_t>( remaining ) : -1;
            CostToB = -1;
            NumExtraA = 1;
            NumExtraB = 0;
            IndexA = pBehind->NumNeighbors;
            IndexB = 0;
        }
    } else if ( pBehind != nullptr ) {
        WaypointA = pAhead->ID;
        WaypointB = pBehind->ID;
        CostToA = bAhead ? static_cast<int32_t>( covered ) : -1;
        CostToB = bBehind ? static_cast<int32_t>( remaining ) : -1;
        NumExtraA = 0;
        NumExtraB = 0;
        IndexA = pAhead->getRoadIndex( pRoad );
        IndexB = pBehind->getRoadIndex( pRoad );
    } else {
        WaypointA = pAhead->ID;
        WaypointB = 0;
        CostToA = bAhead ? static_cast<int32_t>( covered ) : -1;
        CostToB = -1;
        NumExtraA = 1;
        NumExtraB = 0;
        IndexA = pAhead->NumNeighbors;
        IndexB = 0;
    }
}

void AiPathGraph::setStart( const AiRoadIt &it, const int32_t side ) {
    // FUN_009681a0
    Start.It = it;
    Start.Side = side;
    Start.Position = it.computePosition( -1 );
}

void AiPathGraph::setEnd( const AiRoadIt &it ) {
    // FUN_00968160
    End.It = it;
    End.Side = 0;
    End.Position = it.computePosition( -1 );
}

void AiPathGraph::buildEndpoints() {
    // FUN_00968730
    if ( Start.It.pRoad->ID == End.It.pRoad->ID ) {
        buildSameRoadEndpoints();
        return;
    }

    Start.buildLinks( bIgnoreOneWay, bReversePenalty, bAllowReverseStart );
    End.buildLinks( bIgnoreOneWay, false, false );
    if ( Start.WaypointA == End.WaypointA && Start.NumExtraA >= 1 &&
         End.NumExtraA >= 1 && Start.IndexA == End.IndexA ) {
        End.IndexA++;
    }
}

void AiPathGraph::buildSameRoadEndpoints() {
    // FUN_00968730
    if ( End.It.bWay != Start.It.bWay ) {
        End.It.bWay = Start.It.bWay;
    }

    AiRoadMap::Road *pRoad = Start.It.pRoad;
    AiRoadMap::Junction *pBehind =
        Start.It.bWay ? pRoad->pJunctionBegin : pRoad->pJunctionEnd;
    AiRoadMap::Junction *pAhead =
        Start.It.bWay ? pRoad->pJunctionEnd : pRoad->pJunctionBegin;
    const AiRoadShape *pShape = pRoad->getShape();

    const bool bStartFirst = Start.It.isBefore( End.It );
    AiPathEndpoint &rear = bStartFirst ? Start : End;
    AiPathEndpoint &front = bStartFirst ? End : Start;

    const float length = pShape->TotalLength;
    const float curvatureFactor = ComputeCurvatureFactor( pRoad );
    const float rearCovered =
        ( length - ComputeTravelledFraction( pShape, rear.It ) * length ) *
        curvatureFactor;
    const float frontRemaining =
        ComputeTravelledFraction( pShape, front.It ) * length * curvatureFactor;
    const float gap = curvatureFactor * length - frontRemaining - rearCovered;

    int32_t rearCost = static_cast<int32_t>( rearCovered );
    int32_t backCost = rearCost;
    int32_t frontCostA = static_cast<int32_t>( frontRemaining );
    int32_t frontCostB = frontCostA;
    int32_t gapCostA = static_cast<int32_t>( gap );
    int32_t gapCostB = gapCostA;
    if ( bReversePenalty ) {
        backCost = bAllowReverseStart
                       ? backCost + static_cast<int32_t>( kReversePenalty )
                       : -1;
    }

    if ( !bIgnoreOneWay ) {
        const uint8_t numForwardLanes =
            Start.It.bWay ? GetLastEntity( pRoad )->NumRightLanes
                          : GetFirstEntity( pRoad )->NumLeftLanes;
        if ( numForwardLanes == 0 ) {
            rearCost = -1;
            gapCostB = -1;
            frontCostA = -1;
        }
        const uint8_t numBackwardLanes =
            Start.It.bWay ? GetFirstEntity( pRoad )->NumLeftLanes
                          : GetLastEntity( pRoad )->NumRightLanes;
        if ( numBackwardLanes == 0 ) {
            frontCostB = -1;
            gapCostA = -1;
            backCost = -1;
        }
    }

    rear.CostFromA = gapCostA;
    rear.CostToA = gapCostB;
    rear.WaypointA =
        bStartFirst ? AiPathNode::kGoalWaypoint : AiPathNode::kStartWaypoint;
    rear.IndexA = 0;
    rear.NumExtraA = ( pBehind == nullptr ) ? 1 : 0;

    front.CostFromA = gapCostB;
    front.CostToA = gapCostA;
    front.IndexA = 0;
    front.WaypointA =
        bStartFirst ? AiPathNode::kStartWaypoint : AiPathNode::kGoalWaypoint;
    front.NumExtraA = ( pAhead == nullptr ) ? 1 : 0;

    if ( pBehind != nullptr ) {
        rear.WaypointB = pBehind->ID;
        rear.CostToB = backCost;
        rear.CostFromB = rearCost;
        if ( pAhead != nullptr ) {
            rear.NumExtraB = 0;
            rear.IndexB = pBehind->getRoadIndex( rear.It.pRoad );
        } else {
            rear.NumExtraB = 1;
            rear.IndexB = pBehind->NumNeighbors;
        }
    } else {
        rear.WaypointB = 0;
        rear.CostToB = -1;
        rear.CostFromB = -1;
        rear.NumExtraB = 0;
        rear.IndexB = 0;
    }

    if ( pAhead == nullptr ) {
        front.WaypointB = 0;
        front.CostToB = -1;
        front.CostFromB = -1;
        front.NumExtraB = 0;
        front.IndexB = 0;
        return;
    }

    front.WaypointB = pAhead->ID;
    front.CostToB = frontCostA;
    front.CostFromB = frontCostB;
    if ( pBehind == nullptr ) {
        front.NumExtraB = 1;
        front.IndexB = pAhead->NumNeighbors;
    } else {
        front.NumExtraB = 0;
        front.IndexB = ( pBehind == pAhead )
                           ? pAhead->getRoadIndex( front.It.pRoad, 1 )
                           : pAhead->getRoadIndex( front.It.pRoad );
    }
}

int32_t AiPathGraph::getNumStartNodes() const {
    // FUN_00967d70
    if ( Start.It.pRoad == nullptr || End.It.pRoad == nullptr ) {
        return 0;
    }
    if ( bIgnoreRoadLocks ) {
        return 1;
    }
    return ( IsRoadAvailable( Start.It.pRoad->ID ) &&
             IsRoadAvailable( End.It.pRoad->ID ) )
               ? 1
               : 0;
}

void AiPathGraph::initStartNode( AiPathNode *pNode ) {
    // FUN_00968aa0
    buildEndpoints();
    pNode->Waypoint = AiPathNode::kStartWaypoint;
    pNode->IndexNeighbor = -1;
    pNode->Side = Start.Side;
    pNode->G = 0;
    pNode->H = static_cast<int32_t>( ( End.Position - Start.Position ).norm() );
}

int32_t AiPathGraph::getNumNeighbors( const AiPathNode *pNode ) const {
    // FUN_009670f0
    const int32_t waypoint = pNode->Waypoint;
    if ( waypoint == 0 ) {
        return 0;
    }
    if ( waypoint == AiPathNode::kStartWaypoint ) {
        return ( Start.WaypointB != 0 ) ? 2 : ( Start.WaypointA != 0 ? 1 : 0 );
    }
    if ( waypoint == AiPathNode::kGoalWaypoint ) {
        return ( End.WaypointB != 0 ) ? 2 : ( End.WaypointA != 0 ? 1 : 0 );
    }

    const AiRoadMap::Junction *pJunction = GetRoadMap().getJunction( waypoint );
    if ( pJunction == nullptr ) {
        return 0;
    }
    int32_t numNeighbors = pJunction->NumNeighbors;
    numNeighbors += ( waypoint == Start.WaypointA ) ? Start.NumExtraA : 0;
    numNeighbors += ( waypoint == Start.WaypointB ) ? Start.NumExtraB : 0;
    numNeighbors += ( waypoint == End.WaypointA ) ? End.NumExtraA : 0;
    numNeighbors += ( waypoint == End.WaypointB ) ? End.NumExtraB : 0;
    return numNeighbors;
}

int32_t AiPathGraph::getNeighbor( const AiPathNode *pNode,
                                  const int32_t index ) const {
    // FUN_00967050
    const int32_t waypoint = pNode->Waypoint;
    if ( waypoint == 0 ) {
        return 0;
    }
    if ( waypoint == AiPathNode::kStartWaypoint ) {
        return ( index == 1 ) ? Start.WaypointB : Start.WaypointA;
    }
    if ( waypoint == AiPathNode::kGoalWaypoint ) {
        return ( index == 1 ) ? End.WaypointB : End.WaypointA;
    }
    if ( ( waypoint == Start.WaypointA && index == Start.IndexA ) ||
         ( waypoint == Start.WaypointB && index == Start.IndexB ) ) {
        return AiPathNode::kStartWaypoint;
    }
    if ( ( waypoint == End.WaypointA && index == End.IndexA ) ||
         ( waypoint == End.WaypointB && index == End.IndexB ) ) {
        return AiPathNode::kGoalWaypoint;
    }

    const AiRoadMap::Junction *pJunction = GetRoadMap().getJunction( waypoint );
    if ( pJunction == nullptr ) {
        return 0;
    }
    const AiRoadMap::Junction *pNeighbor = pJunction->getNeighbor( index );
    return ( pNeighbor != nullptr ) ? static_cast<int32_t>( pNeighbor->ID ) : 0;
}

int32_t AiPathGraph::computeHeuristic( const int32_t waypoint ) const {
    // FUN_00967ac0
    Eigen::Vector4f position;
    if ( waypoint == 0 ) {
        return INT32_MAX;
    }
    if ( waypoint == AiPathNode::kGoalWaypoint ) {
        return 0;
    }
    if ( waypoint == AiPathNode::kStartWaypoint ) {
        position = Start.Position;
    } else {
        const AiRoadMap::Junction *pJunction =
            GetRoadMap().getJunction( waypoint );
        if ( pJunction == nullptr ) {
            return INT32_MAX;
        }
        position = pJunction->Position.toVector4();
    }
    return static_cast<int32_t>( ( End.Position - position ).norm() );
}

bool AiPathGraph::computePseudoEdgeCost( const int32_t fromWaypoint,
                                         const int32_t toWaypoint,
                                         int32_t *pOutIndex,
                                         int32_t *pOutCost ) const {
    // FUN_00966cc0
    auto linkTo = [&]( const AiPathEndpoint &endpoint ) {
        if ( toWaypoint == endpoint.WaypointA ) {
            *pOutIndex = endpoint.IndexA;
            *pOutCost = endpoint.CostToA;
        } else if ( toWaypoint == endpoint.WaypointB ) {
            *pOutIndex = endpoint.IndexB;
            *pOutCost = endpoint.CostToB;
        }
        return true;
    };
    auto linkFrom = [&]( const AiPathEndpoint &endpoint ) {
        if ( fromWaypoint == endpoint.WaypointA ) {
            *pOutIndex = endpoint.IndexA;
            *pOutCost = endpoint.CostFromA;
        } else if ( fromWaypoint == endpoint.WaypointB ) {
            *pOutIndex = endpoint.IndexB;
            *pOutCost = endpoint.CostFromB;
        }
        return true;
    };

    if ( fromWaypoint == AiPathNode::kStartWaypoint ) {
        return linkTo( Start );
    }
    if ( fromWaypoint == AiPathNode::kGoalWaypoint ) {
        return linkTo( End );
    }
    if ( toWaypoint == AiPathNode::kStartWaypoint ) {
        return linkFrom( Start );
    }
    if ( toWaypoint == AiPathNode::kGoalWaypoint ) {
        return linkFrom( End );
    }
    return false;
}

bool AiPathGraph::computeRoadCost( const AiRoadMap::Road *pRoad,
                                   const AiRoadMap::Junction *pFrom,
                                   const AiRoadMap::Junction *pTo,
                                   const Eigen::Vector4f &toPosition,
                                   int32_t *pOutCost ) const {
    // FUN_00967bb0
    if ( !bIgnoreRoadLocks && !IsRoadAvailable( pRoad->ID ) ) {
        *pOutCost = -1;
        return true;
    }

    Eigen::Vector4f roadEnd;
    uint8_t numLanes = 0;
    if ( pRoad->pJunctionBegin == pTo && pRoad->pJunctionEnd == pFrom ) {
        roadEnd = GetFirstEntity( pRoad )->FirstCV1.toVector4();
        numLanes = pRoad->MinLeftLane;
    } else if ( pRoad->pJunctionEnd == pTo && pRoad->pJunctionBegin == pFrom ) {
        const AiRoadMap::Entity *pLastEntity = GetLastEntity( pRoad );
        roadEnd = pLastEntity->getWaypoint( pLastEntity->getNumWaypoints() - 1 )
                      .toVector4();
        numLanes = pRoad->MinRightLane;
    } else {
        return false;
    }

    int32_t cost = pRoad->Distance +
                   static_cast<int32_t>( ( roadEnd - toPosition ).norm() );
    cost = ( cost * ( ( pRoad->Curvature >> 1 ) + 256 ) ) / 256;
    if ( !bIgnoreOneWay && numLanes == 0 ) {
        cost = -1;
    }
    *pOutCost = cost;
    return true;
}

int32_t AiPathGraph::computeTurnCost( const int32_t waypoint,
                                      const int32_t parentWaypoint,
                                      const int32_t inSlot,
                                      const int32_t outSlot ) const {
    // FUN_009674c0
    if ( !IsJunctionWaypoint( waypoint ) ) {
        return 0;
    }
    const AiRoadMap::Junction *pJunction = GetRoadMap().getJunction( waypoint );
    if ( pJunction == nullptr ) {
        return 0;
    }

    int32_t inIndex = 0;
    if ( IsJunctionWaypoint( parentWaypoint ) ) {
        const AiRoadMap::Junction *pParent =
            GetRoadMap().getJunction( parentWaypoint );
        if ( pParent == nullptr ) {
            return 0;
        }
        inIndex = pJunction->getRoadIndex( pParent->getRoad( inSlot ) );
    } else {
        const AiPathEndpoint &endpoint =
            ( parentWaypoint == AiPathNode::kStartWaypoint ) ? Start : End;
        if ( waypoint == endpoint.WaypointA ) {
            inIndex = endpoint.IndexA;
        } else if ( waypoint == endpoint.WaypointB ) {
            inIndex = endpoint.IndexB;
        } else {
            return 0;
        }
    }

    const float inAngle = pJunction->computeRoadAngle( inIndex );
    const float outAngle = pJunction->computeRoadAngle( outSlot );
    return static_cast<int32_t>(
        std::fabs( std::fabs( inAngle - outAngle ) - 180.0f ) *
        ( 1.0f / 180.0f ) * 100.0f );
}

AiRoadMap::Road *AiPathGraph::getEdgeRoad( const int32_t fromWaypoint,
                                           const int32_t slot ) const {
    // FUN_00966fe0
    if ( fromWaypoint == AiPathNode::kStartWaypoint ) {
        return Start.It.pRoad;
    }
    if ( fromWaypoint == AiPathNode::kGoalWaypoint ) {
        return End.It.pRoad;
    }
    for ( const AiPathEndpoint *pEndpoint : { &Start, &End } ) {
        if ( ( fromWaypoint == pEndpoint->WaypointA &&
               slot == pEndpoint->IndexA ) ||
             ( fromWaypoint == pEndpoint->WaypointB &&
               slot == pEndpoint->IndexB ) ) {
            return pEndpoint->It.pRoad;
        }
    }
    const AiRoadMap::Junction *pJunction =
        GetRoadMap().getJunction( fromWaypoint );
    return ( pJunction != nullptr ) ? pJunction->getRoad( slot ) : nullptr;
}

bool AiPathGraph::computeSideTransition( const int32_t toWaypoint,
                                         const int32_t fromWaypoint,
                                         const int32_t sideIn,
                                         const int32_t slot,
                                         int32_t *pOutSide ) const {
    // FUN_00967e60
    AiRoadMap::Junction *pFrom = IsJunctionWaypoint( fromWaypoint )
                                     ? GetRoadMap().getJunction( fromWaypoint )
                                     : nullptr;
    AiRoadMap::Junction *pTo = IsJunctionWaypoint( toWaypoint )
                                   ? GetRoadMap().getJunction( toWaypoint )
                                   : nullptr;
    const bool bFromSplit = IsSplitJunction( pFrom );
    const bool bToSplit = IsSplitJunction( pTo );
    if ( !bFromSplit && !bToSplit ) {
        *pOutSide = sideIn;
        return true;
    }

    AiRoadMap::Road *pRoad = getEdgeRoad( fromWaypoint, slot );
    if ( pRoad == nullptr || ( IsSplitJunction( pRoad->pJunctionBegin ) &&
                               IsSplitJunction( pRoad->pJunctionEnd ) ) ) {
        *pOutSide = sideIn;
        return true;
    }

    AiRoadIt it;
    AiRoadMap::Junction *pSplitJunction = nullptr;
    if ( bToSplit ) {
        it.setRoadExit( pRoad, pRoad->pJunctionEnd == pTo );
        pSplitJunction = pTo;
    } else {
        it.setRoadEntry( pRoad, pRoad->pJunctionBegin == pFrom );
        pSplitJunction = pFrom;
    }
    const Eigen::Vector4f position = it.computePosition( 0 );
    int32_t side = pSplitJunction->computeSideOfLine(
        Eigen::Vector2f( position.x(), position.z() ) );

    const AiRoadMap::Road *pInnerRoad = nullptr;
    for ( int32_t i = 0; i < pSplitJunction->NumNeighbors; i++ ) {
        const AiRoadMap::Road *pCandidate = pSplitJunction->getRoad( i );
        if ( IsSplitJunction( pCandidate->pJunctionBegin ) &&
             IsSplitJunction( pCandidate->pJunctionEnd ) ) {
            pInnerRoad = pCandidate;
            break;
        }
    }
    if ( pInnerRoad != nullptr && pInnerRoad->pJunctionEnd == pSplitJunction ) {
        side = ( side + 1 ) % 2;
    }

    *pOutSide = ( side != 0 ) ? 2 : 1;
    if ( bToSplit ) {
        return true;
    }
    return sideIn == *pOutSide || sideIn == 0;
}

int32_t AiPathGraph::computeEdgeCost( const AiPathNode *pNode,
                                      const int32_t index,
                                      const int32_t toWaypoint,
                                      int32_t *pOutSide ) const {
    // FUN_00968380
    const int32_t fromWaypoint = pNode->Waypoint;
    int32_t cost = -1;
    int32_t outIndex = index;
    if ( !computeSideTransition( toWaypoint, fromWaypoint, pNode->Side, index,
                                 pOutSide ) ) {
        return -1;
    }

    if ( !computePseudoEdgeCost( fromWaypoint, toWaypoint, &outIndex,
                                 &cost ) ) {
        const AiRoadMap::Junction *pFrom =
            GetRoadMap().getJunction( fromWaypoint );
        const AiRoadMap::Junction *pTo = GetRoadMap().getJunction( toWaypoint );
        if ( pFrom == nullptr || pTo == nullptr ) {
            return -1;
        }
        const AiRoadMap::Road *pRoad = pFrom->getRoad( index );
        if ( pRoad == nullptr ||
             !computeRoadCost( pRoad, pFrom, pTo, pTo->Position.toVector4(),
                               &cost ) ) {
            return -1;
        }
    }
    if ( cost < 0 ) {
        return cost;
    }

    if ( pNode->pParent != nullptr && pNode->pParent->Waypoint != 0 ) {
        cost += computeTurnCost( fromWaypoint, pNode->pParent->Waypoint,
                                 pNode->IndexNeighbor, outIndex );
    }
    return cost;
}

int32_t AiPathGraph::computePropagationCost( const AiPathNode *pParent,
                                             const int32_t childWaypoint,
                                             int32_t *pIoIndex,
                                             int32_t *pOutSide ) const {
    // FUN_009681e0
    int32_t bestCost = -1;
    int32_t bestIndex = -1;
    if ( !computePseudoEdgeCost( pParent->Waypoint, childWaypoint, &bestIndex,
                                 &bestCost ) ) {
        const AiRoadMap::Junction *pFrom =
            GetRoadMap().getJunction( pParent->Waypoint );
        const AiRoadMap::Junction *pTo =
            GetRoadMap().getJunction( childWaypoint );
        if ( pFrom != nullptr && pTo != nullptr ) {
            const Eigen::Vector4f toPosition = pTo->Position.toVector4();
            for ( int32_t i = 0; i < pFrom->NumNeighbors; i++ ) {
                int32_t cost = -1;
                if ( computeRoadCost( pFrom->getRoad( i ), pFrom, pTo,
                                      toPosition, &cost ) &&
                     ( bestCost < 0 || cost < bestCost ) ) {
                    bestCost = cost;
                    bestIndex = i;
                }
            }
        }
    }

    if ( !computeSideTransition( childWaypoint, pParent->Waypoint,
                                 pParent->Side, *pIoIndex, pOutSide ) ) {
        return -1;
    }
    *pIoIndex = bestIndex;
    if ( bestIndex == -1 ) {
        return -1;
    }

    if ( bestCost >= 0 && pParent->pParent != nullptr &&
         pParent->pParent->Waypoint != 0 ) {
        bestCost +=
            computeTurnCost( pParent->Waypoint, pParent->pParent->Waypoint,
                             pParent->IndexNeighbor, bestIndex );
    }
    return bestCost;
}

AiPathSolver::AiPathSolver()
    : graph(),
      pPool( nullptr ),
      pOpen( nullptr ),
      pClosed( nullptr ),
      pStack( nullptr ),
      pResult( nullptr ) {
    // FUN_00407210
}

void AiPathSolver::initialize( AiPathPool *pNodePool ) {
    pResult = nullptr;
    pPool = pNodePool;
    resetLists();
}

void AiPathSolver::reset() {
    if ( pResult != nullptr ) {
        pPool->freeNode( pResult );
        pResult = nullptr;
    }
    resetLists();
}

void AiPathSolver::resetLists() {
    // FUN_00966e80
    while ( pStack != nullptr ) {
        pStack = pStack->pStack;
    }
    for ( AiPathNode **ppList : { &pOpen, &pClosed } ) {
        while ( *ppList != nullptr ) {
            AiPathNode *pNode = *ppList;
            *ppList = pNode->pNext;
            pPool->freeNode( pNode );
        }
    }
}

void AiPathSolver::insertSorted( AiPathNode *pNode ) {
    AiPathNode **ppLink = &pOpen;
    while ( *ppLink != nullptr && ( *ppLink )->F < pNode->F ) {
        ppLink = &( *ppLink )->pNext;
    }
    pNode->pNext = *ppLink;
    *ppLink = pNode;
}

static void AddChild( AiPathNode *pParent, AiPathNode *pChild ) {
    // FUN_00966f70
    for ( int32_t i = 0; i < pParent->NumChildren; i++ ) {
        if ( pParent->pChildren[i] == pChild ) {
            return;
        }
    }
    if ( pParent->NumChildren < AiPathNode::kMaxChildren ) {
        pParent->pChildren[pParent->NumChildren++] = pChild;
    }
}

static AiPathNode *FindNode( AiPathNode *pList,
                             const int32_t waypoint,
                             const int32_t side,
                             AiPathNode **ppOutPrevious ) {
    AiPathNode *pPrevious = nullptr;
    for ( AiPathNode *pNode = pList; pNode != nullptr; pNode = pNode->pNext ) {
        if ( pNode->Waypoint == waypoint && pNode->Side == side ) {
            if ( ppOutPrevious != nullptr ) {
                *ppOutPrevious = pPrevious;
            }
            return pNode;
        }
        pPrevious = pNode;
    }
    return nullptr;
}

static void UpdateNode( AiPathNode *pNode,
                        AiPathNode *pParent,
                        const int32_t g,
                        const int32_t index,
                        const int32_t side ) {
    pNode->Side = side;
    pNode->IndexNeighbor = index;
    pNode->pParent = pParent;
    pNode->G = g;
    pNode->F = pNode->H + g;
}

void AiPathSolver::pushUnique( AiPathNode *pNode ) {
    for ( AiPathNode *pEntry = pStack; pEntry != nullptr;
          pEntry = pEntry->pStack ) {
        if ( pEntry == pNode ) {
            return;
        }
    }
    pNode->pStack = pStack;
    pStack = pNode;
}

bool AiPathSolver::propagate( AiPathNode *pNode ) {
    // FUN_00407270
    int32_t index = -1;
    int32_t side = 0;

    for ( int32_t i = 0; i < pNode->NumChildren; i++ ) {
        AiPathNode *pChild = pNode->pChildren[i];
        const int32_t g = graph.computePropagationCost( pNode, pChild->Waypoint,
                                                        &index, &side ) +
                          pNode->G;
        if ( g < pChild->G ) {
            UpdateNode( pChild, pNode, g, index, side );
            pushUnique( pChild );
        }
    }

    int32_t numIterations = 0;
    while ( pStack != nullptr ) {
        AiPathNode *pEntry = pStack;
        pStack = pEntry->pStack;
        for ( int32_t i = 0; i < pEntry->NumChildren; i++ ) {
            AiPathNode *pChild = pEntry->pChildren[i];
            const int32_t cost = graph.computePropagationCost(
                pEntry, pChild->Waypoint, &index, &side );
            const int32_t g = pEntry->G + cost;
            if ( g < pChild->G && cost >= 0 ) {
                UpdateNode( pChild, pEntry, g, index, side );
                pushUnique( pChild );
            }
        }
        if ( ++numIterations > kMaxSolverIterations ) {
            while ( pStack != nullptr ) {
                pStack = pStack->pStack;
            }
            return false;
        }
    }
    return true;
}

int32_t AiPathSolver::solve() {
    // FUN_004076f0
    for ( int32_t i = 0; i < graph.getNumStartNodes(); i++ ) {
        AiPathNode *pNode = pPool->allocNode();
        if ( pNode == nullptr ) {
            return S_OutOfMemory;
        }
        graph.initStartNode( pNode );
        pNode->pParent = nullptr;
        pNode->F = pNode->G + pNode->H;
        insertSorted( pNode );
    }

    int32_t numIterations = 0;
    while ( pOpen != nullptr ) {
        AiPathNode *pCurrent = pOpen;
        pOpen = pCurrent->pNext;
        pCurrent->pNext = nullptr;
        pResult = pCurrent;
        if ( pCurrent->Waypoint == AiPathNode::kGoalWaypoint ) {
            return S_Success;
        }

        const int32_t numNeighbors = graph.getNumNeighbors( pCurrent );
        for ( int32_t i = 0; i < numNeighbors; i++ ) {
            const int32_t waypoint = graph.getNeighbor( pCurrent, i );
            if ( waypoint == 0 ) {
                continue;
            }
            int32_t side = 0;
            const int32_t cost =
                graph.computeEdgeCost( pCurrent, i, waypoint, &side );
            if ( cost < 0 ) {
                continue;
            }
            const int32_t g = pCurrent->G + cost;

            AiPathNode *pPrevious = nullptr;
            AiPathNode *pNode = FindNode( pOpen, waypoint, side, &pPrevious );
            if ( pNode != nullptr ) {
                if ( pPrevious != nullptr ) {
                    pPrevious->pNext = pNode->pNext;
                } else {
                    pOpen = pNode->pNext;
                }
                pNode->pNext = nullptr;
                AddChild( pCurrent, pNode );
                if ( g < pNode->G ) {
                    UpdateNode( pNode, pCurrent, g, i, side );
                }
                insertSorted( pNode );
                continue;
            }

            pNode = FindNode( pClosed, waypoint, side, nullptr );
            if ( pNode != nullptr ) {
                AddChild( pCurrent, pNode );
                if ( g < pNode->G ) {
                    UpdateNode( pNode, pCurrent, g, i, side );
                    if ( !propagate( pNode ) ) {
                        return S_Failed;
                    }
                }
                continue;
            }

            pNode = pPool->allocNode();
            if ( pNode == nullptr ) {
                return S_OutOfMemory;
            }
            pNode->IndexNeighbor = i;
            pNode->Side = side;
            pNode->Waypoint = waypoint;
            pNode->pParent = pCurrent;
            pNode->G = g;
            pNode->H = graph.computeHeuristic( waypoint );
            pNode->F = g + pNode->H;
            insertSorted( pNode );
            AddChild( pCurrent, pNode );
        }

        pCurrent->pNext = pClosed;
        pClosed = pCurrent;
        if ( ++numIterations > kMaxSolverIterations ) {
            return S_Failed;
        }
    }

    pResult = nullptr;
    return S_Failed;
}

AiPathFloodSolver::AiPathFloodSolver()
    : AiPathSolver(), radius( 0 ), bFloodBehind( false ), bFloodAhead( false ) {
    // FUN_00407400
}

void AiPathFloodSolver::setStart( const AiRoadIt &it ) {
    // FUN_009681a0
    graph.setStart( it, 0 );
}

void AiPathFloodSolver::setRadius( const int32_t newRadius ) {
    // FUN_00966770
    radius = newRadius;
    graph.End.Position = Eigen::Vector4f::Zero();
    graph.End.It = AiRoadIt();
}

void AiPathFloodSolver::initStartNode( AiPathNode *pNode ) {
    graph.Start.buildFloodLinks( bFloodBehind, bFloodAhead );
    AiPathEndpoint &end = graph.End;
    end.WaypointA = 0;
    end.WaypointB = 0;
    end.CostToA = 0;
    end.CostToB = 0;
    end.NumExtraA = 0;
    end.NumExtraB = 0;
    end.IndexA = -1;
    end.IndexB = -1;

    pNode->Waypoint = AiPathNode::kStartWaypoint;
    pNode->IndexNeighbor = -1;
    pNode->G = 0;
    pNode->H = radius;
    pNode->pParent = nullptr;
    pNode->F = radius;
}

bool AiPathFloodSolver::isGoal( const AiPathNode *pNode ) const {
    // FUN_009667c0
    if ( pNode == nullptr || pNode->Waypoint == AiPathNode::kStartWaypoint ) {
        return false;
    }
    const AiRoadMap::Junction *pJunction =
        GetRoadMap().getJunction( static_cast<uint32_t>( pNode->Waypoint ) );
    if ( pJunction == nullptr ) {
        return false;
    }
    const float distance =
        ( pJunction->Position.toVector4() - graph.Start.Position ).norm();
    return static_cast<float>( radius ) <= distance;
}

int32_t AiPathFloodSolver::computeHeuristic( const int32_t waypoint ) const {
    // FUN_00966870
    if ( waypoint == 0 ) {
        return INT32_MAX;
    }
    if ( waypoint == AiPathNode::kStartWaypoint ) {
        return radius;
    }
    const AiRoadMap::Junction *pJunction =
        GetRoadMap().getJunction( static_cast<uint32_t>( waypoint ) );
    if ( pJunction == nullptr ) {
        return INT32_MAX;
    }
    const float distance =
        ( graph.Start.Position - pJunction->Position.toVector4() ).norm();
    return std::max(
        static_cast<int32_t>( static_cast<float>( radius ) - distance ), 0 );
}

int32_t AiPathFloodSolver::solve() {
    // FUN_00407ac0
    AiPathNode *pStart = pPool->allocNode();
    if ( pStart == nullptr ) {
        return S_OutOfMemory;
    }
    initStartNode( pStart );
    insertSorted( pStart );

    int32_t numIterations = 0;
    while ( pOpen != nullptr ) {
        AiPathNode *pCurrent = pOpen;
        pOpen = pCurrent->pNext;
        pCurrent->pNext = nullptr;
        pResult = pCurrent;
        if ( isGoal( pCurrent ) ) {
            return S_Success;
        }

        const int32_t numNeighbors = graph.getNumNeighbors( pCurrent );
        for ( int32_t i = 0; i < numNeighbors; i++ ) {
            const int32_t waypoint = graph.getNeighbor( pCurrent, i );
            if ( waypoint == 0 ) {
                continue;
            }
            int32_t side = 0;
            const int32_t cost =
                graph.computeEdgeCost( pCurrent, i, waypoint, &side );
            if ( cost < 0 ) {
                continue;
            }
            const int32_t g = pCurrent->G + cost;

            AiPathNode *pPrevious = nullptr;
            AiPathNode *pNode = FindNode( pOpen, waypoint, side, &pPrevious );
            if ( pNode != nullptr ) {
                if ( pPrevious != nullptr ) {
                    pPrevious->pNext = pNode->pNext;
                } else {
                    pOpen = pNode->pNext;
                }
                pNode->pNext = nullptr;
                AddChild( pCurrent, pNode );
                if ( g < pNode->G ) {
                    UpdateNode( pNode, pCurrent, g, i, side );
                }
                insertSorted( pNode );
                continue;
            }

            pNode = FindNode( pClosed, waypoint, side, nullptr );
            if ( pNode != nullptr ) {
                AddChild( pCurrent, pNode );
                if ( g < pNode->G ) {
                    UpdateNode( pNode, pCurrent, g, i, side );
                    if ( !propagate( pNode ) ) {
                        return S_Failed;
                    }
                }
                continue;
            }

            pNode = pPool->allocNode();
            if ( pNode == nullptr ) {
                return S_OutOfMemory;
            }
            pNode->Waypoint = waypoint;
            pNode->Side = side;
            pNode->IndexNeighbor = i;
            pNode->pParent = pCurrent;
            pNode->G = g;
            pNode->H = computeHeuristic( waypoint );
            pNode->F = g + pNode->H;
            insertSorted( pNode );
            AddChild( pCurrent, pNode );
        }

        pCurrent->pNext = pClosed;
        pClosed = pCurrent;
        if ( ++numIterations > kMaxSolverIterations ) {
            return S_Failed;
        }
    }

    pResult = nullptr;
    return S_Failed;
}

AiPath::AiPath()
    : pPool( nullptr ),
      Status( 0 ),
      bIgnoreOneWay( true ),
      bAllowReverseStart( true ),
      bIgnoreRoadLocks( false ),
      StartFrameSaved( Matrix4x4f_t::Identity() ),
      StartFrame( Matrix4x4f_t::Identity() ),
      Start(),
      StartSide( 0 ),
      EndFrameSaved( Matrix4x4f_t::Identity() ),
      EndFrame( Matrix4x4f_t::Identity() ),
      End(),
      EndSide( 0 ),
      pHead( nullptr ),
      pTail( nullptr ),
      NumElems( 0 ),
      TotalDistance( 0 ) {
    // FUN_00965560
}

void AiPath::clear() {
    // FUN_00965340
    AiRouteElem *pElem = pHead;
    while ( pElem != nullptr ) {
        AiRouteElem *pNext = pElem->pNext;
        pPool->freeRouteElem( pElem );
        pElem = pNext;
    }
    pHead = nullptr;
    pTail = nullptr;
    NumElems = 0;
    TotalDistance = 0;
    Status = 0;
}

void AiPath::setStart( const AiRoadIt &it, const int32_t side ) {
    // FUN_00965b70
    if ( it.pRoad == nullptr ) {
        Start = AiRoadIt();
        StartFrame = Matrix4x4f_t::Identity();
        StartFrameSaved = StartFrame;
        StartSide = 0;
        return;
    }
    Start = it;
    Start.computeFrame( StartFrame, -1 );
    StartFrameSaved = StartFrame;
    StartSide = side;
}

void AiPath::setEnd( const AiRoadIt &it ) {
    // FUN_00965a70
    if ( it.pRoad == nullptr ) {
        // NOTE: original pcode resets the start fields here.
        Start = AiRoadIt();
        StartFrame = Matrix4x4f_t::Identity();
        StartFrameSaved = StartFrame;
        return;
    }
    End = it;
    End.computeFrame( EndFrame, -1 );
    EndFrameSaved = EndFrame;
}

void AiPath::setStartFromFrame( const Matrix4x4f_t &frame ) {
    // FUN_00966670
    StartFrameSaved = frame;
    AiRoadMap::Road *pRoad =
        gpAI->getRoadMap().findNearestRoadExhaustive( StartFrameSaved, true );
    if ( pRoad->pDetail == nullptr ) {
        Status = 1;
        return;
    }
    ProjectOnRoadShape( StartFrameSaved, Start, &StartFrame, false,
                        pRoad->getShape() );
}

void AiPath::setEndFromFrame( const Matrix4x4f_t &frame ) {
    // FUN_00966530
    EndFrameSaved = frame;
    AiRoadMap::Road *pRoad =
        gpAI->getRoadMap().findNearestRoadExhaustive( EndFrameSaved, true );
    if ( pRoad->pDetail == nullptr ) {
        Status = 1;
        return;
    }
    ProjectOnRoadShape( EndFrameSaved, End, &EndFrame, false,
                        pRoad->getShape() );
}

static void ClampPathT( AiRoadIt &it ) {
    if ( it.T < kMinPathT ) {
        it.T = 0.0f;
    }
    if ( it.T > kMaxPathT ) {
        it.T = 1.0f;
    }
}

int32_t AiPath::solve( const bool bReversePenalty ) {
    // FUN_00965d70
    pPool = &gpAI->getPathPool();
    if ( pHead != nullptr ) {
        clear();
    }
    pHead = nullptr;
    pTail = nullptr;
    NumElems = 0;
    TotalDistance = 0;
    if ( Start.pRoad == nullptr || End.pRoad == nullptr ) {
        return AiPathSolver::S_Failed;
    }
    ClampPathT( Start );
    ClampPathT( End );

    AiPathSolver &solver = gpAI->getPathSolver();
    solver.reset();
    AiPathGraph &graph = solver.getGraph();
    graph.bReversePenalty = bReversePenalty;
    graph.bIgnoreOneWay = bIgnoreOneWay;
    graph.bAllowReverseStart = bAllowReverseStart;
    graph.bIgnoreRoadLocks = bIgnoreRoadLocks;
    graph.setStart( Start, StartSide );
    graph.setEnd( End );

    Status = solver.solve();
    if ( Status != AiPathSolver::S_Success ) {
        return Status;
    }

    AiPathNode *pNode = solver.getResult();
    if ( pNode != nullptr && pNode->Waypoint == AiPathNode::kGoalWaypoint ) {
        pNode = pNode->pParent;
    }
    if ( pNode == nullptr ) {
        Status = AiPathSolver::S_Failed;
        return Status;
    }

    AiRoadIt roadIt;
    if ( pNode->Waypoint == AiPathNode::kStartWaypoint ) {
        // Start and goal on the same road
        if ( Start.bWay != End.bWay ) {
            End.T = 1.0f - End.T;
            End.bWay = Start.bWay;
        }
        if ( !Start.isBefore( End ) && !Start.isSamePosition( End, 0.0f ) ) {
            Start.flip();
            End.flip();
        }
        Start.computeFrame( StartFrame, -1 );
        End.computeFrame( EndFrame, -1 );

        const AiRoadShape *pShape = Start.pRoad->getShape();
        const float startDistance = ComputeRoadDistance( pShape, false, Start );
        const float endDistance = ComputeRoadDistance( pShape, false, End );
        TotalDistance =
            static_cast<int32_t>( std::fabs( startDistance - endDistance ) );

        // NOTE: original pcode does not check the allocation here.
        AiRouteElem *pElem = pPool->allocRouteElem();
        pHead = pElem;
        pElem->pNext = nullptr;
        pElem->pPrevious = nullptr;
        pElem->From.pack( Start );
        pElem->To.pack( End );
        pTail = pHead;
        NumElems++;
        StartSide = pNode->Side;
        EndSide = pNode->Side;
        Status = AiPathSolver::S_Success;
        return Status;
    }

    AiRoadMap &roadMap = GetRoadMap();

    // Orient the goal road from the last junction
    const AiRoadMap::Junction *pLastJunction =
        roadMap.getJunction( pNode->Waypoint );
    AiRoadMap::Road *pEndRoad = End.pRoad;
    if ( pEndRoad->pJunctionBegin == pEndRoad->pJunctionEnd ) {
        const AiRoadShape *pShape = pEndRoad->getShape();
        const float remaining = ComputeRoadDistance( pShape, false, End );
        if ( pShape->TotalLength - remaining > remaining ) {
            End.flip();
        }
    } else if ( pLastJunction == pEndRoad->pJunctionEnd ) {
        End.setWay( false );
    } else if ( pLastJunction == pEndRoad->pJunctionBegin ) {
        End.setWay( true );
    }
    End.computeFrame( EndFrame, -1 );
    TotalDistance += static_cast<int32_t>(
        ComputeRoadDistance( End.pRoad->getShape(), true, End ) );

    pTail = pPool->allocRouteElem();
    if ( pTail == nullptr ) {
        Status = AiPathSolver::S_OutOfMemory;
        return Status;
    }
    pTail->To.pack( End );
    roadIt.setRoadEntry( End.pRoad, End.bWay );
    pTail->From.pack( roadIt );
    pTail->pNext = nullptr;
    NumElems++;

    // Walk back to the start, one road per junction
    const AiRoadMap::Junction *pPreviousJunction = pLastJunction;
    AiRouteElem *pCurrent = pTail;
    const AiPathNode *pChild = pNode;
    AiRouteElem *pElem = pPool->allocRouteElem();
    EndSide = pNode->Side;
    AiPathNode *pParent = pNode->pParent;
    while ( pParent != nullptr && pElem != nullptr &&
            pParent->pParent != nullptr ) {
        if ( pParent->Waypoint == AiPathNode::kStartWaypoint ) {
            pParent = pParent->pParent;
            if ( pParent == nullptr || pParent->pParent == nullptr ) {
                break;
            }
        }
        pCurrent->pPrevious = pElem;
        pElem->pNext = pCurrent;

        const AiRoadMap::Junction *pJunction =
            roadMap.getJunction( pParent->Waypoint );
        AiRoadMap::Road *pRoad = pJunction->getRoad( pChild->IndexNeighbor );
        const bool bForward = pPreviousJunction == pRoad->pJunctionEnd;
        roadIt.setRoadEntry( pRoad, bForward );
        pElem->From.pack( roadIt );
        roadIt.setRoadExit( pRoad, bForward );
        pElem->To.pack( roadIt );
        NumElems++;
        TotalDistance += pRoad->Distance;

        pChild = pParent;
        pPreviousJunction = pJunction;
        pCurrent = pElem;
        pParent = pParent->pParent;
        if ( pParent == nullptr ) {
            break;
        }
        pElem = pPool->allocRouteElem();
    }
    pCurrent->pPrevious = nullptr;
    pHead = pCurrent;
    StartSide = pChild->Side;

    if ( pParent == nullptr ||
         pParent->Waypoint != AiPathNode::kStartWaypoint || pElem == nullptr ) {
        Status = AiPathSolver::S_OutOfMemory;
        if ( pElem != nullptr && pElem != pCurrent ) {
            pPool->freeRouteElem( pElem );
        }
        return Status;
    }

    // Orient the start road toward the first junction
    Status = AiPathSolver::S_Success;
    const AiRoadMap::Junction *pFirstJunction =
        roadMap.getJunction( pChild->Waypoint );
    AiRoadMap::Road *pStartRoad = Start.pRoad;
    if ( pStartRoad->pJunctionBegin == pStartRoad->pJunctionEnd ) {
        const AiRoadShape *pShape = pStartRoad->getShape();
        const float remaining = ComputeRoadDistance( pShape, false, Start );
        if ( remaining > pShape->TotalLength - remaining ) {
            Start.flip();
        }
    } else if ( pFirstJunction == pStartRoad->pJunctionEnd ) {
        Start.setWay( true );
    } else if ( pFirstJunction == pStartRoad->pJunctionBegin ) {
        Start.setWay( false );
    }
    Start.computeFrame( StartFrame, -1 );
    TotalDistance += static_cast<int32_t>(
        ComputeRoadDistance( Start.pRoad->getShape(), false, Start ) );

    pCurrent->pPrevious = pElem;
    pElem->pNext = pCurrent;
    pElem->From.pack( Start );
    roadIt.setRoadExit( Start.pRoad, Start.bWay );
    pElem->To.pack( roadIt );
    pHead = pElem;
    NumElems++;
    return Status;
}

void AiPath::append( AiPath &leg, const bool bMergeJoint ) {
    // FUN_009653e0
    if ( leg.pHead == nullptr || leg.pTail == nullptr || leg.NumElems == 0 ) {
        return;
    }

    if ( pTail == nullptr || pHead == nullptr || NumElems == 0 ) {
        *this = leg;
    } else {
        AiRouteElem *pLegHead = leg.pHead;
        AiRouteElem *pLegNext = pLegHead->pNext;
        const uint32_t jointInfos =
            pTail->To.RoadInfos ^ pLegHead->From.RoadInfos;
        if ( bMergeJoint && ( jointInfos & 0xfffcu ) == 0 &&
             ( jointInfos & 1u ) == 0 ) {
            // Same road and way on both sides of the joint
            pTail->To = pLegHead->To;
            pPool->freeRouteElem( pLegHead );
            NumElems--;
            pLegHead = pLegNext;
        }

        pTail->pNext = pLegHead;
        if ( pLegHead != nullptr ) {
            pLegHead->pPrevious = pTail;
            pTail = leg.pTail;
        }
        NumElems += leg.NumElems;
        TotalDistance += leg.TotalDistance;
        EndFrameSaved = leg.EndFrameSaved;
        EndFrame = leg.EndFrame;
        End = leg.End;
    }

    leg.pHead = nullptr;
    leg.pTail = nullptr;
    leg.NumElems = 0;
    leg.TotalDistance = 0;
    leg.Status = 0;
}

void AiPath::popFront() {
    // FUN_00965c80
    AiRouteElem *pElem = pHead;
    if ( pElem == nullptr ) {
        return;
    }

    pHead = pElem->pNext;
    if ( pHead != nullptr ) {
        pHead->pPrevious = nullptr;
    }
    NumElems--;
    if ( NumElems == 0 ) {
        pTail = nullptr;
        TotalDistance = 0;
        pPool->freeRouteElem( pElem );
        return;
    }

    AiRoadIt from;
    AiRoadIt to;
    pElem->From.unpack( from );
    pElem->To.unpack( to );
    const AiRoadShape *pShape = from.pRoad->getShape();
    const float distanceFrom = ComputeRoadDistance( pShape, false, from );
    const float distanceTo = ComputeRoadDistance( pShape, false, to );
    TotalDistance -= static_cast<int32_t>( distanceFrom - distanceTo );
    pPool->freeRouteElem( pElem );
}

void AiPath::computeBounds() {
    // FUN_00965670
    auto computePosition = []( const AiRoadIt &it ) {
        return it.getEntity()->computeOffsetPosition(
            it.IndWay, it.bWay ? it.T : 1.0f - it.T, 0.0f, 0.0f );
    };

    for ( AiRouteElem *pElem = pHead; pElem != nullptr; pElem = pElem->pNext ) {
        AiRoadIt it;
        AiRoadIt end;
        pElem->From.unpack( it );
        pElem->To.unpack( end );

        Eigen::Vector4f position = computePosition( it );
        pElem->BoundsMinX = position.x();
        pElem->BoundsMinZ = position.z();
        pElem->BoundsMaxX = position.x();
        pElem->BoundsMaxZ = position.z();
        auto expand = [pElem]( const Eigen::Vector4f &point ) {
            pElem->BoundsMinX = std::min( pElem->BoundsMinX, point.x() );
            pElem->BoundsMinZ = std::min( pElem->BoundsMinZ, point.z() );
            pElem->BoundsMaxX = std::max( pElem->BoundsMaxX, point.x() );
            pElem->BoundsMaxZ = std::max( pElem->BoundsMaxZ, point.z() );
        };

        it.stepCurve( false );
        while ( it.isBefore( end ) ) {
            expand( computePosition( it ) );
            if ( !it.stepCurve( false ) ) {
                break;
            }
        }
        expand( computePosition( end ) );
    }
}
