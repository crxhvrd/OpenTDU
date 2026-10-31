#include "shared.h"
#include "police_state.h"

#include "ai/ai_config.h"
#include "ai/ai_cop_manager.h"
#include "ai/ai_road_buckets.h"
#include "ai/ai_road_shape.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "game/gm_base.h"
#include "game/scenaric/event_parameters.h"
#include "game/scenaric/game_intro_tutorial.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "gs_timer.h"
#include "physics/collision_event.h"
#include "player_data/gs_playerdata.h"

static constexpr float kWitnessDelay = 0.1f;               // FLOAT_00f41668
static constexpr float kPoliceDamForcedDecayTime = 30.0f;  // DAT_00f416e0
static constexpr float kYieldApproachAngle = 1.5707964f;   // DAT_00f418a4
static constexpr float kNoArrestTime = -144000.125f;       // DAT_00f41a00
static constexpr float kSpeedLimitScale = 1.0f;            // DAT_00faa990
static constexpr float kOncomingMostlyFactor = 0.66f;      // DAT_00f03f00
static constexpr float kOncomingPartialFactor = 0.33f;     // DAT_00f41a0c
static constexpr float kNonVehicleCollisionFactor = 0.25f; // DAT_00f416dc
static constexpr float kLaneRatioSentinel = 1024.0f;       // DAT_00f41cc0
static constexpr int32_t kMaxNumPoliceDamForced = 3;

static constexpr uint32_t kSignalRed = 1;
static constexpr uint32_t kSignalOrange = 2;
static constexpr uint32_t kSignalGreen = 4;

static constexpr uint32_t kControlDeviceStop = 1;
static constexpr uint32_t kControlDeviceYield = 2;
static constexpr uint32_t kControlDeviceSignal = 3;

static bool IsTutorialOver() {
    const int32_t step = gpScenaric->getIntroTutorial().getStep();
    return step == kTutorialStatePending || step == kTutorialStateRunning;
}

static bool IsCopVoiceAllowed( const bool bAllowTestCar ) {
    const eGameMode mode = gpActiveGameMode->getCurrentGameMode();
    if ( mode == GM_Mission &&
         gpScenaric->getSelectedEventParameters().getCops().getValue() > 0 ) {
        return true;
    }
    return mode == GM_FreeRide || ( bAllowTestCar && mode == GM_TestCar );
}

static float GetEntitySpeedLimit( const AiRoadMap::Entity *pEntity ) {
    // FUN_0092e850
    const AiRoadMap &roadMap = gpAI->getRoadMap();
    int32_t index = pEntity->SpeedLimitIndice;
    if ( index >= roadMap.getNumSpeedLimits() ) {
        index = 0;
    }
    return roadMap.getSpeedLimit( index ) * kSpeedLimitScale;
}

static int32_t ComputeOncomingLaneOverlap( const AiRoadProfile &profile,
                                           const float lateralMin,
                                           const float lateralMax,
                                           const float speed ) {
    // FUN_00930d40
    const float lateral = ( lateralMin + lateralMax ) * 0.5f;

    float rightLanesMin = kLaneRatioSentinel;
    for ( int32_t lane = 0; lane < int32_t( profile.NumRightLanes ); lane++ ) {
        float ratioStart = 0.0f;
        float ratioEnd = 0.0f;
        profile.computeLaneWidthRatios( lane, true, ratioStart, ratioEnd );
        rightLanesMin = std::min( { rightLanesMin, ratioStart, ratioEnd } );
    }

    float leftLanesMax = -kLaneRatioSentinel;
    for ( int32_t lane = int32_t( profile.NumLeftLanes ) - 1; lane >= 0;
          lane-- ) {
        float ratioStart = 0.0f;
        float ratioEnd = 0.0f;
        profile.computeLaneWidthRatios( lane, false, ratioStart, ratioEnd );
        leftLanesMax = std::max( { leftLanesMax, ratioStart, ratioEnd } );
    }

    if ( speed > 0.0f ) {
        if ( leftLanesMax <= lateralMin ) {
            return 0;
        }
        if ( leftLanesMax <= lateral ) {
            return 1;
        }
        return ( leftLanesMax > lateralMax ) ? 3 : 2;
    }
    if ( 0.0f > speed ) {
        if ( lateralMax <= rightLanesMin ) {
            return 0;
        }
        if ( lateral <= rightLanesMin ) {
            return 1;
        }
        return ( lateralMin > rightLanesMin ) ? 3 : 2;
    }
    return ( leftLanesMax > lateralMin && lateralMax > rightLanesMin ) ? 1 : 0;
}

PoliceState::PoliceState() {
    // FUN_008ae8b0
    for ( CollidedCar &car : CarCollisions ) {
        car = CollidedCar();
    }
    reset();

    LastValidRoadIT = AiRoadIt();
    LastTimeForValidRoadIT = 0.0f;
    CurControlDevice = 0;
    bCurControlDeviceRespected = false;
    LastTimeForDeviceRespected = 0.0f;
    LastGameCounterTime = 0.0f;
    SignalLastState = 4;
}

PoliceState::~PoliceState() {}

void PoliceState::reset() {
    // FUN_008ae790
    WitnessedOffence = 0.0f;
    Collision = 0.0f;
    NumCollisionCar = 0;
    NumCollisionSettings = 0;
    OnComingLane = 0.0f;
    RedLight = 0.0f;
    SpeedLimit = 0.0f;
    Stop = 0.0f;
    Yield = 0.0f;
    bIsChased = false;
    NumChasers = 0;
    NumWantedChaser = 0;
    bIsSearched = false;
    LastSearchRefresh = 0.0f;
    bArrested = false;
    bArrestAfterBackOnTrack = false;
    bPlayingArrestCinematic = false;
    AfterCaptureGraceTime = 0.0f;
    JunctionNotRespected = -1;
    WasHitLastTime = 0.0f;
    HitLastTime = 0.0f;
    ReduceInfBaseTime = 0.0f;
    LastInfTimeWitness = 0.0f;
    MinTimeToStopSearch = 0.0f;
    MinTimeToStopSearchMalus = 0.0f;
    NumPoliceDamForced = 0;
    LastPoliceDamEventTime = 0.0f;
    for ( CollidedCar &car : CarCollisions ) {
        car = CollidedCar();
    }
    LastSearchPos = AiRoadIt();
}

bool PoliceState::isRecentCarCollision( const void *pCar ) const {
    // FUN_008aff60 (inlined)
    for ( const CollidedCar &car : CarCollisions ) {
        if ( car.pCarPointer == pCar &&
             gpTimer->GameTotalTime <=
                 gAIConfig.InfGracePeriod + car.CollisionTime ) {
            return true;
        }
    }
    return false;
}

bool PoliceState::checkRecentCollision( const void *pCar,
                                        const void *pObject ) {
    // FUN_008aef10
    const void *pKey = ( pCar != nullptr ) ? pCar : pObject;
    for ( CollidedCar &car : CarCollisions ) {
        if ( pKey == nullptr || car.pCarPointer != pKey ) {
            continue;
        }
        if ( gpTimer->GameTotalTime - car.CollisionTime <=
             gAIConfig.CollisionRepeatDelay ) {
            return true;
        }
        car.CollisionTime = gpTimer->GameTotalTime;
        return false;
    }

    for ( CollidedCar &car : CarCollisions ) {
        if ( car.pCarPointer != nullptr ) {
            continue;
        }
        if ( pKey != nullptr ) {
            car.pCarPointer = pKey;
        }
        car.CollisionTime = gpTimer->GameTotalTime;
        return false;
    }
    return false;
}

float PoliceState::computeOffenceLevel() {
    // FUN_008ae320
    const float offenceLevel = SpeedLimit + WitnessedOffence + Stop + RedLight +
                               Collision + OnComingLane + Yield;
    const float maxOffenceLevel =
        gAIConfig.InfCollisionValue + gAIConfig.InfAggressiveLimit;
    if ( offenceLevel <= maxOffenceLevel ) {
        return offenceLevel;
    }

    float excess = offenceLevel - maxOffenceLevel;
    if ( excess <= WitnessedOffence ) {
        WitnessedOffence -= excess;
        return maxOffenceLevel;
    }

    excess -= WitnessedOffence;
    WitnessedOffence = 0.0f;
    if ( excess > 0.0f ) {
        Collision = ( excess <= Collision ) ? Collision - excess : 0.0f;
    }
    return maxOffenceLevel;
}

bool PoliceState::isWanted() {
    // FUN_008aeb70
    return gAIConfig.InfLimit <= computeOffenceLevel();
}

void PoliceState::updateAllTimeCounters() {
    // FUN_008ae600
    const float delta = LastGameCounterTime - gpTimer->GameTotalTime;
    LastPoliceDamEventTime -= delta;
    WasHitLastTime -= delta;
    HitLastTime -= delta;
    ReduceInfBaseTime -= delta;
    LastInfTimeWitness -= delta;
    if ( gpPlayerData->getLastArrestedTime() != kNoArrestTime ) {
        gpPlayerData->setLastArrestedTime( gpPlayerData->getLastArrestedTime() -
                                           delta );
    }
    MinTimeToStopSearch -= delta;
    LastTimeForValidRoadIT -= delta;
    LastTimeForDeviceRespected -= delta;
    AfterCaptureGraceTime -= delta;
    for ( CollidedCar &car : CarCollisions ) {
        if ( car.pCarPointer != nullptr ) {
            car.CollisionTime -= delta;
        }
    }
    LastGameCounterTime = gpTimer->GameTotalTime;
}

void PoliceState::onPoliceCollision( const uint8_t collisionType,
                                     CarState *pOther ) {
    // FUN_008b1270
    const bool bWasWanted = computeOffenceLevel() >= gAIConfig.InfLimit;
    const bool bWasAggressive =
        computeOffenceLevel() >= gAIConfig.InfAggressiveLimit;

    bool bIgnored = false;
    switch ( gpActiveGameMode->getCurrentGameMode() ) {
    case GM_FreeRide:
    case GM_Map:
    case GM_TestCar:
        break;
    case GM_Mission:
        bIgnored =
            gpScenaric->getSelectedEventParameters().getCops().getValue() < 1;
        break;
    default:
        bIgnored = true;
        break;
    }
    if ( !IsTutorialOver() ) {
        bIgnored = true;
    }

    const AiVehicle *pOtherVehicle = nullptr;
    if ( pOther != nullptr ) {
        pOtherVehicle =
            static_cast<const AiVehicle *>( pOther->getAIController() );
        if ( pOtherVehicle != nullptr &&
             ( pOtherVehicle->getCategory() ==
                   eAiVehicleCategory::AVC_Opponent ||
               pOtherVehicle->getCategory() == eAiVehicleCategory::AVC_Bot ) ) {
            bIgnored = true;
        }
        if ( pOther->getP2POwner() != nullptr ) {
            return;
        }
    }
    if ( bIgnored ) {
        return;
    }

    const float now = gpTimer->GameTotalTime;
    HitLastTime = now;
    float value = gAIConfig.InfCollisionValue;
    if ( collisionType != uint8_t( eCollisionType::CT_Vehicle ) ) {
        value *= kNonVehicleCollisionFactor;
    }
    if ( pOtherVehicle != nullptr ) {
        if ( pOtherVehicle->getBehaviourType() == 0 ) {
            value *= gAIConfig.InfCopMalusFactor;
        }
        if ( pOtherVehicle->isSoftCollision() &&
             NumPoliceDamForced < kMaxNumPoliceDamForced ) {
            NumPoliceDamForced++;
            LastPoliceDamEventTime = now;
        }
    }
    if ( collisionType == uint8_t( eCollisionType::CT_DynamicBody ) ) {
        NumCollisionSettings++;
    } else {
        NumCollisionCar++;
    }
    Collision += value;
    ReduceInfBaseTime = now;
    LastInfTimeWitness = now;

    static constexpr bool bOnlineSession = false;
    if ( !bWasWanted && computeOffenceLevel() >= gAIConfig.InfLimit ) {
        if ( !bOnlineSession && IsCopVoiceAllowed( true ) ) {
            gAiCopMng.onVoiceEvent( eCopVoiceEvent::CVE_InfLimitReached );
        }
        const float malus = MinTimeToStopSearchMalus;
        MinTimeToStopSearchMalus = gAIConfig.PoliceSearchMinTimeStep + malus;
        MinTimeToStopSearch =
            gpTimer->GameTotalTime + malus + gAIConfig.PoliceSearchMinTime;
    }
    if ( !bWasAggressive && gAiCopMng.canBeAggressive() &&
         computeOffenceLevel() >= gAIConfig.InfAggressiveLimit &&
         !bOnlineSession && IsCopVoiceAllowed( true ) ) {
        gAiCopMng.onVoiceEvent( eCopVoiceEvent::CVE_AskForAggressive );
    }
}

void PoliceState::updateLastValidRoadIt( AiRoadMap::Road *pRoad,
                                         const float fraction,
                                         const bool bWay ) {
    AiRoadShape *pShape = ( pRoad != nullptr ) ? pRoad->getShape() : nullptr;
    if ( pShape != nullptr ) {
        pShape->computeIt( fraction, LastValidRoadIT );
    }
    LastTimeForValidRoadIT = gpTimer->GameTotalTime;
    LastValidRoadIT.setWay( bWay );
}

void PoliceState::checkInfringements( CarState *pOwner,
                                      const float deltaTime ) {
    // FUN_008b1650
    const eGameMode mode = gpActiveGameMode->getCurrentGameMode();
    if ( mode != GM_FreeRide && mode != GM_Mission && mode != GM_Map &&
         mode != GM_TestCar ) {
        reset();
        return;
    }
    if ( !IsTutorialOver() ) {
        reset();
        return;
    }

    if ( LastGameCounterTime > gpTimer->GameTotalTime ) {
        updateAllTimeCounters();
    } else {
        LastGameCounterTime = gpTimer->GameTotalTime;
    }

    const bool bWasWanted = computeOffenceLevel() >= gAIConfig.InfLimit;
    const bool bWasAggressive =
        computeOffenceLevel() >= gAIConfig.InfAggressiveLimit;

    for ( CollidedCar &car : CarCollisions ) {
        if ( car.pCarPointer != nullptr &&
             gpTimer->GameTotalTime >
                 car.CollisionTime + gAIConfig.InfGracePeriod ) {
            car.pCarPointer = nullptr;
            car.CollisionTime = 0.0f;
        }
    }

    const ObjectWorldLocation &location = pOwner->getWorldLocation();
    const AiRoadMap &roadMap = gpAI->getRoadMap();
    const float now = gpTimer->GameTotalTime;
    const float fraction = ( location.LocationRoad.FractionMin +
                             location.LocationRoad.FractionMax ) *
                           0.5f;

    if ( gAIConfig.bInfAllButCollision ) {
        if ( location.LocationType == ObjectWorldLocation::OWL_Road ) {
            updateLastValidRoadIt(
                roadMap.getRoad( location.LocationRoad.RoadID ), fraction,
                location.bWay != 0 );
        } else if ( location.LocationType ==
                    ObjectWorldLocation::OWL_OffRoad ) {
            ReduceInfBaseTime = now;
        }
        reduceOffences( pOwner );
        return;
    }

    bool bInfringed = false;
    if ( location.LocationType == ObjectWorldLocation::OWL_Road ) {
        JunctionNotRespected = -1;
        AiRoadMap::Road *pRoad =
            roadMap.getRoad( location.LocationRoad.RoadID );

        bool bWay = location.bWay != 0;
        if ( location.VelocityX > 0.0f ) {
            bWay = true;
        } else if ( 0.0f > location.VelocityX ) {
            bWay = false;
        }
        if ( pRoad != LastValidRoadIT.pRoad || bWay != LastValidRoadIT.bWay ) {
            bCurControlDeviceRespected = false;
            LastTimeForDeviceRespected = 0.0f;
        }
        updateLastValidRoadIt( pRoad, fraction, bWay );
        if ( pRoad == nullptr ) {
            return;
        }

        CurControlDevice = pRoad->getControlDevice( bWay );
        if ( CurControlDevice == kControlDeviceSignal ) {
            float syncTime = 0.0f;
            SignalLastState = GetTrafficSignalState(
                pRoad->getSignalSynchro( bWay ), &syncTime );
            if ( SignalLastState == kSignalGreen ||
                 SignalLastState == kSignalOrange ) {
                bCurControlDeviceRespected = true;
                LastTimeForDeviceRespected = now;
            }
        } else {
            SignalLastState = kSignalGreen;
            AiRoadShape *pShape = pRoad->getShape();
            if ( CurControlDevice == kControlDeviceStop && pShape != nullptr ) {
                const float invLength = 1.0f / pShape->TotalLength;
                const float stopFraction = bWay ? 1.0f : 0.0f;
                if ( gAIConfig.InfAllowedDistToStop >
                         fabsf( stopFraction - fraction ) / invLength &&
                     invLength * gAIConfig.InfAllowedSpeedToStop >=
                         fabsf( location.VelocityX ) ) {
                    bCurControlDeviceRespected = true;
                    LastTimeForDeviceRespected = now;
                }
            }
        }
    } else if ( location.LocationType == ObjectWorldLocation::OWL_Junction ) {
        const uint32_t junctionID = location.LocationOther.RoadID;
        const float timeOnDevice = now - LastTimeForDeviceRespected;
        if ( CurControlDevice == kControlDeviceSignal ) {
            if ( !bCurControlDeviceRespected ) {
                if ( SignalLastState == kSignalRed ) {
                    if ( gAIConfig.Unknown0xBB ) {
                        RedLight += gAIConfig.InfRedLightValue;
                        bInfringed = true;
                    }
                    JunctionNotRespected = int32_t( junctionID );
                    bCurControlDeviceRespected = true;
                    LastTimeForDeviceRespected = now;
                }
            } else if ( timeOnDevice > gAIConfig.InfAllowedDurationOnControl &&
                        LastValidRoadIT.pRoad != nullptr ) {
                float syncTime = 0.0f;
                SignalLastState = GetTrafficSignalState(
                    LastValidRoadIT.pRoad->getSignalSynchro(
                        LastValidRoadIT.bWay ),
                    &syncTime );
                if ( SignalLastState == kSignalRed ) {
                    if ( gAIConfig.Unknown0xBB ) {
                        RedLight += gAIConfig.InfRedLightValue;
                        bInfringed = true;
                    }
                    JunctionNotRespected = int32_t( junctionID );
                    LastTimeForDeviceRespected = now;
                }
            }
        } else if ( CurControlDevice == kControlDeviceStop ) {
            if ( !bCurControlDeviceRespected ||
                 timeOnDevice > gAIConfig.InfAllowedDurationOnControl ) {
                if ( gAIConfig.Unknown0xBB ) {
                    Stop += gAIConfig.InfStopValue;
                    bInfringed = true;
                }
                JunctionNotRespected = int32_t( junctionID );
                bCurControlDeviceRespected = true;
                LastTimeForDeviceRespected = now;
            }
        } else if ( CurControlDevice == kControlDeviceYield &&
                    timeOnDevice > gAIConfig.InfAllowedDurationOnControl ) {
            bInfringed |= checkYield( pOwner, junctionID );
        }
    }

    if ( location.LocationType != ObjectWorldLocation::OWL_Road ) {
        return;
    }
    const AiRoadMap::Road *pRoad =
        roadMap.getRoad( location.LocationRoad.RoadID );
    if ( pRoad == nullptr || pRoad->pDetail == nullptr ) {
        return;
    }

    const AiRoadMap::Entity *pEntity = pRoad->pDetail->pEntities[0];
    if ( gAIConfig.Unknown0xB9 ) {
        const float speedLimit = GetEntitySpeedLimit( pEntity );
        if ( speedLimit > 0.0f ) {
            const float speed = pOwner->getBody()->SpeedInKmh;
            if ( speed > speedLimit * gAIConfig.InfSpeedLimitTolerance ) {
                SpeedLimit += gAIConfig.InfSpeedLimitFactor / speedLimit *
                              speed * deltaTime;
                bInfringed = true;
            }
        }
    }

    const AiRoadProfile *pProfile = pEntity->pProfile;
    if ( pProfile == nullptr ) {
        return;
    }
    bool bWitnessed = bInfringed;
    if ( gAIConfig.Unknown0xBA ) {
        const float value = gAIConfig.InfOncomingLaneValue *
                            gAIConfig.InfOncomingLaneFactor * deltaTime;
        switch ( ComputeOncomingLaneOverlap(
            *pProfile, location.LocationRoad.LateralMin,
            location.LocationRoad.LateralMax, location.VelocityX ) ) {
        case 3:
            OnComingLane += value;
            bWitnessed = true;
            break;
        case 2:
            OnComingLane += value * kOncomingMostlyFactor;
            bWitnessed = true;
            break;
        case 1:
            OnComingLane += value * kOncomingPartialFactor;
            bWitnessed = true;
            break;
        default:
            break;
        }
    }
    if ( !bWitnessed ) {
        reduceOffences( pOwner );
        return;
    }

    LastInfTimeWitness = now;
    ReduceInfBaseTime = now;

    const bool bOnline = gpPlayerData->isOnline();
    if ( !bWasWanted && computeOffenceLevel() >= gAIConfig.InfLimit ) {
        if ( !bOnline && IsCopVoiceAllowed( false ) ) {
            gAiCopMng.onVoiceEvent( eCopVoiceEvent::CVE_InfLimitReached );
        }
        const float malus = MinTimeToStopSearchMalus;
        MinTimeToStopSearchMalus = gAIConfig.PoliceSearchMinTimeStep + malus;
        MinTimeToStopSearch =
            gpTimer->GameTotalTime + malus + gAIConfig.PoliceSearchMinTime;
    }
    if ( !bWasAggressive && gAiCopMng.canBeAggressive() &&
         computeOffenceLevel() >= gAIConfig.InfAggressiveLimit && !bOnline &&
         IsCopVoiceAllowed( false ) ) {
        gAiCopMng.onVoiceEvent( eCopVoiceEvent::CVE_AskForAggressive );
    }
}

bool PoliceState::checkYield( CarState *pOwner, const uint32_t junctionID ) {
    // FUN_008b1650 (yield junction)
    const AiRoadMap::Road *pLastRoad = LastValidRoadIT.pRoad;
    if ( pLastRoad == nullptr ) {
        return false;
    }
    const AiRoadMap::Junction *pTargetJunction =
        LastValidRoadIT.bWay ? pLastRoad->pJunctionEnd
                             : pLastRoad->pJunctionBegin;
    if ( pTargetJunction == nullptr ) {
        return false;
    }

    bool bInfringed = false;
    const float now = gpTimer->GameTotalTime;
    AiRoadBuckets &buckets = gpAI->getRoadBuckets();
    AiRoadBuckets::Bucket *pJunctionBucket =
        buckets.getJunctionBucket( pTargetJunction->ID );
    GameObject *pObject = ( pJunctionBucket != nullptr )
                              ? pJunctionBucket->pFirstObject
                              : nullptr;
    for ( int32_t i = 0; pJunctionBucket != nullptr &&
                         i < pJunctionBucket->NumObjects && pObject != nullptr;
          i++, pObject = pObject->getNextInBucket() ) {
        if ( pObject == pOwner || pObject->getType() != 0 ) {
            continue;
        }
        CarState *pOtherCar = static_cast<CarState *>( pObject );
        const AiRoadMap::Road *pOtherRoad =
            pOtherCar->getPoliceState().LastValidRoadIT.pRoad;
        if ( pOtherRoad == nullptr || pOtherRoad == pLastRoad ) {
            continue;
        }

        const Eigen::Vector4f velocity = pOtherCar->getLinearVelocity();
        const Eigen::Vector4f delta =
            pOwner->getWorldPosition() - pOtherCar->getWorldPosition();
        const float angle =
            std::atan2( delta.z() * velocity.x() - delta.x() * velocity.z(),
                        delta.z() * velocity.z() + delta.x() * velocity.x() );
        if ( fabsf( angle ) < kYieldApproachAngle ) {
            if ( gAIConfig.Unknown0xBB ) {
                Yield += gAIConfig.InfYieldValue;
                bInfringed = true;
            }
            JunctionNotRespected = int32_t( junctionID );
            LastTimeForDeviceRespected = now;
            break;
        }
    }

    const AiRoadMap::Junction *pJunction =
        gpAI->getRoadMap().getJunction( pTargetJunction->ID );
    if ( pJunction == nullptr ) {
        return bInfringed;
    }
    for ( int32_t roadIndex = 0; roadIndex < pJunction->NumRoads;
          roadIndex++ ) {
        AiRoadMap::Road *pRoad = pJunction->getRoad( roadIndex );
        const bool bTowardJunction = pRoad->pJunctionEnd == pJunction;
        const float junctionFraction = bTowardJunction ? 1.0f : 0.0f;
        if ( pRoad == pLastRoad ) {
            continue;
        }

        AiRoadBuckets::Bucket *pBucket = buckets.getRoadBucket( pRoad->ID );
        if ( pBucket == nullptr ) {
            continue;
        }
        GameObject *pRoadObject = pBucket->pFirstObject;
        for ( int32_t i = 0; i < pBucket->NumObjects && pRoadObject != nullptr;
              i++, pRoadObject = pRoadObject->getNextInBucket() ) {
            const ObjectWorldLocation &objectLocation =
                pRoadObject->getWorldLocation();
            if ( ( objectLocation.bWay != 0 ) != bTowardJunction ) {
                continue;
            }
            const float objectFraction =
                ( objectLocation.LocationRoad.FractionMin +
                  objectLocation.LocationRoad.FractionMax ) *
                0.5f;
            pRoad->getShape();
            const float speed = fabsf( objectLocation.VelocityX );
            if ( speed != 0.0f &&
                 fabsf( junctionFraction - objectFraction ) / speed <
                     gAIConfig.InfYieldObsTimeToJunc ) {
                if ( gAIConfig.Unknown0xBB ) {
                    Yield += gAIConfig.InfYieldValue;
                    bInfringed = true;
                }
                JunctionNotRespected = int32_t( junctionID );
                LastTimeForDeviceRespected = now;
            }
        }
    }
    return bInfringed;
}

void PoliceState::reduceOffences( CarState *pOwner ) {
    // FUN_008b1650 (offence decay)
    const float now = gpTimer->GameTotalTime;
    if ( bIsChased ) {
        ReduceInfBaseTime = now;
    } else if ( now > MinTimeToStopSearch &&
                now - LastInfTimeWitness > kWitnessDelay &&
                computeOffenceLevel() > 0.0f &&
                gAIConfig.InfLoweringFactor != 0.0f &&
                now - ReduceInfBaseTime > gAIConfig.PoliceSearchMinTime ) {
        WitnessedOffence -= gAIConfig.WitnessedOffenceValue;
        Collision -= gAIConfig.InfCollisionValue;
        ReduceInfBaseTime = now;
    }

    Collision = std::max( Collision, 0.0f );
    Stop = std::max( Stop, 0.0f );
    RedLight = std::max( RedLight, 0.0f );
    OnComingLane = std::max( OnComingLane, 0.0f );
    WitnessedOffence = std::max( WitnessedOffence, 0.0f );
    SpeedLimit = std::max( SpeedLimit, 0.0f );
    Yield = std::max( Yield, 0.0f );

    if ( NumPoliceDamForced > 0 &&
         now - LastPoliceDamEventTime > kPoliceDamForcedDecayTime ) {
        NumPoliceDamForced--;
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS (NumPoliceDamForced, cop dam
                                 // mass scale)
        (void)pOwner;
        LastPoliceDamEventTime = now;
    }
}
