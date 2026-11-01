#include "shared.h"
#include "ai_vehicle.h"

#include "ai/ai_bot_manager.h"
#include "ai/ai_config.h"
#include "ai/ai_debug_draw.h"
#include "ai/ai_oriented_box.h"
#include "ai/ai_road_buckets.h"
#include "ai/ai_road_shape.h"
#include "ai/ai_sim_bubble.h"
#include "ai/gs_ai.h"
#include "database/gs_database.h"
#include "game/game_object.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "gs_timer.h"
#include "physics/vehicle/physics_vehicle.h"
#include "world/otdu_lights.h"

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
    // OTDU_ADDITION: headlights stay on at night
    pBody->bHeadlightsOn = bHeadlightOn || AreOTDUHeadlightsOn();
    pBody->bIndicatingLeft = bWarningLeftOn;
    pBody->bIndicatingRight = bWarningRightOn;
    pBody->bPoliceGyroEnabled = bFlashlightOn;
}

AiVehicle::AiVehicle()
    : dpc(),
      infos(),
      signals(),
      infringementChecker( this ),
      carStateIndex( -1 ),
      pCarState( nullptr ),
      pTemplate( nullptr ),
      category( eAiVehicleCategory::AVC_None ),
      locoTraffic(),
      locoOpponent(),
      pLoco( nullptr ),
      locoType( kLocomotionNone ),
      navTraffic(),
      navOpponent(),
      navCinematic(),
      navTrainer(),
      pNav( nullptr ),
      navType( kNavigationNone ),
      ctrlChase(),
      ctrlRoute(),
      ctrlRacePath(),
      ctrlFreeRoam(),
      ctrlCurrentRoad(),
      pController( nullptr ),
      controllerType( kControllerNone ),
      bhvOpponent(),
      bhvTraffic(),
      bhvBot(),
      bhvPolice(),
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
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    random.setSeed( 0x3039 );
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
        float coef = dpc.CurveSpeedFactor;
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
           dpc.BrakingDistanceFactor * kBrakingDistanceScale;
}

void AiVehicle::getAxleSlipRatios( float &outRear,
                                   float &outFront,
                                   float &outMaxSlip ) const {
    // FUN_00923d80
    PhysicsVehicle *pPhysics =
        ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;
    const int32_t numWheels =
        ( pPhysics != nullptr )
            ? static_cast<int32_t>( pPhysics->getNumWheels() )
            : 0;
    if ( numWheels < 1 ) {
        outRear = 0.0f;
        outFront = 0.0f;
        outMaxSlip = 0.0f;
        return;
    }

    const int32_t quarter = numWheels / 4;
    const int32_t shift = numWheels / 2 - 1;
    const PhysicsWheelState &frontA =
        pPhysics->getWheelState( quarter + ( 0 << shift ) );
    const PhysicsWheelState &frontB = pPhysics->getWheelState( 0 << shift );
    const PhysicsWheelState &rearA =
        pPhysics->getWheelState( quarter + ( 1 << shift ) );
    const PhysicsWheelState &rearB = pPhysics->getWheelState( 1 << shift );

    const float frontVelLong =
        ( frontA.ContactVelLong + frontB.ContactVelLong ) * 0.5f;
    const float rearVelLong =
        ( rearA.ContactVelLong + rearB.ContactVelLong ) * 0.5f;
    outFront = ( frontA.ContactVelLat + frontB.ContactVelLat ) * 5.0f;
    outRear = ( rearA.ContactVelLat + rearB.ContactVelLat ) * 5.0f;
    if ( frontVelLong > 0.1f ) {
        outFront /= frontVelLong;
    }
    if ( rearVelLong > 0.1f ) {
        outRear /= rearVelLong;
    }

    const float slipFrontB = fabsf( frontB.NormLongClamped );
    const float slipFrontA = fabsf( frontA.NormLongClamped );
    const float slipRearB = fabsf( rearB.NormLongClamped );
    const float slipRearA = fabsf( rearA.NormLongClamped );
    if ( slipFrontB > slipFrontA && slipFrontB > slipRearB &&
         slipFrontB > slipRearA ) {
        outMaxSlip = frontB.NormLongClamped;
    } else if ( slipFrontA > slipRearB && slipFrontA > slipRearA ) {
        outMaxSlip = frontA.NormLongClamped;
    } else if ( slipRearB > slipRearA ) {
        outMaxSlip = rearB.NormLongClamped;
    } else {
        outMaxSlip = rearA.NormLongClamped;
    }
}

bool AiVehicle::isOverSteering() const {
    // FUN_00923f90
    float rear = 0.0f;
    float front = 0.0f;
    float maxSlip = 0.0f;
    getAxleSlipRatios( rear, front, maxSlip );
    if ( ( rear > 0.0f && front > 0.0f ) || ( rear < 0.0f && front < 0.0f ) ) {
        return fabsf( rear ) > fabsf( front ) && fabsf( rear ) > 1.0f;
    }
    return fabsf( rear ) > 1.0f && fabsf( front ) > 1.0f;
}

bool AiVehicle::isUnderSteering() const {
    // FUN_00924030
    float rear = 0.0f;
    float front = 0.0f;
    float maxSlip = 0.0f;
    getAxleSlipRatios( rear, front, maxSlip );
    if ( ( rear > 0.0f && front > 0.0f ) || ( rear < 0.0f && front < 0.0f ) ) {
        return fabsf( front ) > fabsf( rear ) && fabsf( front ) > 1.0f;
    }
    return false;
}

bool AiVehicle::isFullSliding() const {
    // FUN_009240b0
    float rear = 0.0f;
    float front = 0.0f;
    float maxSlip = 0.0f;
    getAxleSlipRatios( rear, front, maxSlip );
    return ( rear > 1.0f && front > 1.0f ) || ( rear < -1.0f && front < -1.0f );
}

int32_t AiVehicle::computeCurrentLane() const {
    // FUN_009269c0
    const ObjectWorldLocation &location = pCarState->getWorldLocation();
    if ( location.LocationType != ObjectWorldLocation::OWL_Road ) {
        return 0;
    }
    const AiRoadIt it = infos.LastRoadPos;
    if ( it.pRoad == nullptr ) {
        return 0;
    }

    const float lateralMin = location.LocationRoad.LateralMin;
    const float lateralMax = location.LocationRoad.LateralMax;
    AiRoadMap::Road *pLocatedRoad =
        gpAI->getRoadMap().getRoad( location.LocationRoad.RoadID );
    if ( pLocatedRoad != nullptr ) {
        pLocatedRoad->getShape();
    }

    const AiRoadProfile *pProfile = it.getEntity()->pProfile;
    const int32_t laneMin =
        pProfile->computeLaneFromWidthRatio( it.bWay, lateralMin );
    const int32_t laneMax =
        pProfile->computeLaneFromWidthRatio( it.bWay, lateralMax );
    if ( laneMin == laneMax ) {
        return laneMin;
    }

    float ratioStart = 0.0f;
    float ratioEnd = 0.0f;
    pProfile->computeLaneWidthRatios( laneMin, it.bWay, ratioStart, ratioEnd );
    return ( fabsf( lateralMin - ratioEnd ) > fabsf( lateralMax - ratioEnd ) )
               ? laneMin
               : laneMax;
}

bool AiVehicle::isLaneFree( const int32_t lane,
                            const AiRoadIt &position,
                            const float timeHorizon,
                            const float distanceMin,
                            const float distanceMax ) const {
    // FUN_00926ec0
    static constexpr float kVelocityEpsilon =
        1e-6f; // DAT_00f41888 / -DAT_00f420e8
    static constexpr float kNoContactTime = 10000.0f; // DAT_00f416c4

    // Time window [outEnter, outExit] during which a moving interval overlaps
    // [lo, hi]
    auto ComputeOverlapTimes = []( const float velocity, const float lo,
                                   const float hi, const float objectMin,
                                   const float objectMax, float &outEnter,
                                   float &outExit ) {
        if ( velocity > kVelocityEpsilon || velocity < -kVelocityEpsilon ) {
            const float invVelocity = 1.0f / velocity;
            const float from = ( velocity > kVelocityEpsilon ) ? lo : hi;
            const float to = ( velocity > kVelocityEpsilon ) ? hi : lo;
            outEnter = std::min( ( from - objectMin ) * invVelocity,
                                 ( from - objectMax ) * invVelocity );
            outExit = std::max( ( to - objectMin ) * invVelocity,
                                ( to - objectMax ) * invVelocity );
            return;
        }
        const bool bInside = ( lo <= objectMin && objectMin <= hi ) ||
                             ( lo <= objectMax && objectMax <= hi );
        outEnter = bInside ? 0.0f : kNoContactTime;
        outExit = kNoContactTime;
    };

    if ( position.pRoad == nullptr ) {
        return false;
    }
    AiRoadBuckets::Bucket *pBucket =
        gpAI->getRoadBuckets().getRoadBucket( position.pRoad->ID );
    const AiRoadShape *pShape = position.pRoad->getShape();

    float ratioStart = -1.0f;
    float ratioEnd = 1.0f;
    if ( lane >= 0 ) {
        const AiRoadProfile *pProfile = position.getEntity()->pProfile;
        pProfile->computeLaneWidthRatios( lane, position.bWay, ratioStart,
                                          ratioEnd );
    }

    const float fraction = pShape->computeRoadFraction( position );
    const float invLength = 1.0f / pShape->TotalLength;
    float fractionLo = 0.0f;
    float fractionHi = 0.0f;
    if ( position.bWay ) {
        fractionLo = invLength * distanceMin + fraction;
        fractionHi = invLength * distanceMax + fraction;
    } else {
        fractionLo = fraction - invLength * distanceMax;
        fractionHi = fraction - invLength * distanceMin;
    }
    if ( 0.0f > fractionLo || fractionHi > 1.0f ) {
        return false;
    }
    if ( pBucket == nullptr ) {
        return true;
    }

    const uint32_t selfID = pCarState->getID();
    for ( GameObject *pObject = pBucket->pFirstObject; pObject != nullptr;
          pObject = pObject->getNextInBucket() ) {
        if ( selfID == pObject->getID() || selfID == pObject->getParentID() ) {
            continue;
        }
        const ObjectWorldLocation &location = pObject->getWorldLocation();
        if ( location.LocationType != ObjectWorldLocation::OWL_Road ) {
            continue;
        }

        float enterAlong = 0.0f;
        float exitAlong = 0.0f;
        ComputeOverlapTimes( location.VelocityX, fractionLo, fractionHi,
                             location.LocationRoad.FractionMin,
                             location.LocationRoad.FractionMax, enterAlong,
                             exitAlong );
        float enterLateral = 0.0f;
        float exitLateral = 0.0f;
        ComputeOverlapTimes( location.VelocityZ, ratioStart, ratioEnd,
                             location.LocationRoad.LateralMin,
                             location.LocationRoad.LateralMax, enterLateral,
                             exitLateral );

        const float enter = std::max( enterAlong, enterLateral );
        const float exit = std::min( exitAlong, exitLateral );
        if ( exit >= enter && timeHorizon >= enter && exit >= 0.0f ) {
            return false;
        }
    }
    return true;
}

float AiVehicle::computeSphereContactTime( GameObject *pObject ) const {
    // FUN_00924880
    static constexpr float kMaxContactTimeSq = 2500.0f; // DAT_00f41c5c
    static constexpr float kStaticSpeedSq =
        1e-4f; // DAT_00f41760 (-: DAT_00f41890)
    static constexpr float kDiscriminantTolerance = -0.001f; // DAT_00f41a4c

    Eigen::Vector4f objectCenter = Eigen::Vector4f::Zero(); // DAT_00fac350
    float objectRadius = 0.0f;
    const Eigen::Vector4f &objectVelocity = pObject->getLinearVelocity();
    pObject->retrieveBoundingSphere( &objectCenter, &objectRadius );

    Eigen::Vector4f center = Eigen::Vector4f::Zero();
    float radius = 0.0f;
    const Eigen::Vector4f &velocity = pCarState->getLinearVelocity();
    pCarState->retrieveBoundingSphere( &center, &radius );

    const Eigen::Vector4f delta = objectCenter - center;
    const Eigen::Vector4f relativeVelocity = objectVelocity - velocity;
    const float a = relativeVelocity.dot( relativeVelocity );
    const float b = delta.dot( relativeVelocity ) * 2.0f;
    const float sumRadius = radius + objectRadius;
    const float c = delta.dot( delta ) - sumRadius * sumRadius;
    if ( c > a * kMaxContactTimeSq ) {
        return -1.0f;
    }
    if ( a >= -kStaticSpeedSq && a <= kStaticSpeedSq ) {
        return ( c <= 0.0f ) ? 0.0f : -1.0f;
    }

    float discriminant = b * b - a * c * 4.0f;
    if ( discriminant < kDiscriminantTolerance ) {
        return -1.0f;
    }
    discriminant = std::max( discriminant, 0.0f );
    const float root = sqrtf( discriminant );
    const float tEnter = ( -b - root ) / ( a * 2.0f );
    const float tExit = ( root - b ) / ( a * 2.0f );
    if ( tExit > tEnter && tEnter >= 0.0f ) {
        return tEnter;
    }
    if ( tExit >= 0.0f ) {
        return tExit;
    }
    return -1.0f;
}

float AiVehicle::computeRoadContactTime( GameObject *pObject ) const {
    // FUN_00923c10
    static constexpr float kStaticVelocity =
        1e-5f; // DAT_00f41710 (-: DAT_00f41aac)
    static constexpr float kNoExitTime = 1000.0f; // DAT_00f41718

    const ObjectWorldLocation &self = pCarState->getWorldLocation();
    const ObjectWorldLocation &other = pObject->getWorldLocation();
    const ObjectLocationRoad &roadA = self.LocationRoad;
    const ObjectLocationRoad &roadB = other.LocationRoad;
    const float velocityLong = other.VelocityX - self.VelocityX;
    const float velocityLat = other.VelocityZ - self.VelocityZ;

    float longEnter = 0.0f;
    float longExit = kNoExitTime;
    if ( velocityLong > -kStaticVelocity && velocityLong < kStaticVelocity ) {
        if ( !( roadB.FractionMax > roadA.FractionMin ) ||
             !( roadA.FractionMax > roadB.FractionMin ) ) {
            return -1.0f;
        }
    } else {
        const float invVelocity = 1.0f / velocityLong;
        longEnter = ( roadA.FractionMin - roadB.FractionMax ) * invVelocity;
        longExit = ( roadA.FractionMax - roadB.FractionMin ) * invVelocity;
    }

    float latEnter = 0.0f;
    float latExit = kNoExitTime;
    if ( velocityLat > -kStaticVelocity && velocityLat < kStaticVelocity ) {
        if ( !( roadB.LateralMax > roadA.LateralMin ) ||
             !( roadA.LateralMax > roadB.LateralMin ) ) {
            return -1.0f;
        }
    } else {
        const float invVelocity = 1.0f / velocityLat;
        latEnter = ( roadA.LateralMin - roadB.LateralMax ) * invVelocity;
        latExit = ( roadA.LateralMax - roadB.LateralMin ) * invVelocity;
    }

    if ( longEnter > longExit ) {
        std::swap( longEnter, longExit );
    }
    if ( latEnter > latExit ) {
        std::swap( latEnter, latExit );
    }
    const float enter = std::max( longEnter, latEnter );
    const float exit = std::min( longExit, latExit );
    if ( enter > exit || exit < 0.0f ) {
        return -1.0f;
    }
    return std::max( enter, 0.0f );
}

float AiVehicle::findClosestObstacle( GameObject **ppOutObstacle ) const {
    // FUN_00925e40
    static constexpr float kMaxContactTime = 50.0f; // DAT_00f41848

    float bestTime = -1.0f;
    if ( !infos.bValid ) {
        return bestTime;
    }

    const uint32_t selfID = pCarState->getID();
    for ( GameObject *pObject = gGameObjectDico.getFirstObject();
          pObject != nullptr; pObject = pObject->getNextInDico() ) {
        if ( selfID == pObject->getID() || selfID == pObject->getParentID() ) {
            continue;
        }

        float contactTime = computeSphereContactTime( pObject );
        if ( contactTime < 0.0f || contactTime > kMaxContactTime ) {
            contactTime = -1.0f;
        } else {
            const ObjectWorldLocation &otherLocation =
                pObject->getWorldLocation();
            const ObjectWorldLocation &selfLocation =
                pCarState->getWorldLocation();
            if ( otherLocation.LocationType == ObjectWorldLocation::OWL_Road &&
                 selfLocation.LocationType == otherLocation.LocationType &&
                 otherLocation.LocationRoad.RoadID ==
                     selfLocation.LocationRoad.RoadID ) {
                contactTime = computeRoadContactTime( pObject );
            } else {
                contactTime = ComputeObjectsContactTime( pCarState, pObject,
                                                         kMaxContactTime );
            }
        }

        if ( bestTime < 0.0f ||
             ( contactTime >= 0.0f && contactTime < bestTime ) ) {
            bestTime = contactTime;
            if ( ppOutObstacle != nullptr ) {
                *ppOutObstacle = pObject;
            }
        }
    }
    return bestTime;
}

GameObject *FindObjectAheadOnRoad( const ObjectWorldLocation &location,
                                   const uint32_t selfID,
                                   float *pOutGap,
                                   float *pOutSpeed ) {
    // FUN_009275a0
    if ( location.LocationType != ObjectWorldLocation::OWL_Road ) {
        return nullptr;
    }

    const uint32_t roadID = location.LocationRoad.RoadID;
    AiRoadBuckets::Bucket *pBucket =
        gpAI->getRoadBuckets().getRoadBucket( roadID );
    AiRoadMap::Road *pRoad = gpAI->getRoadMap().getRoad( roadID );
    if ( pBucket == nullptr || pRoad == nullptr ) {
        return nullptr;
    }
    const AiRoadShape *pShape = pRoad->getShape();

    GameObject *pClosest = nullptr;
    float closestGap = 0.0f;
    float closestSpeed = 0.0f;
    for ( GameObject *pObject = pBucket->pFirstObject; pObject != nullptr;
          pObject = pObject->getNextInBucket() ) {
        if ( selfID == pObject->getID() || selfID == pObject->getParentID() ) {
            continue;
        }

        const ObjectWorldLocation &other = pObject->getWorldLocation();
        if ( other.LocationType != ObjectWorldLocation::OWL_Road ||
             location.LocationType != other.LocationType ||
             other.LocationRoad.RoadID != roadID ) {
            continue;
        }
        if ( location.bWay ) {
            if ( !( other.LocationRoad.FractionMax >
                    location.LocationRoad.FractionMax ) ) {
                continue;
            }
        } else if ( !( location.LocationRoad.FractionMin >
                       other.LocationRoad.FractionMin ) ) {
            continue;
        }
        if ( !( location.LocationRoad.LateralMax >
                other.LocationRoad.LateralMin ) ||
             !( other.LocationRoad.LateralMax >
                location.LocationRoad.LateralMin ) ) {
            continue;
        }

        const float gap =
            location.computeFractionGap( other ) * pShape->TotalLength;
        if ( pClosest == nullptr || closestGap > gap ) {
            pClosest = pObject;
            closestSpeed = pShape->TotalLength * other.VelocityX;
            closestGap = gap;
        }
    }

    if ( pOutGap != nullptr ) {
        *pOutGap = closestGap;
    }
    if ( pOutSpeed != nullptr ) {
        *pOutSpeed = closestSpeed;
    }
    return pClosest;
}

bool FindObjectApproachingJunction( const uint32_t junctionID,
                                    const uint32_t roadID,
                                    GameObject **ppOutObject,
                                    float *pOutDistance ) {
    // FUN_00926ce0
    static constexpr float kStaticVelocity = 1e-5f;  // DAT_00f41710
    static constexpr float kNoContactTime = 1000.0f; // DAT_00f41718
    static constexpr float kMaxDistance = 50.0f;     // DAT_00f41848
    static constexpr float kMaxContactTime = 10.0f;  // DAT_00f4166c

    AiRoadMap::Road *pRoad = gpAI->getRoadMap().getRoad( roadID );
    if ( pRoad == nullptr ) {
        return false;
    }

    bool bAtEnd = false;
    if ( pRoad->pJunctionEnd != nullptr &&
         pRoad->pJunctionEnd->ID == junctionID ) {
        bAtEnd = true;
    } else if ( pRoad->pJunctionBegin == nullptr ||
                pRoad->pJunctionBegin->ID != junctionID ) {
        return false;
    }

    const AiRoadShape *pShape = pRoad->getShape();
    AiRoadBuckets::Bucket *pBucket =
        gpAI->getRoadBuckets().getRoadBucket( roadID );
    if ( pBucket == nullptr ) {
        return false;
    }

    float minFraction = 1.0f;
    GameObject *pMinObject = nullptr;
    float maxFraction = 0.0f;
    GameObject *pMaxObject = nullptr;
    float velocity = 0.0f;
    int32_t index = 0;
    for ( GameObject *pObject = pBucket->pFirstObject;
          pObject != nullptr && index < pBucket->NumObjects;
          pObject = pObject->getNextInBucket(), index++ ) {
        const ObjectWorldLocation &location = pObject->getWorldLocation();
        if ( location.LocationType != ObjectWorldLocation::OWL_Road ||
             pObject->getParentID() != 0 || ( location.bWay != 0 ) != bAtEnd ) {
            continue;
        }
        if ( minFraction > location.LocationRoad.FractionMin ) {
            minFraction = location.LocationRoad.FractionMin;
            pMinObject = pObject;
        }
        if ( location.LocationRoad.FractionMax > maxFraction ) {
            maxFraction = location.LocationRoad.FractionMax;
            pMaxObject = pObject;
        }
        velocity = location.VelocityX;
    }

    GameObject *pObject = nullptr;
    float distance = 0.0f;
    float contactTime = kNoContactTime;
    if ( bAtEnd ) {
        pObject = pMaxObject;
        distance = pShape->TotalLength * ( 1.0f - maxFraction );
        if ( velocity > kStaticVelocity ) {
            contactTime = ( 1.0f - maxFraction ) / velocity;
        }
    } else {
        pObject = pMinObject;
        distance = pShape->TotalLength * minFraction;
        if ( velocity > kStaticVelocity ) {
            contactTime = 0.0f - minFraction / velocity;
        }
    }

    if ( ppOutObject != nullptr ) {
        *ppOutObject = pObject;
    }
    if ( pOutDistance != nullptr ) {
        *pOutDistance = distance;
    }
    return ( kMaxDistance > distance || kMaxContactTime > contactTime ) &&
           pObject != nullptr;
}

GameObject *AiVehicle::findObjectAheadInLane( const AiRoadIt &it,
                                              const int32_t lane,
                                              float *pOutGap,
                                              float *pOutSpeed ) const {
    // FUN_00927710
    AiRoadShape *pShape = it.pRoad->getShape();
    const float fraction = pShape->computeRoadFraction( it );
    const AiRoadProfile *pProfile = it.getEntity()->pProfile;
    float ratioStart = 0.0f;
    float ratioEnd = 0.0f;
    pProfile->computeLaneWidthRatios( lane, it.bWay, ratioStart, ratioEnd );

    ObjectWorldLocation location;
    location.LocationType = ObjectWorldLocation::OWL_Road;
    location.bWay = it.bWay ? 1 : 0;
    location.bNearJunction = 0;
    location.LocationRoad.RoadID = it.pRoad->ID;
    location.LocationRoad.FractionMin = fraction;
    location.LocationRoad.FractionMax = fraction;
    location.LocationRoad.LateralMin = ratioStart;
    location.LocationRoad.LateralMax = ratioEnd;

    const uint32_t selfID =
        ( pCarState != nullptr ) ? pCarState->getID() : 0xffffffffu;
    return FindObjectAheadOnRoad( location, selfID, pOutGap, pOutSpeed );
}

bool AiVehicle::isJunctionOccupied( const uint32_t junctionID ) const {
    // FUN_00923b80
    AiRoadBuckets::Bucket *pBucket =
        gpAI->getRoadBuckets().getJunctionBucket( junctionID );
    if ( pBucket == nullptr ) {
        return false;
    }

    const uint32_t selfID = pCarState->getID();
    int32_t index = 0;
    for ( GameObject *pObject = pBucket->pFirstObject;
          pObject != nullptr && index < pBucket->NumObjects;
          pObject = pObject->getNextInBucket(), index++ ) {
        if ( selfID != pObject->getID() && selfID != pObject->getParentID() ) {
            return true;
        }
    }
    return false;
}

bool AiVehicle::isCopInPursuit() const {
    // FUN_00923a60
    if ( behaviourType != 0 || navType != 1 ) {
        return false;
    }
    return bhvPolice.isPursuing();
}

int32_t CollectRoadObjects( const uint32_t roadID,
                            GameObject **ppOutObjects,
                            const int32_t maxObjects,
                            const float fractionMin,
                            const float fractionMax,
                            const int32_t lane,
                            const bool bWay ) {
    // FUN_00927250
    AiRoadMap::Road *pRoad = gpAI->getRoadMap().getRoad( roadID );
    if ( pRoad == nullptr ) {
        return 0;
    }
    AiRoadBuckets::Bucket *pBucket =
        gpAI->getRoadBuckets().getRoadBucket( roadID );

    AiRoadIt it;
    it.setRoadEntry( pRoad, bWay );
    pRoad->getShape();

    float ratioStart = -1.0f;
    float ratioEnd = 1.0f;
    if ( lane >= 0 ) {
        it.getEntity()->pProfile->computeLaneWidthRatios(
            lane, bWay, ratioStart, ratioEnd );
    }

    const float windowMin = bWay ? fractionMin : 1.0f - fractionMin;
    const float windowMax = bWay ? fractionMax : 1.0f - fractionMax;
    auto NearEdge = [bWay]( const ObjectWorldLocation &location ) {
        return bWay ? location.LocationRoad.FractionMin
                    : 1.0f - location.LocationRoad.FractionMin;
    };

    int32_t numObjects = 0;
    if ( pBucket == nullptr ) {
        return numObjects;
    }
    for ( GameObject *pObject = pBucket->pFirstObject;
          pObject != nullptr && numObjects < maxObjects;
          pObject = pObject->getNextInBucket() ) {
        const ObjectWorldLocation &location = pObject->getWorldLocation();
        if ( location.LocationType != ObjectWorldLocation::OWL_Road ) {
            continue;
        }
        const float nearEdge = NearEdge( location );
        const float farEdge = bWay ? location.LocationRoad.FractionMax
                                   : 1.0f - location.LocationRoad.FractionMax;
        if ( !( farEdge >= windowMin ) || !( windowMax >= nearEdge ) ||
             !( location.LocationRoad.LateralMax >= ratioStart ) ||
             !( ratioEnd >= location.LocationRoad.LateralMin ) ) {
            continue;
        }

        int32_t slot = 0;
        while ( slot < numObjects &&
                !( NearEdge( ppOutObjects[slot]->getWorldLocation() ) >
                   nearEdge ) ) {
            slot++;
        }
        for ( int32_t i = numObjects; i > slot; i-- ) {
            ppOutObjects[i] = ppOutObjects[i - 1];
        }
        ppOutObjects[slot] = pObject;
        numObjects++;
    }
    return numObjects;
}

GameObject *FindObjectInLaneNear( const uint32_t roadID,
                                  const float fraction,
                                  const int32_t lane,
                                  const bool bWay,
                                  bool bAhead,
                                  const uint32_t selfID ) {
    // FUN_00927420
    if ( !bWay ) {
        bAhead = !bAhead;
    }

    AiRoadBuckets::Bucket *pBucket =
        gpAI->getRoadBuckets().getRoadBucket( roadID );
    AiRoadMap::Road *pRoad = gpAI->getRoadMap().getRoad( roadID );
    if ( pBucket == nullptr || pRoad == nullptr ) {
        return nullptr;
    }
    pRoad->getShape();

    AiRoadIt it;
    it.setRoadEntry( pRoad, bWay );
    float ratioStart = -1.0f;
    float ratioEnd = 1.0f;
    it.getEntity()->pProfile->computeLaneWidthRatios( lane, it.bWay, ratioStart,
                                                      ratioEnd );

    GameObject *pClosest = nullptr;
    float closest = 1.0f;
    for ( GameObject *pObject = pBucket->pFirstObject; pObject != nullptr;
          pObject = pObject->getNextInBucket() ) {
        if ( selfID == pObject->getID() || selfID == pObject->getParentID() ) {
            continue;
        }
        const ObjectWorldLocation &location = pObject->getWorldLocation();
        if ( location.LocationType != ObjectWorldLocation::OWL_Road ||
             ratioStart > location.LocationRoad.LateralMax ||
             location.LocationRoad.LateralMin > ratioEnd ) {
            continue;
        }
        const float center = ( location.LocationRoad.FractionMin +
                               location.LocationRoad.FractionMax ) *
                             0.5f;
        const float delta = bAhead ? center - fraction : fraction - center;
        if ( closest >= delta && delta > 0.0f ) {
            closest = delta;
            pClosest = pObject;
        }
    }
    return pClosest;
}

void AiVehicle::applyBotPersonality( const int32_t context,
                                     const int32_t personality,
                                     const int32_t difficulty ) {
    // FUN_0090c460
    static constexpr float kGearIndexToUpshiftBand = 0.1f; // DAT_00f41668

    const AiVehIndDPCPattern *pPattern = gpAI->getBotPersonalities().getPattern(
        difficulty, personality, context );
    if ( pPattern == nullptr ) {
        dpc.reset();
        return;
    }

    dpc.DPCPattern = *pPattern;
    dpc.randomizeFactors( gpAI->getRandom() );
    if ( pCarState != nullptr && pCarState->getPhysicsVehicle() != nullptr ) {
        pCarState->getPhysicsVehicle()->setUpshiftBand(
            static_cast<float>( dpc.DPCPattern.RPMToChangeGearIndex ) *
            kGearIndexToUpshiftBand );
    }
}

void AiVehicle::assignBotPersonality( const int32_t botIndex ) {
    // FUN_0090bfa0
    const DBEntryBot *pBot = GetBotDBEntry( botIndex );
    if ( pBot == nullptr ) {
        dpc.pName[0] = '\0';
        dpc.BotID = -1;
        dpc.BotSectorIndex = -1;
        return;
    }

    dpc.BotID = botIndex;
    dpc.BotSectorIndex = pBot->ZoneIndex;
    strncpy( dpc.pName,
             ( pBot->FirstName.pAsString != nullptr )
                 ? pBot->FirstName.pAsString
                 : "",
             AiVehIndDPC::kNameLength );

    struct BehaviourPreset {
        uint64_t Hashcode;
        int32_t Difficulty;
        int32_t Personality;
    };
    static constexpr BehaviourPreset kBehaviourPresets[] = {
        {0x32fefe8, 2, 1},
        {0x33dab88, 2, 2},
        {0x33f3228, 1, 1},
        {0x34e7468, 0, 1},
        {0x35c3008, 0, 2},
        {0x36b7248, 2, 3},
        {0x37ab488, 1, 3},
        {0x389f6c8, 0, 3},
        {0x3993908, 2, 0},
        {0x3a87b48, 1, 0},
        {0x3b7bd88, 0, 0},
    };

    const uint64_t behaviour =
        gpDatabase->getGameDB().Databases[0xe].getHashcodeForString(
            pBot->Behaviour.pAsString );
    dpc.Difficulty = 1;
    dpc.Personality = 2;
    for ( const BehaviourPreset &preset : kBehaviourPresets ) {
        if ( preset.Hashcode == behaviour ) {
            dpc.Difficulty = preset.Difficulty;
            dpc.Personality = preset.Personality;
            break;
        }
    }
}

void AiVehicle::initializeParked( CarState *pCar ) {
    // FUN_00928830
    initialize( eAiVehicleCategory::AVC_Parked, pCar, AiRoadIt(), 0,
                kBehaviourNone, kControllerNone, kNavigationNone, 2, false );
    bRemovable = false;
    signals.bHeadlightActivated = false;
}

void AiVehicle::initializeCop( CarState *pCar,
                               const AiRoadIt &location,
                               const int32_t lane ) {
    // FUN_009229c0 (inlined)
    initialize( eAiVehicleCategory::AVC_Cop, pCar, location, lane, 0, 0, 0, 0,
                false );
    bhvPolice.requestState( AiVehBhvPolice::PS_Patrol );
    bRemovable = false;
}

void AiVehicle::initializeCopDam( CarState *pCar ) {
    // FUN_009287a0
    const AiRoadIt location = infos.LastRoadPos;
    initialize( eAiVehicleCategory::AVC_CopDam, pCar, location, 0, 0,
                kControllerNone, kNavigationNone, 2, false );
    bSoftCollision = true;
    bhvPolice.requestState( AiVehBhvPolice::PS_PoliceDam );
    locoParked.startParked();
    bRemovable = false;
    signals.bHeadlightActivated = false;
}

void AiVehicle::InfringementChecker::check( CarState *pCar, float deltaTime ) {
    // FUN_00927e90
    if ( pOwner->bPlayerCar ) {
        pCar->getPoliceState().checkInfringements( pCar, deltaTime );
    }
}

void AiVehicle::initializeCinematic( CarState *pCar ) {
    // FUN_009284a0
    static constexpr float kCinematicSmoothSteerCoef = 0.25f; // DAT_00faa9c4

    initialize( eAiVehicleCategory::AVC_Cinematic, pCar, AiRoadIt(), 0,
                kBehaviourNone, kControllerNone, 2, 0, false );
    locoTraffic.setSmoothSteering( true, kCinematicSmoothSteerCoef );
    if ( pLoco != nullptr ) {
        pLoco->setPhysicsActive( true, false );
    }
}

void AiVehicle::initializeOpponent( CarState *pCar,
                                    const Matrix4x4f_t &startFrame,
                                    const int32_t newControllerType,
                                    const AiRoadIt &startHint ) {
    // FUN_00928650
    static constexpr float kOpponentSmoothSteerCoef = 0.2f; // DAT_00faa9c8

    AiRoadIt location;
    AiRoadMap::Road *pRoad = gpAI->getRoadMap().findNearestRoad(
        startFrame.row( 3 ).transpose(), true );
    if ( pRoad != nullptr ) {
        Matrix4x4f_t projectedFrame = Matrix4x4f_t::Identity();
        ProjectOnRoadShape( startFrame, location, &projectedFrame, false,
                            pRoad->getShape() );
    }
    if ( startHint.pRoad != nullptr && location.pRoad != startHint.pRoad ) {
        location = startHint;
    }

    pCar->getBody()->OffsetedWorldMatrix = startFrame;
    initialize( eAiVehicleCategory::AVC_Opponent, pCar, location, 0, 1,
                newControllerType, 1, 1, false );
    if ( pLoco != nullptr ) {
        pLoco->setPhysicsActive( true, false );
    }
    locoOpponent.setSmoothSteering( true, kOpponentSmoothSteerCoef );
    bRemovable = false;
    signals.bHeadlightActivated = false;
}

void AiVehicle::initializeTrainingDriver( const Matrix4x4f_t &startFrame,
                                          const AiRoadIt &startHint ) {
    // FUN_00928520
    CarState *pCar = gpScenaric->getLocalPlayer().getCarState();
    AiRoadIt location;
    AiRoadMap::Road *pRoad = gpAI->getRoadMap().findNearestRoad(
        startFrame.row( 3 ).transpose(), true );
    Matrix4x4f_t projectedFrame = Matrix4x4f_t::Identity();
    ProjectOnRoadShape( startFrame, location, &projectedFrame, false,
                        pRoad->getShape() );
    if ( startHint.pRoad != nullptr && location.pRoad != startHint.pRoad ) {
        location = startHint;
    }

    pCar->getBody()->WorldMatrix = startFrame;
    initialize( eAiVehicleCategory::AVC_Network, pCar, location, 0, 4, 2, 1,
                kLocomotionNone, true );
    bRemovable = false;
    signals.bHeadlightActivated = false;
}

void AiVehicle::reset( const bool bFull, const bool bKeepSecondary ) {
    // FUN_00924e40
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
    netReceiveTime = netSendTime;
    bFadeActive = false;
    bFadeByDistance = false;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AiVehicle+0x2308 holder release
    if ( !bFull ) {
        return;
    }

    dpc.reset();
    infos.reset();
    if ( !bKeepSecondary ) {
        infos.reset();
    }
    if ( pLoco != nullptr ) {
        pLoco->reset( true );
    }
    if ( pNav != nullptr ) {
        pNav->reset( true );
    }
    if ( pController != nullptr ) {
        pController->reset( true );
    }
    if ( pBehaviour != nullptr ) {
        pBehaviour->reset( true );
    }
}

void AiVehicle::startFade( const bool bByDistance ) {
    // FUN_009246e0
    bFadeActive = true;
    bFadeByDistance = bByDistance;
    VehicleBase *pDisplay = pCarState->getVehicleInstance();
    if ( pDisplay != nullptr ) {
        pDisplay->setAlpha( 0.0f );
    }
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

    if ( type == 1 ) {
        pLoco = &locoOpponent;
    } else if ( type == 2 ) {
        pLoco = &locoParked;
    } else {
        pLoco = &locoTraffic;
    }
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

    if ( type == 1 ) {
        pNav = &navOpponent;
    } else if ( type == 2 ) {
        pNav = &navCinematic;
    } else if ( type == 3 ) {
        pNav = &navTrainer;
    } else {
        pNav = &navTraffic;
    }
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

    if ( type == 1 ) {
        pController = &ctrlChase;
    } else if ( type == 2 ) {
        pController = &ctrlRacePath;
    } else if ( type == 6 ) {
        pController = &ctrlCurrentRoad;
    } else if ( type == 7 ) {
        pController = &ctrlFreeRoam;
    } else {
        if ( type != 0 && type != 3 && type != 4 && type != 5 ) {
            OTDU_UNIMPLEMENTED_ONCE;
        }
        pController = &ctrlRoute;
    }
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

    if ( type == 0 ) {
        pBehaviour = &bhvPolice;
    } else if ( type == 1 ) {
        pBehaviour = &bhvOpponent;
    } else if ( type == 2 ) {
        pBehaviour = &bhvTraffic;
    } else if ( type == 3 ) {
        pBehaviour = &bhvBot;
    } else if ( type == 4 ) {
        pBehaviour = &bhvPassive;
    } else {
        OTDU_UNIMPLEMENTED_ONCE;
        pBehaviour = nullptr;
        return;
    }
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
        pCar->getPhysicsVehicle()->setAutoReverseAllowed( false );
    }
    gpCarState->registerCallback( pCar, this, nullptr );

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar != nullptr && pCar == pLocalCar ) {
        gpCarState->setPoliceStateJob( pCar, &infringementChecker );
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
    infos.GroundMatrix( 3, 1 ) -= pPhysics->computeGroundToOriginOffset();
    infos.LastRoadPos = location;
    infos.LastRoadLane = lane;
    if ( location.pRoad != nullptr ) {
        pCar->setRoadLocation( location, nullptr );
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
        infos.Speed = pPhysics->getChassisVelocity().head<3>().norm();
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
        if ( pCarState->getPhysicsVehicle() != nullptr ) {
            pCarState->getPhysicsVehicle()->setAutoReverseAllowed( true );
        }
        gpCarState->unregisterCallback( pCarState, this );
        gpCarState->setPoliceStateJob( pCarState, nullptr );
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
    // TODO: ONLINE - feed the net interpolation slot (+0x2308, GSAI+0x1398
    // pool, allocated by the network update FUN_00927a10) with the ground
    // matrix and centre (FUN_00924530 + FUN_00921a20)
}

void AiVehicle::debugDraw( DrawList *pDrawList ) {
    // FUN_00925940
    drawNetPositionBox( pDrawList );
    if ( pBehaviour != nullptr ) {
        pBehaviour->debugDraw( pDrawList );
    }
    if ( pController != nullptr ) {
        pController->debugDraw( pDrawList );
    }
    if ( pNav != nullptr ) {
        pNav->debugDraw( pDrawList );
    }
    if ( pLoco != nullptr ) {
        pLoco->debugDraw( pDrawList );
    }
}

void AiVehicle::drawNetPositionBox( DrawList *pDrawList ) {
    // FUN_00925890
    const bool bInOnlineSession = false; // TODO: ONLINE (gpCP2PGameService)
    if ( !bInOnlineSession || pCarState == nullptr ) {
        return;
    }

    Eigen::Vector3f extents = pCarState->getDimensions().head<3>();
    Matrix4x4f_t matrix = MakeScaleMatrix( extents ) * infos.GroundMatrix;
    matrix.row( 3 ) = infos.NetPos.transpose();
    DrawDebugBox( matrix, 0xff00ffff, *pDrawList, 2 );
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
    static constexpr float kFlashAlphaMin = 0.5f; // FLOAT_00f41674

    const PhysicsVehicle *pPhysics = pCarState->getPhysicsVehicle();
    if ( pPhysics == nullptr || pPhysics->getPhantomMode() != 1 ) {
        return;
    }
    const float now = gpTimer->GameTotalTime;
    if ( !bFlashStarted ) {
        bFlashStarted = true;
        flashStartTime = now;
        return;
    }

    float alpha = 1.0f;
    if ( now >= flashStartTime ) {
        alpha = ( now - flashStartTime + 1.0f ) * kFlashAlphaMin;
    }
    if ( now < flashStartTime || alpha > 1.0f ) {
        alpha = 1.0f;
        bFlashStarted = false;
        bFlashFade = false;
        flashStartTime = 0.0f;
    }
    pCarState->getVehicleInstance()->setAlpha( alpha );
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

static float GetFrontWheelRadius( CarState *pCarState ) {
    PhysicsVehicle *pPhysics = pCarState->getPhysicsVehicle();
    return ( pPhysics != nullptr ) ? pPhysics->getWheelState( 0 ).Radius : 0.0f;
}

void AiVehicle::setGroundMatrixFromCenter( const Matrix4x4f_t &center ) {
    // FUN_00924b40
    const float height = pCarState->getBody()->BodyDimensions.y() * 0.5f +
                         GetFrontWheelRadius( pCarState );
    infos.GroundMatrix = center;
    infos.GroundMatrix.row( 3 ) -= center.row( 1 ) * height;
}

void AiVehicle::teleport( const Matrix4x4f_t &frame, const bool bSnapToRoad ) {
    // FUN_00927850
    infos.GroundMatrix = frame;
    infos.GroundMatrix( 3, 1 ) -= GetFrontWheelRadius( pCarState );
    if ( !bSnapToRoad ) {
        return;
    }

    AiRoadMap::Road *pRoad =
        gpAI->getRoadMap().findNearestRoadExhaustive( frame, true );
    ProjectOnRoadShape( frame, infos.LastRoadPos, nullptr, false,
                        pRoad->getShape() );
    infos.LastRoadLane = 0;
}
