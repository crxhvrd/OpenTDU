#include "shared.h"
#include "ai_vehicle.h"

#include "ai/ai_config.h"
#include "ai/ai_sim_bubble.h"
#include "ai/gs_ai.h"
#include "database/gs_database.h"
#include "game/game_object.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "gs_timer.h"
#include "physics/vehicle/physics_vehicle.h"

static constexpr float kKmhToMs = 0.27777779f;     // FLOAT_00f41868
static constexpr float kDefaultMaxSpeed = 45.0f;   // FLOAT_00f41808
static constexpr float kSpeedSmoothing = 0.8f;     // DAT_00faa9a8
static constexpr float kSpeedEpsilon = 1.0e-4f;    // DAT_00f41760
static constexpr float kPositionEpsilon = 1.0e-5f; // 0x3727c5ac
static constexpr float kFadeDistanceMin = 100.0f;  // DAT_00faa9c0
static constexpr float kFadeDistanceMax = 200.0f;  // DAT_00faa9bc
static constexpr float kWarningBlinkPeriod = 0.5f; // FLOAT_00f41674
static float gDebugAlphaOverride = -1.0f;          // DAT_00faa9b8

bool IsLocallyOwned( CarState *pCarState ) {
    // FUN_0096a700
    (void)pCarState;
    return true; // TODO: ONLINE
}

AiVehicleInfo::AiVehicleInfo()
    : GroundMatrix( Matrix4x4f_t::Identity() ),
      bValid( false ),
      Speed( 0.0f ),
      SmoothedSpeed( 0.0f ),
      BrakingDistC1( 0.0f ),
      BrakingDistC2( 0.0f ),
      TurningSpeedC1( 0.0f ),
      TurningSpeedC2( 0.0f ),
      HandlingMark( 100.0f ),
      MaxSpeed( 150.0f ),
      LastTimeSeenFront( -1.0f ),
      LastTimeSeenRear( -1.0f ),
      Visibility( 0.0f ),
      bVisible( false ),
      bHandbrake( false ),
      BlockedTime( 0.0f ),
      LastRoadPos(),
      LastRoadLane( 0 ),
      NetPos( Eigen::Vector4f::Zero() ),
      bCrashed( false ) {
    reset();
}

void AiVehicleInfo::reset() {
    // FUN_00924710
    GroundMatrix = Matrix4x4f_t::Identity();
    bValid = false;
    Speed = 0.0f;
    SmoothedSpeed = 0.0f;
    LastTimeSeenFront = -1.0f;
    LastTimeSeenRear = -1.0f;
    Visibility = 0.0f;
    bVisible = false;
    bHandbrake = false;
    BlockedTime = 0.0f;
    bCrashed = false;
}

AiVehSignals::AiVehSignals()
    : HornCommandsQueue{},
      HornCommandIndex( 0 ),
      bHornActive( false ),
      HornLastTime( 0.0f ),
      HeadlightCommandQueue{},
      HeadlightCommandIndex( 0 ),
      bHeadlightsActivated( false ),
      HeadlightLastTime( 0.0f ),
      HeadlightBlinkingCycle( 0.3f ),
      HeadlightBlinkingDelay( 1.0f ),
      bHeadlightActivated( true ),
      bHeadlightBlinking( false ),
      bWarningLeft( false ),
      bWarningRight( false ),
      bWarningActive( false ),
      WarningTimeNextBlink( 0.0f ),
      bHornOn( false ),
      bHeadlightOn( true ),
      bWarningLeftOn( false ),
      bWarningRightOn( false ),
      bFlashlightOn( false ),
      bIsCops( false ),
      pCarState( nullptr ) {}

void AiVehSignals::reset() {
    // FUN_00920d20
    const bool bWasCops = bIsCops;
    CarState *pCar = pCarState;
    *this = AiVehSignals();
    bIsCops = bWasCops;
    pCarState = pCar;
    apply();
}

void AiVehSignals::tick( const float deltaTime, const float totalTime ) {
    // FUN_00920c00
    if ( deltaTime <= 0.0f ) {
        return;
    }

    auto clampSlot = []( const int32_t index ) {
        return ( index < 0 || index > 7 ) ? 0 : index;
    };

    if ( bHornActive ) {
        if ( totalTime - HornLastTime >=
             HornCommandsQueue[clampSlot( HornCommandIndex )] ) {
            bHornOn = !bHornOn;
            HornCommandIndex++;
            if ( HornCommandsQueue[clampSlot( HornCommandIndex )] <= 0.0f ) {
                bHornOn = false;
                HornCommandIndex = 0;
                bHornActive = false;
            }
        }
    }

    if ( !bHeadlightsActivated ) {
        bHeadlightOn = bHeadlightActivated;
    } else if ( totalTime - HeadlightLastTime >=
                HeadlightCommandQueue[clampSlot( HeadlightCommandIndex )] ) {
        bHeadlightOn = !bHeadlightOn;
        HeadlightCommandIndex++;
        if ( !bHeadlightBlinking ) {
            if ( HeadlightCommandQueue[clampSlot( HeadlightCommandIndex )] <=
                 0.0f ) {
                bHeadlightOn = bHeadlightActivated;
                HeadlightCommandIndex = 0;
                bHeadlightsActivated = false;
            }
        } else {
            if ( HeadlightCommandQueue[clampSlot( HeadlightCommandIndex )] <=
                 0.0f ) {
                HeadlightCommandIndex = 0;
            }
            HeadlightLastTime = totalTime;
        }
    }

    if ( bWarningActive && totalTime >= WarningTimeNextBlink ) {
        if ( bWarningLeft ) {
            bWarningLeftOn = !bWarningLeftOn;
        }
        if ( bWarningRight ) {
            bWarningRightOn = !bWarningRightOn;
        }
        WarningTimeNextBlink = totalTime + kWarningBlinkPeriod;
    }

    apply();
}

void AiVehSignals::apply() {
    // FUN_009209c0
    if ( pCarState == nullptr ) {
        return;
    }

    CarBodyState *pBody = pCarState->getBody();
    pBody->bHonking = bHornOn;
    pBody->bHeadlightsOn = bHeadlightOn;
    pBody->bIndicatingLeft = bWarningLeftOn;
    pBody->bIndicatingRight = bWarningRightOn;
    pBody->bPoliceGyroEnabled = bFlashlightOn;
}

AiVehicle::AiVehicle()
    : brakingDistanceCoef( 1.0f ),
      turningSpeedCoef( 1.0f ),
      infos(),
      signals(),
      carStateIndex( -1 ),
      pCarState( nullptr ),
      pTemplate( nullptr ),
      category( eAiVehicleCategory::AVC_None ),
      locoTraffic(),
      pLoco( nullptr ),
      locoType( kLocomotionNone ),
      navTraffic(),
      pNav( nullptr ),
      navType( kNavigationNone ),
      ctrlRoute(),
      pController( nullptr ),
      controllerType( kControllerNone ),
      bhvTraffic(),
      pBehaviour( nullptr ),
      behaviourType( kBehaviourNone ),
      bRemovable( false ),
      bPendingRemoval( false ),
      removalTime( 0.0f ),
      fadeInEndTime( 0.0f ),
      bFadeByDistance( false ),
      bFadeActive( false ),
      bPlayerCar( false ),
      bSoftCollision( false ),
      bFlashFade( false ),
      bFlashStarted( false ),
      flashStartTime( 0.0f ),
      netSendTime( 0.0f ),
      netReceiveTime( 0.0f ),
      random(),
      removalReason{} {
    // FUN_00926690
    random.setSeed( 0x3039 );
    OTDU_UNIMPLEMENTED;
}

bool AiVehicle::isPhysicsDriven() const {
    // FUN_00923530
    return pLoco != nullptr && pLoco->isPhysicsDriven();
}

float AiVehicle::computeTurningSpeed( const float radius ) const {
    // FUN_009243c0
    static constexpr float kRadiusBlendStart = 30.0f;        // DAT_00faa9b4
    static constexpr float kRadiusBlendEnd = 100.0f;         // DAT_00faa9b0
    static constexpr float kRadiusBlendStartScale = 1.0f;    // DAT_00f7c3c4
    static constexpr float kTrafficTurningSpeedScale = 0.8f; // DAT_00faa9ac

    if ( radius < 0.0f ) {
        return -1.0f;
    }

    const float scale = std::clamp(
        ( radius - kRadiusBlendStart ) * ( 1.0f - kRadiusBlendStartScale ) /
                ( kRadiusBlendEnd - kRadiusBlendStart ) +
            kRadiusBlendStartScale,
        std::min( kRadiusBlendStartScale, 1.0f ),
        std::max( kRadiusBlendStartScale, 1.0f ) );
    const float scaledRadius = scale * radius;
    float speed = sqrtf( scaledRadius ) * infos.TurningSpeedC1 +
                  scaledRadius * infos.TurningSpeedC2;
    if ( speed < 1.0f ) {
        speed = 1.0f;
    }

    if ( locoType == 0 ) {
        return std::min( kTrafficTurningSpeedScale * speed, infos.MaxSpeed );
    }

    if ( fabsf( infos.MaxSpeed - speed ) > 1.0f ) {
        float coef = turningSpeedCoef;
        if ( pLoco != nullptr && !pLoco->isPhysicsDriven() ) {
            coef = std::clamp( coef, 0.0f, 1.0f );
        }
        speed *= coef;
    }
    return speed;
}

float ComputeBrakingDistance( const float coefLinear,
                              const float coefQuadratic,
                              const float speedFrom,
                              const float speedTo,
                              const float slope ) {
    // FUN_00924110
    static constexpr float kGravity = 9.81f; // DAT_00f7c3b8

    if ( speedTo > speedFrom ) {
        return -ComputeBrakingDistance( coefLinear, coefQuadratic, speedTo,
                                        speedFrom, slope );
    }

    const float from = std::max( speedFrom, 0.0f );
    const float to = std::max( speedTo, 0.0f );
    const float speedSqDiff = from * from - to * to;
    float distance = coefLinear * ( from - to ) + coefQuadratic * speedSqDiff;
    if ( distance <= 0.0f ) {
        return 0.0f;
    }

    if ( distance >= 1.0f && slope != 0.0f && speedSqDiff >= 1.0f ) {
        const float invDistance =
            kGravity * slope * 2.0f / speedSqDiff + 1.0f / distance;
        if ( invDistance > 1.0e-4f ) {
            distance = 1.0f / invDistance;
        }
    }
    return distance;
}

float AiVehicle::computeBrakingDistance( const float speedFrom,
                                         const float speedTo,
                                         const float slope ) const {
    // FUN_00924200
    static constexpr float kBrakingDistanceScale = 1.0f; // DAT_00f7c3c8

    if ( speedTo > speedFrom ) {
        return -computeBrakingDistance( speedTo, speedFrom, slope );
    }
    return ComputeBrakingDistance( infos.BrakingDistC1, infos.BrakingDistC2,
                                   speedFrom, speedTo, slope ) *
           brakingDistanceCoef * kBrakingDistanceScale;
}

void AiVehicle::setLocomotion( const int32_t type,
                               const bool bReset,
                               const bool bKeepPhysics ) {
    // FUN_009235b0
    if ( pLoco != nullptr ) {
        pLoco->detach();
    }
    locoType = type;
    if ( type == kLocomotionNone ) {
        pLoco = nullptr;
        return;
    }

    if ( type != 0 ) {
        OTDU_UNIMPLEMENTED_ONCE;
    }
    pLoco = &locoTraffic;
    if ( bReset ) {
        pLoco->reset( true );
    }
    pLoco->setResetPhysicsOnAttach( !bKeepPhysics );
    pLoco->attach( this );
}

void AiVehicle::setNavigation( const int32_t type ) {
    // FUN_009236b0
    if ( pNav != nullptr ) {
        pNav->detach();
    }
    navType = type;
    if ( type == kNavigationNone ) {
        pNav = nullptr;
        return;
    }

    if ( type != 0 ) {
        OTDU_UNIMPLEMENTED_ONCE;
    }
    pNav = &navTraffic;
    pNav->attach( this );
    const int32_t numWarmUpTicks = ( type == 0 ) ? 5 : 1;
    for ( int32_t i = 0; i < numWarmUpTicks; i++ ) {
        if ( pNav != nullptr ) {
            pNav->tick( 0.0f, 0.0f );
        }
    }
}

void AiVehicle::onNavigationLost() {
    // FUN_00923960
    if ( infos.bCrashed ) {
        return;
    }
    infos.bCrashed = true;
    if ( pLoco != nullptr ) {
        pLoco->setDrivenByNavigation( false );
        pLoco->setManualInputs( 0.0f, 1.0f, 0.0f, 0.0f, 0.0f );
    }
    if ( pNav != nullptr ) {
        pNav->detach();
    }
    navType = kNavigationNone;
    pNav = nullptr;

    if ( !signals.bWarningLeft || !signals.bWarningRight ) {
        signals.bWarningLeft = true;
        signals.bWarningRight = true;
        signals.bWarningLeftOn = true;
        signals.bWarningRightOn = true;
        signals.bWarningActive = true;
        signals.WarningTimeNextBlink = gpTimer->GameTotalTime + 0.5f;
    }
}

void AiVehicle::setController( const int32_t type ) {
    // FUN_00923a10
    if ( pController != nullptr ) {
        pController->release();
    }
    controllerType = type;
    if ( type == kControllerNone ) {
        pController = nullptr;
        return;
    }

    if ( type != 0 ) {
        OTDU_UNIMPLEMENTED_ONCE;
    }
    pController = &ctrlRoute;
    pController->attach( this );
    pController->tick( 0.0f, 0.0f );
}

void AiVehicle::setBehaviour( const int32_t type ) {
    // FUN_00923890
    if ( pBehaviour != nullptr ) {
        pBehaviour->release();
    }
    behaviourType = type;
    if ( type == kBehaviourNone ) {
        pBehaviour = nullptr;
        return;
    }

    if ( type != 2 ) {
        OTDU_UNIMPLEMENTED_ONCE;
        pBehaviour = nullptr;
        return;
    }
    pBehaviour = &bhvTraffic;
    pBehaviour->attach( this );
    pBehaviour->tick( 0.0f, 0.0f );
}

void AiVehicle::initialize( const eAiVehicleCategory newCategory,
                            CarState *pCar,
                            const AiRoadIt &location,
                            const int32_t lane,
                            const int32_t newBehaviourType,
                            const int32_t newControllerType,
                            const int32_t newNavType,
                            const int32_t newLocoType,
                            const bool bKeepPhysics ) {
    // FUN_00928000
    if ( pCar == nullptr || pCar->getPhysicsVehicle() == nullptr ) {
        return;
    }

    pCarState = pCar;
    category = newCategory;
    if ( newCategory != eAiVehicleCategory::AVC_Network ) {
        OTDU_UNIMPLEMENTED_ONCE;
    }
    gpCarState->registerCallback( pCar, this, nullptr );

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar != nullptr && pCar == pLocalCar ) {
        OTDU_UNIMPLEMENTED_ONCE;
    }

    removalTime = -1.0f;
    fadeInEndTime = -1.0f;
    bRemovable = true;
    bPendingRemoval = false;
    bSoftCollision = false;
    bPlayerCar = false;
    bFlashFade = false;
    bFlashStarted = false;
    flashStartTime = 0.0f;
    netSendTime = gpTimer->GameTotalTime;
    netReceiveTime = gpTimer->GameTotalTime;
    bFadeActive = false;
    bFadeByDistance = false;

    infos.reset();
    signals.pCarState = pCarState;
    signals.bIsCops = ( newBehaviourType == 0 );
    signals.reset();

    PhysicsVehicle *pPhysics = pCar->getPhysicsVehicle();
    if ( !bKeepPhysics ) {
        pPhysics->resetSimulationState();
    }

    const Eigen::Vector4f position = pCar->getWorldPosition();
    infos.bValid = !position.head<3>().isZero( kPositionEpsilon );
    infos.GroundMatrix = pCar->getWorldMatrix();
    infos.LastRoadPos = location;
    infos.LastRoadLane = lane;
    if ( location.pRoad != nullptr ) {
        pCar->setRoadLocation( location, false );
    }

    const DBEntryCarPhysicsData *pCarData =
        gpDatabase->getCarDBEntry( pCar->getCarID() );
    infos.MaxSpeed = ( pCarData != nullptr ) ? pCarData->TopSpeedKMH * kKmhToMs
                                             : kDefaultMaxSpeed;
    infos.BrakingDistC1 = 0.3f;
    infos.BrakingDistC2 = 0.033f;
    infos.TurningSpeedC1 = 2.8f;
    infos.TurningSpeedC2 = 0.0f;
    infos.HandlingMark = 100.0f;
    for ( int32_t i = 0; i < gAIConfig.NumVehiclePhysics; i++ ) {
        const AIVehiclePhysics &entry = gAIConfig.VehiclePhysics[i];
        if ( entry.DatabaseId != pCar->getCarID() ) {
            continue;
        }
        infos.TurningSpeedC1 = entry.TurningSpeedC1;
        infos.TurningSpeedC2 = entry.TurningSpeedC2;
        infos.HandlingMark = entry.HandlingMark;
        infos.BrakingDistC1 = entry.BrakingDistC1;
        infos.BrakingDistC2 = entry.BrakingDistC2;
        if ( entry.LogMaxSpeed > 0.0f ) {
            infos.MaxSpeed = entry.LogMaxSpeed;
        }
        break;
    }
    infos.Speed = infos.MaxSpeed;
    infos.SmoothedSpeed = infos.MaxSpeed;

    setBehaviour( newBehaviourType );
    setController( newControllerType );
    setNavigation( newNavType );

    if ( pNav == nullptr ) {
        infos.Speed = 0.0f;
    } else if ( navType == 1 ) {
        OTDU_UNIMPLEMENTED_ONCE;
        infos.Speed = 0.0f;
    } else {
        Eigen::Vector4f direction;
        if ( !pNav->getTarget( direction, infos.Speed ) ) {
            infos.Speed = 0.0f;
        }
    }
    infos.SmoothedSpeed = infos.Speed;

    setLocomotion( newLocoType, true, bKeepPhysics );

    VehicleBase *pDisplay = pCar->getVehicleInstance();
    if ( pDisplay != nullptr ) {
        pDisplay->onEvent( pCar, 0.0f, nullptr );
    }

    fadeInEndTime = gAIConfig.FadeInTime + gpTimer->GameTotalTime;
    switch ( category ) {
    case eAiVehicleCategory::AVC_Traffic:
    case eAiVehicleCategory::AVC_Cop:
    case eAiVehicleCategory::AVC_CopDam:
    case eAiVehicleCategory::AVC_Parked:
    case eAiVehicleCategory::AVC_Bot:
        bFadeActive = true;
        bFadeByDistance = true;
        if ( pDisplay != nullptr ) {
            pDisplay->setAlpha( 0.0f );
        }
        break;
    default:
        break;
    }
}

void AiVehicle::release() {
    // FUN_00927eb0
    if ( pCarState != nullptr ) {
        OTDU_UNIMPLEMENTED_ONCE;
        gpCarState->unregisterCallback( pCarState, this );
    }
    if ( pLoco != nullptr ) {
        pLoco->detach();
    }
    if ( pNav != nullptr ) {
        pNav->detach();
    }
    if ( pController != nullptr ) {
        pController->release();
    }
    if ( pBehaviour != nullptr ) {
        pBehaviour->release();
    }
    pBehaviour = nullptr;
    pController = nullptr;
    pNav = nullptr;
    pLoco = nullptr;

    infos.reset();
    signals.reset();
    signals.pCarState = nullptr;
    bPlayerCar = false;
    bSoftCollision = false;
    bFlashFade = false;
    bFlashStarted = false;
    flashStartTime = 0.0f;
}

void AiVehicle::detachCarState() {
    pCarState = nullptr;
    signals.reset();
    signals.pCarState = nullptr;
    signals.reset();
}

void AiVehicle::requestRemoval( const float delay, const char *pReason ) {
    // FUN_00923a90
    if ( !bRemovable ) {
        return;
    }
    const float now = gpTimer->GameTotalTime;
    if ( bPendingRemoval && now + delay > removalTime && removalTime >= 0.0f ) {
        return;
    }
    bPendingRemoval = true;
    removalTime = ( delay >= 0.0f ) ? now + delay : -1.0f;
    strncpy( removalReason, pReason, kRemovalReasonLength );
    removalReason[kRemovalReasonLength - 1] = '\0';
}

void AiVehicle::tick( const float deltaTime, const float totalTime ) {
    // FUN_00926560
    signals.tick( deltaTime, totalTime );
    if ( bPendingRemoval && totalTime >= removalTime ) {
        return;
    }
    updateFade( totalTime );
    updateFlashFade();
    if ( pBehaviour != nullptr ) {
        pBehaviour->tick( deltaTime, totalTime );
    }
    if ( pController != nullptr ) {
        pController->tick( deltaTime, totalTime );
    }
    if ( pNav != nullptr ) {
        pNav->tick( deltaTime, totalTime );
    }
    if ( pLoco != nullptr ) {
        pLoco->tick( deltaTime, totalTime );
    }
}

void AiVehicle::updateFade( const float totalTime ) {
    // FUN_00924c10
    if ( !bFadeActive ) {
        return;
    }

    const float fadeIn = gAIConfig.FadeInTime;
    const float fadeOut = gAIConfig.FadeOutTime;
    float alpha = 1.0f;
    if ( fadeInEndTime > fadeIn + totalTime + 1.0f ) {
        fadeInEndTime = totalTime;
    }
    if ( removalTime > totalTime + fadeOut + 1.0f ) {
        removalTime = totalTime * 2.0f + fadeOut; // sic
    }

    if ( removalTime >= 0.0f ) {
        if ( totalTime > removalTime ) {
            alpha = 0.0f;
        } else if ( totalTime > removalTime - fadeOut ) {
            // sic: always <= 0 here, so orig pcode cuts instead of fading out
            alpha =
                std::clamp( ( totalTime - removalTime ) / fadeOut, 0.0f, 1.0f );
        }
    } else if ( fadeInEndTime > totalTime ) {
        alpha = std::clamp( 1.0f - ( fadeInEndTime - totalTime ) / fadeIn, 0.0f,
                            1.0f );
        if ( bFadeByDistance && pCarState != nullptr &&
             category != eAiVehicleCategory::AVC_Opponent ) {
            const Eigen::Vector4f delta = gpAI->getSimBubble().getPosition() -
                                          pCarState->getWorldPosition();
            const float distance = delta.norm();
            const float distanceAlpha =
                std::clamp( 1.0f - ( distance - kFadeDistanceMin ) /
                                       ( kFadeDistanceMax - kFadeDistanceMin ),
                            0.0f, 1.0f );
            alpha = std::max( alpha, distanceAlpha );
        }
    }

    VehicleBase *pDisplay =
        ( pCarState != nullptr ) ? pCarState->getVehicleInstance() : nullptr;
    if ( pDisplay == nullptr ) {
        return;
    }
    pDisplay->setAlpha( ( gDebugAlphaOverride >= 0.0f ) ? gDebugAlphaOverride
                                                        : alpha );
    if ( pDisplay->getAlpha() >= 1.0f ) {
        bFadeActive = false;
    }
}

void AiVehicle::updateFlashFade() {
    // FUN_009245e0
    if ( !bFlashFade || pCarState == nullptr ||
         pCarState->getVehicleInstance() == nullptr ) {
        return;
    }
    OTDU_UNIMPLEMENTED_ONCE;
}

void AiVehicle::onEvent( CarState *pCar, float deltaTime, void *pData ) {
    // FUN_009257a0
    (void)deltaTime;
    (void)pData;
    if ( !isPhysicsDriven() && locoType != kLocomotionNone ) {
        return;
    }

    infos.bValid = true;
    infos.Speed = pCar->getBody()->SpeedInKmh;
    infos.SmoothedSpeed = ( 1.0f - kSpeedSmoothing ) * infos.Speed +
                          kSpeedSmoothing * infos.SmoothedSpeed;
    if ( infos.SmoothedSpeed >= -kSpeedEpsilon &&
         infos.SmoothedSpeed <= kSpeedEpsilon ) {
        infos.SmoothedSpeed = 0.0f;
    }

    infos.GroundMatrix = pCar->getWorldMatrix();
}

Eigen::Vector4f AiVehicle::computeCenterPosition() const {
    // FUN_00924530
    const float halfHeight = pCarState->getBody()->BodyDimensions.y() * 0.5f;
    return infos.GroundMatrix.row( 3 ) +
           infos.GroundMatrix.row( 1 ) * halfHeight;
}

void AiVehicle::setGroundMatrixFromCenter( const Matrix4x4f_t &center ) {
    // FUN_00924b40
    const float halfHeight = pCarState->getBody()->BodyDimensions.y() * 0.5f;
    infos.GroundMatrix = center;
    infos.GroundMatrix.row( 3 ) -= center.row( 1 ) * halfHeight;
}
