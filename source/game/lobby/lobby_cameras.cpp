#include "shared.h"
#include "lobby_cameras.h"

#include "gm_lobby.h"

#include "ai/ai_road_shape.h"
#include "ai/gs_ai.h"
#include "config/gs_config.h"
#include "core/maths_helper.h"
#include "core/time.h"
#include "flash/implementation/ed_flash.h"
#include "flash/mng_fade.h"
#include "game/scenaric/entity/entity_check_point.h"
#include "game/scenaric/entity/entity_volume.h"
#include "game/scenaric/event_parameters.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/scenaric_bank.h"
#include "input/gs_device.h"
#include "input/input_device.h"
#include "render/camera/camera_vehicle.h"
#include "render/camera/gs_camera.h"
#include "world/gs_world.h"
#include "world/mng_world.h"
#include "world/sector_manager.h"
#include "world/streaming_manager.h"

static constexpr float kLobbyCameraNearPlane = 0.5f;    // FLOAT_00f41674
static constexpr float kLobbyCameraFarPlane = 40000.0f; // DAT_00f41c78
static constexpr float kFadeInDuration = 1.0f;          // FLOAT_00f4165c
static constexpr float kStartOrbitSpeed = 0.39269909f;  // DAT_00f41bfc
static constexpr float kStartOrbitDistance = 15.0f;     // DAT_00f41700
static constexpr uint32_t kStartOrbitFadeDelayMs = 1000u;
static constexpr double kTwoPi = 6.2831854820251465;    // DAT_00f41948
static constexpr float kPi = 3.1415927f;                // DAT_00f41940
static constexpr float kPlayersViewBobAmplitude = 0.0f; // FLOAT_00f41660
static constexpr float kFlybySpeed = 2.0f;              // DAT_00f416b0
static constexpr float kFlybySplineLengthStep = 0.01f;  // 0x3c23d70a
static constexpr float kFlybyFadeDuration = 0.5f;       // FLOAT_00f41674
static constexpr float kFlybyTargetHeight = 3.0f;       // DAT_00f416b4
static constexpr float kFlybyEndDistance = 20.0f;       // DAT_00f03ee0
static constexpr float kFlybyEndRotation = 180.0f;      // 0x43340000
static constexpr float kFlybyStartHeight = 7.0f;        // DAT_00f41844
static constexpr float kFlybyFirstHandleHeight = 5.0f;  // DAT_00f41750
static constexpr float kFlybyEndHeight = 3.5f;          // DAT_00f41b4c
static constexpr float kOneThird = 0.33333334f;         // DAT_00f41764
static constexpr float kTwoThirds = 0.6666667f;         // DAT_00f41ad8

static uint32_t GetTickCountMs() {
    return uint32_t( MonotonicToMs( GetTimeMonotonic() ) );
}

static void ApplyLobbyCamera( CameraGame *pCamera,
                              const Eigen::Vector4f &position,
                              const Eigen::Vector4f &target ) {
    pCamera->setPosition( position );
    pCamera->setNearPlane( kLobbyCameraNearPlane );
    pCamera->setFarPlane( kLobbyCameraFarPlane );

    gpCamera->setFarPlane( pCamera->getFarPlane() );
    gpCamera->setNearPlane( pCamera->getNearPlane() );
    gpCamera->setEnableDepthAware( true );
    gpCamera->setInteriorFarPlane( pCamera->getInteriorFarPlane() );
    gpCamera->setInteriorNearPlane( pCamera->getInteriorNearPlane() );
    gpCamera->setEnableDepthAwareInterior( true );

    pCamera->updateMatrix( position.head<3>(), Eigen::Vector3f::UnitY(),
                           target.head<3>() );
}

static void StartFadeIn( MngFade *pFade, const float duration ) {
    pFade->startTransition( ( pFade->getTransitionFlags() & ~8u ) | 5u,
                            duration );
}

static void MarkStreamingDirty() {
    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bNeedStreamUpdate = true;
    pStreaming->sectorManager.bDrawSector = false;
    pStreaming->areaGrid.bNeedStreamUpdate = true;
    pStreaming->areaGrid.bDrawArea = false;
}

static bool ProjectCheckpointOnRoad( GMLobby *pOwner,
                                     const int32_t index,
                                     AiRoadIt &outIt ) {
    // FUN_0075e150
    const Matrix4x4f_t &frame =
        pOwner->findCheckpoint( index )->getModelMatrix();
    AiRoadMap::Road *pRoad =
        gpAI->getRoadMap().findNearestRoadExhaustive( frame, true );
    // NOTE: Retail dereferences the road unconditionally.
    if ( pRoad == nullptr ) {
        return false;
    }
    ProjectOnRoadShape( frame, outIt, nullptr, false, pRoad->getShape() );
    return true;
}

static bool IsStreamedAtCamera( const Eigen::Vector4f &position ) {
    // Inlined in FUN_007594e0 and FUN_0075b890
    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    SectorManager &sectorManager = pStreaming->sectorManager;
    const Sector *pCurrentSector = sectorManager.pCurrentSector;
    const Area *pCurrentArea = pStreaming->areaGrid.pCurrentArea;

    GridPos2D sectorCell;
    GridPos2D areaCell;
    sectorManager.computeCell( position, sectorCell, areaCell );
    return pCurrentSector->installedArea.posY == areaCell.posY &&
           pCurrentSector->installedArea.posX == areaCell.posX &&
           pCurrentSector->installedSector.posY == sectorCell.posY &&
           pCurrentSector->installedSector.posX == sectorCell.posX &&
           pCurrentSector->state == 1 && pCurrentArea != nullptr &&
           pCurrentArea->state == 1;
}

static constexpr float kRouteThrottleMax = 0.5f;       // FLOAT_00f41674
static constexpr float kRouteThrottleMin = 0.2f;       // DAT_00f41730
static constexpr float kRouteThrottleRate = 0.1f;      // FLOAT_00f41668
static constexpr float kRouteCameraDistance = 12.0f;   // DAT_00f41850
static constexpr float kRouteCameraPitch = 3.1415927f; // DAT_00f41940
static constexpr float kRouteStartDelay = 2.0f;        // DAT_00f416b0
static constexpr float kRouteRoadEndMargin = 50.0f;    // DAT_00f41848
static constexpr float kRouteRoadEndFraction = 0.8f;   // DAT_00f416d0
static constexpr float kRouteCruiseSpeed = 50.0f;      // DAT_00f41848
static constexpr float kRouteBrakeSpeed = 25.0f;       // DAT_00f41834
static constexpr float kRouteAcceleration = 10.0f;     // DAT_00f4166c
static constexpr float kRouteSpeedBlend = 10.000002f;  // DAT_00f420a0
static constexpr float kRouteCameraHeight = 4.5f;      // DAT_00f41ba4
static constexpr float kRouteCurveLengthStep = 0.01f;
static constexpr float kRouteUTurnHandleLength = 10.0f;       // DAT_00f4166c
static constexpr float kRouteJunctionHandleRatio = 0.5f;      // FLOAT_00f41674
static constexpr float kRouteJunctionHandleBackRatio = -0.5f; // DAT_00f4186c
static constexpr float kRouteUserSpeedRate = 100.0f;          // DAT_00f41708
static constexpr float kRouteUserSpeedMax = 80.0f;            // DAT_00f419f0
static constexpr float kRouteUserSpeedMin = -80.0f;           // DAT_00f420a8
static constexpr float kRouteUserBrakeRate = 19.999998f;      // DAT_00f41bec
static constexpr float kRouteUserIdleTimeout = 10.0f;         // DAT_00f4166c
static constexpr float kRouteUserSpeedThreshold = 2.0f;       // DAT_00f416b0
static constexpr float kRouteUserTakeoverDelay = 2.0f;        // DAT_00f416b0
static constexpr float kRouteUserInputEpsilon = 1e-4f;        // DAT_00f41760
static constexpr float kRouteReverseRecovery = 10.0f;         // DAT_00f4166c
static constexpr float kRouteReverseRecoveryRate = 50.0f;     // DAT_00f41848
static constexpr float kRouteYawScale = 3.926991f;            // DAT_00f420a4
static constexpr float kRouteYawMax = 3.1415927f;             // DAT_00f41940
static constexpr float kRouteYawMin = -3.1415927f;            // DAT_00f41944
static constexpr float kRouteOffsetBlend = 0.999999f;         // DAT_00f41c00
static constexpr float kRouteHeightScale = 12.5f;             // DAT_00f41990
static constexpr float kRouteHeightMin = 2.0f;                // DAT_00f416b0
static constexpr float kRouteHeightMax = 10.0f;               // DAT_00f4166c
static constexpr float kRouteLookEpsilon = 1e-5f;             // DAT_00f41710
static constexpr float kRouteLookNudge = 0.01f;               // DAT_00f416d4
static constexpr uint32_t kRouteLoopLedType = 3;

static Eigen::Vector4f EvaluateTransitionCurve( const int32_t curveType,
                                                const CubicBezier &curve,
                                                const float t ) {
    // FUN_004091c0
    if ( curveType == 0 ) {
        QuadraticBezier quadratic;
        quadratic.ControlPoints[0] = curve.ControlPoints[0];
        quadratic.ControlPoints[1] = curve.ControlPoints[1];
        quadratic.ControlPoints[2] = curve.ControlPoints[2];
        return quadratic.evaluate( t );
    }
    return curve.evaluate( t );
}

static inline void SetTransitionCurve( int32_t &outCurveType,
                                       CubicBezier &outCurve,
                                       const Eigen::Vector4f &p0,
                                       const Eigen::Vector4f &p1,
                                       const Eigen::Vector4f &p2,
                                       const Eigen::Vector4f &p3 ) {
    // FUN_0040a5c0
    outCurve.ControlPoints[0] = p0;
    outCurve.ControlPoints[1] = p1;
    outCurve.ControlPoints[2] = p2;
    outCurve.ControlPoints[3] = p3;
    outCurveType = 1;
}

static inline float ApplyDeadZone( const float value, const float deadZone ) {
    if ( deadZone > value ) {
        return 0.0f;
    }
    return ( value - deadZone ) / ( 1.0f - deadZone );
}

static inline Eigen::Vector4f ComputeEntityDirection( const AiRoadIt &it ) {
    // FUN_00930b90 + normalization (inlined)
    const AiRoadMap::Entity *pEntity = it.getEntity();
    if ( !it.bWay ) {
        return NormalizeVector4(
                   pEntity->computeTangent( it.IndWay, 1.0f - it.T ) ) *
               -1.0f;
    }
    return NormalizeVector4( pEntity->computeTangent( it.IndWay, it.T ) );
}

LobbyCameraRoutePreview::LobbyCameraRoutePreview( GMLobby *pOwner )
    : LobbyCamera( pOwner ),
      path(),
      pCurrentElem( nullptr ),
      transitionState( TS_OnRoad ),
      roadIt(),
      progress( 0.0f ),
      throttle( kRouteThrottleMax ),
      speed( 0.0f ),
      transitionCurveType( 0 ),
      transitionCurve(),
      cameraDistance( kRouteCameraDistance ),
      cameraPitch( kRouteCameraPitch ),
      roadPosition( Eigen::Vector4f::Zero() ),
      cameraPosition( Eigen::Vector4f::Zero() ),
      previousCameraPosition( Eigen::Vector4f::Zero() ),
      roadDirection( Eigen::Vector4f::Zero() ),
      roadSide( Eigen::Vector4f::Zero() ),
      bWaitingForStreaming( false ),
      startDelay( 0.0f ),
      bLoop( false ),
      nextCheckpoint( 1 ),
      bRunning( false ),
      bUserControl( false ),
      userControlTime( 0.0f ),
      yawOffset( 0.0f ),
      heightOffset( 0.0f ) {}

bool LobbyCameraRoutePreview::init() {
    // FUN_0075bc00
    path.clear();

    AiPath legPath;
    AiRoadIt it;
    ProjectCheckpointOnRoad( pOwner, 1, it );

    const int32_t maxCheckpointIndex = pOwner->getMaxCheckpointIndex();
    for ( int32_t i = 2; i <= maxCheckpointIndex; i++ ) {
        AiRoadIt previousIt = it;
        ProjectCheckpointOnRoad( pOwner, i, it );

        legPath.setStart( previousIt, 0 );
        legPath.setEnd( it );
        legPath.setIgnoreRoadLocks( true );
        legPath.solve( false );

        // NOTE: Retail dereferences the leg tail/head unconditionally (a
        // failed solve leaves them null).
        if ( legPath.getTail() != nullptr ) {
            it.bWay = legPath.getTail()->To.getWay();
        }
        if ( i == 2 && legPath.getHead() != nullptr ) {
            previousIt.bWay = legPath.getHead()->From.getWay();
            roadIt = previousIt;
            AiRoadShape *pShape = roadIt.pRoad->getShape();
            if ( pShape != nullptr ) {
                progress = pShape->computeRoadFraction( roadIt );
                transitionState = TS_OnRoad;
            }
        }
        path.append( legPath, true );
    }

    bLoop = gpScenaric->getSelectedEventParameters().getLoop().getValue() != 0;
    if ( bLoop ) {
        const AiRoadIt previousIt = it;
        ProjectCheckpointOnRoad( pOwner, 1, it );

        legPath.setStart( previousIt, 0 );
        legPath.setEnd( it );
        legPath.setIgnoreRoadLocks( true );
        legPath.solve( false );
        path.append( legPath, true );

        swapCheckpointTemplate( pOwner->findCheckpoint( maxCheckpointIndex ),
                                kRouteLoopLedType );
    }

    restart();

    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCamera( false );
    }

    legPath.clear();
    return true;
}

void LobbyCameraRoutePreview::update( float deltaTime ) {
    // FUN_0075b890
    if ( bWaitingForStreaming ) {
        if ( IsStreamedAtCamera( cameraPosition ) ) {
            StartFadeIn( gpMngFade, kFadeInDuration );
            bWaitingForStreaming = false;
        }
        return;
    }

    startDelay -= deltaTime;
    if ( startDelay > 0.0f ) {
        return;
    }
    startDelay = 0.0f;

    if ( bUserControl && userControlTime > kRouteUserIdleTimeout ) {
        updateCruiseSpeed( deltaTime );
        if ( std::abs( speed ) > kRouteUserSpeedThreshold ) {
            userControlTime = 0.0f;
            bUserControl = false;
        }
    }
    updateCamera( deltaTime );
}

void LobbyCameraRoutePreview::restart() {
    // FUN_0075ba20
    throttle = kRouteThrottleMax;
    cameraDistance = kRouteCameraDistance;
    speed = 0.0f;
    cameraPitch = kRouteCameraPitch;
    bRunning = false;
    bUserControl = false;
    yawOffset = 0.0f;
    heightOffset = 0.0f;
    userControlTime = 0.0f;
    nextCheckpoint = 1;

    const int32_t maxCheckpointIndex = pOwner->getMaxCheckpointIndex();
    for ( int32_t i = 1; i < maxCheckpointIndex; i++ ) {
        EntityCheckPoint *pCheckpoint = pOwner->findCheckpoint( i );
        if ( pCheckpoint != nullptr ) {
            pCheckpoint->setVisible( false );
        }
    }
    EntityCheckPoint *pStart = pOwner->findCheckpoint( 1 );
    if ( pStart != nullptr ) {
        pStart->setVisible( true );
    }

    pCurrentElem = path.getHead();
    // NOTE: Retail dereferences the route head unconditionally (empty route
    // when no road map is loaded).
    if ( pCurrentElem != nullptr ) {
        pCurrentElem->From.unpack( roadIt );
        AiRoadShape *pShape = roadIt.pRoad->getShape();
        if ( pShape != nullptr ) {
            progress = pShape->computeRoadFraction( roadIt );
            transitionState = TS_OnRoad;
        }
    }

    updateCamera( 0.0f );

    gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() | 8u );
    bWaitingForStreaming = true;
    startDelay = kRouteStartDelay;
}

void LobbyCameraRoutePreview::setRoadPosition( const AiRoadIt &it ) {
    // FUN_0075a1c0
    roadIt = it;
    AiRoadShape *pShape = roadIt.pRoad->getShape();
    if ( pShape != nullptr ) {
        progress = pShape->computeRoadFraction( roadIt );
        transitionState = TS_OnRoad;
    }
}

EntityCheckPoint *
LobbyCameraRoutePreview::swapCheckpointTemplate( EntityCheckPoint *pCheckpoint,
                                                 const uint32_t ledType ) {
    // FUN_0075a220
    const SerializedCheckPoint *pLEDData = pCheckpoint->getLEDData();
    if ( pLEDData->Type == ledType ) {
        return pCheckpoint;
    }

    const bool bBigCheckpoint =
        pLEDData != nullptr && pLEDData->BigCheckpoint != 0;
    Entity *pTemplate = gpScenaric->getCheckpointBank()->findCheckpointTemplate(
        ledType, bBigCheckpoint );

    Matrix4x4f_t transform = pCheckpoint->getModelMatrix();
    const int32_t index = pCheckpoint->getIndex();
    // NOTE: Retail reads the old checkpoint flags after uninstanciating it
    // (use after free); they are read before instead.
    const uint32_t flags = pCheckpoint->getFlags();

    MngActor &mngActor = pOwner->getActorManager();
    MngActor::Uninstanciate( &mngActor, pCheckpoint );

    EntityCheckPoint *pInstance = static_cast<EntityCheckPoint *>(
        mngActor.instanciateActor( pTemplate ) );
    Matrix4x4f_t identity = Matrix4x4f_t::Identity();
    pInstance->setTransform( identity, true );
    pInstance->propagateTransform( transform, true );
    pInstance->setTransform( transform, true );
    pInstance->setIndex( index );
    pInstance->setPrimaryInstance( true );
    pInstance->setFlags( flags );
    return pInstance;
}

void LobbyCameraRoutePreview::updateCruiseSpeed( const float deltaTime ) {
    // FUN_007599f0
    float lowFraction = kRouteThrottleMin;
    float highFraction = kRouteRoadEndFraction;
    AiRoadShape *pShape = roadIt.pRoad->getShape();
    if ( pShape != nullptr ) {
        const float marginFraction = kRouteRoadEndMargin / pShape->TotalLength;
        // NOTE: Retail subtracts the margin fraction from the road LENGTH
        // (not from 1), so the upper threshold is usually the road length.
        const float upperFraction = pShape->TotalLength - marginFraction;
        if ( upperFraction > highFraction ) {
            highFraction = upperFraction;
        }
        if ( kRouteThrottleMin > marginFraction ) {
            lowFraction = marginFraction;
        }
    }

    const bool bApproachingRoadEnd =
        transitionState == TS_OnRoad &&
        ( ( roadIt.bWay && progress >= highFraction ) ||
          ( !roadIt.bWay && lowFraction >= progress ) );
    if ( bApproachingRoadEnd ) {
        speed -= deltaTime * kRouteAcceleration;
        if ( kRouteBrakeSpeed > speed ) {
            speed +=
                ( kRouteBrakeSpeed - speed ) * deltaTime * kRouteSpeedBlend;
        }
        throttle -= deltaTime * kRouteThrottleRate;
        if ( kRouteThrottleMin >= throttle ) {
            throttle = kRouteThrottleMin;
        }
        return;
    }

    speed += deltaTime * kRouteAcceleration;
    if ( speed > kRouteCruiseSpeed ) {
        speed += ( kRouteCruiseSpeed - speed ) * deltaTime * kRouteSpeedBlend;
    }
    throttle += deltaTime * kRouteThrottleRate;
    if ( throttle >= kRouteThrottleMax ) {
        throttle = kRouteThrottleMax;
    }
}

bool LobbyCameraRoutePreview::updateCamera( const float deltaTime ) {
    // FUN_0075b3d0
    if ( roadIt.pRoad == nullptr ) {
        return false;
    }

    advance( deltaTime );

    if ( Vector4DCross( roadSide, roadDirection ).y() < 0.0f ) {
        roadSide *= -1.0f;
    }

    Eigen::Vector4f pitchAxis = ToDirection( roadSide );
    Matrix4x4f_t rotation;
    MatrixRotateZ( rotation, pitchAxis, cameraPitch );

    Eigen::Vector4f target = roadPosition;
    previousCameraPosition = cameraPosition;
    cameraPosition =
        TDUMat4x4MulVec( rotation, roadDirection ) * cameraDistance +
        roadPosition;
    cameraPosition.y() += kRouteCameraHeight;
    if ( deltaTime == 0.0f ) {
        previousCameraPosition = cameraPosition;
    }

    applyUserControl( target, deltaTime );

    CameraVehicle *pCamera = gpCamera->getActiveVehicleCamera();
    const Eigen::Vector4f up( 0.0f, 1.0f, 0.0f, 0.0f );
    pCamera->setUp( up );
    pCamera->setPosition( cameraPosition );
    pCamera->setTarget( target + up );
    pCamera->setNearPlane( kLobbyCameraNearPlane );
    pCamera->setFarPlane( kLobbyCameraFarPlane );
    pCamera->updateMatrix( cameraPosition.head<3>(), up.head<3>(),
                           pCamera->getTarget() );

    const int32_t maxCheckpointIndex = pOwner->getMaxCheckpointIndex();
    EntityCheckPoint *pCheckpoint = nullptr;
    if ( speed >= 0.0f ) {
        int32_t index = nextCheckpoint;
        if ( uint32_t( index ) > uint32_t( maxCheckpointIndex ) ) {
            index -= maxCheckpointIndex;
        }
        pCheckpoint = pOwner->findCheckpoint( index );
    } else {
        pCheckpoint = pOwner->findCheckpoint( nextCheckpoint - 1 );
    }
    if ( pCheckpoint == nullptr || pCheckpoint->getZoneTrigger() == nullptr ) {
        return true;
    }

    Eigen::Vector4f groundPosition = cameraPosition;
    groundPosition.y() -= kRouteCameraHeight;
    Eigen::Vector4f previousGroundPosition = previousCameraPosition;
    previousGroundPosition.y() -= kRouteCameraHeight;
    const EntityVolume *pZoneTrigger =
        static_cast<const EntityVolume *>( pCheckpoint->getZoneTrigger() );
    if ( !pZoneTrigger->isPointInside( groundPosition,
                                       &previousGroundPosition ) ) {
        return true;
    }

    pCheckpoint->setVisible( false );
    checkRouteReversal();

    if ( nextCheckpoint == 1 ) {
        if ( bRunning && 0.0f > speed ) {
            bRunning = false;
        } else if ( speed >= 0.0f ) {
            bRunning = true;
        }
        nextCheckpoint = 2;
    } else if ( ( bLoop && nextCheckpoint == maxCheckpointIndex + 1 ) ||
                ( !bLoop && nextCheckpoint == maxCheckpointIndex ) ) {
        EntityCheckPoint *pLast = pOwner->findCheckpoint( nextCheckpoint );
        if ( pLast != nullptr ) {
            pLast->setVisible( true );
        }
        gpMngFade->startTransition(
            ( gpMngFade->getTransitionFlags() & ~0xcu ) | 1u, kFadeInDuration,
            [this]( void *, void * ) {
                // FUN_00759980
                restart();
            },
            nullptr, this );
        bRunning = false;
        return true;
    } else if ( speed >= 0.0f ) {
        nextCheckpoint++;
    } else {
        nextCheckpoint--;
        if ( nextCheckpoint == 1 ) {
            bRunning = false;
        }
    }

    EntityCheckPoint *pNext = pOwner->findCheckpoint( nextCheckpoint );
    if ( pNext != nullptr ) {
        pNext->setVisible( true );
    }
    return true;
}

void LobbyCameraRoutePreview::advance( const float deltaTime ) {
    // FUN_0075ac90
    AiRoadShape *pShape = roadIt.pRoad->getShape();
    if ( pShape == nullptr ) {
        return;
    }

    if ( transitionState == TS_OnRoad ) {
        const float step = ( speed * deltaTime ) / pShape->TotalLength;
        if ( step > 0.0f && ( ( progress >= 1.0f && roadIt.bWay ) ||
                              ( progress <= 0.0f && !roadIt.bWay ) ) ) {
            pickNextRoad( step );
            return;
        }
        if ( 0.0f > step && ( ( progress >= 1.0f && !roadIt.bWay ) ||
                              ( progress <= 0.0f && roadIt.bWay ) ) ) {
            pickNextRoad( step );
            return;
        }

        const bool bForward = roadIt.bWay;
        progress = bForward ? progress + step : progress - step;
        progress = std::clamp( progress, 0.0f, 1.0f );

        pShape->computeIt( progress, roadIt );
        if ( !bForward ) {
            roadIt.flip();
        }

        roadPosition = roadIt.computeOffsetPosition( 0.0f, 0.0f );
        roadDirection = NormalizeVector4( roadIt.computeDirection() );
    } else if ( transitionState == TS_Junction ||
                transitionState == TS_UTurn ) {
        float curveStep = ( speed * deltaTime ) / transitionCurve.computeLength(
                                                      kRouteCurveLengthStep );
        if ( transitionState == TS_UTurn && 0.0f > curveStep ) {
            curveStep = -curveStep;
        }

        progress = std::clamp( progress + curveStep, 0.0f, 1.0f );
        roadPosition = EvaluateTransitionCurve( transitionCurveType,
                                                transitionCurve, progress );
        roadDirection = transitionCurve.evaluateTangent( progress );
        roadDirection.w() = 0.0f;
        roadDirection = NormalizeVector4( roadDirection );
        roadSide = NormalizeVector4( Eigen::Vector4f(
            roadDirection.z(), 0.0f, -roadDirection.x(), 0.0f ) );

        if ( ( curveStep > 0.0f && progress >= 1.0f ) ||
             ( 0.0f > curveStep && 0.0f >= progress ) ) {
            progress = pShape->computeRoadFraction( roadIt );
            transitionState = TS_OnRoad;
        }
        return;
    } else {
        return;
    }

    roadSide = NormalizeVector4(
        Eigen::Vector4f( roadDirection.z(), 0.0f, -roadDirection.x(), 0.0f ) );
}

void LobbyCameraRoutePreview::pickNextRoad( const float step ) {
    // FUN_0075a310
    transitionState = TS_Junction;

    const bool bTowardsEnd = ( 0.0f > step ) ? !roadIt.bWay : roadIt.bWay;
    AiRoadMap::Road *pRoad = roadIt.pRoad;
    AiRoadMap::Junction *pJunction =
        bTowardsEnd ? pRoad->pJunctionEnd : pRoad->pJunctionBegin;

    AiRoadIt nextIt;
    int32_t nextRoadIndex = -1;
    if ( pJunction != nullptr ) {
        // NOTE: Retail also computes the angle of every junction road
        // relative to the current one here and discards the results.
        for ( AiRouteElem *pElem = pCurrentElem; pElem != nullptr; ) {
            const AiCompressedRoadIt &elemIt =
                ( step > 0.0f ) ? pElem->To : pElem->From;
            elemIt.unpack( nextIt );

            AiRouteElem *pNextElem = nullptr;
            if ( nextIt.pRoad == pRoad ) {
                if ( step > 0.0f && pElem->pNext != nullptr ) {
                    pNextElem = pElem->pNext;
                } else if ( 0.0f > step && pElem->pPrevious != nullptr ) {
                    pNextElem = pElem->pPrevious;
                }
            }

            if ( pNextElem != nullptr ) {
                pNextElem->To.unpack( nextIt );
                for ( int32_t i = 0; i < pJunction->NumRoads; i++ ) {
                    if ( pJunction->getRoad( i ) == nextIt.pRoad ) {
                        pCurrentElem = pNextElem;
                        nextRoadIndex = i;
                        break;
                    }
                }
                break;
            }

            pElem = ( step > 0.0f ) ? pElem->pNext : pElem->pPrevious;
        }

        if ( pCurrentElem == nullptr ) {
            pCurrentElem = path.getHead();
            pCurrentElem->From.unpack( roadIt );
            setRoadPosition( roadIt );
        }
    }

    if ( nextRoadIndex == -1 ) {
        roadIt.bWay = !roadIt.bWay;
        roadIt.T = 0.0f;

        const Eigen::Vector4f position =
            roadIt.computeOffsetPosition( 0.0f, 0.0f );
        const Eigen::Vector4f direction = ComputeEntityDirection( roadIt );
        const Eigen::Vector4f side = roadIt.computeSideDirection();
        SetTransitionCurve(
            transitionCurveType, transitionCurve, position,
            ( direction * -1.0f - side ) * kRouteUTurnHandleLength + position,
            ( direction * -1.0f + side ) * kRouteUTurnHandleLength + position,
            position );
        progress = 0.0f;
        return;
    }

    AiRoadMap::Road *pNextRoad = pJunction->getRoad( nextRoadIndex );
    if ( pNextRoad->pJunctionBegin == pJunction ) {
        if ( step > 0.0f ) {
            nextIt.setRoadEntry( pNextRoad, true );
        } else {
            nextIt.setRoadExit( pNextRoad, false );
        }
    } else if ( pNextRoad->pJunctionEnd == pJunction ) {
        if ( step > 0.0f ) {
            nextIt.setRoadEntry( pNextRoad, false );
        } else {
            nextIt.setRoadExit( pNextRoad, true );
        }
    }

    const Eigen::Vector4f position = roadIt.computeOffsetPosition( 0.0f, 0.0f );
    const Eigen::Vector4f nextPosition =
        nextIt.computeOffsetPosition( 0.0f, 0.0f );
    const float handleLength = ( nextPosition - position ).norm();
    const Eigen::Vector4f direction =
        NormalizeVector4( ComputeEntityDirection( roadIt ) );
    const Eigen::Vector4f nextDirection = ComputeEntityDirection( nextIt );

    const float handleRatio = ( step > 0.0f ) ? kRouteJunctionHandleRatio
                                              : kRouteJunctionHandleBackRatio;
    const Eigen::Vector4f handle =
        direction * ( handleLength * handleRatio ) + position;
    const Eigen::Vector4f nextHandle =
        nextDirection * ( handleLength * kRouteJunctionHandleBackRatio ) +
        nextPosition;
    if ( step > 0.0f ) {
        SetTransitionCurve( transitionCurveType, transitionCurve, position,
                            handle, nextHandle, nextPosition );
        progress = 0.0f;
    } else {
        SetTransitionCurve( transitionCurveType, transitionCurve, nextPosition,
                            nextHandle, handle, position );
        progress = 1.0f;
    }
    roadIt = nextIt;
}

void LobbyCameraRoutePreview::applyUserControl( Eigen::Vector4f &target,
                                                const float deltaTime ) {
    // FUN_00759b60
    if ( gpConfig == nullptr || gpConfig->pPlayerConfig == nullptr ) {
        return;
    }

    float lookRight = 0.0f;
    float lookLeft = 0.0f;
    float lookDown = 0.0f;
    float lookUp = 0.0f;
    if ( gEdFlash.Input.bMouseButton0Down ) {
        const float mouseX =
            ( gEdFlash.Input.MouseCoordinates[0] - 0.5f ) * 2.0f;
        lookRight = std::max( mouseX, 0.0f );
        lookLeft = std::min( mouseX, 0.0f );
        const float mouseY =
            ( gEdFlash.Input.MouseCoordinates[1] - 0.5f ) * 2.0f;
        lookDown = std::max( mouseY, 0.0f );
        lookUp = std::min( mouseY, 0.0f );
    }

    const MouseState &mouse = gpDevice->getMouseState();
    float wheelDown =
        IsChannelJustPressed( mouse.pChannelWheelDown ) ? 1.0f : 0.0f;
    float wheelUp = IsChannelJustPressed( mouse.pChannelWheelUp ) ? 1.0f : 0.0f;
    if ( !bEnabled ) {
        wheelDown = 0.0f;
        wheelUp = 0.0f;
        lookUp = 0.0f;
        lookLeft = 0.0f;
        lookDown = 0.0f;
        lookRight = 0.0f;
    }
    wheelDown = ApplyDeadZone( wheelDown, gAccelDeadZone );
    // NOTE: DAT_00f8a290 is assumed to be gBrakeDeadZone.
    wheelUp = ApplyDeadZone( wheelUp, gBrakeDeadZone );

    if ( bRunning && !pOwner->getMngLobby().isVehicleListOpen() ) {
        const float input = wheelDown - wheelUp;
        if ( bUserControl && kRouteUserInputEpsilon >= std::abs( input ) &&
             kRouteUserIdleTimeout > userControlTime ) {
            speed += ( 0.0f - speed ) * deltaTime * kRouteUserBrakeRate;
            userControlTime += deltaTime;
        } else if ( bUserControl &&
                    kRouteUserTakeoverDelay > userControlTime ) {
            userControlTime += deltaTime;
        } else if ( !bUserControl &&
                    kRouteUserSpeedThreshold >= std::abs( speed ) ) {
            bUserControl = true;
            userControlTime = 0.0f;
        } else {
            speed += input * deltaTime * kRouteUserSpeedRate;
            speed = std::clamp( speed, kRouteUserSpeedMin, kRouteUserSpeedMax );
            if ( bUserControl &&
                 std::abs( speed ) > kRouteUserSpeedThreshold ) {
                userControlTime = 0.0f;
                bUserControl = false;
            }
        }
    } else if ( 0.0f > speed && !bRunning ) {
        speed += ( ( speed + kRouteReverseRecovery ) - speed ) * deltaTime *
                 kRouteReverseRecoveryRate;
    }

    if ( !bRunning && kRouteUserInputEpsilon >= std::abs( userControlTime ) &&
         !bUserControl && deltaTime > 0.0f ) {
        updateCruiseSpeed( deltaTime );
    }

    float yawTarget = 0.0f;
    if ( std::abs( lookRight ) > 0.0f ) {
        yawTarget = 0.0f - std::min( lookRight * kRouteYawScale, kRouteYawMax );
    } else if ( std::abs( lookLeft ) > 0.0f ) {
        yawTarget = 0.0f - std::min( lookLeft * kRouteYawScale, kRouteYawMax );
    }
    yawOffset += ( yawTarget - yawOffset ) * deltaTime * kRouteOffsetBlend;
    yawOffset = std::clamp( yawOffset, kRouteYawMin, kRouteYawMax );

    float heightTarget = 0.0f;
    const float lookVertical =
        ( std::abs( lookDown ) > 0.0f ) ? lookDown : lookUp;
    if ( std::abs( lookVertical ) > 0.0f ) {
        heightTarget = 0.0f - std::min( lookVertical * kRouteHeightScale,
                                        kRouteHeightMax );
    }
    heightOffset +=
        ( heightTarget - heightOffset ) * deltaTime * kRouteOffsetBlend;
    // NOTE: Retail clamps to [2, 10] while the target is never positive, so
    // the offset sits at 2 m.
    heightOffset = std::clamp( heightOffset, kRouteHeightMin, kRouteHeightMax );

    Eigen::Vector4f lookOffset =
        TDUMat4x4MulVec( MakeRotationMatY( yawOffset ),
                         Eigen::Vector4f( target - cameraPosition ) );
    if ( std::abs( lookOffset.x() ) < kRouteLookEpsilon &&
         std::abs( lookOffset.z() ) < kRouteLookEpsilon ) {
        lookOffset.x() = kRouteLookNudge;
    }
    lookOffset.y() += heightOffset;
    target = cameraPosition + lookOffset;
}

void LobbyCameraRoutePreview::checkRouteReversal() {
    // FUN_0075afb0
    if ( transitionState == TS_UTurn ) {
        return;
    }

    AiRoadIt elemIt;
    AiRoadIt nextElemIt;
    bool bSameRoad = false;
    if ( pCurrentElem != nullptr && pCurrentElem->pNext != nullptr &&
         speed > 0.0f ) {
        pCurrentElem->To.unpack( elemIt );
        pCurrentElem->pNext->To.unpack( nextElemIt );
        bSameRoad = elemIt.pRoad == nextElemIt.pRoad;
    } else if ( pCurrentElem != nullptr && pCurrentElem->pPrevious != nullptr &&
                0.0f > speed ) {
        pCurrentElem->To.unpack( elemIt );
        pCurrentElem->pPrevious->To.unpack( nextElemIt );
        bSameRoad = elemIt.pRoad->ID == nextElemIt.pRoad->ID;
    }

    if ( bSameRoad ) {
        const int32_t maxCheckpointIndex = pOwner->getMaxCheckpointIndex();
        if ( ( bLoop && nextCheckpoint == maxCheckpointIndex + 1 ) ||
             ( !bLoop && nextCheckpoint == maxCheckpointIndex ) ) {
            return;
        }

        AiRoadMap::Road *pElemRoad = elemIt.pRoad;
        Matrix4x4f_t frame = Matrix4x4f_t::Identity();
        frame.row( 3 ) = cameraPosition.transpose();

        AiRoadIt cameraIt;
        AiRoadIt checkpointIt;
        const bool bForward = speed > 0.0f;
        int32_t checkpointIndex = nextCheckpoint;
        if ( bForward ) {
            checkpointIndex++;
            if ( uint32_t( checkpointIndex ) >
                 uint32_t( maxCheckpointIndex ) ) {
                checkpointIndex = 1;
            }
        } else {
            checkpointIndex--;
        }
        ProjectCheckpointOnRoad( pOwner, checkpointIndex, checkpointIt );

        if ( checkpointIt.pRoad != pElemRoad ) {
            if ( bForward && transitionState == TS_OnRoad ) {
                transitionState = TS_UTurn;
            }
        } else {
            AiRoadShape *pShape = pElemRoad->getShape();
            if ( pShape != nullptr ) {
                ProjectOnRoadShape( frame, cameraIt, nullptr, false, pShape );
                cameraIt.bWay = roadIt.bWay;
                const float cameraFraction =
                    pShape->computeRoadFraction( cameraIt );
                const float checkpointFraction =
                    pShape->computeRoadFraction( checkpointIt );
                const bool bBehind =
                    bForward ? ( ( cameraFraction > checkpointFraction &&
                                   cameraIt.bWay ) ||
                                 ( checkpointFraction > cameraFraction &&
                                   !cameraIt.bWay ) )
                             : ( ( checkpointFraction > cameraFraction &&
                                   cameraIt.bWay ) ||
                                 ( cameraFraction > checkpointFraction &&
                                   !cameraIt.bWay ) );
                if ( bBehind ) {
                    transitionState = TS_UTurn;
                }
            }
        }
    }

    if ( transitionState != TS_UTurn ) {
        return;
    }

    // NOTE: Retail flips the way without mirroring T (the road position jumps
    // to the mirrored curve parameter).
    roadIt.bWay = !roadIt.bWay;
    const Eigen::Vector4f position = roadIt.computeOffsetPosition( 0.0f, 0.0f );
    const Eigen::Vector4f direction = roadIt.computeDirection();
    const Eigen::Vector4f side = roadIt.computeSideDirection();
    SetTransitionCurve(
        transitionCurveType, transitionCurve, position,
        ( direction * -1.0f - side ) * kRouteUTurnHandleLength + position,
        ( direction * -1.0f + side ) * kRouteUTurnHandleLength + position,
        position );
    progress = 0.0f;
    pCurrentElem =
        ( speed > 0.0f ) ? pCurrentElem->pNext : pCurrentElem->pPrevious;
}

void LobbyCameraRoutePreview::term() {
    // FUN_00759990
    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCamera( true );
    }
}

LobbyCameraStartOrbit::LobbyCameraStartOrbit( GMLobby *pOwner )
    : LobbyCamera( pOwner ),
      position( Eigen::Vector4f::Zero() ),
      angle( 0.0f ),
      bWaitingForStreaming( false ),
      bWaitingForFadeIn( false ),
      streamedTime( 0u ) {
    // FUN_009c3430
}

bool LobbyCameraStartOrbit::init() {
    // FUN_009c3690
    EntityCheckPoint *pStart = pOwner->findCheckpoint( 1 );
    if ( pStart == nullptr ) {
        position = *gpWorld->getStreamingPositionPtr();
    } else {
        position = pStart->getModelMatrix().row( 3 ).transpose();
    }

    gpMngWorld->setDisableStreamingOnCar( true );
    gpCamera->lockScriptedView();
    gpWorld->setStreamingPosition( &position );
    gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() | 8u );

    bWaitingForStreaming = true;
    bWaitingForFadeIn = true;
    streamedTime = 0u;
    return true;
}

void LobbyCameraStartOrbit::update( float deltaTime ) {
    // FUN_009c3450
    if ( bWaitingForStreaming ) {
        if ( gpWorld->isStreamedAround( &position, true, true ) ) {
            MarkStreamingDirty();
            streamedTime = GetTickCountMs();
            bWaitingForStreaming = false;
            bWaitingForFadeIn = true;
        }
        return;
    }

    if ( bWaitingForFadeIn ) {
        if ( streamedTime + kStartOrbitFadeDelayMs < GetTickCountMs() ) {
            StartFadeIn( gpMngFade, kFadeInDuration );
            bWaitingForFadeIn = false;
        }
        return;
    }

    angle = float( fmod( deltaTime * kStartOrbitSpeed + angle, kTwoPi ) );

    CameraVehicle *pCamera = gpCamera->getActiveVehicleCamera();
    pCamera->setTarget( position );
    gpWorld->setStreamingPosition( &position );

    Eigen::Vector4f direction( 3.0f, -2.0f, 0.0f, 0.0f );
    direction.normalize();
    direction =
        ( direction.transpose() * MakeRotationMatY( angle ) ).transpose();

    const Eigen::Vector4f cameraPosition =
        position - direction * kStartOrbitDistance;
    ApplyLobbyCamera( pCamera, cameraPosition, position );
}

void LobbyCameraStartOrbit::term() {
    // FUN_009c3420
    gpCamera->setMouseCam( true );
}

LobbyCameraCheckpointFlyby::LobbyCameraCheckpointFlyby( GMLobby *pOwner )
    : LobbyCamera( pOwner ),
      targetPosition( Eigen::Vector4f::Zero() ),
      cameraPosition( Eigen::Vector4f::Zero() ),
      roadDirection( Eigen::Vector4f::Zero() ),
      roadSide( Eigen::Vector4f::Zero() ),
      spline(),
      splineType( 0 ),
      splineTime( 0.0f ),
      state( FS_BuildSpline ),
      nextCheckpoint( 2 ) {
    // FUN_007592d0
}

bool LobbyCameraCheckpointFlyby::init() {
    // FUN_00759280
    EntityCheckPoint *pStart = pOwner->findCheckpoint( 1 );
    if ( pStart != nullptr ) {
        targetPosition = pStart->getModelMatrix().row( 3 ).transpose();
    }
    state = FS_BuildSpline;
    nextCheckpoint = 2;
    return true;
}

void LobbyCameraCheckpointFlyby::OnFadedOut( void *pData, void * /*pSource*/ ) {
    // FUN_007592e0
    LobbyCameraCheckpointFlyby *pCamera =
        static_cast<LobbyCameraCheckpointFlyby *>( pData );
    pCamera->nextCheckpoint++;
    if ( pCamera->nextCheckpoint > pCamera->pOwner->getMaxCheckpointIndex() ) {
        pCamera->nextCheckpoint = 2;
    }

    Eigen::Vector4f position =
        pCamera->pOwner->findCheckpoint( pCamera->nextCheckpoint )
            ->getModelMatrix()
            .row( 3 )
            .transpose();
    gpWorld->setStreamingPosition( &position );
    pCamera->state = FS_BuildSpline;
}

void LobbyCameraCheckpointFlyby::advance( const float deltaTime ) {
    // FUN_00759340
    splineTime += ( deltaTime * kFlybySpeed ) /
                  spline.computeLength( kFlybySplineLengthStep );
    if ( splineTime > 1.0f ) {
        splineTime = 1.0f;
        state = FS_WaitForFade;
        gpMngFade->startTransition(
            ( gpMngFade->getTransitionFlags() & ~0xcu ) | 1u,
            kFlybyFadeDuration, &LobbyCameraCheckpointFlyby::OnFadedOut,
            nullptr, this );
    }

    if ( splineType == 0 ) {
        QuadraticBezier quadratic;
        quadratic.ControlPoints[0] = spline.ControlPoints[0];
        quadratic.ControlPoints[1] = spline.ControlPoints[1];
        quadratic.ControlPoints[2] = spline.ControlPoints[2];
        cameraPosition = quadratic.evaluate( splineTime );
    } else {
        cameraPosition = spline.evaluate( splineTime );
    }
    cameraPosition.w() = 1.0f;

    CameraVehicle *pCamera = gpCamera->getActiveVehicleCamera();
    pCamera->setTarget( targetPosition );
    ApplyLobbyCamera( pCamera, cameraPosition, targetPosition );
    gpWorld->setStreamingPosition( &cameraPosition );
}

void LobbyCameraCheckpointFlyby::update( float deltaTime ) {
    // FUN_007594e0
    switch ( state ) {
    case FS_BuildSpline: {
        AiRoadIt it;
        // NOTE: Retail has no road check (crashes off-road).
        if ( !ProjectCheckpointOnRoad( pOwner, nextCheckpoint, it ) ) {
            return;
        }

        roadDirection = NormalizeVector4( it.computeDirection() );
        roadSide = NormalizeVector4( Eigen::Vector4f(
            roadDirection.z(), 0.0f, -roadDirection.x(), 0.0f ) );

        const Eigen::Vector4f up = Vector4DCross( roadSide, roadDirection );
        bool bWay = it.bWay;
        if ( up.y() < 0.0f ) {
            roadSide *= -1.0f;
            bWay = !bWay;
        }
        state = FS_WaitForStreaming;
        splineTime = 0.0f;

        const AiRoadProfile *pProfile = it.getEntity()->pProfile;
        float lateralStart = 0.0f;
        float lateralEnd = 0.0f;
        float heightStart = 0.0f;
        float heightEnd = 0.0f;
        pProfile->computeLaneBounds( !bWay, 0, lateralStart, lateralEnd,
                                     heightStart, heightEnd );
        float ratioStart = 0.0f;
        float ratioEnd = 0.0f;
        pProfile->computeLaneWidthRatios( 0, !bWay, ratioStart, ratioEnd );

        if ( bWay ) {
            targetPosition = it.computeOffsetPosition(
                lateralStart - ratioStart, heightStart );
        } else {
            targetPosition =
                it.computeOffsetPosition( lateralEnd - ratioEnd, heightEnd );
        }
        targetPosition.y() += kFlybyTargetHeight;

        Eigen::Vector4f startPosition = it.computeOffsetPosition( 0.0f, 0.0f );

        Eigen::Vector4f rotationAxis = ToDirection( roadSide );
        Matrix4x4f_t rotation;
        MatrixRotateZ( rotation, rotationAxis, kFlybyEndRotation );
        Eigen::Vector4f endPosition =
            TDUMat4x4MulVec( rotation, Eigen::Vector4f( -roadDirection ) ) *
                kFlybyEndDistance +
            targetPosition;

        const Eigen::Vector4f delta = endPosition - startPosition;
        const float length = delta.norm();
        const Eigen::Vector4f direction = NormalizeVector4( delta );
        Eigen::Vector4f firstHandle =
            startPosition + direction * ( length * kOneThird );
        Eigen::Vector4f secondHandle =
            startPosition + direction * ( length * kTwoThirds );

        const float roadHeight = startPosition.y();
        firstHandle.y() = roadHeight + kFlybyFirstHandleHeight;
        firstHandle.w() = 1.0f;
        startPosition.y() = roadHeight + kFlybyStartHeight;
        secondHandle.y() = roadHeight + kFlybyEndHeight;
        secondHandle.w() = 1.0f;
        endPosition.y() = roadHeight + kFlybyEndHeight;

        // FUN_0040a5c0
        spline.ControlPoints[0] = startPosition;
        spline.ControlPoints[1] = firstHandle;
        spline.ControlPoints[2] = secondHandle;
        spline.ControlPoints[3] = endPosition;
        splineType = 1;

        advance( 0.0f );
        gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() | 8u );
        break;
    }
    case FS_WaitForStreaming:
        if ( !IsStreamedAtCamera( cameraPosition ) ) {
            return;
        }
        StartFadeIn( gpMngFade, kFlybyFadeDuration );
        advance( deltaTime );
        state = FS_Flying;
        break;
    case FS_Flying:
        advance( deltaTime );
        break;
    default:
        break;
    }
}

void LobbyCameraCheckpointFlyby::term() {
    // FUN_00759270
    // NOP
}

LobbyCameraPlayersView::LobbyCameraPlayersView( GMLobby *pOwner )
    : LobbyCamera( pOwner ),
      angle( 0.0f ),
      target( Eigen::Vector4f::Zero() ),
      spawnPosition( Eigen::Vector4f::Zero() ),
      startFrame( Matrix4x4f_t::Identity() ),
      fovCoef( 0.18f ),        // DAT_00f41ba0
      targetDistance( 20.0f ), // DAT_00f03ee0
      orbitPeriod( 8.0f ),     // DAT_00f41ac4
      orbitRadius( 2.0f ),     // DAT_00f416b0
      orbitOffsetY( 10.0f ),   // FLOAT_00f4166c
      orbitOffsetZ( -15.0f ),  // DAT_00f41b9c
      orbitOffsetX( 7.0f ) {   // DAT_00f41844
    // FUN_009c30e0
}

bool LobbyCameraPlayersView::init() {
    // FUN_009c32e0
    // NOTE: Retail dereferences the start checkpoint unconditionally.
    EntityCheckPoint *pStart = pOwner->findCheckpoint( 1 );
    if ( pStart != nullptr ) {
        startFrame = pStart->getModelMatrix();
    }

    const Eigen::Vector4f localTarget( 0.0f, 0.0f, targetDistance, 1.0f );
    target = ( localTarget.transpose() * startFrame ).transpose();

    CameraVehicle *pCamera = gpCamera->getActiveVehicleCamera();
    pCamera->setFOV( fovCoef * kPi );

    spawnPosition = target;
    gpMngWorld->setDisableStreamingOnCar( true );
    gpCamera->lockScriptedView();
    gpWorld->setStreamingPosition( &target );
    return true;
}

void LobbyCameraPlayersView::update( float deltaTime ) {
    // FUN_009c3160
    angle = float( fmod( deltaTime / orbitPeriod * kPi + angle, kTwoPi ) );

    CameraVehicle *pCamera = gpCamera->getActiveVehicleCamera();
    pCamera->setTarget( target );
    gpWorld->setStreamingPosition( &target );

    const Eigen::Vector4f localPosition(
        cosf( angle ) * orbitRadius + orbitOffsetX,
        sinf( angle ) * kPlayersViewBobAmplitude + orbitOffsetY, orbitOffsetZ,
        1.0f );
    const Eigen::Vector4f cameraPosition =
        ( localPosition.transpose() * startFrame ).transpose();
    ApplyLobbyCamera( pCamera, cameraPosition, target );
}

void LobbyCameraPlayersView::term() {
    // FUN_009c30d0
    gpCamera->setMouseCam( true );
}
