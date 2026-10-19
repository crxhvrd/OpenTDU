#include "shared.h"
#include "ai_veh_controller.h"

#include "ai/ai_bot_manager.h"
#include "ai/ai_config.h"
#include "ai/ai_debug_draw.h"
#include "ai/ai_opt_path.h"
#include "ai/ai_pool.h"
#include "ai/ai_pool_traffic.h"
#include "ai/ai_road_shape.h"
#include "ai/ai_vehicle.h"
#include "ai/ai_veh_nav_opponent.h"
#include "ai/gs_ai.h"
#include "core/maths_helper.h"
#include "core/random/bcpl.h"
#include "core/color.h"
#include "game/vehicle/car_state.h"
#include "render/draw_list.h"
#include "world/gs_world.h"
#include "world/sector_manager.h"
#include "world/streaming_manager.h"

static constexpr int32_t kTrafficRouteCapacity = 2;
static constexpr int32_t kRouteCapacity = 32;
static constexpr int32_t kMaxNumCandidates = 16;
static constexpr float kMinWeightSum = 0.001f;      // DAT_00f4167c
static constexpr float kDebugLineEpsilon = 1.0e-5f; // DAT_00f41710

static float gNavPathSurelevation = 1.5f; // DAT_00f7a114

static bool IsRoadAccessible( const AiRoadMap::Road *pRoad ) {
    // FUN_0088d4b0
    (void)pRoad;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: Locked zone logic
    return true;
}

static AiRoadMap::Junction *GetEndJunction( const AiRoadIt &it ) {
    return it.bWay ? it.pRoad->pJunctionEnd : it.pRoad->pJunctionBegin;
}

bool HasAccessibleExit( const AiRoadIt &it ) {
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

static constexpr float kExcludedRoadScore = -1.0f; // DAT_00f41664
static constexpr float kOtherRoadScore = 10.0f;    // DAT_00f4166c
static constexpr float kBonusScore = 1.0f;         // DAT_00f4165c
static constexpr float kEntryWidthRatio = 0.5f;    // DAT_00f41674
static constexpr float kCrossedWidthRatio = -0.5f; // DAT_00f4186c

static bool IsSplitJunctionLink( const AiRoadMap::Road *pRoad ) {
    // FUN_0092dd60
    return pRoad->isOncomingOvertakeForbidden();
}

static int32_t ComputeSideOfLine( const AiRoadMap::Junction *pJunction,
                                  const AiRoadIt &it,
                                  const float widthRatio ) {
    const Eigen::Vector4f position = it.computeWidthPosition( widthRatio );
    return pJunction->computeSideOfLine(
        Eigen::Vector2f( position.x(), position.z() ) );
}

static void
SetRoadUTurn( const AiRoadIt &last, AiRoadIt &outFrom, AiRoadIt &outTo ) {
    outFrom = last;
    outFrom.bWay = !last.bWay;
    outFrom.T = 1.0f - outFrom.T;
    outTo.setRoadExit( last.pRoad, outFrom.bWay );
}

static void ResetRouteElem( AiRoadIt &outFrom, AiRoadIt &outTo ) {
    outFrom = AiRoadIt();
    outTo = AiRoadIt();
}

AiVehCtrlFreeRoam::AiVehCtrlFreeRoam()
    : AiVehCtrlRoute(),
      pPath(),
      pPathElem( nullptr ),
      pNextPathElem( nullptr ),
      numPathElems( 0 ),
      pathFront(),
      pathSecond(),
      mode( M_Route ) {
    // FUN_009ced90
}

void AiVehCtrlFreeRoam::attach( AiVehicle *pOwner ) {
    // FUN_009cf8b0
    AiVehCtrlRoute::attach( pOwner );
    pPath = std::make_unique<AiPath>();
}

void AiVehCtrlFreeRoam::reset( const bool bFull ) {
    // FUN_009cece0
    clear();
    if ( bFull && pPath != nullptr ) {
        pPath->clear();
    }
    mode = M_Route;
    pPath.reset();
    pPathElem = nullptr;
    pNextPathElem = nullptr;
    numPathElems = 0;
    pathFront = RouteElem();
    pathSecond = RouteElem();
}

void AiVehCtrlFreeRoam::tick( const float deltaTime, const float totalTime ) {
    // FUN_009cea30
    (void)deltaTime;
    (void)totalTime;
    if ( mode == M_Route ) {
        refill();
    }
}

void AiVehCtrlFreeRoam::release() {
    // FUN_009cee20
    AiVehCtrlRoute::release();
    if ( pPath != nullptr ) {
        pPath->clear();
        pPath.reset();
    }
}

bool AiVehCtrlFreeRoam::getFront( AiRoadIt &outFrom, AiRoadIt &outTo ) const {
    // FUN_009ce9b0
    if ( mode != M_Path ) {
        return AiVehCtrlRoute::getFront( outFrom, outTo );
    }
    outFrom = pathFront.From;
    outTo = pathFront.To;
    return outFrom.pRoad != nullptr && outTo.pRoad != nullptr;
}

bool AiVehCtrlFreeRoam::getSecond( AiRoadIt &outFrom, AiRoadIt &outTo ) const {
    // FUN_009ce930
    if ( mode != M_Path ) {
        return AiVehCtrlRoute::getSecond( outFrom, outTo );
    }
    outFrom = pathSecond.From;
    outTo = pathSecond.To;
    return outFrom.pRoad != nullptr && outTo.pRoad != nullptr;
}

int32_t AiVehCtrlFreeRoam::getCount() const {
    // FUN_009ce910
    return ( mode == M_Path ) ? numPathElems : count;
}

void AiVehCtrlFreeRoam::getAt( const int32_t index,
                               AiRoadIt &outFrom,
                               AiRoadIt &outTo ) const {
    // FUN_009cec70
    if ( mode != M_Path ) {
        AiVehCtrlRoute::getAt( index, outFrom, outTo );
        return;
    }
    if ( pPathElem == nullptr ) {
        return;
    }
    const int32_t clampedIndex = std::clamp( index, 0, numPathElems );
    const AiRouteElem *pElem = pPathElem;
    for ( int32_t i = 0; i < clampedIndex && pElem->pNext != nullptr; i++ ) {
        pElem = pElem->pNext;
    }
    pElem->From.unpack( outFrom );
    pElem->To.unpack( outTo );
}

void AiVehCtrlFreeRoam::popFront() {
    // FUN_009ceb70
    if ( mode != M_Path ) {
        AiVehCtrlRoute::popFront();
        return;
    }
    if ( pNextPathElem == nullptr ) {
        return;
    }
    pathFront = pathSecond;
    pPathElem = pNextPathElem;
    pNextPathElem = pNextPathElem->pNext;
    numPathElems--;
    if ( pNextPathElem != nullptr ) {
        pNextPathElem->From.unpack( pathSecond.From );
        pNextPathElem->To.unpack( pathSecond.To );
    } else {
        pathSecond = RouteElem();
    }
}

bool AiVehCtrlFreeRoam::chooseNextRoad( const AiRoadIt &last,
                                        AiRoadIt &outFrom,
                                        AiRoadIt &outTo ) {
    // FUN_009cef40
    const AiRoadMap::Junction *pJunction = nullptr;
    if ( last.pRoad != nullptr ) {
        pJunction = GetEndJunction( last );
        if ( !HasAccessibleExit( last ) ) {
            SetRoadUTurn( last, outFrom, outTo );
        }
    }
    if ( last.pRoad == nullptr ) {
        ResetRouteElem( outFrom, outTo );
        return false;
    }
    if ( pJunction == nullptr ) {
        SetRoadUTurn( last, outFrom, outTo );
        return true;
    }

    const AiVehIndDPC &dpc = pVehicle->getDPC();
    float scores[kMaxNumCandidates] = {};
    for ( int32_t i = 0; i < pJunction->NumNeighbors; i++ ) {
        AiRoadMap::Road *pRoad = pJunction->getRoad( i );
        if ( pJunction->Flags == 2 ) {
            AiRoadIt candidate;
            candidate.setRoadExit( pRoad, pRoad->pJunctionEnd == pJunction );

            bool bSameSide;
            if ( IsSplitJunctionLink( last.pRoad ) ) {
                float lastWidthRatio;
                if ( count > 1 ) {
                    AiRoadIt entry;
                    entry.setRoadEntry( last.pRoad, last.bWay );
                    const AiRoadIt &previousTo = at( count - 2 ).To;
                    const AiRoadMap::Junction *pPreviousJunction =
                        GetEndJunction( previousTo );
                    const bool bSameEntrySide =
                        ComputeSideOfLine( pPreviousJunction, entry,
                                           kEntryWidthRatio ) ==
                        ComputeSideOfLine( pPreviousJunction, previousTo,
                                           0.0f );
                    // Retail reads the bot manager pointer (GSAI+0x2a988) as
                    // the width ratio here: a denormal, i.e. 0
                    lastWidthRatio = bSameEntrySide ? 0.0f : kCrossedWidthRatio;
                } else {
                    if ( pVehicle->getNavigation() == nullptr ) {
                        return false;
                    }
                    const AiVehNavOpponent::Situation &situation =
                        pVehicle->getOpponentNavigation().getSituation();
                    if ( situation.LastValidRoadIt.pRoad == nullptr ) {
                        return false;
                    }
                    lastWidthRatio = situation.LastV;
                }
                bSameSide =
                    ComputeSideOfLine( pJunction, last, lastWidthRatio ) ==
                    ComputeSideOfLine( pJunction, candidate, 0.0f );
            } else {
                bSameSide = IsSplitJunctionLink( pRoad );
            }
            if ( !bSameSide ) {
                scores[i] = kExcludedRoadScore;
                continue;
            }
        }

        if ( pRoad != last.pRoad ) {
            scores[i] += kOtherRoadScore;
        }

        const AiRoadMap::Junction *pFarJunction =
            ( pRoad->pJunctionBegin == pJunction ) ? pRoad->pJunctionEnd
                                                   : pRoad->pJunctionBegin;
        if ( pFarJunction == nullptr || !IsRoadAccessible( pRoad ) ) {
            scores[i] = kExcludedRoadScore;
            continue;
        }

        GridPos2D sector;
        GridPos2D area;
        gpWorld->getStreamingManager()->sectorManager.computeCell(
            pFarJunction->Position.toVector4(), sector, area );
        if ( GetBotSectorIndex( area ) == dpc.BotSectorIndex ) {
            scores[i] += kBonusScore;
        }
        if ( !dpc.DPCPattern.bRespectWay ||
             ( pRoad->pJunctionBegin == pJunction &&
               pRoad->MinRightLane > 0 ) ||
             ( pRoad->pJunctionEnd == pJunction && pRoad->MinLeftLane > 0 ) ) {
            scores[i] += kBonusScore;
        }
    }

    // Slots past the junction roads keep a score of 0
    int32_t bestIndices[kMaxNumCandidates];
    int32_t numBest = 0;
    float bestScore = kExcludedRoadScore;
    for ( int32_t i = 0; i < kMaxNumCandidates; i++ ) {
        if ( scores[i] < 0.0f ) {
            continue;
        }
        if ( scores[i] > bestScore ) {
            bestScore = scores[i];
            bestIndices[0] = i;
            numBest = 1;
        } else if ( scores[i] == bestScore ) {
            bestIndices[numBest++] = i;
        }
    }
    if ( numBest < 1 ) {
        return false;
    }

    const int32_t pick = std::clamp(
        static_cast<int32_t>( RandomBelow( pVehicle->getRandom(), numBest ) ),
        0, numBest - 1 );
    AiRoadMap::Road *pRoad = pJunction->getRoad( bestIndices[pick] );
    if ( pRoad == nullptr ) {
        ResetRouteElem( outFrom, outTo );
        return false;
    }

    const bool bWay = ( pRoad->pJunctionBegin == pJunction );
    outFrom.setRoadEntry( pRoad, bWay );
    outTo.setRoadExit( pRoad, bWay );
    ClipSegmentEnd( outTo, outFrom );
    return true;
}

static constexpr int32_t kMaxRaceRouteElems = 50;

AiVehCtrlRacePath::AiVehCtrlRacePath()
    : AiVehController(),
      AiFsm(),
      pVehicle( nullptr ),
      pPath(),
      numCheckpoints( 0 ),
      lastCheckpointRouted( 0 ),
      maxRouteElems( kMaxRaceRouteElems ) {
    // FUN_009629c0
}

void AiVehCtrlRacePath::attach( AiVehicle *pOwner ) {
    // FUN_00962960
    pVehicle = pOwner;
    pPath = std::make_unique<AiPath>();
    currentState = RPS_Idle;
    bTransitionPending = false;
    nextState = 0;
    stackSize = 0;
    process( FE_Enter );
}

void AiVehCtrlRacePath::reset( const bool bFull ) {
    // FUN_00962920
    if ( bFull && pPath != nullptr ) {
        pPath->clear();
    }
    numCheckpoints = 0;
    lastCheckpointRouted = 0;
}

void AiVehCtrlRacePath::tick( const float deltaTime, const float totalTime ) {
    // FUN_00962880
    process( FE_Update );
}

void AiVehCtrlRacePath::release() {
    // FUN_00962a00
    if ( pPath != nullptr ) {
        pPath->clear();
        pPath.reset();
    }
    reset( true );
}

bool AiVehCtrlRacePath::getFront( AiRoadIt &outFrom, AiRoadIt &outTo ) const {
    // FUN_009627b0
    if ( getCount() < 1 ) {
        outFrom = AiRoadIt();
        outTo = AiRoadIt();
        return false;
    }
    getAt( 0, outFrom, outTo );
    return true;
}

bool AiVehCtrlRacePath::getSecond( AiRoadIt &outFrom, AiRoadIt &outTo ) const {
    // FUN_00962750
    if ( getCount() < 2 ) {
        outFrom = AiRoadIt();
        outTo = AiRoadIt();
        return false;
    }
    getAt( 1, outFrom, outTo );
    return true;
}

int32_t AiVehCtrlRacePath::getCount() const {
    // FUN_00962740
    return ( pPath != nullptr ) ? pPath->getNumElems() : 0;
}

void AiVehCtrlRacePath::getAt( const int32_t index,
                               AiRoadIt &outFrom,
                               AiRoadIt &outTo ) const {
    // FUN_009628b0
    const int32_t clampedIndex = std::clamp( index, 0, getCount() );
    const AiRouteElem *pElem = pPath->getHead();
    if ( pElem == nullptr ) {
        return;
    }
    for ( int32_t i = 0; i < clampedIndex && pElem->pNext != nullptr; i++ ) {
        pElem = pElem->pNext;
    }
    pElem->From.unpack( outFrom );
    pElem->To.unpack( outTo );
}

void AiVehCtrlRacePath::popFront() {
    // FUN_00962a40
    if ( getCount() != 0 ) {
        pPath->popFront();
    }
}

void AiVehCtrlRacePath::forceNextRoad( const uint32_t roadID ) {
    // FUN_00923360
}

bool AiVehCtrlRacePath::chooseNextRoad( const AiRoadIt &last,
                                        AiRoadIt &outFrom,
                                        AiRoadIt &outTo ) {
    return false;
}

void AiVehCtrlRacePath::onRaceMessage( const AiRaceMessage &message ) {
    // FUN_00962810
    if ( message.Type != 0 ) {
        return;
    }
    switch ( message.ID ) {
    case ARM_Prepare:
        goTo( RPS_Reset );
        break;
    case ARM_Countdown:
        goTo( RPS_Countdown );
        break;
    case ARM_Go:
        goTo( RPS_Racing );
        break;
    case ARM_Restart:
        restart( RPS_Idle );
        break;
    default:
        break;
    }
}

bool AiVehCtrlRacePath::handle( const int32_t event, const int32_t state ) {
    // FUN_00962c40
    switch ( state ) {
    case RPS_Reset:
        if ( event != FE_Enter ) {
            return false;
        }
        reset( true );
        return true;
    case RPS_Idle:
        return true;
    case RPS_Countdown:
    case RPS_Racing:
        if ( event != FE_Update ) {
            return false;
        }
        updateRoute();
        return true;
    default:
        return false;
    }
}

void AiVehCtrlRacePath::updateRoute() {
    // FUN_00962a60
    AiRaceDescriptor *pRace = gpAI->getRace();
    if ( pRace == nullptr ) {
        return;
    }
    if ( numCheckpoints == 0 ) {
        numCheckpoints = pRace->getNumCheckpoints();
    }
    if ( numCheckpoints <= 0 || pPath->getNumElems() > maxRouteElems ||
         lastCheckpointRouted == numCheckpoints ) {
        return;
    }

    const int32_t nextCheckpoint = lastCheckpointRouted + 1;
    AiRoadIt goal;
    pRace->getCheckpointRoadLocation( nextCheckpoint, goal );

    AiRoadIt start;
    if ( pPath->getNumElems() > 0 ) {
        pPath->getTail()->To.unpack( start );
    } else {
        if ( goal.pRoad == nullptr ) {
            return;
        }
        Matrix4x4f_t projectedFrame = Matrix4x4f_t::Identity();
        ProjectOnRoadShape( pVehicle->getCarState()->getWorldMatrix(), start,
                            &projectedFrame, false, goal.pRoad->getShape() );
    }
    if ( start.pRoad == nullptr || goal.pRoad == nullptr ) {
        return;
    }

    AiPath leg;
    leg.setStart( start, 0 );
    leg.setEnd( goal );
    leg.solve( true );
    if ( lastCheckpointRouted > 0 ) {
        pRace->setCheckpointRoadLocation( lastCheckpointRouted,
                                          leg.getStart() );
    }
    if ( pRace->getNumCheckpoints() == nextCheckpoint ) {
        pRace->setCheckpointRoadLocation( nextCheckpoint, leg.getEnd() );
    }
    pPath->append( leg, true );
    lastCheckpointRouted = nextCheckpoint;
    leg.clear();
}

static void ComputeLocationRoadIt( const ObjectWorldLocation &location,
                                   AiRoadIt &outIt ) {
    AiRoadMap::Road *pRoad =
        gpAI->getRoadMap().getRoad( location.LocationRoad.RoadID );
    AiRoadShape *pShape = ( pRoad != nullptr ) ? pRoad->getShape() : nullptr;
    if ( pShape == nullptr ) {
        return;
    }
    pShape->computeIt( ( location.LocationRoad.FractionMin +
                         location.LocationRoad.FractionMax ) *
                           0.5f,
                       outIt );
}

static void ProjectOnNearestRoad( CarState *pCar, AiRoadIt &io ) {
    AiRoadMap::Road *pRoad =
        gpAI->getRoadMap().findNearestRoad( pCar->getWorldPosition(), true );
    AiRoadShape *pShape = ( pRoad != nullptr ) ? pRoad->getShape() : nullptr;
    if ( pShape == nullptr ) {
        return;
    }
    ProjectOnRoadShape( pCar->getWorldMatrix(), io, nullptr, false, pShape );
}

static int32_t PickJunctionRoad( const AiRoadMap::Junction *pJunction,
                                 const Eigen::Vector4f &direction ) {
    // FUN_00963650
    static constexpr float kMaxAngleGap = 7.853982f; // DAT_00f41e6c

    if ( pJunction == nullptr ) {
        return -1;
    }

    const int32_t numRoads = pJunction->NumRoads;
    int32_t referenceIndex = 0;
    for ( int32_t i = 0; i < numRoads; i++ ) {
        if ( pJunction->computeRoadAngle( i ) == 0.0f ) {
            referenceIndex = i;
            break;
        }
    }

    AiRoadMap::Road *pReferenceRoad = pJunction->getRoad( referenceIndex );
    AiRoadIt referenceIt;
    if ( pReferenceRoad->pJunctionEnd == pJunction ) {
        referenceIt.setRoadExit( pReferenceRoad, true );
    } else {
        referenceIt.setRoadEntry( pReferenceRoad, true );
    }
    const Eigen::Vector4f delta = referenceIt.computeWidthPosition( 0.0f ) -
                                  pJunction->Position.toVector4();

    float heading =
        -std::atan2( delta.x() * direction.z() - delta.z() * direction.x(),
                     delta.x() * direction.x() + delta.z() * direction.z() );
    if ( heading < 0.0f ) {
        heading += kTwoPI;
    }

    int32_t bestIndex = 0;
    float bestGap = kMaxAngleGap;
    for ( int32_t i = 0; i < numRoads; i++ ) {
        const float gap =
            std::fabs( pJunction->computeRoadAngle( i ) * kDegToRad - heading );
        if ( gap < bestGap ) {
            bestGap = gap;
            bestIndex = i;
        }
    }
    if ( std::fabs( kTwoPI - heading ) < bestGap ) {
        bestIndex = referenceIndex;
    }
    return bestIndex;
}

static void LocateAtJunction( CarState *pCar, AiRoadIt &outIt ) {
    const AiRoadMap::Junction *pJunction = gpAI->getRoadMap().getJunction(
        pCar->getWorldLocation().LocationOther.RoadID );
    if ( pJunction == nullptr ) {
        return;
    }
    const Eigen::Vector4f direction =
        pCar->getWorldMatrix().row( 2 ).transpose();
    AiRoadMap::Road *pRoad =
        pJunction->getRoad( PickJunctionRoad( pJunction, direction ) );
    outIt.setRoadEntry( pRoad, pRoad->pJunctionEnd != pJunction );
}

AiVehCtrlChase::AiVehCtrlChase()
    : AiVehController(),
      AiFsm(),
      pVehicle( nullptr ),
      pPath(),
      pCurrentElem( nullptr ),
      pNextElem( nullptr ),
      count( 0 ),
      frontFrom(),
      frontTo(),
      secondFrom(),
      secondTo(),
      pTarget( nullptr ),
      pGoal( nullptr ),
      targetIt(),
      goalIt(),
      vehicleIt(),
      debugLineStart( Eigen::Vector4f::Zero() ),
      debugLineEnd( Eigen::Vector4f::Zero() ) {
    // FUN_00963a90
}

void AiVehCtrlChase::attach( AiVehicle *pOwner ) {
    // FUN_00963a30
    pVehicle = pOwner;
    pPath = std::make_unique<AiPath>();
    restart( CS_Idle );
}

void AiVehCtrlChase::reset( const bool bFull ) {
    // FUN_00963900
    if ( bFull && pPath != nullptr ) {
        pPath->clear();
    }
    pPath.reset();
    pCurrentElem = nullptr;
    pNextElem = nullptr;
    count = 0;
    frontFrom = AiRoadIt();
    frontTo = AiRoadIt();
    secondFrom = AiRoadIt();
    secondTo = AiRoadIt();
    targetIt = AiRoadIt();
    goalIt = AiRoadIt();
    vehicleIt = AiRoadIt();
    debugLineStart = Eigen::Vector4f::Zero();
    debugLineEnd = Eigen::Vector4f::Zero();
    pTarget = nullptr;
    pGoal = nullptr;
}

void AiVehCtrlChase::tick( const float deltaTime, const float totalTime ) {
    // FUN_00963fb0
    if ( pVehicle->getCarStateIndex() !=
             gpAI->getAIPool()->getNextController1Index() &&
         frontFrom.pRoad != nullptr ) {
        return;
    }

    CarState *pCar = pVehicle->getCarState();
    const ObjectWorldLocation &location = pCar->getWorldLocation();
    if ( location.LocationType == ObjectWorldLocation::OWL_Road ) {
        ComputeLocationRoadIt( location, vehicleIt );
        vehicleIt.setWay( location.bWay != 0 );
    } else if ( location.LocationType == ObjectWorldLocation::OWL_Junction ) {
        LocateAtJunction( pCar, vehicleIt );
    } else {
        if ( vehicleIt.pRoad == nullptr ) {
            ProjectOnNearestRoad( pCar, vehicleIt );
        }
        if ( vehicleIt.pRoad == nullptr ) {
            return;
        }
    }

    if ( pTarget != nullptr ) {
        const ObjectWorldLocation &targetLocation = pTarget->getWorldLocation();
        switch ( targetLocation.LocationType ) {
        case ObjectWorldLocation::OWL_Road:
            ComputeLocationRoadIt( targetLocation, targetIt );
            targetIt.setWay( targetLocation.bWay != 0 );
            break;
        case ObjectWorldLocation::OWL_Junction:
            LocateAtJunction( pTarget, targetIt );
            break;
        case ObjectWorldLocation::OWL_OffRoad: {
            AiRoadIt projectedIt;
            ProjectOnNearestRoad( pTarget, projectedIt );
            if ( projectedIt.pRoad != nullptr ) {
                targetIt = projectedIt;
            }
            break;
        }
        default:
            break;
        }
    } else if ( pGoal != nullptr ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: police search location
                                 // (CarState+0xb8 = goalIt, +0xcc = now)
    }
    process( FE_Update );
}

void AiVehCtrlChase::release() {
    // FUN_00963ba0
    if ( pPath != nullptr ) {
        pPath->clear();
        pPath.reset();
    }
    reset( true );
}

void AiVehCtrlChase::debugDraw( DrawList *pDrawList ) {
    // FUN_00963480
    const Eigen::Vector4f delta = debugLineStart - debugLineEnd;
    if ( ( delta.array().square() <= kDebugLineEpsilon * kDebugLineEpsilon )
             .all() ) {
        return;
    }

    Matrix4x4f_t transform = Matrix4x4f_t::Identity();
    transform.row( 3 ) +=
        Eigen::Vector4f( 0.0f, gNavPathSurelevation, 0.0f, 0.0f ).transpose();
    pDrawList->setActiveTransformMatrix( transform );
    pDrawList->setActiveMaterial( nullptr );
    pDrawList->beginPrimitive( PT_LineStrip, 2 );
    pDrawList->setDiffuse( 0, ColorRGBA( 0xffffffffu ) );
    const Eigen::Vector4f start = debugLineStart - GetShiftWorldPosition();
    pDrawList->pushVertex( start.x(), start.y(), start.z() );
    const Eigen::Vector4f end = debugLineEnd - GetShiftWorldPosition();
    pDrawList->pushVertex( end.x(), end.y(), end.z() );
    pDrawList->commitPrimitive();
}

bool AiVehCtrlChase::getFront( AiRoadIt &outFrom, AiRoadIt &outTo ) const {
    // FUN_00963100
    outFrom = frontFrom;
    outTo = frontTo;
    return frontFrom.pRoad != nullptr && frontTo.pRoad != nullptr;
}

bool AiVehCtrlChase::getSecond( AiRoadIt &outFrom, AiRoadIt &outTo ) const {
    // FUN_00963090
    outFrom = secondFrom;
    outTo = secondTo;
    return secondFrom.pRoad != nullptr && secondTo.pRoad != nullptr;
}

int32_t AiVehCtrlChase::getCount() const {
    // FUN_00962e50
    return count;
}

void AiVehCtrlChase::getAt( const int32_t index,
                            AiRoadIt &outFrom,
                            AiRoadIt &outTo ) const {
    // FUN_009633c0
    if ( count == 1 ) {
        if ( index == 0 ) {
            outFrom = frontFrom;
            outTo = frontTo;
        }
        return;
    }

    const int32_t clampedIndex = std::clamp( index, 0, std::max( count, 0 ) );
    const AiRouteElem *pElem = pCurrentElem;
    if ( pElem == nullptr ) {
        return;
    }
    for ( int32_t i = 0; i < clampedIndex && pElem->pNext != nullptr; i++ ) {
        pElem = pElem->pNext;
    }
    pElem->From.unpack( outFrom );
    pElem->To.unpack( outTo );
}

void AiVehCtrlChase::popFront() {
    // FUN_00963300
    if ( pNextElem == nullptr ) {
        return;
    }
    frontFrom = secondFrom;
    frontTo = secondTo;
    pCurrentElem = pNextElem;
    pNextElem = pNextElem->pNext;
    count--;
    if ( pNextElem != nullptr ) {
        pNextElem->From.unpack( secondFrom );
        pNextElem->To.unpack( secondTo );
    } else {
        secondFrom = AiRoadIt();
        secondTo = AiRoadIt();
    }
}

void AiVehCtrlChase::forceNextRoad( const uint32_t roadID ) {
    // FUN_00923360
}

bool AiVehCtrlChase::chooseNextRoad( const AiRoadIt &last,
                                     AiRoadIt &outFrom,
                                     AiRoadIt &outTo ) {
    // FUN_00962e60
    return false;
}

void AiVehCtrlChase::setTarget( CarState *pNewTarget ) {
    // FUN_00963c70
    pGoal = nullptr;
    goalIt = AiRoadIt();
    ComputeLocationRoadIt( pNewTarget->getWorldLocation(), targetIt );
    targetIt.bWay = pNewTarget->getWorldLocation().bWay != 0;
    pTarget = pNewTarget;
    goTo( CS_Chase );
}

void AiVehCtrlChase::setGoal( CarState *pNewGoal ) {
    // FUN_00963be0
    ComputeLocationRoadIt( pNewGoal->getWorldLocation(), goalIt );
    goalIt.bWay = pNewGoal->getWorldLocation().bWay != 0;
    pGoal = pNewGoal;
    goTo( CS_Goal );
}

bool AiVehCtrlChase::isOnTargetRoad( const AiRoadIt &it ) const {
    // FUN_00963070
    return targetIt.pRoad == nullptr ||
           ( it.pRoad != nullptr && it.pRoad == targetIt.pRoad );
}

bool AiVehCtrlChase::handle( const int32_t event, const int32_t state ) {
    // FUN_009647d0
    switch ( state ) {
    case CS_Chase:
        if ( event != FE_Update && event != FE_Enter ) {
            return false;
        }
        if ( pTarget != nullptr && targetIt.pRoad != nullptr ) {
            updateChaseRoute();
            refreshRoute();
        }
        return true;
    case CS_Goal:
        if ( event == FE_Update || event == FE_Enter ) {
            if ( pGoal != nullptr && goalIt.pRoad != nullptr ) {
                updateGoalRoute();
                refreshRoute();
            }
            return true;
        }
        return event == FE_Exit;
    case CS_Idle:
        return event <= FE_Exit;
    default:
        return false;
    }
}

void AiVehCtrlChase::refreshRoute() {
    // FUN_009631f0
    if ( count == 1 && goalIt.pRoad == nullptr ) {
        secondFrom = AiRoadIt();
        secondTo = AiRoadIt();
        return;
    }

    pCurrentElem = nullptr;
    pNextElem = ( pPath->getStatus() == AiPathSolver::S_Success )
                    ? pPath->getHead()
                    : nullptr;
    if ( pNextElem == nullptr ) {
        frontFrom = AiRoadIt();
        frontTo = AiRoadIt();
        secondFrom = AiRoadIt();
        secondTo = AiRoadIt();
        return;
    }

    pNextElem->From.unpack( frontFrom );
    pNextElem->To.unpack( frontTo );
    pCurrentElem = pNextElem;
    pNextElem = pNextElem->pNext;
    if ( pNextElem != nullptr ) {
        pNextElem->From.unpack( secondFrom );
        pNextElem->To.unpack( secondTo );
    } else {
        secondFrom = AiRoadIt();
        secondTo = AiRoadIt();
    }
}

void AiVehCtrlChase::setSingleSegment( const AiRoadIt &from,
                                       const AiRoadIt &to ) {
    frontFrom = from;
    frontTo = to;
    count = 1;
    frontFrom.setWay( frontTo.bWay );
    ClipSegmentEnd( frontTo, frontFrom );
}

void AiVehCtrlChase::updateChaseRoute() {
    // FUN_00964480
    if ( vehicleIt.pRoad == nullptr ) {
        return;
    }

    const AiRoadIt from = vehicleIt;
    const eAiVehicleCategory category = pVehicle->getCategory();
    AiRoadIt goal;
    if ( category == eAiVehicleCategory::AVC_Cop &&
         gAIConfig.bPoliceCanCollide && vehicleIt.pRoad == targetIt.pRoad ) {
        const bool bWay = IsBeforeInWay( vehicleIt, targetIt, vehicleIt.bWay )
                              ? vehicleIt.bWay
                              : targetIt.bWay;
        goal.setRoadExit( vehicleIt.pRoad, bWay );
        setSingleSegment( from, goal );
        return;
    }

    if ( category == eAiVehicleCategory::AVC_Bot ) {
        if ( vehicleIt.pRoad == targetIt.pRoad ) {
            goal.setRoadExit( targetIt.pRoad, targetIt.bWay );
            setSingleSegment( from, goal );
            return;
        }
        goal.setRoadEntry( targetIt.pRoad, targetIt.bWay );
    } else {
        goal.setRoadExit( targetIt.pRoad, targetIt.bWay );
    }

    AiRoadIt goalEntry;
    goalEntry.setRoadEntry( goal.pRoad, goal.bWay );
    ClipSegmentEnd( goal, goalEntry );

    pPath->setStart( from, 0 );
    pPath->setEnd( goal );
    if ( pVehicle->getDPC().DPCPattern.bRespectWay ) {
        pPath->setIgnoreOneWay( false );
    }
    pPath->solve( true );
    count = pPath->getNumElems();

    AiRouteElem *pTail = pPath->getTail();
    if ( pTail == nullptr ) {
        return;
    }
    AiRoadIt tailFrom;
    AiRoadIt tailTo;
    pTail->From.unpack( tailFrom );
    pTail->To.unpack( tailTo );
    if ( tailFrom.pRoad == nullptr || tailTo.pRoad == nullptr ) {
        return;
    }
    if ( category != eAiVehicleCategory::AVC_Bot &&
         !tailFrom.isSamePosition( tailTo, 0.0f ) ) {
        return;
    }
    tailTo.setRoadExit( tailTo.pRoad, tailTo.bWay );
    ClipSegmentEnd( tailTo, tailFrom );
    pTail->To.pack( tailTo );
}

void AiVehCtrlChase::updateGoalRoute() {
    // FUN_00963ea0
    if ( vehicleIt.pRoad == nullptr ) {
        return;
    }
    if ( goalIt.pRoad == nullptr ) {
        frontFrom = AiRoadIt();
        frontTo = AiRoadIt();
        count = 0;
        return;
    }
    pPath->setStart( vehicleIt, 0 );
    pPath->setEnd( goalIt );
    pPath->solve( true );
    count = pPath->getNumElems();
}
