#include "shared.h"
#include "ai_veh_nav_traffic.h"

#include "ai/ai_config.h"
#include "ai/ai_road_shape.h"
#include "ai/ai_veh_controller.h"
#include "ai/ai_veh_locomotion.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "core/random/bcpl.h"
#include "game/vehicle/car_state.h"
#include "gs_timer.h"

static constexpr float kDefaultFollowSpeed = 150.0f;  // DAT_00f4182c
static constexpr float kNoLeaderFollowSpeed = 100.0f; // DAT_00f41708
static constexpr float kDefaultSecDistCoef = 0.5f;    // DAT_00f41674
static constexpr float kAutoLaneWidthCoef = -2.0f;
static constexpr float kStopSpeedLimitScale = 10.0f;           // DAT_00f4166c
static constexpr float kMinPathLength = 20.0f;                 // DAT_00f03ee0
static constexpr float kNoJunctionDistance = 100000.0f;        // DAT_00f418e0
static constexpr float kMaxStopDistance = 1000.0f;             // DAT_00f41718
static constexpr float kLaneMargin = 0.1f;                     // DAT_00faa82c
static constexpr float kRoadSpeedLimitScale = 1.0f;            // DAT_00faa990
static constexpr float kStopBrakingScale = 1.2f;               // DAT_00faa928
static constexpr float kWaitBrakingScale = 1.2f;               // DAT_00faa924
static constexpr float kIndicatorDistance = 150.0f;            // DAT_00f4182c
static constexpr float kIndicatorBlinkDelay = 0.5f;            // DAT_00f41674
static constexpr float kUrgentTurnMinSpeed = 10.0f;            // DAT_00f4166c
static constexpr float kStoppedSpeed = 1.0f;                   // DAT_00f4165c
static constexpr float kLaneChangeEndDistance = 5.0f;          // DAT_00f41750
static constexpr float kHornMinInterval = 3.0f;                // DAT_00f416b4
static constexpr float kBackOvertakeStopDelay = 0.1f;          // DAT_00f41668
static constexpr float kBackOvertakeMinRatio = 0.1f;           // DAT_00f41668
static constexpr float kBackOvertakeLaneCheckDistance = 10.0f; // 0x41200000
static constexpr float kDegenerateSegmentTolerance = 2.5e-5f;  // 0x38d1b717
static constexpr float kDegenerateSegmentBackup = -5.0f;       // 0xc0a00000
static constexpr float kStopLineDistance = 0.1f;               // DAT_00faa804
static constexpr float kStopRampMargin = 2.0f;                 // DAT_00faa7fc
static constexpr float kStopRampSpeed = 1.0f;                  // DAT_00faa800

static constexpr uint8_t kSkipRoute = 1 << 0;
static constexpr uint8_t kSkipObstacle = 1 << 1;
static constexpr uint8_t kSkipFollowing = 1 << 2;
static constexpr uint8_t kSkipJunctionDistance = 1 << 3;
static constexpr uint8_t kSkipOvertake = 1 << 4;

static void ApplyStopRamp( AiVehNavTraffic::StopRamp &stop,
                           bool &bHandbrake,
                           float &speed,
                           const bool bZeroDeltaTime ) {
    // FUN_0095ae60
    if ( stop.State == 0 ) {
        return;
    }

    bHandbrake = false;
    if ( stop.State == 3 || kStopLineDistance > stop.RemainingDistance ) {
        stop.State = 3;
        speed = std::min( 0.0f, speed );
        bHandbrake = true;
        return;
    }

    if ( stop.State != 2 &&
         stop.BrakingDistance + kStopRampMargin < stop.RemainingDistance ) {
        return;
    }

    if ( bZeroDeltaTime ) {
        const float t = std::clamp(
            ( stop.RemainingDistance - kStopRampMargin ) / stop.BrakingDistance,
            0.0f, 1.0f );
        speed = std::min( ( 1.0f - t ) * kStopRampSpeed + speed * t, speed );
        return;
    }

    stop.State = 2;
    speed = std::min( kStopRampSpeed, speed );
}

static void SetLeftIndicator( AiVehSignals &signals ) {
    if ( signals.bWarningLeft && !signals.bWarningRight ) {
        return;
    }
    signals.bWarningLeft = true;
    signals.bWarningRight = false;
    signals.bWarningLeftOn = true;
    signals.bWarningRightOn = false;
    signals.bWarningActive = true;
    signals.WarningTimeNextBlink =
        gpTimer->GameTotalTime + kIndicatorBlinkDelay;
}

static void SetRightIndicator( AiVehSignals &signals ) {
    if ( !signals.bWarningLeft && signals.bWarningRight ) {
        return;
    }
    signals.bWarningLeft = false;
    signals.bWarningRight = true;
    signals.bWarningLeftOn = false;
    signals.bWarningRightOn = true;
    signals.bWarningActive = true;
    signals.WarningTimeNextBlink =
        gpTimer->GameTotalTime + kIndicatorBlinkDelay;
}

static void ClearIndicators( AiVehSignals &signals ) {
    if ( !signals.bWarningLeft && !signals.bWarningRight ) {
        return;
    }
    signals.bWarningLeft = false;
    signals.bWarningRight = false;
    signals.bWarningLeftOn = false;
    signals.bWarningRightOn = false;
    signals.bWarningActive = false;
}

static void QueueHorn( AiVehSignals &signals, const float random ) {
    // FUN_00920af0
    (void)signals;
    (void)random;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: Honk
}

static float
RandomRangeFloat( RandomGen &random, const float min, const float max ) {
    // FUN_006d9b70
    return RandomUnit( random ) * ( max - min ) + min;
}

AiFsm::AiFsm()
    : currentState( 0 ),
      nextState( 0 ),
      bTransitionPending( false ),
      stateStack{ 0, 0, 0, 0 },
      stackSize( 0 ) {}

void AiFsm::process( const int32_t event ) {
    // FUN_00407fb0
    handle( event, currentState );
    for ( int32_t i = kMaxChainedTransitions; bTransitionPending && i > 0;
          i-- ) {
        bTransitionPending = false;
        handle( FE_Exit, currentState );
        currentState = nextState;
        handle( FE_Enter, currentState );
    }
}

AiVehNavTraffic::AiVehNavTraffic()
    : AiVehNavigation(),
      AiFsm(),
      pVehicle( nullptr ),
      segFrom(),
      segTo(),
      curLane( 0 ),
      turnLane( 0 ),
      nextFrom(),
      nextTo(),
      nextLane( 0 ),
      pJunction( nullptr ),
      junctionSlotIn( 0 ),
      junctionSlotOut( 0 ),
      bMustStopAtJunction( false ),
      pathEnd(),
      pathEndLane( 0 ),
      obstacleDistance( -1.0f ),
      waitReason( WR_Continue ),
      bLaneChangeLock( false ),
      targetLane( 0 ),
      junctionTurnType( 2 ),
      distanceToJunction( 0.0f ),
      followSpeed( kDefaultFollowSpeed ),
      speedCoef( 1.0f ),
      stoppedSince( -1.0f ),
      path(),
      targetDirection( 1.0f, 0.0f, 0.0f, 0.0f ),
      targetSpeed( 0.0f ),
      stop(),
      pContextVehicle( nullptr ),
      field_0x258( false ),
      backOvertakeStart( 0.0f, 0.0f, 0.0f, 1.0f ),
      backOvertakeTime( -1.0f ),
      backOvertakeDistance( 0.0f ),
      field_0x278( 0.0f ),
      bOvertakeWanted( false ),
      overtakeDecisionTime( -1.0f ),
      bZeroDeltaTime( false ),
      speedDistribCoef( 1.0f ),
      secDistCoef( kDefaultSecDistCoef ),
      lateralOffsetCoefs{ 0.0f, 1.0f } {
    // FUN_0093c650
}

void AiVehNavTraffic::attach( AiVehicle *pOwner ) {
    // FUN_0093a4e0
    pVehicle = pOwner;
    path.attach( pOwner );
    reset( false );

    stop.State = 0;
    stop.InitialDistance = 0.0f;
    stop.RemainingDistance = 0.0f;
    pContextVehicle = pOwner;
    speedDistribCoef = gAIConfig.SpeedDistrib.sample( pVehicle->getRandom() );
    secDistCoef = gAIConfig.SecDistDistrib.sample( pVehicle->getRandom() );
    lateralOffsetCoefs[0] = 0.0f;
    lateralOffsetCoefs[1] = 1.0f;

    currentState = NS_NoPlan;
    bTransitionPending = false;
    nextState = 0;
    stackSize = 0;
    process( FE_Enter );
}

void AiVehNavTraffic::reset( const bool bFull ) {
    // FUN_0093a2b0
    followSpeed = kDefaultFollowSpeed;
    speedCoef = 1.0f;
    curLane = 0;
    turnLane = 0;
    nextLane = 0;
    pJunction = nullptr;
    junctionSlotIn = 0;
    junctionSlotOut = 0;
    bMustStopAtJunction = false;
    pathEndLane = 0;
    obstacleDistance = -1.0f;
    waitReason = WR_Continue;
    bLaneChangeLock = false;
    targetLane = 0;
    junctionTurnType = 2;
    distanceToJunction = 0.0f;
    stoppedSince = -1.0f;
    targetDirection = Eigen::Vector4f( 1.0f, 0.0f, 0.0f, 0.0f );
    targetSpeed = 0.0f;
    speedDistribCoef = 1.0f;
    secDistCoef = kDefaultSecDistCoef;
    lateralOffsetCoefs[1] = 1.0f;
    lateralOffsetCoefs[0] = 0.0f;
    field_0x258 = false;
    backOvertakeDistance = 0.0f;
    field_0x278 = 0.0f;
    backOvertakeStart = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    backOvertakeTime = -1.0f;
    bOvertakeWanted = false;
    overtakeDecisionTime = -1.0f;
    bZeroDeltaTime = false;

    if ( bFull ) {
        currentState = 0;
        nextState = 0;
        bTransitionPending = false;
        stackSize = 0;
        segFrom = AiRoadIt();
        segTo = AiRoadIt();
        nextFrom = AiRoadIt();
        nextTo = AiRoadIt();
        pathEnd = AiRoadIt();
        path.reset( true );
        stop.State = 0;
        stop.InitialDistance = 0.0f;
        stop.RemainingDistance = 0.0f;
    }
}

void AiVehNavTraffic::tick( const float deltaTime, const float totalTime ) {
    // FUN_00940120
    (void)totalTime;
    bZeroDeltaTime = !( 0.0f < deltaTime );

    AiVehicle *pOwner = pVehicle;
    if ( !pOwner->getInfos().bValid ) {
        return;
    }

    if ( path.getNumPoints() == 0 ) {
        bTransitionPending = true;
        nextState = NS_NoPlan;
    }
    process( FE_Update );

    path.updateProgress( pOwner->getInfos().GroundMatrix );
    if ( !path.computeTarget( targetDirection, targetSpeed, followSpeed,
                              speedCoef ) ) {
        pOwner->onNavigationLost();
        return;
    }
    ApplyStopRamp( stop, pOwner->getInfos().bHandbrake, targetSpeed,
                   bZeroDeltaTime );
}

void AiVehNavTraffic::detach() {
    // FUN_00939e30
    pContextVehicle = nullptr;
    path.release();
}

bool AiVehNavTraffic::getTarget( Eigen::Vector4f &outDirection,
                                 float &outSpeed ) const {
    // FUN_0093a5f0
    outDirection = targetDirection;
    outSpeed = targetSpeed;
    return ( pVehicle != nullptr ) ? pVehicle->getInfos().bValid : false;
}

void AiVehNavTraffic::getHeightAndTwist( float &outHeight,
                                         float &outTwist ) const {
    // FUN_0093aa40
    Eigen::Vector4f position;
    path.computePositionAndTwistAt( path.getProgress(), position, outTwist );
    outHeight = position.y();
}

bool AiVehNavTraffic::checkObstacleTransition() {
    const int32_t obstacle =
        checkObstacle( pContextVehicle, obstacleDistance, secDistCoef );
    if ( obstacle == NO_SlowPassable ) {
        const AiVehLocomotion *pLoco = pVehicle->getLocomotion();
        if ( pVehicle->getInfos().Speed > kUrgentTurnMinSpeed &&
             pLoco != nullptr && pLoco->isPhysicsDriven() &&
             pVehicle->getCategory() != eAiVehicleCategory::AVC_Cop ) {
            push( NS_UrgentTurn );
            return true;
        }
    } else if ( obstacle == NO_Blocked ) {
        push( NS_UrgentBrake );
        return true;
    }
    return false;
}

bool AiVehNavTraffic::updateAll( const uint8_t skipFlags, const int32_t lane ) {
    // FUN_0093f590
    if ( ( skipFlags & kSkipRoute ) == 0 ) {
        updateRoute();
    }
    if ( ( skipFlags & kSkipJunctionDistance ) == 0 ) {
        distanceToJunction = computeDistanceToJunction();
    }
    if ( ( skipFlags & kSkipObstacle ) == 0 && checkObstacleTransition() ) {
        return false;
    }
    if ( ( skipFlags & kSkipFollowing ) == 0 ) {
        updateFollowing( lane );
    }
    if ( ( skipFlags & kSkipOvertake ) == 0 ) {
        updateOvertake();
    }
    return true;
}

bool AiVehNavTraffic::updateFollowing( const int32_t lane ) {
    // FUN_0093e370
    (void)lane;
    float newFollowSpeed = 0.0f;
    float newSpeedCoef = 0.0f;
    computeFollowing( newFollowSpeed, newSpeedCoef, false );
    followSpeed = newFollowSpeed;
    speedCoef = newSpeedCoef;
    if ( newFollowSpeed == 0.0f && newSpeedCoef == 1.0f ) {
        if ( 0.0f > stoppedSince ) {
            stoppedSince = gpTimer->GameTotalTime;
        }
        return true;
    }
    stoppedSince = -1.0f;
    return false;
}

void AiVehNavTraffic::updateRoute() {
    // FUN_0093e920
    static const char *kLostReason = "traffic vehicle is lost";

    AiVehicle *pOwner = pVehicle;
    if ( pOwner->getControllerType() == 0 &&
         ( nextFrom.pRoad == nullptr || pJunction == nullptr ) &&
         pOwner->getBehaviourType() != 0 ) {
        pOwner->requestRemoval( -1.0f, kLostReason );
        return;
    }

    if ( segFrom.pRoad == nullptr || segTo.pRoad == nullptr ) {
        if ( kMinPathLength > path.getTotalLength() ) {
            pOwner->getInfos().bHandbrake = true;
        }
        return;
    }

    if ( pathEnd.pRoad->ID == segTo.pRoad->ID && pathEnd.isBefore( segTo ) ) {
        const int32_t lane = pathEndLane;
        const AiRoadIt start = pathEnd;
        const float speedLimit = computeSpeedLimit( segTo );
        const float widthRatioTo =
            computeLaneWidthRatio( segTo, lane, kAutoLaneWidthCoef );
        const float widthRatioFrom =
            computeLaneWidthRatio( start, lane, kAutoLaneWidthCoef );
        path.appendSpan( start, segTo, widthRatioFrom, widthRatioTo, &pathEnd,
                         speedLimit );
    }

    if ( pathEnd.isSamePosition( segTo, 0.0f ) ) {
        if ( junctionSlotOut != -1 ) {
            if ( path.hasRoom() ) {
                float speedLimit = computeSpeedLimit( segFrom );
                if ( bMustStopAtJunction ) {
                    speedLimit = speedDistribCoef * kStopSpeedLimitScale;
                }
                const float widthRatioNext = computeLaneWidthRatio(
                    nextFrom, nextLane, kAutoLaneWidthCoef );
                const float widthRatioEnd = computeLaneWidthRatio(
                    segTo, pathEndLane, kAutoLaneWidthCoef );
                path.appendJunctionSpan( segTo, widthRatioEnd, nextFrom,
                                         widthRatioNext, speedLimit );
                pathEnd = nextFrom;
                pathEndLane = nextLane;
            }
        } else if ( nextFrom.pRoad != nullptr && nextTo.pRoad != nullptr ) {
            const int32_t lane = nextLane;
            const float speedLimit = computeSpeedLimit( nextTo );
            const float widthRatioTo =
                computeLaneWidthRatio( nextTo, lane, kAutoLaneWidthCoef );
            const float widthRatioFrom =
                computeLaneWidthRatio( nextFrom, lane, kAutoLaneWidthCoef );
            path.appendSpan( nextFrom, nextTo, widthRatioFrom, widthRatioTo,
                             &pathEnd, speedLimit );
            pathEndLane = nextLane;
        }
    }

    if ( path.getNumPoints() <= 0 ) {
        return;
    }

    bool bPopRoute = false;
    if ( junctionSlotOut != -1 ) {
        const NavPath::Point &point = path.current();
        if ( point.Type == NavPath::PT_Junction && pJunction != nullptr &&
             point.pJunction->ID == pJunction->ID &&
             pathEnd.pRoad->ID == nextFrom.pRoad->ID ) {
            bPopRoute = true;
        }
    }
    if ( junctionSlotOut == -1 ) {
        const NavPath::Point &point = path.current();
        if ( point.Type == NavPath::PT_Road &&
             !point.It.isBefore( nextFrom ) ) {
            bPopRoute = true;
        }
    }
    if ( !bPopRoute ) {
        return;
    }

    AiVehController *pController = pOwner->getController();
    pController->popFront();
    pController->getFront( segFrom, segTo );
    pController->getSecond( nextFrom, nextTo );
    curLane = nextLane;

    AiRoadMap::Road *pRoadTo = segTo.pRoad;
    if ( pRoadTo != nullptr ) {
        pJunction =
            segTo.bWay ? pRoadTo->pJunctionEnd : pRoadTo->pJunctionBegin;
    } else {
        pJunction = nullptr;
    }

    AiRoadMap::Road *pRoadNext = nextFrom.pRoad;
    if ( pRoadNext == nullptr || segFrom.pRoad->ID == pRoadNext->ID ||
         pJunction == nullptr || pRoadTo == nullptr ) {
        junctionSlotIn = -1;
        junctionSlotOut = -1;
        turnLane = curLane;
        bMustStopAtJunction = false;
        return;
    }

    bool bSplit = false;
    junctionSlotIn = pJunction->getRoadIndex( pRoadTo );
    junctionSlotOut = pJunction->getRoadIndex( pRoadNext );
    turnLane =
        computeTurnLane( pJunction, pRoadTo, pRoadNext, curLane, bSplit );
    nextLane = computeNextLane( segTo, nextFrom, turnLane );
    bMustStopAtJunction =
        bSplit ? false : isStopRequired( segTo, nextFrom, junctionTurnType );
}

float AiVehNavTraffic::computeDistanceToJunction() const {
    // FUN_00939280
    const AiRoadIt &position = pContextVehicle->getInfos().LastRoadPos;
    if ( position.pRoad == nullptr ) {
        return kNoJunctionDistance;
    }

    const AiRoadShape *pShape = position.pRoad->getShape();
    if ( pShape == nullptr ) {
        return ComputeRoadDistance( nullptr, false, position );
    }

    float fraction = gbUniformRoadFraction
                         ? pShape->computeUniformRoadFraction( position )
                         : pShape->computeRoadFraction( position );
    if ( position.bWay ) {
        fraction = 1.0f - fraction;
    }
    return pShape->TotalLength * fraction;
}

float AiVehNavTraffic::computeStopDistance( const float distance ) const {
    // FUN_00938fd0
    if ( distance > kMaxStopDistance ) {
        return distance;
    }
    const AiRoadIt &position = pContextVehicle->getInfos().LastRoadPos;
    if ( position.pRoad == nullptr ) {
        return distance;
    }

    const float halfLength =
        pContextVehicle->getCarState()->getDimensions().z() * 0.5f;
    const float stopDistance =
        distance - halfLength - position.getEntity()->pProfile->DistToRoadsign;
    return ( 0.0f > stopDistance ) ? 0.0f : stopDistance;
}

float AiVehNavTraffic::computeLaneWidthRatio( const AiRoadIt &it,
                                              const int32_t lane,
                                              const float coefOverride ) const {
    // FUN_00939e50
    const AiRoadProfile *pProfile = it.getEntity()->pProfile;
    float ratioStart = 0.0f;
    float ratioEnd = 0.0f;
    pProfile->computeLaneWidthRatios( lane, it.bWay, ratioStart, ratioEnd );

    float lateralStart = 0.0f;
    float lateralEnd = 0.0f;
    float heightStart = 0.0f;
    float heightEnd = 0.0f;
    pProfile->computeLaneBounds( it.bWay, lane, lateralStart, lateralEnd,
                                 heightStart, heightEnd );

    const float halfWidth =
        pVehicle->getCarState()->getDimensions().x() * 0.5f + kLaneMargin;
    const float margin =
        halfWidth * ( ratioEnd - ratioStart ) / ( lateralEnd - lateralStart );
    const float low = margin + ratioStart;
    const float high = ratioEnd - margin;
    if ( low > high ) {
        return ( ratioStart + ratioEnd ) * 0.5f;
    }

    float coef =
        bOvertakeWanted ? lateralOffsetCoefs[1] : lateralOffsetCoefs[0];
    if ( coefOverride >= -1.0f && 1.0f >= coefOverride ) {
        coef = coefOverride;
    }
    const float k = ( coef + 1.0f ) * 0.5f;
    return it.bWay ? ( 1.0f - k ) * low + k * high
                   : ( 1.0f - k ) * high + k * low;
}

float AiVehNavTraffic::computeSpeedLimit( const AiRoadIt &it ) const {
    // FUN_0093a5b0
    const AiRoadMap &roadMap = gpAI->getRoadMap();
    int32_t index = it.getEntity()->SpeedLimitIndice;
    if ( index < 0 || index >= roadMap.getNumSpeedLimits() ) {
        index = 0;
    }

    static bool bLoggedSpeedLimits = false;
    if ( !bLoggedSpeedLimits ) {
        bLoggedSpeedLimits = true;
        for ( int32_t i = 0; i < roadMap.getNumSpeedLimits(); i++ ) {
            OTDU_LOG_DEBUG( "Road speed limit %d: %f\n", i,
                            roadMap.getSpeedLimit( i ) );
        }
    }

    return roadMap.getSpeedLimit( index ) * kRoadSpeedLimitScale *
           speedDistribCoef;
}

int32_t
AiVehNavTraffic::computeTurnLane( const AiRoadMap::Junction *pTurnJunction,
                                  const AiRoadMap::Road *pRoadIn,
                                  const AiRoadMap::Road *pRoadOut,
                                  const int32_t lane,
                                  bool &bOutSplit ) {
    // FUN_0093b600
    const bool bTowardEnd =
        !( pRoadIn->pJunctionBegin != nullptr &&
           pRoadIn->pJunctionBegin->ID == pTurnJunction->ID );
    const int32_t indexIn = pTurnJunction->getRoadIndex( pRoadIn );
    const int32_t indexOut = pTurnJunction->getRoadIndex( pRoadOut );
    junctionTurnType =
        pTurnJunction->computeTurnType( indexIn, indexOut, nullptr );

    const int32_t numLanes = pRoadIn->getNumLanesAt( bTowardEnd );
    const AiRoadMap::Junction *pSplitJunction =
        bTowardEnd ? pRoadIn->pJunctionEnd : pRoadIn->pJunctionBegin;
    AiRoadMap::JunctionSplit split;
    pSplitJunction->classifySplit( split, pRoadIn->ID );
    bOutSplit = ( split.Type != AiRoadMap::JunctionSplit::JST_None );

    if ( bOutSplit ) {
        int32_t laneMin = 0;
        int32_t laneMax = numLanes - 1;
        switch ( split.Type ) {
        case AiRoadMap::JunctionSplit::JST_SymmetricSplit:
            if ( indexOut == split.PairIndexA ) {
                const int32_t numPairLanes =
                    pTurnJunction->getRoad( split.PairIndexA )
                        ->getNumLanesAt( bTowardEnd );
                laneMin =
                    numLanes - std::min( numPairLanes, ( numLanes + 1 ) / 2 );
            } else {
                const int32_t numPairLanes =
                    pTurnJunction->getRoad( split.PairIndexB )
                        ->getNumLanesAt( bTowardEnd );
                laneMax = std::min( numPairLanes, ( numLanes + 1 ) / 2 ) - 1;
            }
            break;
        case AiRoadMap::JunctionSplit::JST_SymmetricMerge: {
            const int32_t numSingleLanes =
                pTurnJunction->getRoad( split.SingleIndex )
                    ->getNumLanesAt( bTowardEnd );
            if ( indexIn == split.PairIndexA ) {
                laneMin = numLanes - std::min( numSingleLanes / 2, numLanes );
            } else {
                laneMax = std::min( numSingleLanes / 2, numLanes ) - 1;
            }
            break;
        }
        case AiRoadMap::JunctionSplit::JST_Merge:
        case AiRoadMap::JunctionSplit::JST_Split: {
            const int32_t index =
                ( split.Type == AiRoadMap::JunctionSplit::JST_Merge )
                    ? indexIn
                    : indexOut;
            if ( index == split.PairIndexA ) {
                laneMin = ( laneMax < 1 ) ? laneMax : 1;
            } else {
                laneMax = 0;
            }
            break;
        }
        default:
            break;
        }

        if ( lane < laneMin ) {
            return laneMin;
        }
        if ( lane > laneMax ) {
            return laneMax;
        }
        return lane;
    }

    if ( numLanes <= 0 ) {
        return 0;
    }

    const int32_t numRightLanes = pRoadIn->getNumLanesAt( true );
    const int32_t numLeftLanes = pRoadIn->getNumLanesAt( false );
    int32_t bestLane = -1;
    int32_t bestDistance = -1;
    for ( int32_t i = 0; i < numLanes; i++ ) {
        int32_t index = bTowardEnd ? numLeftLanes - i + numRightLanes - 1
                                   : numRightLanes - i - 1;
        if ( index < 0 || index >= numLeftLanes + numRightLanes ) {
            index = 0;
        }
        int32_t laneFlags = static_cast<uint8_t>( pRoadIn->LaneFlags[index] );
        if ( laneFlags == 0 ) {
            laneFlags = 7;
        }
        if ( ( junctionTurnType & laneFlags ) == 0 ) {
            continue;
        }
        const int32_t distance = std::abs( lane - i );
        if ( bestDistance < 0 || bestDistance > distance ) {
            bestLane = i;
            bestDistance = distance;
        }
    }
    return ( bestLane >= 0 ) ? bestLane : 0;
}

int32_t AiVehNavTraffic::computeNextLane( const AiRoadIt &to,
                                          const AiRoadIt &next,
                                          const int32_t lane ) const {
    // FUN_0093a640
    const AiRoadMap::Junction *pSplitJunction =
        to.bWay ? to.pRoad->pJunctionEnd : to.pRoad->pJunctionBegin;
    AiRoadMap::JunctionSplit split;
    pSplitJunction->classifySplit( split, to.pRoad->ID );

    if ( split.Type == AiRoadMap::JunctionSplit::JST_Merge &&
         split.RoadIndex == split.PairIndexB ) {
        return 0;
    }

    if ( split.Type == AiRoadMap::JunctionSplit::JST_SymmetricMerge ) {
        const AiRoadMap::Junction *pTurnJunction =
            segTo.bWay ? segTo.pRoad->pJunctionEnd
                       : segTo.pRoad->pJunctionBegin;
        const int32_t numLanes = pTurnJunction->getRoad( split.SingleIndex )
                                     ->getNumLanesAt( next.bWay );
        const int32_t half = ( numLanes + 1 ) / 2;
        if ( split.RoadIndex == split.PairIndexA ) {
            const int32_t laneMin = numLanes - half;
            if ( lane < laneMin ) {
                return laneMin;
            }
            return ( lane > numLanes - 1 ) ? numLanes - 1 : lane;
        }
        if ( lane < 0 ) {
            return 0;
        }
        return ( lane > half - 1 ) ? half - 1 : lane;
    }

    const AiRoadMap::Entity *pEntity = next.getEntity();
    const int32_t numLanes =
        next.bWay ? pEntity->NumRightLanes : pEntity->NumLeftLanes;
    return ( lane < numLanes ) ? lane : numLanes - 1;
}

bool AiVehNavTraffic::isStopRequired( const AiRoadIt &to,
                                      const AiRoadIt &next,
                                      const int32_t turnType ) const {
    // FUN_00939180
    const AiRoadMap::Road *pRoadTo = to.pRoad;
    if ( pRoadTo == nullptr || next.pRoad == nullptr ) {
        return false;
    }

    const AiRoadMap::Junction *pSplitJunction =
        to.bWay ? pRoadTo->pJunctionEnd : pRoadTo->pJunctionBegin;
    AiRoadMap::JunctionSplit split;
    pSplitJunction->classifySplit( split, pRoadTo->ID );
    if ( split.Type != AiRoadMap::JunctionSplit::JST_None ) {
        return false;
    }

    const uint8_t device =
        to.bWay ? pRoadTo->CtrlDeviceEnd : pRoadTo->CtrlDeviceBegin;
    if ( device != 0 ) {
        return true;
    }
    if ( turnType == 2 ) {
        return false;
    }

    const AiRoadMap::Junction *pTurnJunction =
        to.bWay ? pRoadTo->pJunctionEnd : pRoadTo->pJunctionBegin;
    if ( pTurnJunction == nullptr ) {
        return false;
    }

    const int32_t numRoads = pTurnJunction->NumRoads;
    const int32_t indexIn = pTurnJunction->getRoadIndex( pRoadTo );
    const int32_t indexOut = pTurnJunction->getRoadIndex( next.pRoad );
    for ( int32_t i = 0; i < numRoads; i++ ) {
        if ( i == indexIn || i == indexOut ) {
            continue;
        }
        if ( pTurnJunction->canTurnInto( i, indexOut ) ) {
            return true;
        }
    }
    return false;
}

int32_t AiVehNavTraffic::checkObstacle( AiVehicle *pContext,
                                        float &outDistance,
                                        const float secDistance ) {
    // FUN_00939300
    (void)pContext;
    (void)outDistance;
    (void)secDistance;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: obstacle detection
    return NO_None;
}

void AiVehNavTraffic::computeFollowing( float &outFollowSpeed,
                                        float &outSpeedCoef,
                                        const bool bDebug ) {
    // FUN_0093d5b0
    (void)bDebug;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: car following model
    outFollowSpeed = kNoLeaderFollowSpeed;
    outSpeedCoef = 1.0f;
}

void AiVehNavTraffic::updateOvertake() {
    // FUN_0093f040
    OTDU_UNIMPLEMENTED_ONCE; // TODO: overtake decision
}

bool AiVehNavTraffic::isBackOvertakeWanted() {
    // FUN_0093df90
    OTDU_UNIMPLEMENTED_ONCE; // TODO: back-overtake trigger
    return false;
}

bool AiVehNavTraffic::isLaneChangeWanted() {
    // FUN_0093f980
    OTDU_UNIMPLEMENTED_ONCE; // TODO: lane change trigger
    return false;
}

bool AiVehNavTraffic::isLaneChangeFinished() {
    // FUN_0093e400
    OTDU_UNIMPLEMENTED_ONCE; // TODO: lane change end
    return true;
}

bool AiVehNavTraffic::isOnLane( const int32_t lane ) const {
    // FUN_0093c720
    (void)lane;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: CarState lane test
    return true;
}

bool AiVehNavTraffic::mustWaitAtJunction( int32_t &outReason,
                                          const bool bUpdate ) {
    // FUN_0093c850
    (void)bUpdate;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: junction wait decision
    outReason = WR_Continue;
    return false;
}

bool AiVehNavTraffic::isBackOvertakePossible() {
    // FUN_0093e610
    OTDU_UNIMPLEMENTED_ONCE; // TODO: back-overtake lane check
    return false;
}

float AiVehNavTraffic::computeUrgentTurnSteer( const bool bDebug ) {
    // FUN_0093cca0
    (void)bDebug;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: urgent turn steer
    return 0.0f;
}

void AiVehNavTraffic::moveToTurnLane( const AiRoadIt &position,
                                      const int32_t lane ) {
    // FUN_0093f680
    (void)position;
    (void)lane;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: squeeze into the turn lane
}

bool AiVehNavTraffic::isLaneFree( const int32_t lane,
                                  const AiRoadIt &position,
                                  const float distance,
                                  const float rangeMin,
                                  const float rangeMax ) const {
    // FUN_00926ec0
    (void)lane;
    (void)position;
    (void)distance;
    (void)rangeMin;
    (void)rangeMax;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AiVehicle lane free check
    return true;
}

int32_t AiVehNavTraffic::computeCurrentLane() const {
    // FUN_009269c0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AiVehicle current lane
    return turnLane;
}

bool AiVehNavTraffic::handle( const int32_t event, const int32_t state ) {
    // FUN_009db3f0
    AiVehicle *pOwner = pVehicle;
    AiVehicleInfo &infos = pOwner->getInfos();
    AiVehSignals &signals = pOwner->getSignals();
    AiVehLocomotion *pLoco = pOwner->getLocomotion();

    switch ( state ) {
    case NS_NoPlan: {
        if ( event != FE_Update ) {
            return false;
        }

        const int32_t lastLane = infos.LastRoadLane;
        AiVehController *pController = pOwner->getController();
        if ( pController == nullptr ) {
            return true;
        }
        const bool bValidSegment = pController->getFront( segFrom, segTo );
        pController->getSecond( nextFrom, nextTo );
        if ( !bValidSegment ) {
            return true;
        }

        if ( segFrom.isSamePosition( segTo, kDegenerateSegmentTolerance ) &&
             ComputeRoadDistance( nullptr, true, segFrom ) > 0.0f ) {
            segFrom.advanceByDistance( kDegenerateSegmentBackup, false );
        }

        const AiRoadMap::Entity *pEntity = segFrom.getEntity();
        const int32_t numLanes =
            segFrom.bWay ? pEntity->NumRightLanes : pEntity->NumLeftLanes;
        curLane =
            ( lastLane < numLanes ) ? lastLane : std::max( numLanes - 1, 0 );

        pJunction = segTo.bWay ? segTo.pRoad->pJunctionEnd
                               : segTo.pRoad->pJunctionBegin;
        AiRoadMap::Road *pRoadNext = nextFrom.pRoad;
        if ( pJunction != nullptr && pRoadNext != nullptr &&
             segTo.pRoad->ID != pRoadNext->ID ) {
            bool bSplit = false;
            junctionSlotIn = pJunction->getRoadIndex( segTo.pRoad );
            junctionSlotOut = pJunction->getRoadIndex( pRoadNext );
            turnLane = computeTurnLane( pJunction, segTo.pRoad, pRoadNext,
                                        curLane, bSplit );
            nextLane = computeNextLane( segTo, nextFrom, turnLane );
            bMustStopAtJunction =
                bSplit ? false
                       : isStopRequired( segTo, nextFrom, junctionTurnType );
        } else {
            junctionSlotIn = -1;
            junctionSlotOut = -1;
            turnLane = curLane;
            nextLane = curLane;
            bMustStopAtJunction = false;
        }

        const float speedLimit = computeSpeedLimit( segTo );
        const float widthRatioTo =
            computeLaneWidthRatio( segTo, curLane, kAutoLaneWidthCoef );
        const float widthRatioFrom =
            computeLaneWidthRatio( segFrom, curLane, kAutoLaneWidthCoef );
        path.appendSpan( segFrom, segTo, widthRatioFrom, widthRatioTo, &pathEnd,
                         speedLimit );
        pathEndLane = curLane;
        goTo( NS_OnRoad );
        return true;
    }

    case NS_Lost:
        if ( event == FE_Enter ) {
            infos.bHandbrake = true;
            return true;
        }
        if ( event == FE_Exit ) {
            infos.bHandbrake = false;
            return true;
        }
        return false;

    case NS_OnRoad: {
        if ( event == FE_Exit ) {
            bLaneChangeLock = false;
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }

        if ( !updateAll( 0, -1 ) ) {
            return true;
        }
        if ( path.getNumPoints() == 0 ) {
            return true;
        }

        const float stopDistance = computeStopDistance( distanceToJunction );
        const float brakingDistance =
            pOwner->computeBrakingDistance( infos.Speed, 0.0f, 0.0f ) *
            kStopBrakingScale;
        const NavPath::ePointType pointType = path.current().Type;
        if ( pointType == NavPath::PT_Junction ) {
            goTo( NS_OnJunction );
            return true;
        }
        if ( pointType == NavPath::PT_Road && brakingDistance >= stopDistance &&
             distanceToJunction >= 0.0f && bMustStopAtJunction ) {
            goTo( NS_JunctionWait );
            return true;
        }
        if ( isBackOvertakeWanted() ) {
            push( NS_BackOvertakeStop );
            return true;
        }
        if ( isLaneChangeWanted() ) {
            goTo( NS_LaneChange );
            return true;
        }
        if ( kIndicatorDistance > distanceToJunction &&
             distanceToJunction >= 0.0f ) {
            if ( junctionTurnType == 1 ) {
                SetLeftIndicator( signals );
            } else if ( junctionTurnType == 4 ) {
                SetRightIndicator( signals );
            }
        }
        return true;
    }

    case NS_LaneChange: {
        if ( event == FE_Enter ) {
            if ( targetLane < computeCurrentLane() ) {
                SetRightIndicator( signals );
            } else {
                SetLeftIndicator( signals );
            }
            return true;
        }
        if ( event == FE_Exit ) {
            bLaneChangeLock = false;
            ClearIndicators( signals );
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }

        const int32_t lane = targetLane;
        updateRoute();
        if ( checkObstacleTransition() ) {
            return true;
        }
        updateFollowing( lane );
        updateOvertake();
        if ( isLaneChangeFinished() ) {
            goTo( NS_OnRoad );
            return true;
        }
        const NavPath::Point &point = path.current();
        if ( ( point.Type == NavPath::PT_Road &&
               kLaneChangeEndDistance >
                   ComputeRoadDistance( nullptr, false, point.It ) ) ||
             isOnLane( targetLane ) ) {
            goTo( NS_OnRoad );
        }
        return true;
    }

    case NS_JunctionWait: {
        if ( event == FE_Enter ) {
            distanceToJunction = computeDistanceToJunction();
            const float stopDistance =
                computeStopDistance( distanceToJunction );
            stop.State = 1;
            stop.InitialDistance = stopDistance;
            stop.RemainingDistance = stopDistance;
            const int32_t lane = computeCurrentLane();
            if ( lane != turnLane ) {
                moveToTurnLane( infos.LastRoadPos, lane );
            }
            return true;
        }
        if ( event == FE_Exit ) {
            stop.State = 0;
            stop.InitialDistance = 0.0f;
            stop.RemainingDistance = 0.0f;
            infos.bHandbrake = false;
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }

        updateRoute();
        if ( checkObstacleTransition() ) {
            return true;
        }
        updateFollowing( -1 );
        updateOvertake();
        if ( path.current().Type == NavPath::PT_Junction ) {
            goTo( NS_OnJunction );
            return true;
        }
        distanceToJunction = computeDistanceToJunction();
        if ( !mustWaitAtJunction( waitReason, true ) ) {
            goTo( NS_JunctionApproach );
            return true;
        }
        stop.RemainingDistance = computeStopDistance( distanceToJunction );
        stop.BrakingDistance =
            pOwner->computeBrakingDistance( infos.Speed, 0.0f, 0.0f ) *
            kWaitBrakingScale;
        return true;
    }

    case NS_JunctionApproach:
        if ( event != FE_Update ) {
            return false;
        }
        updateRoute();
        if ( checkObstacleTransition() ) {
            return true;
        }
        updateFollowing( -1 );
        updateOvertake();
        if ( path.current().Type == NavPath::PT_Junction ) {
            goTo( NS_OnJunction );
        }
        return true;

    case NS_OnJunction: {
        if ( event == FE_Exit ) {
            ClearIndicators( signals );
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        updateRoute();
        updateFollowing( -1 );
        updateOvertake();
        const NavPath::Point &point = path.current();
        if ( point.Type == NavPath::PT_Road &&
             infos.LastRoadPos.pRoad != nullptr &&
             infos.LastRoadPos.pRoad->ID == point.It.pRoad->ID ) {
            goTo( NS_OnRoad );
        }
        return true;
    }

    case NS_UrgentBrake: {
        if ( event == FE_Enter ) {
            followSpeed = 0.0f;
            speedCoef = 1.0f;
            if ( 0.0f >= signals.HornLastTime ||
                 gpTimer->GameTotalTime - signals.HornLastTime >
                     kHornMinInterval ) {
                QueueHorn( signals, RandomUnit( pOwner->getRandom() ) );
            }
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        updateRoute();
        const int32_t obstacle =
            checkObstacle( pContextVehicle, obstacleDistance, secDistCoef );
        if ( obstacle == NO_SlowPassable && infos.Speed > kUrgentTurnMinSpeed &&
             pOwner->isPhysicsDriven() &&
             pOwner->getCategory() != eAiVehicleCategory::AVC_Cop ) {
            goTo( NS_UrgentTurn );
            return true;
        }
        if ( kStoppedSpeed >= infos.Speed && obstacle <= NO_Far ) {
            pop();
        }
        return true;
    }

    case NS_UrgentTurn: {
        if ( event == FE_Enter ) {
            followSpeed = 0.0f;
            speedCoef = 1.0f;
            const float steer = computeUrgentTurnSteer( false );
            if ( pLoco != nullptr ) {
                const float throttle =
                    std::clamp( pLoco->getPadThrottle(), 0.0f, 1.0f );
                pLoco->setDrivenByNavigation( false );
                pLoco->setManualInputs( throttle, 0.0f, 0.0f, steer, 0.0f );
            }
            if ( steer == 0.0f ) {
                goTo( NS_UrgentTurn2 );
            }
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        updateRoute();
        if ( kStoppedSpeed >= infos.Speed || !pOwner->isPhysicsDriven() ) {
            pOwner->onNavigationLost();
        }
        const ObjectWorldLocation &location =
            pOwner->getCarState()->getWorldLocation();
        if ( location.LocationType != ObjectWorldLocation::OWL_Road ||
             ( fabsf( location.LocationRoad.LateralMin ) > 1.0f &&
               fabsf( location.LocationRoad.LateralMax ) > 1.0f ) ) {
            goTo( NS_UrgentTurn2 );
        }
        return true;
    }

    case NS_UrgentTurn2:
        if ( event == FE_Enter ) {
            if ( pLoco != nullptr ) {
                pLoco->setDrivenByNavigation( false );
                pLoco->setManualInputs( 0.0f, 1.0f, 0.0f, -1.0f, 0.0f );
            }
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        updateRoute();
        if ( kStoppedSpeed >= infos.Speed || !pOwner->isPhysicsDriven() ) {
            pOwner->onNavigationLost();
        }
        return true;

    case NS_BackOvertakeStop: {
        if ( event == FE_Enter ) {
            pLoco->setDrivenByNavigation( false );
            pLoco->setManualInputs( 0.0f, 0.0f, 0.0f, 0.0f, 0.0f );
            OTDU_UNIMPLEMENTED_ONCE;
            const float random =
                RandomRangeFloat( pOwner->getRandom(), -1.0f, 0.5f );
            if ( random >= 0.0f ) {
                QueueHorn( signals, random );
            }
            return true;
        }
        if ( event == FE_Exit ) {
            pLoco->setDrivenByNavigation( true );
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        pLoco->setManualInputs( 0.0f, 0.0f, 0.0f, 0.0f, 0.0f );
        if ( !( gpTimer->GameTotalTime - backOvertakeTime >
                kBackOvertakeStopDelay ) ) {
            return true;
        }
        if ( backOvertakeDistance > 0.0f ) {
            goTo( NS_BackOvertakeReverse );
            return true;
        }
        if ( isBackOvertakePossible() ) {
            goTo( NS_BackOvertakePass );
            return true;
        }
        pop();
        return true;
    }

    case NS_BackOvertakeReverse: {
        if ( event == FE_Enter ) {
            pLoco->setDrivenByNavigation( false );
            pLoco->setManualInputs( 0.0f, 1.0f, 0.0f, 0.0f, 0.0f );
            return true;
        }
        if ( event == FE_Exit ) {
            pLoco->setDrivenByNavigation( true );
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        const Eigen::Vector4f delta =
            Eigen::Vector4f( infos.GroundMatrix.row( 3 ) ) - backOvertakeStart;
        const float distance = delta.norm();
        const float remaining = backOvertakeDistance - distance;
        const float slope = ( infos.LastRoadPos.pRoad != nullptr )
                                ? infos.LastRoadPos.computeSlope()
                                : 0.0f;
        const float brakingDistance =
            pOwner->computeBrakingDistance( infos.Speed, 0.0f, slope );
        if ( brakingDistance >= remaining &&
             distance > backOvertakeDistance * kBackOvertakeMinRatio ) {
            goTo( NS_BackOvertakeForward );
            return true;
        }
        if ( checkObstacle( pContextVehicle, obstacleDistance, secDistCoef ) ==
             NO_Blocked ) {
            goTo( NS_CancelBackOvertake );
        }
        return true;
    }

    case NS_BackOvertakeForward:
        if ( event == FE_Enter ) {
            pLoco->setDrivenByNavigation( false );
            pLoco->setManualInputs( 1.0f, 0.0f, 0.0f, 0.0f, 0.0f );
            return true;
        }
        if ( event == FE_Exit ) {
            pLoco->setDrivenByNavigation( true );
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        if ( infos.Speed < 0.0f ) {
            return true;
        }
        if ( isBackOvertakePossible() ) {
            goTo( NS_BackOvertakePass );
            return true;
        }
        pop();
        return true;

    case NS_BackOvertakePass: {
        if ( event == FE_Enter ) {
            return true;
        }
        if ( event == FE_Exit ) {
            OTDU_UNIMPLEMENTED_ONCE;
            field_0x258 = false;
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }

        const int32_t lane = targetLane;
        updateRoute();
        if ( checkObstacleTransition() ) {
            return true;
        }
        updateFollowing( lane );
        if ( isOnLane( targetLane ) ) {
            bLaneChangeLock = false;
            pop();
        }
        const float halfLength =
            pOwner->getCarState()->getDimensions().z() * 0.5f;
        if ( infos.LastRoadPos.pRoad != nullptr &&
             !isLaneFree( targetLane, infos.LastRoadPos,
                          kBackOvertakeLaneCheckDistance, -halfLength,
                          halfLength ) ) {
            followSpeed = 0.0f;
            speedCoef = 1.0f;
        }
        return true;
    }

    case NS_CancelBackOvertake:
        if ( event == FE_Enter ) {
            pLoco->setDrivenByNavigation( false );
            pLoco->setManualInputs( 1.0f, 0.0f, 0.0f, 0.0f, 0.0f );
            return true;
        }
        if ( event == FE_Exit ) {
            pLoco->setDrivenByNavigation( true );
            OTDU_UNIMPLEMENTED_ONCE;
            return true;
        }
        if ( event != FE_Update ) {
            return false;
        }
        if ( infos.Speed >= 0.0f ) {
            pop();
        }
        return true;

    default:
        return false;
    }
}
