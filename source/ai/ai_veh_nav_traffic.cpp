#include "shared.h"
#include "ai_veh_nav_traffic.h"

#include "ai/ai_config.h"
#include "ai/ai_oriented_box.h"
#include "ai/ai_road_shape.h"
#include "ai/ai_veh_controller.h"
#include "ai/ai_veh_locomotion.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "core/maths_helper.h"
#include "core/random/bcpl.h"
#include "game/game_object.h"
#include "game/vehicle/car_state.h"
#include "gs_timer.h"

static constexpr float kDefaultFollowSpeed = 150.0f; // DAT_00f4182c
static constexpr uint32_t kSignalRed = 1;
static constexpr uint32_t kSignalOrange = 2;
static constexpr uint32_t kSignalGreen = 4;
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

static int32_t CompareJunctionPriority( const AiRoadMap::Road *pRoad,
                                        const bool bWay,
                                        const AiRoadMap::Road *pOtherRoad,
                                        const bool bOtherAtEnd,
                                        const bool bRightTurn ) {
    // FUN_00939040
    const uint8_t device = pRoad->getControlDevice( bWay );
    const uint8_t otherDevice = pOtherRoad->getControlDevice( bOtherAtEnd );
    uint32_t signal = 0;
    uint32_t otherSignal = 0;
    if ( device == 3 ) {
        signal = GetTrafficSignalState( pRoad->getSignalSynchro( bWay ),
                                        gGSAI.getSignalSyncTime() );
    }
    if ( bRightTurn ) {
        signal = kSignalGreen;
    }
    if ( otherDevice == 3 ) {
        otherSignal =
            GetTrafficSignalState( pOtherRoad->getSignalSynchro( bOtherAtEnd ),
                                   gGSAI.getSignalSyncTime() );
    }

    const bool bOtherRed = ( otherSignal & kSignalRed ) != 0;
    switch ( device ) {
    case 1:
        if ( otherDevice == 3 ) {
            return bOtherRed ? 1 : -1;
        }
        return ( otherDevice == 1 ) ? 0 : -1;
    case 2:
        if ( otherDevice == 0 ) {
            return -1;
        }
        if ( otherDevice == 3 ) {
            return bOtherRed ? 1 : -1;
        }
        return ( otherDevice == 2 ) ? 0 : 1;
    case 3:
        if ( ( signal & kSignalGreen ) != 0 ) {
            if ( otherDevice == 0 || ( otherDevice == 3 && !bOtherRed ) ) {
                return 0;
            }
            return 1;
        }
        return ( otherDevice == 3 && bOtherRed ) ? 0 : -1;
    default:
        return ( otherDevice != 0 ) ? 1 : 0;
    }
}

static bool IntervalsMeetInWindow( const float position,
                                   const float otherPosition,
                                   const float timeMin,
                                   const float timeMax,
                                   const float margin,
                                   const float otherSpeed,
                                   const float speed ) {
    // FUN_00938f30
    static constexpr float kSameSpeed = 0.001f; // DAT_00f4167c

    if ( kSameSpeed > fabsf( speed - otherSpeed ) ) {
        return position >= otherPosition - margin &&
               otherPosition + margin >= position;
    }

    const float relative = otherSpeed - speed;
    float timeA = ( position - otherPosition - margin ) / relative;
    float timeB = ( position - otherPosition + margin ) / relative;
    if ( timeA > timeB ) {
        std::swap( timeA, timeB );
    }
    return timeB > timeMin && timeMax >= timeA;
}

static float ComputeLaneCenterWidthRatio( const AiRoadIt &it,
                                          const int32_t lane ) {
    // FUN_0092c970
    const AiRoadProfile *pProfile = it.getEntity()->pProfile;
    float lateral = 0.0f;
    float height = 0.0f;
    pProfile->computeLaneOffset( it.bWay, lane, lateral, height );
    return lateral / ( pProfile->InnerWidth * 0.5f );
}

static uint8_t GetLaneTurnMask( const AiRoadMap::Road *pRoad,
                                const bool bWay,
                                const int32_t lane ) {
    // FUN_00930cf0
    const int32_t numRightLanes = pRoad->getNumLanesAt( true );
    const int32_t numLeftLanes = pRoad->getNumLanesAt( false );
    int32_t index = bWay ? numLeftLanes - lane + numRightLanes - 1
                         : numRightLanes - lane - 1;
    if ( index < 0 || index >= numLeftLanes + numRightLanes ) {
        index = 0;
    }
    const uint8_t flags = static_cast<uint8_t>( pRoad->LaneFlags[index] );
    return ( flags != 0 ) ? flags : 7;
}

static bool ComputeQueueAhead( const AiRoadIt &position,
                               const int32_t lane,
                               const float fraction,
                               const uint32_t selfID,
                               float *pOutDistance,
                               float *pOutQueueEnd,
                               float *pOutGapStart ) {
    // FUN_00939c10
    static constexpr int32_t kMaxObjects = 16;
    static constexpr float kMaxQueueGap = 20.0f; // DAT_00faa950

    const bool bWay = position.bWay;
    const AiRoadShape *pShape = position.pRoad->getShape();
    GameObject *objects[kMaxObjects];
    const int32_t numObjects =
        CollectRoadObjects( position.pRoad->ID, objects, kMaxObjects, fraction,
                            bWay ? 1.0f : 0.0f, lane, bWay );

    *pOutQueueEnd = -1.0f;
    *pOutGapStart = -1.0f;
    for ( int32_t i = 0; i < numObjects; i++ ) {
        GameObject *pObject = objects[i];
        if ( selfID == pObject->getID() || selfID == pObject->getParentID() ) {
            continue;
        }

        const ObjectLocationRoad &road =
            pObject->getWorldLocation().LocationRoad;
        const float nearEdge = bWay ? road.FractionMin : road.FractionMax;
        const float farEdge = bWay ? road.FractionMax : road.FractionMin;
        if ( 0.0f > *pOutQueueEnd ) {
            *pOutQueueEnd = farEdge;
            *pOutGapStart = bWay ? 1.0f : 0.0f;
            continue;
        }

        float gap = ( nearEdge - *pOutQueueEnd ) * pShape->TotalLength;
        if ( !bWay ) {
            gap = 0.0f - gap;
        }
        if ( gap > kMaxQueueGap ) {
            *pOutGapStart = nearEdge;
            break;
        }
        *pOutQueueEnd = farEdge;
    }

    if ( *pOutQueueEnd >= 0.0f && *pOutGapStart >= 0.0f ) {
        *pOutDistance = pShape->TotalLength * fabsf( *pOutQueueEnd - fraction );
        return true;
    }
    *pOutDistance = bWay ? ( 1.0f - fraction ) * pShape->TotalLength
                         : pShape->TotalLength * fraction;
    return false;
}

void ApplyStopRamp( AiVehNavTraffic::StopRamp &stop,
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

void QueueHorn( AiVehSignals &signals, const float random ) {
    // FUN_00920af0
    if ( signals.bIsCops || signals.bHornActive ) {
        return;
    }

    float *pQueue = signals.HornCommandsQueue;
    if ( 0.2f > random ) {
        pQueue[0] = 0.2f;
        pQueue[1] = 0.3f;
        pQueue[2] = 0.6f;
        pQueue[3] = 0.0f;
    } else if ( 0.4f > random ) {
        pQueue[0] = 0.2f;
        pQueue[1] = 0.3f;
        pQueue[2] = 1.0f;
        pQueue[3] = 0.0f;
    } else if ( 0.6f > random ) {
        pQueue[0] = 0.2f;
        pQueue[1] = 0.3f;
        pQueue[2] = 0.5f;
        pQueue[3] = 0.6f;
        pQueue[4] = 1.5f;
        pQueue[5] = 0.0f;
    } else {
        pQueue[0] = ( 0.8f > random ) ? 2.0f : 3.0f;
        pQueue[1] = 0.0f;
    }
    signals.HornCommandIndex = 0;
    signals.bHornActive = true;
    signals.bHornOn = true;
    signals.HornLastTime = gpTimer->GameTotalTime;
}

static float
RandomRangeFloat( RandomGen &random, const float min, const float max ) {
    // FUN_006d9b70
    return RandomUnit( random ) * ( max - min ) + min;
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
      obstacleContactTime( -1.0f ),
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
      bBackOvertaking( false ),
      backOvertakeStart( 0.0f, 0.0f, 0.0f, 1.0f ),
      backOvertakeTime( -1.0f ),
      backOvertakeDistance( 0.0f ),
      backOvertakePassDistance( 0.0f ),
      bPullOver( false ),
      pullOverDecisionTime( -1.0f ),
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
    obstacleContactTime = -1.0f;
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
    bBackOvertaking = false;
    backOvertakeDistance = 0.0f;
    backOvertakePassDistance = 0.0f;
    backOvertakeStart = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    backOvertakeTime = -1.0f;
    bPullOver = false;
    pullOverDecisionTime = -1.0f;
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

void AiVehNavTraffic::debugDraw( DrawList *pDrawList ) {
    // FUN_0093b910
    (void)pDrawList;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: DEBUG
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
        checkObstacle( pContextVehicle, obstacleContactTime, secDistCoef );
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
        updatePullOver();
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

    float coef = bPullOver ? lateralOffsetCoefs[1] : lateralOffsetCoefs[0];
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
    static constexpr float kLaneChangeLateralVelocity = 0.4f; // DAT_00faa77c
    static constexpr float kSameHeadingCos = 0.5f;            // DAT_00f41674
    static constexpr float kSecDistOverride = -1.0f;          // DAT_00faa940
    static constexpr float kSecDistLowCoef = 0.5f;            // DAT_00faa948
    static constexpr float kSecDistHighCoef = 1.5f;           // DAT_00faa944
    static constexpr float kContactTimePerLength = 0.2f;      // DAT_00faa93c
    static constexpr float kMinPassableSecDist = 0.2f;        // DAT_00f41730
    static constexpr float kMinPassableSpeed = 20.0f;         // DAT_00faa94c
    static constexpr float kBrakingMargin = 1.0f;             // DAT_00faa938

    GameObject *pObstacle = nullptr;
    const float contactTime = pContext->findClosestObstacle( &pObstacle );
    outDistance = contactTime;
    if ( contactTime < 0.0f || pObstacle == nullptr ) {
        return NO_None;
    }

    const bool bObstacleIsAI =
        pObstacle->getType() == 0 &&
        static_cast<CarState *>( pObstacle )->getAIController() != nullptr;

    CarState *pCar = pContext->getCarState();
    const ObjectWorldLocation &self = pCar->getWorldLocation();
    const ObjectWorldLocation &other = pObstacle->getWorldLocation();
    if ( self.LocationType == ObjectWorldLocation::OWL_Road ) {
        if ( other.LocationType == self.LocationType &&
             self.LocationRoad.RoadID == other.LocationRoad.RoadID ) {
            if ( self.VelocityZ >= -kLaneChangeLateralVelocity &&
                 self.VelocityZ <= kLaneChangeLateralVelocity &&
                 !other.isChangingLane() ) {
                if ( !self.overlapsLaterally( other ) ) {
                    return NO_Far;
                }
                if ( self.isObjectAhead( other ) ) {
                    if ( self.isMovingSameDirection( other ) ) {
                        const Matrix4x4f_t &selfWorld = pCar->getWorldMatrix();
                        const Matrix4x4f_t &otherWorld =
                            pObstacle->getWorldMatrix();
                        if ( otherWorld( 2, 2 ) * selfWorld( 2, 2 ) +
                                 otherWorld( 2, 0 ) * selfWorld( 2, 0 ) >=
                             kSameHeadingCos ) {
                            return NO_Far;
                        }
                    }

                    AiRoadMap::Road *pRoad = nullptr;
                    AiRoadShape *pShape = nullptr;
                    if ( self.overlapsLaterally( other ) ) {
                        pRoad = gpAI->getRoadMap().getRoad(
                            other.LocationRoad.RoadID );
                    }
                    if ( pRoad != nullptr ) {
                        pShape = pRoad->getShape();
                    }
                    if ( pShape != nullptr ) {
                        float speed = pShape->TotalLength * self.VelocityX;
                        float otherSpeed =
                            other.VelocityX * pShape->TotalLength;
                        if ( speed < 0.0f ) {
                            otherSpeed = 0.0f - otherSpeed;
                            speed = 0.0f - speed;
                        }
                        const float stopDistance =
                            pContext->computeBrakingDistance(
                                otherSpeed + speed, 0.0f, 0.0f );
                        const float gap =
                            pShape->TotalLength *
                            std::min( fabsf( self.LocationRoad.FractionMin -
                                             other.LocationRoad.FractionMax ),
                                      fabsf( self.LocationRoad.FractionMax -
                                             other.LocationRoad.FractionMin ) );
                        const float length = pCar->getDimensions().z();
                        const float secDist = ( kSecDistOverride >= 0.0f )
                                                  ? kSecDistOverride
                                                  : secDistance;
                        if ( stopDistance >= gap ) {
                            return NO_Blocked;
                        }
                        if ( ( ( 1.0f - secDist ) * kSecDistLowCoef +
                               kSecDistHighCoef * secDist ) *
                                 kContactTimePerLength * length >=
                             outDistance ) {
                            if ( secDist <= kMinPassableSecDist ||
                                 fabsf( otherSpeed ) <= kMinPassableSpeed ||
                                 bObstacleIsAI ) {
                                return NO_Blocked;
                            }
                            return NO_SlowPassable;
                        }
                    }
                }
            }
        } else if ( other.LocationType == ObjectWorldLocation::OWL_Road ) {
            return NO_Far;
        }
    }

    const Eigen::Vector4f delta =
        pObstacle->getWorldPosition() -
        pContext->getInfos().GroundMatrix.row( 3 ).transpose();
    const float distance = delta.norm();
    const Eigen::Vector4f &velocity = pCar->getLinearVelocity();
    const Eigen::Vector4f relativeVelocity =
        velocity - pObstacle->getLinearVelocity();
    const float closingSpeed = delta.dot( relativeVelocity ) / distance;
    float predictedDistance = closingSpeed * outDistance;
    if ( closingSpeed <= 0.0f ) {
        predictedDistance = distance;
    }
    if ( predictedDistance > pContext->computeBrakingDistance(
                                 pContext->getInfos().Speed, 0.0f, 0.0f ) +
                                 kBrakingMargin ) {
        return NO_Far;
    }
    return ( delta.dot( velocity ) > 0.0f ) ? NO_Blocked : NO_Far;
}

GameObject *AiVehNavTraffic::findLeader( float *pOutGap,
                                         float *pOutLeaderSpeed ) const {
    // FUN_0093aa80
    static constexpr int32_t kMaxCandidates = 128;
    static constexpr float kBoundsHeightMargin = 5.0f;  // DAT_00f41750
    static constexpr float kSegmentRadiusScale = 16.0f; // DAT_00f418d8
    static constexpr float kNoLeaderFraction = 2.0f;    // DAT_00f416b0

    const int32_t numPoints = path.getNumPoints();
    const int32_t first = std::max( path.getNearestIndex() - 1, 0 );
    if ( first + 1 >= numPoints ) {
        return nullptr;
    }

    Eigen::Vector4f boundsMin = path.getPoint( first ).Position;
    Eigen::Vector4f boundsMax = boundsMin;
    for ( int32_t i = first + 1; i < numPoints; i++ ) {
        const Eigen::Vector4f &position = path.getPoint( i ).Position;
        for ( int32_t axis = 0; axis < 3; axis++ ) {
            if ( boundsMin[axis] > position[axis] ) {
                boundsMin[axis] = position[axis];
            }
            if ( position[axis] > boundsMax[axis] ) {
                boundsMax[axis] = position[axis];
            }
        }
    }
    boundsMin.y() -= kBoundsHeightMargin;
    boundsMax.y() += kBoundsHeightMargin;

    CarState *pSelf = pVehicle->getCarState();
    const uint32_t selfID = pSelf->getID();
    GameObject *candidates[kMaxCandidates];
    int32_t numCandidates = 0;
    for ( GameObject *pObject = gGameObjectDico.getFirstObject();
          pObject != nullptr; pObject = pObject->getNextInDico() ) {
        if ( selfID == pObject->getID() || selfID == pObject->getParentID() ) {
            continue;
        }

        const Eigen::Vector4f position = pObject->getWorldPosition();
        const Eigen::Vector4f &dimensions = pObject->getDimensions();
        float radius = dimensions.z();
        if ( dimensions.y() > radius ) {
            radius = dimensions.y();
        }
        if ( dimensions.x() > radius ) {
            radius = dimensions.x();
        }
        radius *= 0.5f;

        if ( boundsMin.x() > position.x() + radius ||
             boundsMin.y() > position.y() + radius ||
             boundsMin.z() > position.z() + radius ||
             position.x() - radius > boundsMax.x() ||
             position.y() - radius > boundsMax.y() ||
             position.z() - radius > boundsMax.z() ) {
            continue;
        }

        const int32_t slot =
            ( numCandidates < 0 || numCandidates >= kMaxCandidates )
                ? 0
                : numCandidates;
        candidates[slot] = pObject;
        numCandidates++;
    }

    const float selfHalfWidth = pSelf->getDimensions().x() * 0.5f;
    float distance = -path.getProgress();
    for ( int32_t i = first; i < numPoints - 1; i++ ) {
        const NavPath::Point &pointA = path.getPoint( i );
        const NavPath::Point &pointB = path.getPoint( i + 1 );
        distance += pointA.Len;

        const float deltaX = pointB.Position.x() - pointA.Position.x();
        const float deltaZ = pointB.Position.z() - pointA.Position.z();
        const float length = sqrtf( deltaZ * deltaZ + deltaX * deltaX );
        const float invLength = 1.0f / length;
        const float directionX = invLength * deltaX;
        const float directionZ = invLength * deltaZ;

        AiOrientedBox2D segmentBox;
        segmentBox.CenterX =
            ( pointB.Position.x() + pointA.Position.x() ) * 0.5f;
        segmentBox.CenterZ =
            ( pointB.Position.z() + pointA.Position.z() ) * 0.5f;
        segmentBox.HalfX = selfHalfWidth;
        segmentBox.HalfZ = length * 0.5f;
        segmentBox.AxisXx = directionZ;
        segmentBox.AxisXz = -directionX;
        segmentBox.AxisZx = directionX;
        segmentBox.AxisZz = directionZ;
        const float radiusSq =
            segmentBox.HalfZ * segmentBox.HalfZ * kSegmentRadiusScale;

        GameObject *pLeader = nullptr;
        float bestFraction = kNoLeaderFraction;
        for ( int32_t c = 0; c < numCandidates; c++ ) {
            GameObject *pObject =
                candidates[( c < 0 || c >= kMaxCandidates ) ? 0 : c];
            const Eigen::Vector4f delta =
                pObject->getWorldPosition() - pointA.Position;
            if ( delta.squaredNorm() >= radiusSq ) {
                continue;
            }

            const Eigen::Vector4f &dimensions = pObject->getDimensions();
            const Matrix4x4f_t &matrix = pObject->getWorldMatrix();
            AiOrientedBox2D objectBox;
            objectBox.CenterX = matrix( 3, 0 );
            objectBox.CenterZ = matrix( 3, 2 );
            objectBox.HalfX = dimensions.x() * 0.5f;
            objectBox.HalfZ = dimensions.z() * 0.5f;
            objectBox.AxisXx = matrix( 0, 0 );
            objectBox.AxisXz = matrix( 0, 2 );
            objectBox.AxisZx = matrix( 2, 0 );
            objectBox.AxisZz = matrix( 2, 2 );
            if ( !TestOrientedBoxes2DOverlap( objectBox, segmentBox ) ) {
                continue;
            }

            const float fraction =
                std::clamp( Project( pObject->getWorldPosition(),
                                     pointA.Position, pointB.Position ),
                            0.0f, 1.0f );
            if ( i == first ) {
                const Eigen::Vector4f projected =
                    pointA.Position * ( 1.0f - fraction ) +
                    pointB.Position * fraction;
                const Eigen::Vector4f relative =
                    projected - pSelf->getWorldPosition();
                const Matrix4x4f_t &selfMatrix = pSelf->getWorldMatrix();
                if ( 0.0f > selfMatrix( 2, 0 ) * relative.x() +
                                selfMatrix( 2, 2 ) * relative.z() ) {
                    continue;
                }
            }
            if ( bestFraction > fraction ) {
                bestFraction = fraction;
                pLeader = pObject;
            }
        }

        if ( pLeader == nullptr ) {
            continue;
        }

        const Eigen::Vector4f segment = pointB.Position - pointA.Position;
        const float segmentLength = segment.norm();
        if ( pOutGap != nullptr ) {
            const float fraction =
                std::clamp( Project( pLeader->getWorldPosition(),
                                     pointA.Position, pointB.Position ),
                            0.0f, 1.0f );
            *pOutGap = segmentLength * fraction + distance;
        }
        if ( pOutLeaderSpeed != nullptr ) {
            *pOutLeaderSpeed =
                ( segment / segmentLength ).dot( pLeader->getLinearVelocity() );
        }
        return pLeader;
    }
    return nullptr;
}

void AiVehNavTraffic::computeFollowing( float &outFollowSpeed,
                                        float &outSpeedCoef,
                                        const bool bDebug ) {
    // FUN_0093d5b0
    static constexpr float kMinGapLow = 4.0f;            // DAT_00faa880
    static constexpr float kMinGapHigh = 10.0f;          // DAT_00faa878
    static constexpr float kMaxGapLow = 10.0f;           // DAT_00faa87c
    static constexpr float kMaxGapHigh = 40.0f;          // DAT_00faa874
    static constexpr float kMinFollowGap = 1.0f;         // DAT_00f4165c
    static constexpr float kTimeGapScale = 0.1f;         // DAT_00f41668
    static constexpr float kMinTimeGap = 0.1f;           // DAT_00f41668
    static constexpr float kMaxTimeGap = 10.0f;          // DAT_00f4166c
    static constexpr float kSlowLeaderSpeed = 10.0f;     // DAT_00f4166c
    static constexpr float kSlowLeaderScale = -0.1f;     // DAT_00f4188c
    static constexpr float kJunctionGapDistance = 50.0f; // DAT_00f41848
    static constexpr float kJunctionGapScale = 0.02f;    // DAT_00f417f0
    static constexpr float kQueueGapCoef = 15.0f;        // DAT_00f41700
    static constexpr float kQueueGapBase = 10.0f;        // DAT_00f4166c
    static constexpr float kFollowMargin = 1.0f;         // DAT_00f4165c
    static constexpr float kBrakingGapScale = 4.0f;      // DAT_00faa86c
    static constexpr float kMinBrakingGap = 50.0f;       // DAT_00faa870

    outFollowSpeed = kNoLeaderFollowSpeed;
    outSpeedCoef = 1.0f;

    float gap = 0.0f;
    float leaderSpeed = 0.0f;
    GameObject *pLeader = findLeader( &gap, &leaderSpeed );
    if ( pLeader != nullptr ) {
        const float selfLength = pVehicle->getCarState()->getDimensions().z();
        gap = gap - selfLength * 0.5f - pLeader->getDimensions().z() * 0.5f;
        if ( 0.0f > gap ) {
            gap = 0.0f;
        }

        const float coef = secDistCoef;
        const float minGapRaw =
            ( 1.0f - coef ) * kMinGapLow + kMinGapHigh * coef;
        const float maxGap = ( 1.0f - coef ) * kMaxGapLow + kMaxGapHigh * coef;
        float minGap =
            std::clamp( minGapRaw, kMinFollowGap, kNoLeaderFollowSpeed );
        const float timeGap = std::clamp(
            ( maxGap - minGapRaw ) * kTimeGapScale, kMinTimeGap, kMaxTimeGap );

        if ( !bLaneChangeLock ) {
            const float slowLeader = std::clamp(
                ( leaderSpeed - kSlowLeaderSpeed ) * kSlowLeaderScale, 0.0f,
                1.0f );
            const float farFromJunction =
                std::clamp( ( distanceToJunction - kJunctionGapDistance ) *
                                kJunctionGapScale,
                            0.0f, 1.0f );
            minGap += ( coef * kQueueGapCoef + kQueueGapBase ) *
                      ( farFromJunction * slowLeader );
        }

        const AiVehicleInfo &infos = pVehicle->getInfos();
        const float slope = ( infos.LastRoadPos.pRoad != nullptr )
                                ? infos.LastRoadPos.computeSlope()
                                : 0.0f;
        float brakingGap = kMinBrakingGap;
        const float brakingDistance = pVehicle->computeBrakingDistance(
                                          infos.Speed, leaderSpeed, slope ) *
                                      kBrakingGapScale;
        if ( brakingDistance > kMinBrakingGap ) {
            brakingGap = brakingDistance;
        }

        const float followDistance =
            timeGap * leaderSpeed + minGap + kFollowMargin;
        const float slowDownDistance = followDistance + brakingGap;
        if ( minGap > gap ) {
            outFollowSpeed = gap / minGap * leaderSpeed;
        } else if ( followDistance > gap ) {
            outFollowSpeed = leaderSpeed;
        } else if ( slowDownDistance > gap ) {
            const float ratio =
                std::clamp( ( gap - followDistance ) /
                                ( slowDownDistance - followDistance ),
                            0.0f, 1.0f );
            outSpeedCoef = std::clamp( 1.0f - ratio, 0.0f, 1.0f );
            outFollowSpeed = leaderSpeed;
        } else {
            outSpeedCoef = 0.0f;
        }

        if ( bDebug ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: DEBUG (follow gap bars)
        }
    }

    if ( 0.0f > outFollowSpeed ) {
        outFollowSpeed = 0.0f;
    }
}

void AiVehNavTraffic::updatePullOver() {
    // FUN_0093f040
    static constexpr int32_t kMaxObjects = 32;
    static constexpr float kDecisionPeriod = 5.0f;      // DAT_00faa918
    static constexpr float kMinCopDistance = 25.0f;     // DAT_00faa91c
    static constexpr float kScanDistance = 500.0f;      // DAT_00faa920
    static constexpr float kMinSpanLength = 10.0f;      // DAT_00f4166c
    static constexpr float kMaxSpanLength = 50.0f;      // DAT_00f41848
    static constexpr float kLaneChangeHandle = 1000.0f; // 0x447a0000

    const float now = gpTimer->GameTotalTime;
    if ( pullOverDecisionTime >= 0.0f &&
         kDecisionPeriod > now - pullOverDecisionTime ) {
        return;
    }

    AiVehicle *pOwner = pVehicle;
    const AiRoadIt &position = pOwner->getInfos().LastRoadPos;
    if ( pOwner->getCarState()->getWorldLocation().LocationType !=
             ObjectWorldLocation::OWL_Road ||
         position.pRoad == nullptr || bLaneChangeLock ) {
        return;
    }

    const AiRoadShape *pShape = position.pRoad->getShape();
    const int32_t lane = pOwner->computeCurrentLane();
    const float fraction = pShape->computeRoadFraction( position );
    const float invLength = 1.0f / pShape->TotalLength;
    const float minCopFraction = invLength * kMinCopDistance;
    const float scanFraction = invLength * kScanDistance;

    GameObject *objects[kMaxObjects];
    const int32_t numObjects = CollectRoadObjects(
        position.pRoad->ID, objects, kMaxObjects, fraction - scanFraction,
        fraction + scanFraction, -1, true );

    float playerFraction = -1.0f;
    float copFractions[kMaxObjects];
    float copVelocities[kMaxObjects];
    int32_t numCops = 0;
    for ( int32_t i = 0; i < numObjects; i++ ) {
        GameObject *pObject = objects[i];
        if ( pObject->getType() != 0 ) {
            continue;
        }
        const ObjectWorldLocation &location = pObject->getWorldLocation();
        if ( location.LocationType != ObjectWorldLocation::OWL_Road ) {
            continue;
        }

        CarState *pCar = static_cast<CarState *>( pObject );
        const float center = ( location.LocationRoad.FractionMax +
                               location.LocationRoad.FractionMin ) *
                             0.5f;
        if ( pCar->getSimulationType() ==
                 CarState::eSimulationType::CSST_Local &&
             pCar->getOwner() == CarState::eOwner::CSO_LocalPlayer ) {
            playerFraction = center;
            continue;
        }
        const AiVehicle *pAI =
            static_cast<const AiVehicle *>( pCar->getAIController() );
        if ( pAI != nullptr && pAI->isCopInPursuit() ) {
            copFractions[numCops] = center;
            copVelocities[numCops] = location.VelocityX;
            numCops++;
        }
    }

    bool bWanted = numCops != 0;
    if ( !bPullOver ) {
        for ( int32_t i = 0; i < numCops; i++ ) {
            if ( minCopFraction >= fabsf( fraction - copFractions[i] ) ) {
                bWanted = false;
                break;
            }
        }
    }

    bool bCheckApproach = bWanted;
    if ( playerFraction >= 0.0f && bWanted ) {
        bWanted = false;
        bCheckApproach = false;
        for ( int32_t i = 0; i < numCops; i++ ) {
            if ( ( fraction >= playerFraction &&
                   copFractions[i] >= fraction ) ||
                 ( playerFraction >= fraction &&
                   fraction >= copFractions[i] ) ) {
                bCheckApproach = true;
                break;
            }
        }
    }
    if ( bCheckApproach ) {
        bWanted = false;
        for ( int32_t i = 0; i < numCops; i++ ) {
            if ( ( fraction - copFractions[i] ) * copVelocities[i] >= 0.0f ) {
                bWanted = true;
                break;
            }
        }
    }

    if ( bWanted == bPullOver ) {
        return;
    }
    pullOverDecisionTime = now;
    bPullOver = bWanted;

    const float spanLength =
        std::clamp( pOwner->getInfos().Speed, kMinSpanLength, kMaxSpanLength );
    const NavPath::Point point = path.front();
    if ( point.Type != NavPath::PT_Road ) {
        return;
    }

    AiRoadVertexIt roadEnd;
    roadEnd.initAtRoadEnd( point.It.pRoad, !point.It.bWay );
    const Eigen::Vector4f delta =
        point.It.computePosition( -1 ) - roadEnd.getWaypoint().toVector4();
    if ( spanLength >= delta.norm() ) {
        return;
    }

    path.reset( true );
    const AiRoadIt from = point.It;
    AiRoadIt to = point.It;
    to.advanceByDistance( spanLength, false );
    const float coefFrom =
        bPullOver ? lateralOffsetCoefs[0] : lateralOffsetCoefs[1];
    const float coefTo =
        bPullOver ? lateralOffsetCoefs[1] : lateralOffsetCoefs[0];
    const float ratioFrom = computeLaneWidthRatio( from, lane, coefFrom );
    const float ratioTo = computeLaneWidthRatio( to, lane, coefTo );
    path.appendLaneChangeSpan( from, to, ratioFrom, ratioTo,
                               computeSpeedLimit( to ), kLaneChangeHandle );
    pathEnd = to;
    pathEndLane = lane;
}

bool AiVehNavTraffic::isBackOvertakeWanted() {
    // FUN_0093df90
    static constexpr float kMinBlockedTime = 5.0f;         // DAT_00faa830
    static constexpr float kJunctionVetoDistance = 150.0f; // DAT_00f4182c
    static constexpr float kReverseTimeHorizon = 10.0f;    // DAT_00faa838
    static constexpr float kLaneMargin = 5.0f;             // DAT_00faa83c
    static constexpr float kReverseEndDistance = -1.0f;    // 0xbf800000
    static constexpr float kPassTimeHorizon = 20.0f;       // DAT_00faa834
    static constexpr float kMaxOncomingQueue = 200.0f;     // DAT_00f4170c
    static constexpr float kPassLengthScale = 2.0f;        // DAT_00f416b0

    const float now = gpTimer->GameTotalTime;
    float blockedTime = 0.0f;
    if ( stoppedSince >= 0.0f ) {
        blockedTime = std::max( now - stoppedSince, 0.0f );
    }
    if ( kMinBlockedTime > blockedTime ) {
        return false;
    }

    AiVehicle *pOwner = pVehicle;
    CarState *pCarState = pOwner->getCarState();
    const ObjectWorldLocation &location = pCarState->getWorldLocation();
    if ( path.current().Type != NavPath::PT_Road ||
         location.LocationType != ObjectWorldLocation::OWL_Road ) {
        stoppedSince = now;
        return false;
    }

    const AiRoadIt &position = pOwner->getInfos().LastRoadPos;
    if ( kJunctionVetoDistance > distanceToJunction && bMustStopAtJunction ) {
        int32_t reason = WR_Continue;
        if ( mustWaitAtJunction( reason, false ) ) {
            stoppedSince = now;
            return false;
        }
    }

    float gap = 0.0f;
    float speed = 0.0f;
    FindObjectAheadOnRoad( location, pCarState->getID(), &gap, &speed );
    const float length = pCarState->getDimensions().z();
    const float reverseDistance = std::max( length - gap, 0.0f );
    const float passDistance = length * kPassLengthScale;
    const int32_t lane = pOwner->computeCurrentLane();

    if ( reverseDistance > 0.0f &&
         !pOwner->isLaneFree( lane, position, kReverseTimeHorizon,
                              0.0f - reverseDistance - kLaneMargin,
                              kReverseEndDistance ) ) {
        stoppedSince -= RandomRangeFloat( pOwner->getRandom(), 0.0f, 1.0f );
        return false;
    }

    const AiRoadMap::Entity *pEntity = position.getEntity();
    const int32_t numSameWayLanes =
        position.bWay ? pEntity->NumRightLanes : pEntity->NumLeftLanes;
    int32_t numOtherWayLanes =
        position.bWay ? pEntity->NumLeftLanes : pEntity->NumRightLanes;
    if ( position.pRoad->isOncomingOvertakeForbidden() ) {
        numOtherWayLanes = 0;
    }
    const int32_t laneUp =
        ( lane + 1 < numSameWayLanes + numOtherWayLanes ) ? lane + 1 : -1;
    const int32_t laneDown = ( lane - 1 >= 0 ) ? lane - 1 : -1;

    float queueDistance = 0.0f;
    float queueEnd = -1.0f;
    float gapStart = -1.0f;
    if ( !ComputeQueueAhead( position, lane, location.LocationRoad.FractionMin,
                             pCarState->getID(), &queueDistance, &queueEnd,
                             &gapStart ) ) {
        stoppedSince -= RandomRangeFloat( pOwner->getRandom(), 0.0f, 1.0f );
        return false;
    }

    const float passEnd = queueDistance + passDistance;
    const bool bUpFree =
        laneUp >= 0 && pOwner->isLaneFree( laneUp, position, kPassTimeHorizon,
                                           0.0f - kLaneMargin, passEnd );
    const bool bDownFree =
        laneDown >= 0 &&
        pOwner->isLaneFree( laneDown, position, kPassTimeHorizon,
                            0.0f - kLaneMargin, passEnd );

    int32_t passLane = -1;
    if ( bUpFree ) {
        passLane =
            ( bDownFree && laneUp >= numSameWayLanes ) ? laneDown : laneUp;
    } else if ( bDownFree ) {
        passLane = laneDown;
    } else {
        stoppedSince -= RandomRangeFloat( pOwner->getRandom(), 0.0f, 1.0f );
        return false;
    }
    if ( passLane >= numSameWayLanes && queueDistance > kMaxOncomingQueue ) {
        stoppedSince -= RandomRangeFloat( pOwner->getRandom(), 0.0f, 1.0f );
        return false;
    }

    bBackOvertaking = true;
    backOvertakeStart = pOwner->getInfos().GroundMatrix.row( 3 );
    backOvertakeTime = now;
    backOvertakeDistance = reverseDistance;
    targetLane = passLane;
    backOvertakePassDistance = passDistance;
    return true;
}

bool AiVehNavTraffic::isLaneChangeWanted() {
    // FUN_0093f980
    static constexpr float kChangeSpeedMin = 1.0f;              // DAT_00faa848
    static constexpr float kChangeSpeedMax = 50.0f;             // DAT_00faa844
    static constexpr float kChangeDistanceMin = 10.0f;          // DAT_00faa850
    static constexpr float kChangeDistanceMax = 200.0f;         // DAT_00faa84c
    static constexpr float kSpanHandleDistance = 50.0f;         // DAT_00faa840
    static constexpr float kMinJunctionTime = 5.0f;             // DAT_00f41750
    static constexpr float kOvertakeJunctionDistance = 50.0f;   // DAT_00f41848
    static constexpr float kKeepRightJunctionDistance = 150.0f; // DAT_00f4182c
    static constexpr float kFarLeaderGap = 150.0f;              // DAT_00f4182c
    static constexpr float kFasterLaneMargin = 3.0f;            // DAT_00f416b4
    static constexpr float kBehindTime = 3.0f;                  // DAT_00f416b4
    static constexpr float kBehindSpeedRatio = 0.95f;           // DAT_00f4195c
    static constexpr float kNoGap = 1000000.0f;                 // DAT_00f41814
    static constexpr float kWantScore = 0.5f;                   // DAT_00f41674
    static constexpr float kStrongWantScore = 0.85f;            // DAT_00f41c24

    if ( nextFrom.pRoad == nullptr || path.getNumPoints() == 0 ) {
        return false;
    }
    const NavPath::Point &point = path.current();
    if ( point.Type != NavPath::PT_Road ) {
        return false;
    }

    AiVehicle *pOwner = pVehicle;
    AiVehicleInfo &infos = pOwner->getInfos();
    const AiRoadIt &position = infos.LastRoadPos;
    if ( position.pRoad == nullptr || bLaneChangeLock || bZeroDeltaTime ) {
        return false;
    }

    const int32_t lane = pOwner->computeCurrentLane();
    int32_t newLane = lane;
    const float speed = infos.Speed;
    const float changeDistance =
        std::clamp( ( kChangeDistanceMax - kChangeDistanceMin ) *
                            ( speed - kChangeSpeedMin ) /
                            ( kChangeSpeedMax - kChangeSpeedMin ) +
                        kChangeDistanceMin,
                    kChangeDistanceMin, kChangeDistanceMax );
    float junctionTime = distanceToJunction;
    if ( speed > 1.0f ) {
        junctionTime = distanceToJunction / speed;
    }

    const AiRoadMap::Entity *pEntity = position.getEntity();
    const int32_t numLanes =
        position.bWay ? pEntity->NumRightLanes : pEntity->NumLeftLanes;
    if ( ( lane >= numLanes && numLanes > 0 ) ||
         ( bPullOver && lane > 0 &&
           distanceToJunction > kOvertakeJunctionDistance ) ) {
        // Lane ends (or overtaking done): move back one lane
        if ( !checkLaneChangeGap( lane, lane - 1, position.bWay,
                                  changeDistance ) ) {
            return false;
        }
        newLane = lane - 1;
    } else if ( kMinJunctionTime > junctionTime ||
                changeDistance * 2.0f > distanceToJunction ) {
        // Close to the junction: head for the turn lane
        if ( lane == turnLane ) {
            return false;
        }
        int32_t stepLane = turnLane;
        if ( turnLane - lane > 1 ) {
            stepLane = lane + 1;
        } else if ( turnLane - lane < -1 ) {
            stepLane = lane - 1;
        }
        if ( changeDistance + 1.0f >= distanceToJunction ) {
            moveToTurnLane( position, lane );
            return false;
        }
        if ( !checkLaneChangeGap( lane, stepLane, position.bWay,
                                  changeDistance ) ) {
            return false;
        }
        newLane = stepLane;
    } else {
        // Overtake a slow leader / keep right
        float speedLimit =
            std::max( targetSpeed * 2.0f, infos.MaxSpeed * 0.1f );
        float pointSpeed = path.computePointSpeed( point );
        if ( pointSpeed >= 10000.0f ) { // FUN_0095e370: no limit ⇒ -1
            pointSpeed = -1.0f;
        }
        if ( !( pointSpeed > speedLimit ) ) {
            speedLimit = pointSpeed;
        }

        float leaderGap = 0.0f;
        float leaderSpeed = 0.0f;
        GameObject *pLeader = pOwner->findObjectAheadInLane(
            position, lane, &leaderGap, &leaderSpeed );
        const float gapFactor =
            std::clamp( ( leaderGap - 350.0f ) * -0.004f, 0.0f, 1.0f );
        const float slowFactor = std::clamp(
            ( leaderSpeed / speedLimit - 1.0f ) * -1.4285714f, 0.0f, 1.0f );
        const float junctionFactor =
            std::clamp( ( junctionTime - 10.0f ) * -0.11111111f, 0.0f, 1.0f );
        const float speedFactor =
            std::clamp( leaderSpeed * -0.033333335f, 0.0f, 1.0f );
        const float score = slowFactor * gapFactor * ( 1.0f - junctionFactor ) *
                            ( 1.0f - speedFactor );

        bool bDecided = false;
        if ( pLeader != nullptr && score >= kWantScore ) {
            int32_t candidates[2] = { -1, -1 };
            int32_t numCandidates = 0;
            if ( lane + 1 < numLanes ) {
                candidates[numCandidates++] = lane + 1;
            }
            if ( lane > 0 && score >= kStrongWantScore ) {
                candidates[numCandidates++] = lane - 1;
            }
            for ( int32_t i = 0; i < numCandidates; i++ ) {
                float otherGap = 0.0f;
                float otherSpeed = 0.0f;
                if ( pOwner->findObjectAheadInLane( position, candidates[i],
                                                    &otherGap,
                                                    &otherSpeed ) != nullptr &&
                     !( otherSpeed > leaderSpeed + kFasterLaneMargin ) ) {
                    continue;
                }
                if ( !checkLaneChangeGap( lane, candidates[i], position.bWay,
                                          changeDistance ) ) {
                    continue;
                }
                newLane = candidates[i];
                bDecided = true;
                break;
            }
        }

        if ( !( bDecided && newLane != lane ) && lane > 0 &&
             distanceToJunction > kKeepRightJunctionDistance &&
             speedLimit > 0.0f ) {
            CarState *pCarState = pOwner->getCarState();
            const ObjectWorldLocation &location = pCarState->getWorldLocation();
            const float center = ( location.LocationRoad.FractionMax +
                                   location.LocationRoad.FractionMin ) *
                                 0.5f;
            float behindSpeed = 0.0f;
            float behindGap = kNoGap;
            GameObject *pBehind = FindObjectInLaneNear(
                location.LocationRoad.RoadID, center, lane, position.bWay,
                false, pCarState->getID() );
            if ( pBehind != nullptr && pBehind->getType() == 0 &&
                 pBehind->getWorldLocation().LocationType ==
                     ObjectWorldLocation::OWL_Road ) {
                const ObjectWorldLocation &other = pBehind->getWorldLocation();
                const AiRoadShape *pShape = position.pRoad->getShape();
                const float delta =
                    center - ( other.LocationRoad.FractionMax +
                               other.LocationRoad.FractionMin ) *
                                 0.5f;
                behindGap = fabsf( delta ) * pShape->TotalLength;
                behindSpeed = other.VelocityX * pShape->TotalLength;
            }
            if ( behindSpeed * kBehindTime > behindGap &&
                 behindSpeed - speed * kBehindSpeedRatio > 0.0f ) {
                float rightGap = 0.0f;
                float rightSpeed = 0.0f;
                const bool bRightLeader =
                    pOwner->findObjectAheadInLane(
                        position, lane - 1, &rightGap, &rightSpeed ) != nullptr;
                if ( ( !bRightLeader ||
                       ( rightSpeed >= speedLimit && rightGap > 10.0f ) ||
                       rightGap > kFarLeaderGap ) &&
                     checkLaneChangeGap( lane, lane - 1, position.bWay,
                                         changeDistance ) ) {
                    newLane = lane - 1;
                }
            }
        }
    }

    if ( newLane == lane ) {
        return false;
    }

    AiRoadIt end = position;
    if ( !end.advanceByDistance( changeDistance, true ) ) {
        return false;
    }
    path.reset( true );
    const float speedLimit = computeSpeedLimit( end );
    const float widthRatioTo =
        computeLaneWidthRatio( end, newLane, kAutoLaneWidthCoef );
    const float widthRatioFrom =
        computeLaneWidthRatio( position, lane, kAutoLaneWidthCoef );
    path.appendLaneChangeSpan( position, end, widthRatioFrom, widthRatioTo,
                               speedLimit, kSpanHandleDistance );
    pathEnd = end;
    pathEndLane = newLane;
    bLaneChangeLock = true;
    targetLane = newLane;

    if ( turnLane != newLane &&
         end.pRoad->getNumLanesAt( end.bWay ) > newLane ) {
        AiRoadMap::Junction *pTurnJunction =
            end.bWay ? end.pRoad->pJunctionEnd : end.pRoad->pJunctionBegin;
        bool bSplit = false;
        if ( computeTurnLane( pTurnJunction, end.pRoad, nextFrom.pRoad, newLane,
                              bSplit ) == targetLane ) {
            turnLane = targetLane;
            nextLane = computeNextLane( segTo, nextFrom, targetLane );
        }
    }
    return true;
}

bool AiVehNavTraffic::abortBlockedLaneChange() {
    // FUN_0093e400
    static constexpr bool kAbortBlockedLaneChange = true; // DAT_00faa828
    static constexpr float kClearanceMargin = 2.0f;       // DAT_00f416b0
    static constexpr float kReplanDistance = 100.0f;
    static constexpr float kReplanHandleDistance = 1000.0f;

    if ( !kAbortBlockedLaneChange || getCurrentState() != NS_LaneChange ) {
        return false;
    }

    AiVehicle *pOwner = pVehicle;
    CarState *pCarState = pOwner->getCarState();
    const ObjectWorldLocation &location = pCarState->getWorldLocation();
    if ( location.LocationType != ObjectWorldLocation::OWL_Road ) {
        return false;
    }
    const AiRoadIt position = pOwner->getInfos().LastRoadPos;
    if ( position.pRoad == nullptr ) {
        return false;
    }

    const float lateralMin = location.LocationRoad.LateralMin;
    const float lateralMax = location.LocationRoad.LateralMax;
    position.pRoad->getShape();
    const AiRoadProfile *pProfile = position.getEntity()->pProfile;
    const int32_t laneMin =
        pProfile->computeLaneFromWidthRatio( position.bWay, lateralMin );
    const int32_t laneMax =
        pProfile->computeLaneFromWidthRatio( position.bWay, lateralMax );
    if ( laneMax == targetLane && laneMin == targetLane ) {
        return false;
    }

    const float halfSpan =
        pCarState->getDimensions().z() * 0.5f + kClearanceMargin;
    if ( pOwner->isLaneFree( targetLane, position, 0.0f, 0.0f - halfSpan,
                             halfSpan ) ) {
        return false;
    }

    AiRoadIt end = position;
    end.advanceByDistance( kReplanDistance, false );
    const float widthRatioTo = ComputeLaneCenterWidthRatio( position, laneMin );
    path.reset( true );
    const float speedLimit = computeSpeedLimit( end );
    path.appendLaneChangeSpan( position, end,
                               ( lateralMin + lateralMax ) * 0.5f, widthRatioTo,
                               speedLimit, kReplanHandleDistance );
    pathEndLane = laneMin;
    pathEnd = end;
    return true;
}

bool AiVehNavTraffic::checkLaneChangeGap( const int32_t laneFrom,
                                          const int32_t laneTo,
                                          const bool bWay,
                                          float distance ) const {
    // FUN_00939750
    static constexpr float kMinDistanceFromEntry = 10.0f; // DAT_00faa934
    static constexpr float kRoadEndMargin = 1.0f;         // DAT_00faa930
    static constexpr float kMinSpeed = 10.0f;             // DAT_00faa92c
    static constexpr float kMarginBehind = 10.0f;         // DAT_00f4166c
    static constexpr float kBehindSpeedScale = 1.25f;     // DAT_00f41870
    static constexpr float kOwnSpeedScale = 0.75f;        // DAT_00f416a4
    static constexpr float kAheadSpeedScale = 0.9f;       // DAT_00f416a0

    CarState *pCarState = pContextVehicle->getCarState();
    const ObjectWorldLocation &location = pCarState->getWorldLocation();
    if ( location.LocationType != ObjectWorldLocation::OWL_Road ) {
        return false;
    }

    float front = 0.0f;
    float rear = 0.0f;
    if ( bWay ) {
        rear = location.LocationRoad.FractionMin;
        front = location.LocationRoad.FractionMax;
    } else {
        distance = 0.0f - distance;
        front = location.LocationRoad.FractionMin;
        rear = location.LocationRoad.FractionMax;
    }

    AiRoadMap::Road *pRoad =
        gpAI->getRoadMap().getRoad( location.LocationRoad.RoadID );
    if ( pRoad == nullptr ) {
        return false;
    }
    const AiRoadShape *pShape = pRoad->getShape();
    const float fromEntry = bWay ? rear : 1.0f - rear;
    const float toEnd = bWay ? 1.0f - front : front;
    if ( kMinDistanceFromEntry > pShape->TotalLength * fromEntry ||
         kRoadEndMargin + distance > pShape->TotalLength * toEnd ) {
        return false;
    }

    float speed = location.VelocityX;
    if ( kMinSpeed > fabsf( location.VelocityX * pShape->TotalLength ) ) {
        speed = bWay ? kMinSpeed / pShape->TotalLength
                     : -1.0f / pShape->TotalLength * kMinSpeed;
    }

    const float middle = ( rear + front ) * 0.5f;
    const uint32_t roadID = location.LocationRoad.RoadID;
    const uint32_t selfID = pCarState->getID();
    GameObject *pAhead =
        FindObjectInLaneNear( roadID, middle, laneFrom, bWay, true, selfID );
    GameObject *pTargetBehind =
        FindObjectInLaneNear( roadID, middle, laneTo, bWay, false, selfID );
    GameObject *pTargetAhead =
        FindObjectInLaneNear( roadID, middle, laneTo, bWay, true, selfID );

    const float invLength = 1.0f / pShape->TotalLength;
    const float changeTime = invLength * distance / speed;
    bool bAheadClear = true;
    bool bTargetBehindClear = true;
    bool bTargetAheadClear = true;
    if ( pAhead != nullptr ) {
        const ObjectWorldLocation &other = pAhead->getWorldLocation();
        const float otherRear = bWay ? other.LocationRoad.FractionMin
                                     : other.LocationRoad.FractionMax;
        bAheadClear =
            !IntervalsMeetInWindow( front, otherRear, 0.0f, changeTime * 0.5f,
                                    invLength, other.VelocityX, speed );
    }
    if ( pTargetBehind != nullptr ) {
        const ObjectWorldLocation &other = pTargetBehind->getWorldLocation();
        const float otherFront = bWay ? other.LocationRoad.FractionMax
                                      : other.LocationRoad.FractionMin;
        bTargetBehindClear = !IntervalsMeetInWindow(
            rear, otherFront, 0.0f, changeTime,
            kMarginBehind / pShape->TotalLength,
            other.VelocityX * kBehindSpeedScale, speed * kOwnSpeedScale );
    }
    if ( pTargetAhead != nullptr ) {
        const ObjectWorldLocation &other = pTargetAhead->getWorldLocation();
        const float otherRear = bWay ? other.LocationRoad.FractionMin
                                     : other.LocationRoad.FractionMax;
        bTargetAheadClear =
            !IntervalsMeetInWindow( front, otherRear, 0.0f, changeTime,
                                    kMarginBehind / pShape->TotalLength,
                                    other.VelocityX * kAheadSpeedScale, speed );
    }
    // Retail draws the three tests when given a debug render list
    return bAheadClear && bTargetBehindClear && bTargetAheadClear;
}

bool AiVehNavTraffic::isOnLane( const int32_t lane ) const {
    // FUN_0093c720
    const ObjectWorldLocation &location =
        pVehicle->getCarState()->getWorldLocation();
    if ( location.LocationType != ObjectWorldLocation::OWL_Road ) {
        return false;
    }

    AiRoadMap::Road *pRoad =
        gpAI->getRoadMap().getRoad( location.LocationRoad.RoadID );
    if ( pRoad == nullptr ) {
        return false;
    }
    AiRoadIt it;
    pRoad->getShape()->computeIt( location.LocationRoad.FractionMin, it );
    if ( it.pRoad == nullptr ) {
        return false;
    }

    const AiRoadProfile *pProfile = it.getEntity()->pProfile;
    const bool bWay = location.bWay != 0;
    const int32_t laneMin = pProfile->computeLaneFromWidthRatio(
        bWay, location.LocationRoad.LateralMin );
    const int32_t laneMax = pProfile->computeLaneFromWidthRatio(
        bWay, location.LocationRoad.LateralMax );
    return laneMin == laneMax && laneMin == lane;
}

bool AiVehNavTraffic::mustWaitAtJunction( int32_t &outReason,
                                          const bool bUpdate ) {
    // FUN_0093c850
    static constexpr int32_t kMaxJunctionRoads = 32;
    static constexpr float kDestinationClearance = 10.0f; // DAT_00f4166c
    static constexpr float kStopSignMaxSpeed = 1.1f;      // DAT_00f4174c
    static constexpr float kNoDistance = 1000.0f;         // DAT_00f41718
    static constexpr float kCourtesyMargin = 2.0f;        // DAT_00f416b0
    static constexpr float kCourtesyTieMargin = -2.0f;    // DAT_00f417d0

    outReason = WR_Continue;
    bool bClear = true;
    bool bCheckJunctionOccupied = true;
    if ( !bMustStopAtJunction ) {
        return false;
    }

    AiVehicle *pOwner = pVehicle;
    CarState *pCarState = pOwner->getCarState();
    if ( bUpdate && FindObjectAheadOnRoad( pCarState->getWorldLocation(),
                                           pCarState->getID(), nullptr,
                                           nullptr ) != nullptr ) {
        outReason = WR_VehicleAhead;
        bClear = false;
    } else {
        float gap = -1.0f;
        float speed = -1.0f;
        if ( pOwner->findObjectAheadInLane( nextFrom, nextLane, &gap,
                                            &speed ) != nullptr &&
             gap + speed < kDestinationClearance ) {
            outReason = WR_DestinationOccupied;
            bClear = false;
        }
    }

    const NavPath::Point &point = path.current();
    if ( point.Type != NavPath::PT_Road ) {
        return bClear;
    }

    const AiRoadMap::Road *pRoadTo = point.It.pRoad;
    const bool bWayTo = point.It.bWay;
    const uint8_t device = pRoadTo->getControlDevice( bWayTo );
    const int32_t numRoads = pJunction->NumRoads;
    const int32_t turnType =
        pJunction->computeTurnType( junctionSlotIn, junctionSlotOut, nullptr );
    switch ( device ) {
    case 0:
        bCheckJunctionOccupied = false;
        break;
    case 1:
        if ( pOwner->getInfos().Speed > kStopSignMaxSpeed ) {
            return true;
        }
        break;
    case 3: {
        uint32_t signal = GetTrafficSignalState(
            pRoadTo->getSignalSynchro( bWayTo ), gGSAI.getSignalSyncTime() );
        if ( ( turnType & 4 ) != 0 ) {
            signal = kSignalGreen;
        }
        if ( ( signal & ( kSignalRed | kSignalOrange ) ) != 0 ) {
            bClear = false;
            outReason = WR_RedLight;
        }
        bCheckJunctionOccupied = false;
        break;
    }
    default:
        break;
    }

    if ( !bClear ) {
        return true;
    }

    auto WrapIndex = []( const int32_t index ) {
        return ( index < 0 || index >= kMaxJunctionRoads ) ? 0 : index;
    };

    int32_t priorityRoads[kMaxJunctionRoads];
    int32_t numPriorityRoads = 0;
    int32_t courtesyRoads[kMaxJunctionRoads];
    int32_t numCourtesyRoads = 0;
    const bool bRightTurn = ( ( turnType >> 2 ) & 1 ) != 0;
    for ( int32_t i = 0; i < numRoads; i++ ) {
        if ( i == junctionSlotIn ) {
            continue;
        }
        const AiRoadMap::Road *pRoad = pJunction->getRoad( i );
        const bool bAtEnd = ( pJunction == pRoad->pJunctionEnd );
        const int32_t priority = CompareJunctionPriority(
            pRoadTo, bWayTo, pRoad, bAtEnd, bRightTurn );
        if ( priority < 0 ) {
            priorityRoads[WrapIndex( numPriorityRoads )] = i;
            numPriorityRoads++;
        } else if ( priority == 0 ) {
            courtesyRoads[WrapIndex( numCourtesyRoads )] = i;
            numCourtesyRoads++;
        }
    }

    for ( int32_t i = 0; i < numPriorityRoads; i++ ) {
        const AiRoadMap::Road *pRoad =
            pJunction->getRoad( priorityRoads[WrapIndex( i )] );
        if ( FindObjectApproachingJunction( pJunction->ID, pRoad->ID, nullptr,
                                            nullptr ) ) {
            outReason = WR_Priority;
            return true;
        }
    }

    for ( int32_t i = 0; i < numCourtesyRoads; i++ ) {
        const AiRoadMap::Road *pRoad =
            pJunction->getRoad( courtesyRoads[WrapIndex( i )] );
        GameObject *pOther = nullptr;
        float otherDistance = kNoDistance;
        if ( !FindObjectApproachingJunction( pJunction->ID, pRoad->ID, &pOther,
                                             &otherDistance ) ||
             pOther->getType() != 0 ) {
            continue;
        }

        const float margin = distanceToJunction -
                             pCarState->getDimensions().z() * 0.5f -
                             otherDistance;
        if ( ( margin > kCourtesyMargin && distanceToJunction >= 0.0f ) ||
             ( margin > kCourtesyTieMargin && distanceToJunction >= 0.0f &&
               pOther->getID() < pCarState->getID() ) ) {
            outReason = WR_Courtesy;
            return true;
        }
    }

    if ( bCheckJunctionOccupied &&
         pOwner->isJunctionOccupied( pJunction->ID ) ) {
        outReason = WR_JunctionOccupied;
        return true;
    }
    return false;
}

bool AiVehNavTraffic::isBackOvertakePossible() {
    // FUN_0093e610
    static constexpr float kLaneWidthDefault =
        -2.0f; // 0xc0000000 (out of [-1, 1])
    static constexpr float kLaneChangeHandle = 1000.0f; // 0x447a0000
    static constexpr float kExitSpanLength = 20.0f;     // 0x41a00000

    const NavPath::Point &point = path.current();
    if ( point.Type != NavPath::PT_Road ) {
        return false;
    }
    AiRoadMap::Road *pRoad = point.It.pRoad;
    if ( pRoad == nullptr ) {
        return false;
    }

    AiVehicle *pOwner = pVehicle;
    CarState *pCarState = pOwner->getCarState();
    gGameObjectDico.locate( pCarState, pRoad, false );
    const ObjectLocationRoad &road = pCarState->getWorldLocation().LocationRoad;
    const float fractionMin = road.FractionMin;
    const float fractionMax = road.FractionMax;

    AiRoadIt from;
    const AiRoadShape *pShape = pRoad->getShape();
    if ( pShape == nullptr ) {
        return false;
    }
    pShape->computeIt( ( fractionMax + fractionMin ) * 0.5f, from );
    const bool bPathWay = path.current().It.bWay;
    if ( bPathWay != from.bWay ) {
        from.T = 1.0f - from.T;
    }
    from.bWay = bPathWay;

    const int32_t lane = pOwner->computeCurrentLane();
    const int32_t passLane = targetLane;
    AiRoadIt to = from;
    to.advanceByDistance( backOvertakePassDistance, true );

    path.reset( true );
    const float ratioTo =
        computeLaneWidthRatio( to, passLane, kLaneWidthDefault );
    const float speedLimit = computeSpeedLimit( to );
    const float ratioFrom =
        computeLaneWidthRatio( from, lane, kLaneWidthDefault );
    path.appendLaneChangeSpan( from, to, ratioFrom, ratioTo, speedLimit,
                               kLaneChangeHandle );
    pathEnd = to;
    pathEndLane = passLane;

    AiRoadIt exit = to;
    if ( exit.advanceByDistance( kExitSpanLength, true ) ) {
        path.appendSpan( to, exit, ratioTo, ratioTo, &pathEnd,
                         computeSpeedLimit( exit ) );
    }

    bLaneChangeLock = true;
    targetLane = passLane;
    if ( turnLane == passLane ) {
        return true;
    }

    const AiRoadMap::Entity *pEntity = from.getEntity();
    const int32_t numSameWayLanes =
        bPathWay ? pEntity->NumRightLanes : pEntity->NumLeftLanes;
    if ( passLane >= numSameWayLanes ) {
        return true;
    }

    AiRoadMap::Junction *pTurnJunction =
        to.bWay ? to.pRoad->pJunctionEnd : to.pRoad->pJunctionBegin;
    bool bSplit = false;
    if ( computeTurnLane( pTurnJunction, to.pRoad, nextFrom.pRoad, passLane,
                          bSplit ) == targetLane ) {
        turnLane = targetLane;
        nextLane = computeNextLane( segTo, nextFrom, targetLane );
    }
    return true;
}

float AiVehNavTraffic::computeUrgentTurnSteer( const bool bDebug ) {
    // FUN_0093cca0
    static constexpr float kMaxObstacleDrift = 0.34906587f;   // DAT_00faa85c
    static constexpr float kObstacleSideAngle = -0.02617994f; // DAT_00faa858
    static constexpr float kVelocitySideAngle = -0.02617994f; // DAT_00faa854
    static constexpr float kLaneCheckTimeHorizon = 2.0f;      // DAT_00faa860
    static constexpr float kLaneCheckDistanceMin = -3.0f;     // DAT_00faa868
    static constexpr float kLaneCheckDistanceMax = 100.0f;    // DAT_00faa864

    AiVehicle *pOwner = pVehicle;
    CarState *pCarState = pOwner->getCarState();
    const Matrix4x4f_t &selfMatrix = pCarState->getWorldMatrix();
    if ( bDebug ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: DEBUG (FUN_009baa70 arc)
    }

    GameObject *pObstacle = nullptr;
    pOwner->findClosestObstacle( &pObstacle );
    if ( pObstacle == nullptr ) {
        return 0.0f;
    }

    const Matrix4x4f_t &obstacleMatrix = pObstacle->getWorldMatrix();
    const Eigen::Vector4f &obstacleVelocity = pObstacle->getLinearVelocity();

    // Obstacle heading vs its own velocity (drift)
    const float obstacleDrift =
        -atan2f( obstacleVelocity.z() * obstacleMatrix( 2, 0 ) -
                     obstacleMatrix( 2, 2 ) * obstacleVelocity.x(),
                 obstacleVelocity.x() * obstacleMatrix( 2, 0 ) +
                     obstacleMatrix( 2, 2 ) * obstacleVelocity.z() );

    // Obstacle bearing in the vehicle frame
    const Eigen::Vector4f toObstacle =
        obstacleMatrix.row( 3 ).transpose() - selfMatrix.row( 3 ).transpose();
    const float obstacleBearing =
        -atan2f( toObstacle.z() * selfMatrix( 2, 0 ) -
                     toObstacle.x() * selfMatrix( 2, 2 ),
                 toObstacle.x() * selfMatrix( 2, 0 ) +
                     toObstacle.z() * selfMatrix( 2, 2 ) );

    // Vehicle bearing relative to the obstacle velocity
    const Eigen::Vector4f fromObstacle = -toObstacle;
    const float velocityBearing =
        -atan2f( fromObstacle.x() * obstacleVelocity.z() -
                     fromObstacle.z() * obstacleVelocity.x(),
                 fromObstacle.x() * obstacleVelocity.x() +
                     fromObstacle.z() * obstacleVelocity.z() );

    if ( bDebug ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: DEBUG (obstacle/velocity lines)
    }

    if ( fabsf( obstacleDrift ) > kMaxObstacleDrift ) {
        return 0.0f;
    }
    const bool bObstacleOnLeft = obstacleBearing >= kObstacleSideAngle;
    const bool bVelocityOnLeft = kVelocitySideAngle > velocityBearing;
    if ( bObstacleOnLeft != bVelocityOnLeft ) {
        return 0.0f;
    }

    if ( bDebug ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: DEBUG (avoidance quad)
    }

    if ( pCarState->getWorldLocation().LocationType ==
             ObjectWorldLocation::OWL_Road &&
         pOwner->getInfos().LastRoadPos.pRoad != nullptr ) {
        const AiRoadIt &position = pOwner->getInfos().LastRoadPos;
        const int32_t currentLane = pOwner->computeCurrentLane();
        const AiRoadProfile *pProfile = position.getEntity()->pProfile;
        const int32_t numLanesAbove =
            static_cast<int32_t>( pProfile->NumLeftLanes +
                                  pProfile->NumRightLanes ) -
            currentLane;

        int32_t numFreeBelow = 0;
        for ( int32_t lane = currentLane - 1; numFreeBelow < currentLane;
              lane-- ) {
            if ( !pOwner->isLaneFree( lane, position, kLaneCheckTimeHorizon,
                                      kLaneCheckDistanceMin,
                                      kLaneCheckDistanceMax ) ) {
                break;
            }
            numFreeBelow++;
        }
        int32_t numFreeAbove = 0;
        while ( numFreeAbove < numLanesAbove ) {
            if ( !pOwner->isLaneFree( currentLane + numFreeAbove + 1, position,
                                      kLaneCheckTimeHorizon,
                                      kLaneCheckDistanceMin,
                                      kLaneCheckDistanceMax ) ) {
                break;
            }
            numFreeAbove++;
        }
        (void)numFreeBelow;
        (void)numFreeAbove;
    }

    if ( bDebug ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: DEBUG (steer triangle)
    }
    return bObstacleOnLeft ? -1.0f : 1.0f;
}

void AiVehNavTraffic::moveToTurnLane( const AiRoadIt &position,
                                      const int32_t lane ) {
    // FUN_0093f680
    static constexpr float kStraightAngle = 180.0f; // DAT_00f417fc
    static constexpr float kNoAngle = 360.0f;       // DAT_00f41800

    AiVehicle *pOwner = pVehicle;
    if ( path.getNumPoints() == 0 &&
         pOwner->getInfos().LastRoadPos.pRoad == nullptr ) {
        return;
    }

    const bool bWay = position.bWay;
    AiRoadMap::Road *pRoad = position.pRoad;
    AiRoadMap::Junction *pTurnJunction =
        bWay ? pRoad->pJunctionEnd : pRoad->pJunctionBegin;

    AiRoadMap::JunctionSplit split;
    pTurnJunction->classifySplit( split, pRoad->ID );
    int32_t exitIndex = -1;
    switch ( split.Type ) {
    case AiRoadMap::JunctionSplit::JST_SymmetricMerge:
    case AiRoadMap::JunctionSplit::JST_Merge:
        exitIndex = split.SingleIndex;
        break;
    case AiRoadMap::JunctionSplit::JST_SymmetricSplit:
        exitIndex = ( ( pRoad->getNumLanesAt( bWay ) + 1 ) / 2 > lane )
                        ? split.PairIndexB
                        : split.PairIndexA;
        break;
    case AiRoadMap::JunctionSplit::JST_None: {
        const uint8_t turnMask = GetLaneTurnMask( pRoad, bWay, lane );
        const int32_t indexIn = pTurnJunction->getRoadIndex( pRoad );
        const int32_t numRoads = pTurnJunction->NumRoads;
        float bestDeviation = kNoAngle;
        int32_t fallbackIndex = -1;
        for ( int32_t i = 0; i < numRoads; i++ ) {
            float angle = 0.0f;
            const int32_t turnType =
                pTurnJunction->computeTurnType( indexIn, i, &angle );
            const float deviation = fabsf( angle - kStraightAngle );
            const AiRoadMap::Road *pExit = pTurnJunction->getRoad( i );
            if ( pExit->getNumLanesAt( pExit->pJunctionBegin ==
                                       pTurnJunction ) == 0 ) {
                continue;
            }
            fallbackIndex = i;
            if ( ( angle == 0.0f || ( turnMask & turnType ) != 0 ) &&
                 bestDeviation > deviation ) {
                exitIndex = i;
                bestDeviation = deviation;
            }
        }
        if ( exitIndex < 0 ) {
            exitIndex = fallbackIndex;
        }
        break;
    }
    default:
        exitIndex = ( lane == 0 ) ? split.PairIndexB : split.PairIndexA;
        break;
    }

    if ( exitIndex < 0 ) {
        exitIndex = 0;
    }
    AiVehController *pController = pOwner->getController();
    pController->forceNextRoad( pTurnJunction->getRoad( exitIndex )->ID );
    pController->getSecond( nextFrom, nextTo );

    junctionSlotOut = ( nextFrom.pRoad == nullptr )
                          ? -1
                          : pJunction->getRoadIndex( nextFrom.pRoad );
    turnLane = lane;
    nextLane = computeNextLane( position, nextFrom, lane );

    pathEnd = ( path.getNumPoints() > 0 ) ? path.current().It : AiRoadIt();
    if ( pathEnd.pRoad == nullptr ) {
        pathEnd = pOwner->getInfos().LastRoadPos;
    }
    pathEndLane = lane;
    path.reset( true );
    updateRoute();
}

bool AiVehNavTraffic::isLaneFree( const int32_t lane,
                                  const AiRoadIt &position,
                                  const float distance,
                                  const float rangeMin,
                                  const float rangeMax ) const {
    // FUN_00926ec0
    return pVehicle->isLaneFree( lane, position, distance, rangeMin, rangeMax );
}

int32_t AiVehNavTraffic::computeCurrentLane() const {
    // FUN_009269c0
    return pVehicle->computeCurrentLane();
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
        updatePullOver();
        if ( abortBlockedLaneChange() ) {
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
        updatePullOver();
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
        updatePullOver();
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
        updatePullOver();
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
            checkObstacle( pContextVehicle, obstacleContactTime, secDistCoef );
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
            pOwner->getCarState()->getPhysicsVehicle()->setAutoReverseAllowed(
                true );
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
        if ( checkObstacle( pContextVehicle, obstacleContactTime,
                            secDistCoef ) == NO_Blocked ) {
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
            pOwner->getCarState()->getPhysicsVehicle()->setAutoReverseAllowed(
                false );
            bBackOvertaking = false;
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
            pOwner->getCarState()->getPhysicsVehicle()->setAutoReverseAllowed(
                false );
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
