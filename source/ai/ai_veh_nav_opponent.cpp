#include "shared.h"
#include "ai_veh_nav_opponent.h"

#include "ai/ai_config.h"
#include "ai/ai_pool.h"
#include "ai/ai_road_shape.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "core/maths_helper.h"
#include "core/random/bcpl.h"
#include "game/gm_base.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "gs_timer.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"

static constexpr float kNoValue = -1000.0f;             // DAT_00f419c4
static constexpr float kRouteTolerance = 1.1f;          // DAT_00f4174c
static constexpr float kDefaultSpeedCap = 100.0f;       // DAT_00f41708
static constexpr float kGridThrottleLow = 0.1f;         // DAT_00f41668
static constexpr float kGridThrottleHigh = 0.9f;        // DAT_00f416a0
static constexpr float kGridIdleThrottle = 0.3f;        // DAT_00f416bc
static constexpr float kStraightEndDistance = 300.0f;   // DAT_00f4183c
static constexpr float kStraightEndStep = 10.0f;        // DAT_00f4166c
static constexpr float kRouteEndTolerance = 0.01f;      // 0x3c23d70a
static constexpr float kBezierMinDistance = 10.0f;      // DAT_00f4166c
static constexpr float kIntervalMinDistance = 3.0f;     // DAT_00f416b4
static constexpr float kPostFinishStopDelay = 2.0f;     // DAT_00f416b0
static constexpr float kPostFinishOffRoadSpeed = 5.0f;  // DAT_00f41750
static constexpr float kStuckDefaultTimeout = 10.0f;    // DAT_00f4166c
static constexpr float kStuckRiderDownTimeout = -10.0f; // DAT_00f418a8
static constexpr float kStuckNotMovingTimeout = 7.0f;   // DAT_00f41844
static constexpr float kStuckSlowTimeout = 3.0f;
static constexpr float kStuckFastTimeout = 1.0f;
static constexpr float kStuckMinMove = 1.0f;
static constexpr float kStuckOffRoadSpeed = 4.0f; // DAT_00f418d4
static constexpr float kStuckOffRoadSlowSpeed = 2.0f;
static constexpr float kStuckGraceTime = 10.0f;
static constexpr float kTeleportSpeed = 10.0f;       // DAT_00f4166c
static constexpr float kTeleportPhantomAlpha = 0.5f; // DAT_00f41674
static constexpr int32_t kMaxCheckpointMissTicks = 10;
static constexpr float kCheckpointStepBack = -0.5f;   // 0xbf000000
static constexpr float kCheckpointWidthFactor = 1.2f; // DAT_00f41dc4

// FUN_00409440 + CarState slot compare
static bool IsRoundRobinTurn( const AiVehicle *pVehicle ) {
    return gpAI->getAIPool()->getNextOpponentIndex() ==
           pVehicle->getCarStateIndex();
}

static bool gbForceOpponentRebuild = false; // DAT_010e73d1

static float ComputeRoadFraction( const AiRoadIt &it ) {
    return it.pRoad->getShape()->computeRoadFraction( it );
}

static const AiRoadMap::Junction *GetEndJunction( const AiRoadIt &it ) {
    return it.bWay ? it.pRoad->pJunctionEnd : it.pRoad->pJunctionBegin;
}

static float GetEffectiveWidthRatio( const OptPathElem &elem ) {
    return ( elem.OptWidthRatio != OptPath::kUnsetWidthRatio )
               ? elem.OptWidthRatio
               : elem.WidthRatio;
}

static void ComputeOuterLaneRatios( const AiRoadProfile *pProfile,
                                    float &outLow,
                                    float &outHigh ) {
    // FUN_00931010
    float start = 0.0f;
    float end = 0.0f;
    if ( pProfile->NumLeftLanes != 0 ) {
        pProfile->computeLaneWidthRatios( 0, false, start, end );
        outLow = std::min( start, end );
    } else {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_0092ece0
        outLow = 0.0f;
    }
    if ( pProfile->NumRightLanes != 0 ) {
        pProfile->computeLaneWidthRatios( 0, true, start, end );
        outHigh = std::max( start, end );
    } else {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_0092ece0
        outHigh = 0.0f;
    }
}

static float ComputeHeadingYaw( const Matrix4x4f_t &matrix,
                                const Eigen::Vector4f &direction ) {
    return -atan2f(
        matrix( 2, 0 ) * direction.z() - matrix( 2, 2 ) * direction.x(),
        matrix( 2, 2 ) * direction.z() + matrix( 2, 0 ) * direction.x() );
}

static void TeleportToRoad( AiVehicle *pVehicle,
                            const AiRoadIt &it,
                            const float widthRatio,
                            const bool bPhantom ) {
    // FUN_00938d20
    if ( pVehicle == nullptr ) {
        return;
    }
    if ( it.pRoad != nullptr ) {
        PhysicsVehicle *pPhysics = pVehicle->getCarState()->getPhysicsVehicle();
        Matrix4x4f_t matrix = pVehicle->getInfos().GroundMatrix;
        float lateral = 0.0f;
        float height = 0.0f;
        it.getEntity()->pProfile->computeWidthOffset( widthRatio, lateral,
                                                      height );
        const Eigen::Vector4f position =
            it.computeOffsetPosition( lateral, height );
        const Eigen::Vector4f direction = it.computeDirection();
        matrix.topRows<3>() = Matrix4x4f_t::Identity().topRows<3>();
        MatrixRotateY( matrix, ComputeHeadingYaw( matrix, direction ) );
        matrix.row( 3 ) = position.transpose();
        {
            PhysicsLock lock;
            pPhysics->resetToTransform( matrix, false );
            pPhysics->setVelocity(
                Eigen::Vector3f( 0.0f, 0.0f, kTeleportSpeed ), false, -1 );
        }
        OTDU_UNIMPLEMENTED_ONCE; // TODO: engine +0x94 = 2
        if ( bPhantom && pPhysics->getPhantomMode() != 5 ) {
            pPhysics->requestPhantomMode( 2, 1.0f );
            VehicleBase *pDisplay =
                pVehicle->getCarState()->getVehicleInstance();
            if ( pDisplay != nullptr ) {
                pDisplay->setAlpha( kTeleportPhantomAlpha );
                pVehicle->setFlashFade( true );
            }
        }
        pVehicle->setGroundMatrixFromCenter( matrix );
    }
    AiVehIndDPC &dpc = pVehicle->getDPC();
    if ( dpc.NumOutsideWanted > 0 ) {
        dpc.NumOutsideWanted--;
    }
}

void AiVehNavOpponent::Situation::init() {
    // FUN_00953a10
    bLineMismatch = false;
    bBike = false;
    RouteFrom = AiRoadIt();
    RouteTo = AiRoadIt();
    NextFrom = AiRoadIt();
    NextTo = AiRoadIt();
    RoadIt = AiRoadIt();
    LastRouteTo = AiRoadIt();
    LastNextTo = AiRoadIt();
    LastValidRoadIt = AiRoadIt();
    U = kNoValue;
    CarLengthU = kNoValue;
    V = kNoValue;
    CarWidthRatio = kNoValue;
    LastV = kNoValue;
    RouteFromU = kNoValue;
    RouteToU = kNoValue;
    NextFromU = kNoValue;
    NextToU = kNoValue;
    LastPointWidthRatio = kNoValue;
    LastWidthRatio = kNoValue;
    TolRoute = kNoValue;
    TolNext = kNoValue;
    LineDistance = 0.0f;
    CurSpeed = 0.0f;
    Velocity = Eigen::Vector4f::Zero();
    Loc.LocationType = ObjectWorldLocation::OWL_None;
    Loc.bNearJunction = 0;
    GroundMatrix = Matrix4x4f_t::Identity();
}

void AiVehNavOpponent::Situation::setup( AiVehicle *pNewVehicle,
                                         OptPath *pNewLine,
                                         NavPathOpponent *pNewPath ) {
    // FUN_00953bb0
    pVehicle = pNewVehicle;
    pOptPath = pNewLine;
    pPath = pNewPath;
    init();
}

void AiVehNavOpponent::Situation::update() {
    // FUN_00955640
    CarState *pCarState = pVehicle->getCarState();
    bBike = pCarState != nullptr && pCarState->getBody()->bIsBike;
    updateLocation();
    if ( pOptPath->getCount() != 0 ) {
        pOptPath->updateCurrentIndex( &LineDistance );
    }
    copyGroundMatrix();
    updateVelocity();
    updateRoute();
    refreshLastPathPoint();
    refreshLast();
    checkLineMismatch();
}

void AiVehNavOpponent::Situation::updateLocation() {
    // FUN_00955380
    AiVehicleInfo &infos = pVehicle->getInfos();
    if ( LastValidRoadIt.pRoad == nullptr &&
         infos.LastRoadPos.pRoad != nullptr ) {
        LastValidRoadIt = infos.LastRoadPos;
    }
    CarState *pCarState = pVehicle->getCarState();
    Loc = pCarState->getWorldLocation();
    LocationFlags =
        Loc.LocationType | ( Loc.bWay << 2 ) | ( Loc.bNearJunction << 3 );
    LocationID = Loc.LocationRoad.RoadID;
    if ( isBikeRiderDown() ) {
        return;
    }

    if ( Loc.LocationType == ObjectWorldLocation::OWL_Road ) {
        U = ( Loc.LocationRoad.FractionMin + Loc.LocationRoad.FractionMax ) *
            0.5f;
        pJunction = nullptr;
        CarLengthU = TolRoute * pCarState->getDimensions().z();
        V = ( Loc.LocationRoad.LateralMin + Loc.LocationRoad.LateralMax ) *
            0.5f;
        AiRoadMap::Road *pRoad =
            gpAI->getRoadMap().getRoad( Loc.LocationRoad.RoadID );
        if ( pRoad == nullptr ) {
            return;
        }
        pRoad->getShape()->computeIt( U, RoadIt );
        RoadIt.setWay( Loc.bWay );
        if ( RoadIt.pRoad == nullptr ) {
            return;
        }
        LastValidRoadIt = RoadIt;
        infos.LastRoadPos = RoadIt;
        LastV = V;
        const AiRoadProfile *pProfile = RoadIt.getEntity()->pProfile;
        CarWidthRatio = pCarState->getDimensions().x() /
                        ( pProfile->InnerWidth * 0.5f ) * kRouteTolerance;
        return;
    }

    RoadIt = AiRoadIt();
    U = kNoValue;
    CarLengthU = kNoValue;
    V = kNoValue;
    CarWidthRatio = kNoValue;
    pJunction = ( Loc.LocationType == ObjectWorldLocation::OWL_Junction )
                    ? gpAI->getRoadMap().getJunction( Loc.LocationOther.RoadID )
                    : nullptr;
}

void AiVehNavOpponent::Situation::updateVelocity() {
    // FUN_009541a0
    Velocity = pVehicle->getCarState()->getLinearVelocity();
    CurSpeed = sqrtf( Dot4( Velocity, Velocity ) );
}

void AiVehNavOpponent::Situation::updateRoute() {
    // FUN_00955120
    AiVehController *pController = pVehicle->getController();
    const int32_t count = pController->getCount();
    if ( count != 0 ) {
        pController->getAt( 0, RouteFrom, RouteTo );
        RouteFromU = ComputeRoadFraction( RouteFrom );
        RouteToU = ComputeRoadFraction( RouteTo );
        if ( count > 1 ) {
            pController->getAt( 1, NextFrom, NextTo );
            NextFromU = ComputeRoadFraction( NextFrom );
            NextToU = ComputeRoadFraction( NextTo );
        } else {
            NextFrom = AiRoadIt();
            NextTo = AiRoadIt();
            NextFromU = kNoValue;
            NextToU = kNoValue;
        }
    } else {
        RouteFrom = AiRoadIt();
        RouteTo = AiRoadIt();
        NextFrom = AiRoadIt();
        NextTo = AiRoadIt();
        RouteFromU = kNoValue;
        RouteToU = kNoValue;
        NextFromU = kNoValue;
        NextToU = kNoValue;
    }

    if ( RouteFrom.pRoad == nullptr ) {
        TolRoute = -1.0f;
        TolNext = -1.0f;
        return;
    }
    if ( pVehicle->getBehaviourType() == 0 &&
         pVehicle->getControllerType() == 1 && count == 2 ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: cop route check (FUN_00963070)
    }
    LastRouteTo = RouteTo;
    TolRoute = kRouteTolerance / RouteFrom.pRoad->getShape()->TotalLength;
    if ( NextFrom.pRoad != nullptr ) {
        LastNextTo = NextTo;
        TolNext = kRouteTolerance / NextFrom.pRoad->getShape()->TotalLength;
    } else {
        TolNext = -1.0f;
    }
}

void AiVehNavOpponent::Situation::updateLineIndex() {
    // FUN_00954180
    if ( pOptPath->getCount() != 0 ) {
        pOptPath->updateCurrentIndex( &LineDistance );
    }
}

void AiVehNavOpponent::Situation::copyGroundMatrix() {
    // FUN_009539f0
    GroundMatrix = pVehicle->getInfos().GroundMatrix;
    Position = GroundMatrix.row( 3 ).transpose();
}

AiVehNavOpponent::AiVehNavOpponent()
    : AiVehNavigation(),
      AiFsm(),
      pOwner( nullptr ),
      prevEntry(),
      prevExit(),
      curEntry(),
      curExit(),
      curTarget(),
      nextEntry(),
      nextExit(),
      checkpointIt(),
      pCurEntryJunction( nullptr ),
      pCurExitJunction( nullptr ),
      curRouteRoadID( 0 ),
      lastBuiltPointId( -1 ),
      now( 0.0f ),
      stuckRefTime( 0.0f ),
      graceTime( 0.0f ),
      stuckRefPos( Eigen::Vector4f::Zero() ),
      stateTime( 0.0f ),
      checkpointMissTicks( 0 ),
      speedCap( kDefaultSpeedCap ),
      speedCap2( kDefaultSpeedCap ),
      path(),
      lineA(),
      lineB(),
      targetDirection( kUnitXVector ),
      targetSpeed( kDefaultSpeedCap ),
      brakeDistFromMax( 0.0f ),
      situation(),
      bGridRevHold( true ),
      gridThrottleLow( 1.0f ),
      gridThrottleHigh( 0.0f ),
      lastLineRebuildTime( 0.0f ),
      bObstacleAhead( false ),
      stopTime( -1.0f ),
      finishSpeed( -1.0f ),
      obstacles(),
      builder() {
    // FUN_00954f90
}

void AiVehNavOpponent::attach( AiVehicle *pVehicle ) {
    // FUN_00954ce0
    pOwner = pVehicle;
    path.initialize( pVehicle );
    lineA.attach( this, pVehicle, &situation );
    lineB.attach( this, pVehicle, &situation );
    builder.attach( this, pVehicle, &lineA, &path, &situation );
    obstacles.attach( this, pVehicle, &lineA, &path, &situation );
    situation.setup( pVehicle, &lineA, &path );
    brakeDistFromMax = pVehicle->computeBrakingDistance(
        pVehicle->getInfos().MaxSpeed, 0.0f, 0.0f );
    reset( true );
}

void AiVehNavOpponent::reset( const bool bFull ) {
    // FUN_00954a30
    targetDirection = kUnitXVector;
    targetSpeed = kDefaultSpeedCap;
    speedCap = kDefaultSpeedCap;
    speedCap2 = kDefaultSpeedCap;
    brakeDistFromMax = 0.0f;
    pCurEntryJunction = nullptr;
    pCurExitJunction = nullptr;
    curRouteRoadID = 0;
    lastBuiltPointId = -1;
    now = 0.0f;
    stuckRefTime = 0.0f;
    graceTime = 0.0f;
    stateTime = 0.0f;
    checkpointMissTicks = 0;
    checkpointIt = AiRoadIt();
    stopTime = -1.0f;
    finishSpeed = -1.0f;
    gridThrottleLow = 1.0f;
    gridThrottleHigh = 0.0f;
    bGridRevHold = true;
    lastLineRebuildTime = 0.0f;
    bObstacleAhead = false;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: 3-point turn block (nav+0x760)
    if ( !bFull ) {
        return;
    }

    prevEntry = AiRoadIt();
    prevExit = AiRoadIt();
    curEntry = AiRoadIt();
    curExit = AiRoadIt();
    curTarget = AiRoadIt();
    nextEntry = AiRoadIt();
    nextExit = AiRoadIt();

    stackSize = 0;
    if ( pOwner->getBehaviourType() == 0 ) {
        currentState = 6;
    } else {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: CarState+0xdd autopilot (state 0xd)
        currentState = NS_Idle;
    }
    bTransitionPending = false;
    process( FE_Enter );
    path.reset( true );
    lineA.reset( true );
    lineB.reset( true );
    obstacles.resetSlots();
    builder.reset( false );
}

void AiVehNavOpponent::tick( const float deltaTime, const float totalTime ) {
    // FUN_00957420
    situation.update();
    now = totalTime;
    speedCap = kDefaultSpeedCap;
    process( FE_Update );

    if ( path.getNumPoints() == 0 || path.getTotalLength() <= 1.0f ||
         getCurrentState() == NS_Stopped ) {
        gGameObjectDico.locate( pOwner->getCarState(), nullptr, false );
        targetDirection = pOwner->getInfos().GroundMatrix.row( 2 );
        targetSpeed = 0.0f;
        situation.update();
        return;
    }

    path.updateProgress( pOwner->getInfos().GroundMatrix );
    path.computeTarget( targetDirection, targetSpeed, -1.0f, 1.0f );
    if ( pOwner->computeBrakingDistance( situation.CurSpeed, 0.0f, 0.0f ) >
         path.getTotalLength() ) {
        speedCap = 0.0f;
    }

    const eAiVehicleCategory category = pOwner->getCategory();
    if ( category != eAiVehicleCategory::AVC_Bot &&
         category != eAiVehicleCategory::AVC_Cop ) {
        float rubberBand = 1.0f;
        if ( getCurrentState() == NS_Race && gpActiveGameMode != nullptr &&
             gpActiveGameMode->getCurrentGameMode() == eGameMode::GM_Mission ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: GMMission ranking rubber band
        }
        targetSpeed = gAIConfig.OpponentDifficulty * targetSpeed * rubberBand;
    }
    if ( targetSpeed > speedCap ) {
        targetSpeed = speedCap;
    }
    situation.update();
}

void AiVehNavOpponent::detach() {
    // FUN_009549e0
    path.release();
    lineA.release();
    lineB.release();
    obstacles.detach();
    builder.detach();
}

bool AiVehNavOpponent::getTarget( Eigen::Vector4f &outDirection,
                                  float &outSpeed ) const {
    // FUN_00953bd0
    outDirection = targetDirection;
    outSpeed = targetSpeed;
    return path.getTotalLength() > 1.0f && pOwner != nullptr &&
           pOwner->getInfos().bValid;
}

void AiVehNavOpponent::getHeightAndTwist( float &outHeight,
                                          float &outTwist ) const {
    // FUN_00923420
    outTwist = 0.0f;
    PhysicsVehicle *pPhysics = pOwner->getCarState()->getPhysicsVehicle();
    if ( pPhysics == nullptr ) {
        outHeight = pOwner->getInfos().GroundMatrix( 3, 1 );
        return;
    }
    const Matrix4x4f_t chassis = pPhysics->getChassisWorldMatrix();
    outHeight = chassis( 3, 1 ) -
                chassis( 1, 1 ) * pPhysics->computeGroundToOriginOffset();
}

void AiVehNavOpponent::resetStuckRefs() {
    stuckRefPos = situation.Position;
    stateTime = now;
    stuckRefTime = now;
    graceTime = now;
}

bool AiVehNavOpponent::isMyRoundRobinTurn() const {
    return IsRoundRobinTurn( pOwner );
}

void AiVehNavOpponent::updateRouteIts() {
    // FUN_00954470
    AiVehController *pController = pOwner->getController();
    AiRoadIt from;
    AiRoadIt to;
    if ( pController->getCount() != 0 ) {
        pController->getAt( 0, from, to );
    }
    if ( pController->getCount() == 0 || from.pRoad == nullptr ) {
        prevEntry = curEntry;
        prevExit = curExit;
        nextEntry = AiRoadIt();
        nextExit = AiRoadIt();
        curTarget = AiRoadIt();
        return;
    }

    if ( from.pRoad->ID != curRouteRoadID ) {
        prevEntry = curEntry;
        prevExit = curExit;
        curRouteRoadID = from.pRoad->ID;
        curEntry.setRoadEntry( from.pRoad, from.bWay );
        curExit.setRoadExit( to.pRoad, to.bWay );
        if ( curEntry.pRoad != nullptr ) {
            pCurEntryJunction = GetEndJunction( curEntry );
        }
        if ( curExit.pRoad != nullptr ) {
            pCurExitJunction = GetEndJunction( curExit );
        }
    }
    curTarget = curExit.isSamePosition( to, 0.0f ) ? AiRoadIt() : to;

    if ( pController->getCount() > 1 ) {
        pController->getAt( 1, from, to );
        if ( from.pRoad != nullptr && to.pRoad != nullptr ) {
            if ( from.pRoad->ID == curRouteRoadID ) {
                nextEntry = AiRoadIt();
                nextExit = AiRoadIt();
                return;
            }
            if ( nextEntry.pRoad != nullptr && from.pRoad == nextEntry.pRoad ) {
                return;
            }
            nextEntry.setRoadEntry( from.pRoad, from.bWay );
            nextExit.setRoadExit( to.pRoad, to.bWay );
            return;
        }
    }
    nextEntry = AiRoadIt();
    nextExit = AiRoadIt();
}

void AiVehNavOpponent::advanceRoute() {
    // FUN_00955ee0
    AiVehController *pController = pOwner->getController();
    pController->popFront();
    lineA.onRoutePopped();
    situation.updateRoute();
    situation.checkLineMismatch();
    if ( pController->getCount() == 1 && pOwner->getBehaviourType() == 0 &&
         getCurrentState() != 6 ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: police behaviour state 3 test
    }
    updateRouteIts();
}

void AiVehNavOpponent::appendStraightEnd() {
    // FUN_00955b00
    if ( path.getNumPoints() >= path.getCapacity() - 3 ||
         lineA.getCount() >= lineA.getCapacity() - 3 ||
         path.getNumPoints() < 2 ) {
        return;
    }

    const NavPath::Point &last = path.back();
    const NavPath::Point &previous = path.getPoint( path.getNumPoints() - 2 );
    Eigen::Vector4f direction = last.Position - previous.Position;
    NormalizeVector4( direction );

    NavPath::Point point;
    point.Position = last.Position + direction * kStraightEndDistance;
    point.Position.w() = 1.0f;
    point.WidthRatio = OptPath::kUnsetWidthRatio;
    point.RoadSpeedLimit = kDefaultSpeedCap;
    point.Type = NavPath::PT_End;

    auto pushEndElem = [this]( const Eigen::Vector4f &position ) {
        if ( lineA.isFull() ) {
            return;
        }
        OptPathElem elem;
        elem.Position = position;
        elem.Type = OptPathElem::ET_Empty;
        elem.WidthRatio = OptPath::kUnsetWidthRatio;
        elem.OptWidthRatio = OptPath::kUnsetWidthRatio;
        lineA.push( elem, false, -1 );
    };

    path.push( point, false );
    pushEndElem( point.Position );
    point.Position += direction * kStraightEndStep;
    path.push( point, false );
    pushEndElem( point.Position );
    lastBuiltPointId = path.back().Id;
    situation.refreshLastPathPoint();
    situation.refreshLast();
}

void AiVehNavOpponent::rebuildPath() {
    // FUN_00956460
    if ( lineA.getCount() != 0 ) {
        lineA.updateCurrentIndex( &situation.LineDistance );
    }
    const int32_t current = lineA.getCurrentIndex();
    if ( current <= 0 ) {
        return;
    }

    const OptPathElem &a = lineA.at( current - 1 );
    const OptPathElem &b = lineA.at( current );
    float fractionA = -1.0f;
    float fractionB = -1.0f;
    float widthA = OptPath::kUnsetWidthRatio;
    float widthB = OptPath::kUnsetWidthRatio;
    AiRoadIt it;
    int32_t index = current;
    if ( a.Type == OptPathElem::ET_Road && b.Type == OptPathElem::ET_Road &&
         situation.RoadIt.pRoad != nullptr ) {
        fractionB = ComputeRoadFraction( b.It );
        fractionA = ComputeRoadFraction( a.It );
        widthB = GetEffectiveWidthRatio( b );
        widthA = GetEffectiveWidthRatio( a );
    } else {
        for ( ; index < lineA.getCount(); index++ ) {
            if ( lineA.at( index ).Type == OptPathElem::ET_Road ) {
                it = lineA.at( index ).It;
                break;
            }
        }
        if ( it.pRoad == nullptr ) {
            return;
        }
    }

    path.reset( true );
    lineA.setField0x128( -1 );
    path.setBatchAppend( true );
    if ( fractionB == -1.0f ) {
        path.pushRoadPoint( it, 0.0f, kDefaultSpeedCap, false );
        lineA.setResampleCursor( index );
    } else {
        const float distance = fabsf( fractionB - fractionA );
        if ( distance > situation.TolRoute * kBezierMinDistance ) {
            OptSpanKeys keys;
            keys.IndexA = current - 1;
            keys.IndexB = current;
            keys.KeyA = Eigen::Vector2f( fractionA, widthA );
            keys.KeyB = Eigen::Vector2f( fractionB, widthB );
            lineA.computeSpanKeys( keys );
            if ( path.appendRoadBezier( a.It, b.It, keys.KeyA, keys.Handle1,
                                        keys.Handle2, keys.KeyB, a.Position,
                                        b.Position ) ) {
                lineA.at( current ).TurningSpeed =
                    pOwner->computeTurningSpeed( path.back().Radius );
            }
        } else if ( distance > situation.TolRoute * kIntervalMinDistance ) {
            AiRoadIt from = a.It;
            if ( a.It.bWay != b.It.bWay ) {
                from.setWay( b.It.bWay );
            }
            path.appendRoadInterval( from, b.It, widthA, widthB,
                                     kDefaultSpeedCap, nullptr );
        } else {
            path.pushRoadPoint( b.It, widthB, kDefaultSpeedCap, false );
        }
        lineA.setField0x128( -1 );
        lineA.setResampleCursor( current );
    }

    lastLineRebuildTime = gpTimer->GameTotalTime;
    bObstacleAhead = false;
    path.setBatchAppend( false );
    path.recomputeAllRadii();
    situation.refreshLastPathPoint();
}

void AiVehNavOpponent::finaliseLine() {
    // FUN_00954d90
    lineB.reset( true );
    lineB.setBatchAppend( true );
    for ( int32_t i = 0; i < lineA.getCount(); i++ ) {
        OptPathElem elem = lineA.at( i );
        if ( fabsf( elem.OptWidthRatio ) <= 1.0f ) {
            elem.Position = elem.OptPosition;
            elem.Position.w() = 1.0f;
            elem.WidthRatio = elem.OptWidthRatio;
        }
        lineB.push( elem, true, elem.RouteIndex );
    }
    lineB.setBatchAppend( false );
    lineB.recomputeAllRadii();
    lineA.recomputeAllRadii();
    for ( int32_t i = 0; i < lineA.getCount(); i++ ) {
        OptPathElem &elem = lineA.at( i );
        elem.TurningSpeed = pOwner->computeTurningSpeed( elem.Radius );
    }
    for ( int32_t i = 0; i < path.getNumPoints(); i++ ) {
        const float crestLimit = path.computeCrestSpeedLimit( i );
        path.getPoint( i ).CrestSpeedLimit =
            ( crestLimit >= 0.0f )
                ? std::max( pOwner->getDPC().AvoidJumpFactor * crestLimit,
                            1.0f )
                : -1.0f;
    }
}

void AiVehNavOpponent::resetLine() {
    // FUN_00954980
    lineA.reset( true );
    builder.reset( false );
    situation.refreshLast();
}

void AiVehNavOpponent::resetPath() {
    // FUN_009549b0
    path.reset( true );
    lineA.setField0x128( -1 );
    lineA.setResampleCursor( 0 );
    situation.refreshLastPathPoint();
}

bool AiVehNavOpponent::handle( const int32_t event, const int32_t state ) {
    // FUN_00958f70
    switch ( state ) {
    case NS_Idle:
    case NS_Idle2:
    case NS_Idle3:
        return event <= FE_Exit;

    case NS_Relocate:
        if ( event == FE_Enter ) {
            enterRelocate();
            return true;
        }
        if ( event == FE_Update ) {
            updateRelocate();
            return true;
        }
        return event == FE_Exit;

    case NS_Grid:
        if ( event == FE_Enter ) {
            bGridRevHold = true;
            gridThrottleLow = kGridThrottleLow;
            gridThrottleHigh = kGridThrottleHigh;
            situation.copyGroundMatrix();
            resetStuckRefs();
            brakeDistFromMax =
                pOwner->computeBrakingDistance( kDefaultSpeedCap, 0.0f, 0.0f );
            if ( pOwner->getLocomotion() != nullptr ) {
                pOwner->getLocomotion()->setManualInputs(
                    kGridIdleThrottle, 0.0f, 0.0f, 0.0f, 0.0f );
            }
            return true;
        }
        if ( event == FE_Update ) {
            updateGrid();
            return true;
        }
        if ( event == FE_Exit ) {
            situation.copyGroundMatrix();
            resetStuckRefs();
            bGridRevHold = false;
            return true;
        }
        return false;

    case NS_Race:
        if ( event == FE_Enter ) {
            pOwner->getInfos().bHandbrake = false;
            lastLineRebuildTime = gpTimer->GameTotalTime;
            checkpointMissTicks = 0;
            return true;
        }
        if ( event == FE_Update ) {
            updateRace();
            return true;
        }
        return event == FE_Exit;

    case NS_Stopped:
        if ( event == FE_Enter ) {
            gGameObjectDico.locate( pOwner->getCarState(), nullptr, false );
            targetDirection = pOwner->getInfos().GroundMatrix.row( 2 );
            speedCap = 0.0f;
            targetSpeed = 0.0f;
            pOwner->getInfos().bHandbrake = true;
            AiVehLocomotion *pLoco = pOwner->getLocomotion();
            if ( pLoco != nullptr ) {
                pLoco->setDrivenByNavigation( false );
                float steer = RandomUnit( pOwner->getRandom() );
                if ( pOwner->getRandom().getRandomUint32() < 0x7fffffffu ) {
                    steer = -steer;
                }
                pLoco->setManualInputs( 0.0f, 1.0f, 1.0f, steer, 0.0f );
            }
            return true;
        }
        return event <= FE_Exit;

    case NS_RaceOver:
        if ( event == FE_Update &&
             pOwner->getCategory() != eAiVehicleCategory::AVC_Network ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: engine +0x94 (2 => 0)
        }
        return event <= FE_Exit;

    case NS_LineEnded:
        if ( event == FE_Enter ) {
            resetLine();
            bGridRevHold = true;
            resetPath();
            situation.bLineMismatch = false;
            return true;
        }
        if ( event == FE_Update ) {
            resetLine();
            resetPath();
            situation.copyGroundMatrix();
            resetStuckRefs();
            const int32_t count = pOwner->getController()->getCount();
            if ( count != 0 && situation.LastValidRoadIt.pRoad != nullptr ) {
                if ( pOwner->getCategory() ==
                     eAiVehicleCategory::AVC_Opponent ) {
                    goTo( NS_PostFinish );
                } else if ( pOwner->getControllerType() == 1 ) {
                    goTo( count > 1 ? NS_BotCruise : NS_BotFollow );
                } else {
                    goTo( NS_BotCruise );
                }
            }
            return true;
        }
        if ( event == FE_Exit ) {
            exitLineEnded();
            return true;
        }
        return false;

    case NS_PostFinish:
        if ( event == FE_Enter ) {
            enterPostFinish();
            return true;
        }
        if ( event == FE_Update ) {
            updatePostFinish();
            return true;
        }
        return event == FE_Exit;

    case NS_Halt:
        if ( event == FE_Enter ) {
            resetLine();
            resetPath();
            return true;
        }
        return event <= FE_Exit;

    case NS_BotCruise:
        if ( event == FE_Enter ) {
            pOwner->getInfos().bHandbrake = false;
            lastLineRebuildTime = gpTimer->GameTotalTime;
            return true;
        }
        if ( event == FE_Update ) {
            updateBotCruise();
            return true;
        }
        return event == FE_Exit;

    case NS_BotFollow:
        if ( event == FE_Update ) {
            updateBotFollow();
            return true;
        }
        return event <= FE_Exit;

    default:
        OTDU_UNIMPLEMENTED_ONCE; // TODO: cop / manoeuvre states
        return false;
    }
}

void AiVehNavOpponent::enterRelocate() {
    // FUN_00958f70 (state 1 enter)
    situation.copyGroundMatrix();
    situation.updateRoute();
    const AiRoadIt *pCandidates[] = { &situation.RouteFrom,
                                      &situation.LastValidRoadIt, &curEntry,
                                      &prevEntry, &nextEntry };
    const AiRoadIt *pBest = nullptr;
    for ( const AiRoadIt *pCandidate : pCandidates ) {
        if ( pCandidate->pRoad != nullptr ) {
            pBest = pCandidate;
            break;
        }
    }

    AiRoadIt it;
    if ( pBest != nullptr ) {
        Matrix4x4f_t projected = Matrix4x4f_t::Identity();
        ProjectOnRoadShape( situation.GroundMatrix, it, &projected, false,
                            pBest->pRoad->getShape() );
        it.setWay( pBest->bWay );
    }
    if ( it.pRoad == nullptr ) {
        return;
    }
    situation.LastValidRoadIt = it;
    if ( situation.LastV == kNoValue ) {
        situation.LastV = 0.0f;
    }

    float widthRatio = 0.0f;
    ComputeWidthRatioAt( it, situation.Position, widthRatio );
    float low = 0.0f;
    float high = 0.0f;
    ComputeOuterLaneRatios( it.getEntity()->pProfile, low, high );
    if ( widthRatio > high ) {
        situation.LastV = high;
    } else if ( low > widthRatio ) {
        situation.LastV = low;
    } else {
        situation.LastV = situation.LastValidRoadIt.bWay ? high : low;
    }

    resetLine();
    resetPath();
    lineA.seed( situation.LastValidRoadIt, 0, situation.LastV, false );
    path.pushRoadPoint( lineA.front().It, situation.LastV, kDefaultSpeedCap,
                        false );
    if ( it.stepCurve( false ) ) {
        lineA.seed( it, 0, situation.LastV, false );
    }
    situation.refreshLast();
    situation.refreshLastPathPoint();
    situation.updateLineIndex();
    if ( pOwner->getCategory() == eAiVehicleCategory::AVC_Opponent ) {
        builder.optimise();
    } else {
        builder.fillOnly();
    }
}

void AiVehNavOpponent::updateRelocate() {
    // FUN_00958f70 (state 1 update)
    situation.updateLocation();
    if ( situation.Loc.LocationType == ObjectWorldLocation::OWL_OffRoad ) {
        if ( path.getNumPoints() == 0 ||
             path.current().Type != NavPath::PT_End ) {
            checkStuck();
        }
        return;
    }

    if ( pOwner->getBehaviourType() == 0 ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: police states 6 / 7
    } else if ( pOwner->getCategory() == eAiVehicleCategory::AVC_Bot ) {
        goTo( ( pOwner->getControllerType() == 1 &&
                pOwner->getController()->getCount() <= 1 )
                  ? NS_BotFollow
                  : NS_BotCruise );
    } else {
        goTo( NS_Race );
    }
}

void AiVehNavOpponent::updateGrid() {
    // FUN_00958f70 (state 2 update)
    AiVehController *pController = pOwner->getController();
    if ( pController != nullptr && pController->getCount() != 0 ) {
        AiRoadIt from;
        AiRoadIt to;
        pController->getAt( 0, from, to );
        if ( from.pRoad != nullptr && to.pRoad != nullptr ) {
            situation.updateRoute();
            if ( path.getNumPoints() != 0 ) {
                if ( gbForceOpponentRebuild || isMyRoundRobinTurn() ) {
                    situation.updateLineIndex();
                    rebuildPath();
                    builder.optimise();
                    if ( pOwner->isPlayerCar() ) {
                        goTo( NS_BotCruise );
                    }
                }
            } else {
                updateRouteIts();
                gGameObjectDico.locate( pOwner->getCarState(), nullptr, false );
                situation.updateLocation();
                lineA.seed( from, 0, situation.V, false );
                path.pushRoadPoint( from, situation.V, kDefaultSpeedCap,
                                    false );
                situation.refreshLast();
                situation.refreshLastPathPoint();
            }
        }
    }

    AiVehLocomotion *pLoco = pOwner->getLocomotion();
    if ( pLoco == nullptr ) {
        return;
    }
    if ( gbForceOpponentRebuild ) {
        pLoco->setManualInputs( 1.0f, 0.0f, 0.0f, 0.0f, 0.0f );
        return;
    }

    const AiVehIndDPCPattern &pattern = pOwner->getDPC().DPCPattern;
    const float low = std::min( pattern.RoaringLevelMin, gridThrottleLow );
    const float high = std::max( pattern.RoaringLevelMax, gridThrottleHigh );
    CarState *pCarState = pOwner->getCarState();
    PhysicsVehicle *pPhysics =
        ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;
    if ( pPhysics == nullptr ) {
        return;
    }
    const float rpmRatio =
        pPhysics->getEngineAngularVelocity() / pPhysics->getEngineOmegaMax();
    if ( rpmRatio >= high ) {
        pLoco->setManualInputs( low, 0.0f, 0.0f, 0.0f, 0.0f );
    } else if ( rpmRatio <= low ) {
        pLoco->setManualInputs( high, 0.0f, 0.0f, 0.0f, 0.0f );
    }
}

void AiVehNavOpponent::exitLineEnded() {
    // FUN_00958f70 (state 0xf exit)
    resetLine();
    resetPath();
    situation.copyGroundMatrix();
    situation.updateLocation();
    bool bSeeded = false;
    float widthRatio = 0.0f;
    if ( situation.RoadIt.pRoad != nullptr ) {
        lineA.seed( situation.RoadIt, 0, situation.V, false );
        widthRatio = situation.V;
        bSeeded = true;
    } else if ( situation.LastValidRoadIt.pRoad != nullptr ) {
        lineA.seed( situation.LastValidRoadIt, 0, situation.LastV, false );
        widthRatio = situation.LastV;
        bSeeded = true;
    }
    if ( bSeeded ) {
        path.pushRoadPoint( lineA.back().It, widthRatio, kDefaultSpeedCap,
                            false );
    }
    resetStuckRefs();

    AiVehController *pController = pOwner->getController();
    if ( pController->getCount() <= 1 ) {
        return;
    }
    AiRoadIt from;
    AiRoadIt to;
    pController->getAt( 0, from, to );
    updateRouteIts();
    gGameObjectDico.locate( pOwner->getCarState(), nullptr, false );
    situation.updateLocation();
    if ( lineA.getCount() == 0 ) {
        lineA.seed( from, 0, situation.V, false );
        path.pushRoadPoint( lineA.at( lineA.getResampleCursor() ).It,
                            situation.V, kDefaultSpeedCap, false );
    }
    situation.updateLineIndex();
    situation.refreshLast();
    situation.refreshLastPathPoint();
    if ( pOwner->getCategory() != eAiVehicleCategory::AVC_Opponent ) {
        builder.fillOnly();
        bGridRevHold = false;
    }
}

void AiVehNavOpponent::enterPostFinish() {
    // FUN_00958f70 (state 0x14 enter)
    pOwner->getCarState()->setRoadLocation( situation.LastValidRoadIt, false );
    pOwner->applyBotPersonality( 0, 0, 2 );
    finishSpeed = situation.CurSpeed;
    stopTime = gpTimer->GameTotalTime + kPostFinishStopDelay;
    if ( gpActiveGameMode != nullptr &&
         gpActiveGameMode->getCurrentGameMode() == eGameMode::GM_Mission ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: GMMission rank-staggered stop time
    }
}

void AiVehNavOpponent::updatePostFinish() {
    // FUN_00958f70 (state 0x14 update)
    speedCap = std::min( speedCap, finishSpeed );
    switch ( situation.Loc.LocationType ) {
    case ObjectWorldLocation::OWL_Road:
        if ( situation.RouteFrom.pRoad == nullptr ) {
            break;
        }
        if ( situation.RoadIt.pRoad == situation.RouteTo.pRoad ) {
            AiRoadIt current = situation.RoadIt;
            current.setWay( situation.RouteTo.bWay );
            if ( situation.RouteTo.isBefore( current ) ||
                 situation.RouteTo.isSamePosition( current, 0.0f ) ) {
                advanceRoute();
            }
        } else if ( situation.RoadIt.pRoad == situation.NextTo.pRoad ) {
            advanceRoute();
        }
        if ( gpTimer->GameTotalTime > stopTime ) {
            speedCap = 0.0f;
            AiVehLocomotion *pLoco = pOwner->getLocomotion();
            if ( situation.CurSpeed < 1.0f && situation.CurSpeed > 0.0f &&
                 pLoco != nullptr && pLoco->isDrivenByNavigation() ) {
                pLoco->setDrivenByNavigation( false );
                pLoco->setManualInputs( 0.0f, 1.0f, 1.0f, 0.0f, 0.0f );
            }
        }
        break;
    case ObjectWorldLocation::OWL_Junction:
        if ( situation.LastRouteTo.pRoad != nullptr ) {
            const AiRoadMap::Junction *pJunction =
                GetEndJunction( situation.LastRouteTo );
            if ( pJunction != nullptr &&
                 situation.Loc.LocationOther.RoadID == pJunction->ID ) {
                advanceRoute();
            }
        }
        break;
    case ObjectWorldLocation::OWL_OffRoad:
        speedCap = 0.0f;
        if ( situation.CurSpeed < kPostFinishOffRoadSpeed ) {
            AiVehLocomotion *pLoco = pOwner->getLocomotion();
            if ( pLoco != nullptr ) {
                pLoco->setDrivenByNavigation( false );
                pLoco->setManualInputs( 0.0f, 1.0f, 1.0f, 0.0f, 0.0f );
            }
        }
        break;
    default:
        break;
    }

    if ( situation.RouteFrom.pRoad != nullptr ) {
        if ( gpTimer->GameTotalTime > stopTime ) {
            updateLineFinished();
        } else {
            updateLine( false );
        }
        return;
    }
    if ( situation.Loc.LocationType == ObjectWorldLocation::OWL_Road ) {
        resetPath();
        lineA.reset( true );
        speedCap = 0.0f;
        path.pushRoadPoint( situation.LastValidRoadIt, situation.LastV,
                            kDefaultSpeedCap, false );
        lineA.seed( situation.LastValidRoadIt, 0, situation.LastV, false );
        situation.updateLineIndex();
        situation.refreshLast();
        situation.refreshLastPathPoint();
    }
}

void AiVehNavOpponent::updateRace() {
    // FUN_00957be0
    situation.updateLocation();
    if ( situation.Loc.LocationType == ObjectWorldLocation::OWL_Road ) {
        if ( situation.RoadIt.pRoad != nullptr &&
             situation.RouteFrom.pRoad != nullptr ) {
            if ( situation.RoadIt.pRoad == situation.RouteTo.pRoad ) {
                if ( fabsf( situation.U - situation.RouteToU ) <
                     situation.TolRoute ) {
                    advanceRoute();
                }
            } else if ( situation.NextTo.pRoad != nullptr &&
                        situation.RoadIt.pRoad == situation.NextTo.pRoad ) {
                float tolerance = situation.TolNext;
                const float length =
                    situation.NextTo.pRoad->getShape()->TotalLength;
                if ( length > 2.0f ) {
                    tolerance = length * 0.5f * tolerance;
                }
                if ( fabsf( situation.U - situation.NextFromU ) < tolerance ) {
                    advanceRoute();
                }
            }
        }
    } else if ( situation.Loc.LocationType ==
                ObjectWorldLocation::OWL_Junction ) {
        if ( situation.LastRouteTo.pRoad != nullptr ) {
            const AiRoadMap::Junction *pJunction =
                GetEndJunction( situation.LastRouteTo );
            if ( pJunction != nullptr &&
                 situation.Loc.LocationOther.RoadID == pJunction->ID ) {
                advanceRoute();
            }
        }
    }

    bool bRouteEnd = false;
    if ( !pOwner->isPlayerCar() ) {
        const NavPath::ePointType lastType = situation.LastPathPoint.Type;
        if ( lastType == NavPath::PT_End ) {
            bRouteEnd = true;
        } else if ( situation.NextTo.pRoad == nullptr &&
                    lastType == NavPath::PT_Road &&
                    ( situation.RouteTo.pRoad == nullptr ||
                      situation.LastPathPoint.It.isSamePosition(
                          situation.RouteTo, kRouteEndTolerance ) ) ) {
            bRouteEnd = true;
        }
    }

    if ( situation.RouteFrom.pRoad != nullptr && !bRouteEnd ) {
        if ( checkCheckpointMissed() ) {
            checkpointMissTicks++;
            if ( checkpointMissTicks > kMaxCheckpointMissTicks ) {
                resetOnCheckpointRoad();
            }
        } else {
            checkpointMissTicks = 0;
        }

        if ( path.getNumPoints() != 0 &&
             path.current().Type == NavPath::PT_End ) {
            stuckRefTime = now;
        } else if ( checkStuck() ) {
            goTo( NS_Relocate );
        }
        updateLine( false );
        situation.updateLineIndex();
        situation.refreshLast();
        return;
    }

    if ( path.back().Id != lastBuiltPointId ) {
        appendStraightEnd();
        situation.updateLineIndex();
        situation.refreshLast();
    }
    if ( lineA.getCount() != 0 &&
         lineA.at( lineA.getCurrentIndex() ).Type == OptPathElem::ET_Empty ) {
        if ( pOwner->getControllerType() != 7 ) {
            pOwner->setController( 7 );
            goTo( NS_LineEnded );
            return;
        }
        speedCap = 0.0f;
    }
}

void AiVehNavOpponent::updateLine( const bool bNoOptim ) {
    // FUN_00957a10
    bool bRebuild = false;
    if ( situation.Loc.LocationType == ObjectWorldLocation::OWL_Road ) {
        if ( path.getNumPoints() <= 0 ) {
            bRebuild = true;
        } else if ( lineA.getCount() != 0 &&
                    path.back().Id != lastBuiltPointId &&
                    path.current().Type == NavPath::PT_Road &&
                    lineA.at( lineA.getCurrentIndex() ).Type ==
                        OptPathElem::ET_Road ) {
            const OptPathElem &elem = lineA.at( lineA.getCurrentIndex() );
            const NavPath::Point &point = path.current();
            bRebuild = elem.It.pRoad != point.It.pRoad ||
                       point.It.bWay == elem.It.bWay;
        }
    }

    const bool bMyTurn = gbForceOpponentRebuild || isMyRoundRobinTurn();
    if ( ( bRebuild && bMyTurn ) || lineA.getCount() < 4 ) {
        if ( bRebuild ) {
            path.reset( true );
            lineA.setField0x128( -1 );
            lineA.setResampleCursor( 0 );
            situation.refreshLastPathPoint();
            if ( pOwner->getCategory() != eAiVehicleCategory::AVC_Network ) {
                avoidObstacles();
            }
            rebuildPath();
        }
        if ( bNoOptim ) {
            builder.fillOnly();
        } else {
            builder.optimise();
        }
        finaliseLine();
    } else {
        builder.resample();
    }
    speedCap = std::min( speedCap, speedCap2 );
}

void AiVehNavOpponent::updateLineFinished() {
    // FUN_009572b0
    bool bRebuild = false;
    if ( situation.Loc.LocationType == ObjectWorldLocation::OWL_Road ) {
        if ( path.getNumPoints() <= 0 ) {
            bRebuild = true;
        } else if ( lineA.getCount() != 0 &&
                    path.back().Id != lastBuiltPointId &&
                    path.current().Type == NavPath::PT_Road &&
                    lineA.at( lineA.getCurrentIndex() ).Type ==
                        OptPathElem::ET_Road ) {
            const OptPathElem &elem = lineA.at( lineA.getCurrentIndex() );
            const NavPath::Point &point = path.current();
            bRebuild = elem.It.pRoad != point.It.pRoad ||
                       point.It.bWay == elem.It.bWay;
        }
    }

    const bool bMyTurn = gbForceOpponentRebuild || isMyRoundRobinTurn();
    if ( ( bRebuild && bMyTurn ) || lineA.getCount() < 4 ) {
        if ( bRebuild ) {
            rebuildPath();
        }
        avoidObstacles();
        builder.extendAlongRoadSide();
        builder.resample();
        finaliseLine();
    } else {
        builder.resample();
    }
}

void AiVehNavOpponent::updateBotCruise() {
    // FUN_00957f10
    if ( !situation.bLineMismatch ) {
        if ( pOwner->getController()->getCount() < 2 &&
             pOwner->getControllerType() == 1 &&
             getCurrentState() != NS_BotFollow ) {
            goTo( NS_BotFollow );
            return;
        }

        if ( situation.Loc.LocationType == ObjectWorldLocation::OWL_Road ) {
            if ( situation.RoadIt.pRoad != nullptr &&
                 situation.RouteFrom.pRoad != nullptr ) {
                if ( situation.RoadIt.pRoad == situation.RouteTo.pRoad ) {
                    AiRoadIt current = situation.RoadIt;
                    current.setWay( situation.RouteTo.bWay );
                    if ( situation.RouteTo.isBefore( current ) ||
                         situation.RouteTo.isSamePosition( current, 0.0f ) ) {
                        advanceRoute();
                    }
                } else if ( situation.RoadIt.pRoad == situation.NextTo.pRoad ) {
                    advanceRoute();
                }
            }
        } else if ( situation.Loc.LocationType ==
                    ObjectWorldLocation::OWL_Junction ) {
            if ( situation.LastRouteTo.pRoad != nullptr ) {
                const AiRoadMap::Junction *pJunction =
                    GetEndJunction( situation.LastRouteTo );
                if ( pJunction != nullptr &&
                     situation.Loc.LocationOther.RoadID == pJunction->ID ) {
                    advanceRoute();
                }
            }
        }

        if ( !situation.bLineMismatch ) {
            if ( situation.RouteFrom.pRoad == nullptr ) {
                if ( situation.Loc.LocationType ==
                     ObjectWorldLocation::OWL_Road ) {
                    resetPath();
                    lineA.reset( true );
                    speedCap = 0.0f;
                    path.pushRoadPoint( situation.LastValidRoadIt,
                                        situation.LastV, kDefaultSpeedCap,
                                        false );
                    lineA.seed( situation.LastValidRoadIt, 0, situation.LastV,
                                false );
                    situation.updateLineIndex();
                    situation.refreshLast();
                    situation.refreshLastPathPoint();
                }
                return;
            }
            if ( checkStuck() ) {
                goTo( NS_Relocate );
            }
            updateLine( true );
            if ( !situation.bLineMismatch ) {
                return;
            }
        }
    }
    onLineMismatch();
}

void AiVehNavOpponent::updateBotFollow() {
    // FUN_00958480
    static constexpr float kFollowMinSpeed = 10.0f; // DAT_00f4166c
    static constexpr float kFollowGap = 2.0f;       // DAT_00f416b0

    if ( pOwner->getBehaviourType() != 3 ) {
        return;
    }
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr ) {
        return;
    }
    if ( pOwner->getController()->getCount() > 1 ) {
        goTo( NS_BotCruise );
        return;
    }

    const ObjectWorldLocation &playerLoc = pPlayerCar->getWorldLocation();
    if ( situation.Loc.LocationType == ObjectWorldLocation::OWL_Road &&
         playerLoc.LocationType != ObjectWorldLocation::OWL_Road ) {
        const Eigen::Vector4f delta =
            pPlayerCar->getWorldPosition() - situation.Position;
        const float distance = sqrtf( Dot4( delta, delta ) );
        updateBotCruise();
        if ( playerLoc.LocationType == ObjectWorldLocation::OWL_Junction ) {
            const Eigen::Vector4f &velocity = pPlayerCar->getLinearVelocity();
            const float playerSpeed = sqrtf( Dot4( velocity, velocity ) );
            if ( playerSpeed > kFollowMinSpeed &&
                 situation.CurSpeed > playerSpeed &&
                 pOwner->computeBrakingDistance( situation.CurSpeed,
                                                 playerSpeed, 0.0f ) >
                     distance - kFollowGap &&
                 speedCap > 0.0f ) {
                speedCap = 0.0f;
            }
        }
    } else {
        updateBotCruise();
    }

    if ( checkStuck() ) {
        goTo( NS_Relocate );
    }
}

void AiVehNavOpponent::onLineMismatch() {
    // FUN_00957240
    const bool bOnRoad =
        situation.Loc.LocationType == ObjectWorldLocation::OWL_Road;
    switch ( pOwner->getBehaviourType() ) {
    case 0:
        if ( bOnRoad && getCurrentState() != 6 ) {
            goTo( 6 );
        }
        break;
    case 3:
        if ( bOnRoad ) {
            goTo( NS_LineEnded );
        } else if ( checkStuck() ) {
            goTo( NS_Relocate );
        }
        break;
    default:
        break;
    }
}

void AiVehNavOpponent::avoidObstacles() {
    // FUN_009562a0
    static constexpr float kBlockedSpeedFactor = 0.5f; // DAT_00f41674
    static constexpr float kDriftBackDistance = 3.0f;  // DAT_00f416b4

    if ( situation.isBikeRiderDown() ) {
        return;
    }
    if ( !gbForceOpponentRebuild && !isMyRoundRobinTurn() ) {
        return;
    }
    if ( lineA.getCount() <= 1 ) {
        return;
    }

    situation.updateLineIndex();
    speedCap2 = pOwner->getInfos().MaxSpeed;
    if ( !obstacles.collect() ) {
        if ( lineA.at( lineA.getCurrentIndex() ).Type ==
             OptPathElem::ET_Road ) {
            lineA.clearOptimisation( 0, lineA.getCount() - 1 );
            if ( !situation.bBike &&
                 situation.LineDistance > kDriftBackDistance ) {
                lineA.driftBackToLine();
            }
        }
        return;
    }

    bObstacleAhead = true;
    bool bStop = false;
    if ( obstacles.solve( bStop ) ||
         pOwner->getCategory() == eAiVehicleCategory::AVC_Autopilot ) {
        return;
    }
    CarState *pCarState = pOwner->getCarState();
    PhysicsVehicle *pPhysics =
        ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;
    if ( pPhysics != nullptr && pPhysics->getPhantomMode() != 1 ) {
        return;
    }
    if ( bStop ) {
        speedCap2 = 0.0f;
        return;
    }

    AiVehLocomotion *pLoco = pOwner->getLocomotion();
    const bool bKinematic = pLoco != nullptr && !pLoco->isPhysicsDriven();
    if ( pOwner->isPlayerCar() || pOwner->getDPC().DPCPattern.bAvoidCollision ||
         bKinematic ) {
        speedCap2 =
            std::min( speedCap2, situation.CurSpeed * kBlockedSpeedFactor );
    }
}

bool AiVehNavOpponent::checkStuck() {
    // FUN_00956e00
    OTDU_UNIMPLEMENTED_ONCE; // TODO: engine +0x94 (0 => 2)
    float timeout = kStuckDefaultTimeout;
    bool bStuck = false;
    if ( situation.isBikeRiderDown() ) {
        bStuck = true;
        timeout = kStuckRiderDownTimeout;
    } else {
        const Eigen::Vector4f delta = stuckRefPos - situation.Position;
        const bool bWantsToMove = speedCap != 0.0f && speedCap2 != 0.0f;
        if ( sqrtf( Dot4( delta, delta ) ) < kStuckMinMove && bWantsToMove ) {
            bStuck = true;
            timeout = kStuckNotMovingTimeout;
        }
    }

    bool bCheckOffRoad = true;
    if ( !bStuck || timeout > kStuckFastTimeout ) {
        const uint32_t roadID = situation.Loc.LocationRoad.RoadID;
        if ( situation.Loc.LocationType == ObjectWorldLocation::OWL_Road &&
             situation.RouteFrom.pRoad != nullptr &&
             roadID != situation.RouteFrom.pRoad->ID &&
             ( situation.NextFrom.pRoad == nullptr ||
               roadID != situation.NextFrom.pRoad->ID ) &&
             ( prevEntry.pRoad == nullptr || roadID != prevEntry.pRoad->ID ) ) {
            bStuck = true;
            timeout = std::min( timeout, kStuckFastTimeout );
            bCheckOffRoad = false;
        }
    }

    if ( bCheckOffRoad && !( bStuck && timeout <= kStuckSlowTimeout ) &&
         situation.Loc.LocationType == ObjectWorldLocation::OWL_OffRoad ) {
        bool bTimedRace = false;
        if ( gpActiveGameMode != nullptr &&
             gpActiveGameMode->getCurrentGameMode() == eGameMode::GM_Mission ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: GMMission timed race type
        }
        if ( path.getNumPoints() != 0 &&
             path.current().Type != NavPath::PT_End ) {
            if ( bTimedRace ) {
                timeout = std::min( timeout, kStuckFastTimeout );
            } else if ( situation.CurSpeed < kStuckOffRoadSpeed ) {
                bStuck = true;
                timeout = std::min( timeout,
                                    situation.CurSpeed < kStuckOffRoadSlowSpeed
                                        ? kStuckFastTimeout
                                        : kStuckSlowTimeout );
            }
        }
    }

    if ( situation.GroundMatrix( 1, 1 ) > 0.0f ) {
        if ( !bStuck ) {
            stuckRefPos = situation.Position;
            stuckRefTime = now;
            return false;
        }
    } else {
        timeout = std::min( timeout, kStuckFastTimeout );
    }

    if ( !( now - graceTime > kStuckGraceTime || timeout < 0.0f ) ||
         !( now - stuckRefTime > timeout ) ) {
        return false;
    }

    if ( pOwner->getBehaviourType() == 0 && !gAIConfig.bPoliceCanBackOnTrack ) {
        pOwner->setRemovable( true );
        pOwner->requestRemoval( -1.0f, "Police car blocked" );
        return true;
    }

    AiRoadIt location;
    float lateral = 0.0f;
    findRespawnLocation( location, lateral );
    TeleportToRoad( pOwner, location, lateral, true );
    graceTime = now;
    stuckRefTime = now;
    situation.copyGroundMatrix();
    situation.updateLocation();
    situation.updateLineIndex();
    stuckRefPos = situation.Position;
    return true;
}

void AiVehNavOpponent::findRespawnLocation( AiRoadIt &outIt,
                                            float &outLateral ) {
    // FUN_00956030
    AiVehController *pController = pOwner->getController();
    if ( pController == nullptr ) {
        return;
    }
    AiRoadIt from;
    AiRoadIt to;
    pController->getFront( from, to );
    if ( from.pRoad == nullptr ) {
        return;
    }

    const Matrix4x4f_t matrix = pOwner->getInfos().GroundMatrix;
    AiRoadIt it;
    Matrix4x4f_t projected = Matrix4x4f_t::Identity();
    ProjectOnRoadShape( matrix, it, &projected, false, from.pRoad->getShape() );
    it.setWay( from.bWay );
    ComputeWidthRatioAt( it, matrix.row( 3 ).transpose(), outLateral );
    const float halfWidth = from.getEntity()->pProfile->InnerWidth * 0.5f;
    const float inset =
        ( pOwner->getCarState()->getDimensions().x() * 0.5f + 1.0f ) /
        halfWidth;
    outLateral = ( outLateral > 0.0f ) ? 1.0f - inset : inset - 1.0f;
    if ( it.isSamePosition( from, 0.0f ) ) {
        if ( it.T == 0.0f ) {
            it.T = 0.1f;
        } else if ( it.T == 1.0f ) {
            it.T = 0.9f;
        }
    }
    if ( !HasAccessibleExit( it ) ) {
        pController->popFront();
        it.flip();
    }
    outIt = it;
}

bool AiVehNavOpponent::checkCheckpointMissed() {
    // FUN_00956c10
    if ( gpActiveGameMode == nullptr ||
         gpActiveGameMode->getCurrentGameMode() != eGameMode::GM_Mission ) {
        return false;
    }
    OTDU_UNIMPLEMENTED_ONCE; // TODO: GMMission race type + opponent checkpoint
    return false;
}

void AiVehNavOpponent::resetOnCheckpointRoad() {
    // FUN_009569a0
    Matrix4x4f_t matrix = pOwner->getInfos().GroundMatrix;
    PhysicsVehicle *pPhysics = pOwner->getCarState()->getPhysicsVehicle();
    checkpointIt.advanceByDistance( kCheckpointStepBack, true );
    const AiRoadProfile *pProfile = checkpointIt.getEntity()->pProfile;
    const float ratio = pOwner->getCarState()->getDimensions().x() /
                        pProfile->InnerWidth * 0.5f * kCheckpointWidthFactor;
    const float widthRatio = std::clamp(
        checkpointIt.bWay ? 1.0f - ratio : ratio - 1.0f, -1.0f, 1.0f );
    float lateral = 0.0f;
    float height = 0.0f;
    pProfile->computeWidthOffset( widthRatio, lateral, height );
    const Eigen::Vector4f position =
        checkpointIt.computeOffsetPosition( lateral, height );
    const Eigen::Vector4f direction = checkpointIt.computeDirection();
    MatrixRotateY( matrix, ComputeHeadingYaw( matrix, direction ) );
    matrix.row( 3 ) = position.transpose();
    {
        PhysicsLock lock;
        pPhysics->resetToTransform( matrix, false );
    }
    pOwner->setGroundMatrixFromCenter( matrix );
    AiVehIndDPC &dpc = pOwner->getDPC();
    if ( dpc.NumOutsideWanted > 0 ) {
        dpc.NumOutsideWanted--;
    }
    checkpointMissTicks = 0;
    goTo( NS_Relocate );
}
