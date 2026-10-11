#include "shared.h"
#include "ai_veh_controller.h"

#include "ai/ai_config.h"
#include "ai/ai_pool.h"
#include "ai/ai_pool_traffic.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "game/vehicle/car_state.h"

static constexpr int32_t kTrafficRouteCapacity = 2;
static constexpr int32_t kRouteCapacity = 32;
static constexpr int32_t kMaxNumCandidates = 16;
static constexpr float kMinWeightSum = 0.001f; // DAT_00f4167c

static bool IsRoadAccessible( const AiRoadMap::Road *pRoad ) {
    // FUN_0088d4b0
    (void)pRoad;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: Locked zone logic
    return true;
}

static AiRoadMap::Junction *GetEndJunction( const AiRoadIt &it ) {
    return it.bWay ? it.pRoad->pJunctionEnd : it.pRoad->pJunctionBegin;
}

static bool HasAccessibleExit( const AiRoadIt &it ) {
    // FUN_00924790
    if ( it.pRoad == nullptr ) {
        return false;
    }
    const AiRoadMap::Junction *pJunction = GetEndJunction( it );
    if ( pJunction == nullptr ) {
        return false;
    }
    for ( int32_t i = 0; i < pJunction->NumRoads; i++ ) {
        const AiRoadMap::Road *pRoad = pJunction->getRoad( i );
        if ( pRoad == nullptr || pRoad->ID == it.pRoad->ID ) {
            continue;
        }
        if ( IsRoadAccessible( pRoad ) ) {
            return true;
        }
    }
    return false;
}

static void ClipSegmentEnd( AiRoadIt &to, const AiRoadIt &from ) {
    // FUN_009277f0
    if ( to.pRoad == nullptr || from.pRoad == nullptr ) {
        return;
    }
    if ( HasAccessibleExit( to ) ) {
        return;
    }
    to.advanceByDistance( -gAIConfig.PoliceCaptureDistance, true );
    if ( to.isBefore( from ) ) {
        to = from;
    }
}

static int32_t GetNumLanesTowardEnd( const AiRoadIt &it ) {
    const AiRoadMap::RoadDetail *pDetail = it.pRoad->pDetail;
    return it.bWay ? pDetail->pEntities[pDetail->NumEntities - 1]->NumRightLanes
                   : pDetail->pEntities[0]->NumLeftLanes;
}

static bool CanTurnFromRoad( const AiRoadIt &it, const uint8_t turnMask ) {
    const AiRoadMap::RoadDetail *pDetail = it.pRoad->pDetail;
    const int32_t numRightLanes =
        pDetail->pEntities[pDetail->NumEntities - 1]->NumRightLanes;
    const int32_t numLeftLanes = pDetail->pEntities[0]->NumLeftLanes;
    const int32_t numLanes = GetNumLanesTowardEnd( it );
    for ( int32_t lane = 0; lane < numLanes; lane++ ) {
        int32_t index = it.bWay ? numLeftLanes - lane + numRightLanes - 1
                                : numRightLanes - lane - 1;
        if ( index < 0 || index >= numLeftLanes + numRightLanes ) {
            index = 0;
        }
        uint8_t laneFlags = static_cast<uint8_t>( it.pRoad->LaneFlags[index] );
        if ( laneFlags == 0 ) {
            laneFlags = 7;
        }
        if ( ( turnMask & laneFlags ) != 0 ) {
            return true;
        }
    }
    return false;
}

AiVehCtrlRoute::AiVehCtrlRoute()
    : pVehicle( nullptr ), route(), count( 0 ), front( -1 ), back( 0 ) {
    // FUN_00961eb0
}

void AiVehCtrlRoute::clear() {
    count = 0;
    front = static_cast<int32_t>( route.size() ) - 1;
    back = 0;
}

const AiVehCtrlRoute::RouteElem &
AiVehCtrlRoute::at( const int32_t index ) const {
    const int32_t capacity = static_cast<int32_t>( route.size() );
    return route[( front + 1 + index ) % capacity];
}

void AiVehCtrlRoute::pushBack( const RouteElem &elem ) {
    // FUN_00408100
    const int32_t capacity = static_cast<int32_t>( route.size() );
    if ( count == capacity ) {
        return;
    }
    route[back] = elem;
    back = ( back + 1 ) % capacity;
    count++;
}

void AiVehCtrlRoute::attach( AiVehicle *pOwner ) {
    // FUN_00962610
    pVehicle = pOwner;
    reset( false );

    const int32_t capacity =
        ( pVehicle->getCategory() == eAiVehicleCategory::AVC_Traffic )
            ? kTrafficRouteCapacity
            : kRouteCapacity;
    route.assign( static_cast<size_t>( capacity ), RouteElem{} );
    clear();

    const AiRoadIt &lastRoadPos = pVehicle->getInfos().LastRoadPos;
    if ( lastRoadPos.pRoad == nullptr ) {
        return;
    }

    RouteElem elem;
    elem.From = lastRoadPos;
    elem.To.setRoadExit( elem.From.pRoad, elem.From.bWay );
    ClipSegmentEnd( elem.To, elem.From );
    pushBack( elem );
    refill();
}

void AiVehCtrlRoute::reset( const bool bFull ) {
    // FUN_00961e60
    (void)bFull;
    clear();
}

void AiVehCtrlRoute::tick( const float deltaTime, const float totalTime ) {
    // FUN_00407f80
    (void)deltaTime;
    (void)totalTime;
}

void AiVehCtrlRoute::release() {
    // FUN_00961e80
    route.clear();
    count = 0;
}

bool AiVehCtrlRoute::getFront( AiRoadIt &outFrom, AiRoadIt &outTo ) const {
    // FUN_00961d90
    if ( count < 1 ) {
        outFrom = AiRoadIt();
        outTo = AiRoadIt();
        return false;
    }
    const RouteElem &elem = at( 0 );
    outFrom = elem.From;
    outTo = elem.To;
    return outFrom.pRoad != nullptr && outTo.pRoad != nullptr;
}

bool AiVehCtrlRoute::getSecond( AiRoadIt &outFrom, AiRoadIt &outTo ) const {
    // FUN_00961cc0
    if ( count < 2 ) {
        outFrom = AiRoadIt();
        outTo = AiRoadIt();
        return false;
    }
    const RouteElem &elem = at( 1 );
    outFrom = elem.From;
    outTo = elem.To;
    return outFrom.pRoad != nullptr && outTo.pRoad != nullptr;
}

int32_t AiVehCtrlRoute::getCount() const {
    // FUN_00961af0
    return count;
}

void AiVehCtrlRoute::getAt( const int32_t index,
                            AiRoadIt &outFrom,
                            AiRoadIt &outTo ) const {
    // FUN_00961c10
    if ( index >= count ) {
        outFrom = AiRoadIt();
        outTo = AiRoadIt();
        return;
    }
    const RouteElem &elem = at( index );
    outFrom = elem.From;
    outTo = elem.To;
}

void AiVehCtrlRoute::popFront() {
    // FUN_00961be0
    if ( count == 0 ) {
        return;
    }
    count--;
    front = ( front + 1 ) % static_cast<int32_t>( route.size() );
    refill();
}

void AiVehCtrlRoute::refill() {
    // FUN_00961b00
    const int32_t capacity = static_cast<int32_t>( route.size() );
    while ( count != capacity ) {
        const AiRoadIt last = ( count > 0 )
                                  ? route[( back - 1 + capacity ) % capacity].To
                                  : AiRoadIt();
        RouteElem elem;
        if ( !chooseNextRoad( last, elem.From, elem.To ) ) {
            return;
        }
        pushBack( elem );
    }
}

void AiVehCtrlRoute::forceNextRoad( const uint32_t roadID ) {
    // FUN_00961f10
    if ( count == 0 ) {
        return;
    }
    const RouteElem current = at( 0 );
    AiRoadMap::Junction *pJunction = GetEndJunction( current.To );
    for ( int32_t i = 0; i < pJunction->NumRoads; i++ ) {
        AiRoadMap::Road *pRoad = pJunction->getRoad( i );
        if ( pRoad->ID != roadID ) {
            continue;
        }
        const bool bWay = ( pRoad->pJunctionBegin == pJunction );
        RouteElem next;
        next.From.setRoadEntry( pRoad, bWay );
        next.To.setRoadExit( pRoad, bWay );
        clear();
        pushBack( current );
        pushBack( next );
        refill();
        return;
    }
}

bool AiVehCtrlRoute::chooseNextRoad( const AiRoadIt &last,
                                     AiRoadIt &outFrom,
                                     AiRoadIt &outTo ) {
    // FUN_00962090
    struct Candidate {
        float TrafficPerLaneKm;
        float NumLanes;
        int32_t RoadIndex;
        float Frequency;
    };

    AiRoadMap::Junction *pJunction =
        ( last.pRoad != nullptr ) ? GetEndJunction( last ) : nullptr;
    if ( pJunction == nullptr ) {
        outFrom = AiRoadIt();
        outTo = AiRoadIt();
        return false;
    }

    const int32_t ownIndex = pJunction->getRoadIndex( last.pRoad );
    const int32_t databaseId = pVehicle->getCarState()->getCarID();

    Candidate candidates[kMaxNumCandidates];
    int32_t numCandidates = 0;
    float sumTrafficPerLaneKm = 0.0f;
    float sumFrequency = 0.0f;
    for ( int32_t i = 0; i < pJunction->NumNeighbors; i++ ) {
        AiRoadMap::Road *pRoad = pJunction->getRoad( i );
        const bool bDrivable =
            ( pRoad->pJunctionBegin == pJunction && pRoad->MinRightLane > 0 ) ||
            ( pRoad->pJunctionEnd == pJunction && pRoad->MinLeftLane > 0 );
        if ( !bDrivable || pRoad->ID == last.pRoad->ID ) {
            continue;
        }

        const uint8_t turnMask = static_cast<uint8_t>(
            pJunction->computeTurnType( ownIndex, i, nullptr ) );
        if ( !CanTurnFromRoad( last, turnMask ) ) {
            continue;
        }

        const bool bLeaveFromBegin = ( pRoad->pJunctionBegin == pJunction );
        const AiRoadMap::RoadDetail *pDetail = pRoad->pDetail;
        const uint8_t numLanes =
            bLeaveFromBegin
                ? pDetail->pEntities[pDetail->NumEntities - 1]->NumRightLanes
                : pDetail->pEntities[0]->NumLeftLanes;

        AiRoadIt entry;
        entry.setRoadEntry( pRoad, bLeaveFromBegin );
        const AIZone &zone =
            gAIConfig.Zones[entry.getEntity()->DensityZoneIndice];

        float frequency = 0.0f;
        for ( const AIZoneVehicle &vehicle : zone.Vehicles ) {
            if ( vehicle.DatabaseId == databaseId ) {
                frequency = vehicle.Frequency;
                break;
            }
        }

        Candidate &candidate = candidates[numCandidates++];
        candidate.TrafficPerLaneKm = zone.TrafficPerLaneKm;
        candidate.NumLanes = static_cast<float>( numLanes );
        candidate.RoadIndex = i;
        candidate.Frequency = frequency;
        sumTrafficPerLaneKm += zone.TrafficPerLaneKm;
        sumFrequency += frequency;
    }

    if ( numCandidates <= 0 ) {
        const int32_t index =
            pJunction->pickStraightestRoad( ownIndex, nullptr );
        if ( index < 0 ) {
            outFrom = AiRoadIt();
            outTo = AiRoadIt();
            return false;
        }
        candidates[0] = Candidate{ 1.0f, 1.0f, index, 1.0f };
        numCandidates = 1;
        sumTrafficPerLaneKm = 1.0f;
        sumFrequency = 1.0f;
    }

    float weights[kMaxNumCandidates];
    if ( pVehicle->getBehaviourType() == 0 || sumFrequency < kMinWeightSum ) {
        for ( int32_t i = 0; i < numCandidates; i++ ) {
            weights[i] = candidates[i].NumLanes;
        }
    } else {
        const float invSumFrequency = 1.0f / sumFrequency;
        for ( int32_t i = 0; i < numCandidates; i++ ) {
            weights[i] = candidates[i].Frequency * invSumFrequency *
                         candidates[i].NumLanes;
        }
    }
    if ( sumTrafficPerLaneKm >= kMinWeightSum &&
         !gpAI->getAIPool()->isZoneDensityIgnored() ) {
        for ( int32_t i = 0; i < numCandidates; i++ ) {
            weights[i] *= candidates[i].TrafficPerLaneKm;
        }
    }

    const int32_t pick =
        RandomWeightedIndex( pVehicle->getRandom(), weights, numCandidates );
    AiRoadMap::Road *pRoad = pJunction->getRoad( candidates[pick].RoadIndex );
    if ( pRoad == nullptr ) {
        outFrom = AiRoadIt();
        outTo = AiRoadIt();
        return false;
    }

    const AiRoadMap::RoadDetail *pDetail = pRoad->pDetail;
    const int32_t lastEntity = pDetail->NumEntities - 1;
    const int32_t lastCurve =
        ( pDetail->pEntities[lastEntity]->NumVertex + 2 ) / 3 - 2;
    if ( pRoad->pJunctionBegin == pJunction ) {
        outFrom = AiRoadIt{ pRoad, 0, 0, true, 0.0f };
        outTo = AiRoadIt{ pRoad, lastEntity, lastCurve, true, 1.0f };
    } else {
        outFrom = AiRoadIt{ pRoad, lastEntity, lastCurve, false, 0.0f };
        outTo = AiRoadIt{ pRoad, 0, 0, false, 1.0f };
    }
    return true;
}
