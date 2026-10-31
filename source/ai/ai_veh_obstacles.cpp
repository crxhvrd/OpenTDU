#include "shared.h"
#include "ai_veh_obstacles.h"

#include "ai/ai_config.h"
#include "ai/ai_opt_path.h"
#include "ai/ai_road_buckets.h"
#include "ai/ai_road_shape.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "core/maths_helper.h"
#include "game/game_object.h"
#include "game/scenaric/entity/entity_car_player.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"

static constexpr float kNoTime = -1.0f;                 // DAT_00f41664
static constexpr float kUnreachableTime = 1000.0f;      // DAT_00f41718
static constexpr float kNoGapDistance = 2048.0f;        // DAT_00f41b5c
static constexpr float kPlayerMarginFactor = 3.0f;      // DAT_00f416b4
static constexpr float kCarMarginFactor = 2.0f;         // DAT_00f416b0
static constexpr float kBikeMarginFactor = 4.0f;        // DAT_00f418d4
static constexpr float kLookAheadFactor = 4.0f;         // DAT_00f418d4
static constexpr float kSlowObjectSpeedRatio = 0.8f;    // DAT_00f416d0
static constexpr float kCopTargetDistanceFactor = 3.0f; // DAT_00f416b4
static constexpr float kCopTargetMaxSpeed = 30.0f;      // DAT_00f416e0
static constexpr float kTargetMaxSpeed = 10.0f;         // DAT_00f4166c
static constexpr float kJunctionMaxDistance = 100.0f;   // DAT_00f41708
static constexpr float kJunctionTimeMargin = 1.0f;      // DAT_00f4165c
static constexpr float kAvoidSlopeFactor = 4.0f / 3.0f; // DAT_00f41878
static constexpr float kMinAvoidDistance = 1.0f;        // DAT_00f4165c
static constexpr int32_t kMaxMarkers = 0x660;           // Shared node pool
static constexpr int32_t kNumCorners = 4;

static bool gbSmoothCurrentAvoidance = true; // DAT_00faa821
static bool gbSmoothNextAvoidance = true;    // DAT_00faa820

using Marker = AiObstacleSlot::Marker;

static const AiVehNavOpponent::Situation &
GetSituation( const OptPathSituation *pSituation ) {
    return *static_cast<const AiVehNavOpponent::Situation *>( pSituation );
}

static bool IsBike( const AiVehicle *pVehicle ) {
    // Situation+0x1 (FUN_00955640)
    const CarState *pCarState = pVehicle->getCarState();
    return pCarState != nullptr && pCarState->getBody()->bIsBike;
}

static float GetEffectiveWidthRatio( const OptPathElem &elem ) {
    return ( elem.OptWidthRatio != OptPath::kUnsetWidthRatio )
               ? elem.OptWidthRatio
               : elem.WidthRatio;
}

static float GetMaxWidthRatio( const AiRoadIt &it ) {
    const AiRoadProfile *pProfile = it.getEntity()->pProfile;
    return pProfile->OuterWidth / pProfile->InnerWidth;
}

static float GetMidLateral( const ObjectWorldLocation &location ) {
    return ( location.LocationRoad.LateralMax +
             location.LocationRoad.LateralMin ) *
           0.5f;
}

static GameObject *GetChaseTarget( AiVehicle *pVehicle ) {
    if ( pVehicle->getControllerType() != 1 ) {
        return nullptr;
    }
    return pVehicle->getChaseController().getTarget();
}

static float
ComputeWidthRatioXZ( const AiRoadIt &it, const float x, const float z ) {
    // FUN_00944570
    const Eigen::Vector4f left = it.computeWidthPosition( -1.0f );
    const Eigen::Vector4f right = it.computeWidthPosition( 1.0f );
    const float axisX = right.x() - left.x();
    const float axisZ = right.z() - left.z();
    float t = ( ( x - left.x() ) * axisX + ( z - left.z() ) * axisZ ) /
              ( axisX * axisX + axisZ * axisZ );
    if ( 0.0f > t ) {
        t = 0.0f;
    } else if ( t > 1.0f ) {
        t = 1.0f;
    }
    return t * 2.0f - 1.0f;
}

static void ComputeCornersXZ( GameObject *pObject,
                              Eigen::Vector2f ( &outCorners )[kNumCorners] ) {
    // FUN_0094ece0
    Matrix4x4f_t box;
    pObject->retrieveBoundingBox( box );
    const Eigen::Vector2f centre( box( 3, 0 ), box( 3, 2 ) );
    const Eigen::Vector2f axisX( box( 0, 0 ), box( 0, 2 ) );
    const Eigen::Vector2f axisZ( box( 2, 0 ), box( 2, 2 ) );
    outCorners[0] = centre - axisX - axisZ;
    outCorners[1] = centre - axisX + axisZ;
    outCorners[2] = centre + axisX - axisZ;
    outCorners[3] = centre + axisX + axisZ;
}

static void ProjectCorners( const AiRoadIt &it,
                            const Eigen::Vector2f ( &corners )[kNumCorners],
                            float &outMin,
                            float &outMax ) {
    // FUN_0094f570
    const AiRoadProfile *pProfile = it.getEntity()->pProfile;
    float lateral = 0.0f;
    float height = 0.0f;
    pProfile->computeWidthOffset( 0.0f, lateral, height );
    const Eigen::Vector4f centre = it.computeOffsetPosition( lateral, height );
    pProfile->computeWidthOffset( 1.0f, lateral, height );
    const Eigen::Vector4f edge = it.computeOffsetPosition( lateral, height );

    const float axisX = edge.x() - centre.x();
    const float axisZ = edge.z() - centre.z();
    const float lengthSq = axisZ * axisZ + axisX * axisX;
    for ( int32_t i = 0; i < kNumCorners; i++ ) {
        const float ratio = ( ( corners[i].x() - centre.x() ) * axisX +
                              ( corners[i].y() - centre.z() ) * axisZ ) /
                            lengthSq;
        if ( i == 0 ) {
            outMin = ratio;
            outMax = ratio;
            continue;
        }
        outMin = ( ratio > outMin ) ? outMin : ratio;
        outMax = ( outMax > ratio ) ? outMax : ratio;
    }

    const float maxRatio = pProfile->OuterWidth / pProfile->InnerWidth;
    const float minRatio = -maxRatio;
    if ( minRatio > outMin ) {
        outMin = minRatio;
    } else if ( outMin > maxRatio ) {
        outMin = maxRatio;
    }
    if ( minRatio > outMax ) {
        outMax = minRatio;
    } else if ( outMax > maxRatio ) {
        outMax = maxRatio;
    }
}

static float ComputeDistanceAlongRoad( GameObject *pObject,
                                       const AiRoadIt &it ) {
    // FUN_0094f920
    const Eigen::Vector4f position = pObject->getWorldPosition();
    const Eigen::Vector4f direction = it.computeDirection();
    const Eigen::Vector4f origin = it.computeWidthPosition( 0.0f );
    return fabsf( Project( position, origin, origin + direction ) );
}

void AiObstacleSlot::reset() {
    // FUN_0094da20
    TimeStart = 0.0f;
    TimeEnd = 0.0f;
    FractionStart = -1.0f;
    FreeEnd = -1.0f;
    FreeStart = -1.0f;
    FractionEnd = -1.0f;
    RoadID = -1;
    bWay = true;
    TargetWidthRatio = kUnsetWidthRatio;
    Markers.clear();
    Objects.clear();
}

bool AiObstacleSlot::hasSoftCollisionCar() const {
    // FUN_0094db30
    for ( GameObject *pObject : Objects ) {
        const AiVehicle *pOwner = static_cast<const AiVehicle *>(
            static_cast<CarState *>( pObject )->getAIController() );
        if ( pOwner != nullptr && pOwner->isSoftCollision() ) {
            return true;
        }
    }
    return false;
}

void AiObstacleSlot::addObject( GameObject *pObject ) {
    // FUN_0094d980
    if ( Objects.size() == kMaxObjects ) {
        return;
    }
    if ( std::find( Objects.begin(), Objects.end(), pObject ) !=
         Objects.end() ) {
        return;
    }
    Objects.push_back( pObject );
}

void AiObstacleSlot::insertInterval( const Marker &start, Marker end ) {
    // FUN_0094df80
    if ( Markers.size() + 2 > kMaxMarkers ) {
        return;
    }
    if ( Markers.empty() ) {
        Markers.push_back( start );
        Markers.push_back( end );
        return;
    }

    size_t cursor = 0;
    if ( Markers.front().WidthRatio > start.WidthRatio ) {
        Markers.insert( Markers.begin(), start );
    } else {
        size_t next = 1;
        while ( next < Markers.size() &&
                !( Markers[next].WidthRatio > start.WidthRatio ) ) {
            next++;
        }
        if ( next == Markers.size() ) {
            Markers.push_back( start );
            cursor = next;
        } else if ( Markers[next].Type == Marker::MT_End ) {
            cursor = next - 1;
            Marker &merged = Markers[cursor];
            merged.FractionMin = ( start.FractionMin > merged.FractionMin )
                                     ? merged.FractionMin
                                     : start.FractionMin;
            merged.FractionMax = ( merged.FractionMax > start.FractionMax )
                                     ? merged.FractionMax
                                     : start.FractionMax;
            merged.Time =
                ( start.Time > merged.Time ) ? merged.Time : start.Time;
            end.FractionMin = merged.FractionMin;
            end.FractionMax = merged.FractionMax;
            end.Time = merged.Time;
        } else {
            Markers.insert( Markers.begin() + next, start );
            cursor = next;
        }
    }

    if ( !( end.WidthRatio > Markers[cursor].WidthRatio ) ) {
        return;
    }
    while ( true ) {
        const size_t next = cursor + 1;
        if ( next == Markers.size() ) {
            Markers.push_back( end );
            return;
        }
        Marker &marker = Markers[next];
        if ( !( end.WidthRatio > marker.WidthRatio ) ) {
            if ( marker.Type == Marker::MT_Start ) {
                Markers.insert( Markers.begin() + next, end );
                return;
            }
            marker.FractionMin = ( end.FractionMin > marker.FractionMin )
                                     ? marker.FractionMin
                                     : end.FractionMin;
            marker.FractionMax = ( marker.FractionMax > end.FractionMax )
                                     ? marker.FractionMax
                                     : end.FractionMax;
            marker.Time = ( end.Time > marker.Time ) ? marker.Time : end.Time;
            return;
        }

        end.FractionMin = ( end.FractionMin > marker.FractionMin )
                              ? marker.FractionMin
                              : end.FractionMin;
        end.FractionMax = ( marker.FractionMax > end.FractionMax )
                              ? marker.FractionMax
                              : end.FractionMax;
        end.Time = ( end.Time > marker.Time ) ? marker.Time : end.Time;
        Markers.erase( Markers.begin() + next );
    }
}

AiVehObstacles::AiVehObstacles()
    : pNav( nullptr ),
      pVehicle( nullptr ),
      pSituation( nullptr ),
      pLine( nullptr ),
      pPath( nullptr ) {
    // FUN_0094e610
}

void AiVehObstacles::attach( AiVehNavOpponent *pOwnerNav,
                             AiVehicle *pOwnerVehicle,
                             OptPath *pOwnerLine,
                             NavPathOpponent *pOwnerPath,
                             OptPathSituation *pOwnerSituation ) {
    // FUN_0094e3f0
    pNav = pOwnerNav;
    pVehicle = pOwnerVehicle;
    pLine = pOwnerLine;
    pPath = pOwnerPath;
    pSituation = pOwnerSituation;
    for ( AiObstacleSlot &slot : slots ) {
        slot.reset();
        slot.Markers.reserve( 8 );
        slot.Objects.reserve( AiObstacleSlot::kMaxObjects );
    }
}

void AiVehObstacles::detach() {
    // FUN_0094f0b0
    for ( AiObstacleSlot &slot : slots ) {
        slot.Markers.clear();
        slot.Markers.shrink_to_fit();
        slot.Objects.clear();
        slot.Objects.shrink_to_fit();
    }
}

void AiVehObstacles::resetSlots() {
    // FUN_0094e3d0
    for ( AiObstacleSlot &slot : slots ) {
        slot.reset();
    }
}

bool AiVehObstacles::collect() {
    // FUN_00953550
    if ( pLine->getCurrentIndex() < 1 ) {
        return false;
    }
    resetSlots();
    pLine->clearOptimisation( 0, pLine->getCount() - 1 );

    int32_t lastRoadID = -1;
    for ( int32_t i = pLine->getCurrentIndex() - 1; i + 1 < pLine->getCount();
          i++ ) {
        const OptPathElem &elem = pLine->at( i );
        const OptPathElem &next = pLine->at( i + 1 );
        if ( elem.Type == OptPathElem::ET_Road ) {
            if ( next.Type != OptPathElem::ET_Road ) {
                continue;
            }
            const int32_t roadID = static_cast<int32_t>( elem.It.pRoad->ID );
            if ( elem.It.pRoad->ID != next.It.pRoad->ID ) {
                const AiRoadMap::Junction *pJunction =
                    elem.It.bWay ? elem.It.pRoad->pJunctionEnd
                                 : elem.It.pRoad->pJunctionBegin;
                if ( pJunction != nullptr ) {
                    collectJunction( pJunction->ID, i + 1 );
                }
            } else if ( elem.It.bWay != next.It.bWay ) {
                lastRoadID = -1;
            } else if ( roadID != lastRoadID ) {
                collectRoad( i );
                lastRoadID = roadID;
            }
        } else if ( elem.Type == OptPathElem::ET_Junction &&
                    next.Type == OptPathElem::ET_Road ) {
            const AiRoadMap::Junction *pJunction =
                next.It.bWay ? next.It.pRoad->pJunctionBegin
                             : next.It.pRoad->pJunctionEnd;
            if ( pJunction != nullptr ) {
                collectJunction( pJunction->ID, i + 1 );
            }
        }
    }

    for ( const AiObstacleSlot &slot : slots ) {
        if ( !slot.Markers.empty() || 1.0f >= fabsf( slot.TargetWidthRatio ) ) {
            return true;
        }
    }
    return false;
}

void AiVehObstacles::collectRoad( const int32_t index ) {
    // FUN_009532b0
    const AiVehNavOpponent::Situation &situation = GetSituation( pSituation );
    const int32_t current = pLine->getCurrentIndex();
    const int32_t count = pLine->getCount();
    if ( !situation.isOnRoad() ) {
        return;
    }

    if ( index == current - 1 ) {
        if ( situation.RoadIt.pRoad != nullptr ) {
            const OptPathElem &elem = pLine->at( current );
            if ( elem.Type == OptPathElem::ET_Road ) {
                if ( situation.RoadIt.pRoad != elem.It.pRoad ) {
                    return;
                }
                if ( situation.RoadIt.bWay == elem.It.bWay &&
                     situation.RoadIt.isBefore( elem.It ) ) {
                    if ( pLine->at( current - 1 ).It.bWay == elem.It.bWay ) {
                        collectCurrentSpan();
                    }
                } else {
                    collectSpan( current - 1, current, current - 1 );
                }
            }
        }
        for ( int32_t i = current + 1; i < count; i++ ) {
            const OptPathElem &previous = pLine->at( i - 1 );
            const OptPathElem &elem = pLine->at( i );
            if ( elem.Type != OptPathElem::ET_Road ||
                 elem.It.pRoad != previous.It.pRoad ) {
                return;
            }
            if ( elem.It.bWay == previous.It.bWay ) {
                collectSpan( i - 1, i, i - 1 );
            }
        }
        return;
    }

    const OptPathElem &first = pLine->at( index );
    if ( first.Type != OptPathElem::ET_Road ||
         first.It.pRoad->ID != situation.LocationID ) {
        return;
    }
    for ( int32_t i = index + 1; i < count; i++ ) {
        const OptPathElem &previous = pLine->at( i - 1 );
        const OptPathElem &elem = pLine->at( i );
        if ( elem.Type != OptPathElem::ET_Road ||
             elem.It.pRoad != previous.It.pRoad ||
             elem.It.bWay != previous.It.bWay ) {
            return;
        }
        collectSpan( i - 1, i, i - 1 );
    }
}

bool AiVehObstacles::isTargetObjectIgnored( GameObject *pObject,
                                            AiObstacleSlot *pSlot ) const {
    GameObject *pTarget = GetChaseTarget( pVehicle );
    if ( pTarget == nullptr || pObject->getType() != 0 || pObject != pTarget ) {
        return false;
    }

    if ( pVehicle->getCategory() == eAiVehicleCategory::AVC_Cop ) {
        if ( pSlot != nullptr ) {
            const Eigen::Vector4f delta = pObject->getWorldPosition() -
                                          GetSituation( pSituation ).Position;
            if ( gAIConfig.PathOptTrackDistance * kCopTargetDistanceFactor >
                 sqrtf( delta.dot( delta ) ) ) {
                pSlot->TargetWidthRatio =
                    GetMidLateral( pObject->getWorldLocation() );
            }
        }
        const Eigen::Vector4f &velocity = pObject->getLinearVelocity();
        return sqrtf( velocity.dot( velocity ) ) >= kCopTargetMaxSpeed;
    }
    if ( pVehicle->getDPC().DPCPattern.bAvoidCollision ) {
        return false;
    }
    const Eigen::Vector4f &velocity = pObject->getLinearVelocity();
    return sqrtf( velocity.dot( velocity ) ) >= kTargetMaxSpeed;
}

bool AiVehObstacles::isBlockingPlayer( GameObject *pObject ) const {
    if ( pVehicle->getCategory() != eAiVehicleCategory::AVC_Bot ||
         !pVehicle->getDPC().DPCPattern.bBlockPlayer ||
         pObject->getType() != 0 ) {
        return false;
    }
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    return pPlayerCar != nullptr && pPlayerCar == pObject;
}

void AiVehObstacles::collectCurrentSpan() {
    // FUN_009515a0
    const AiVehNavOpponent::Situation &situation = GetSituation( pSituation );
    const ObjectLocationRoad &carRoad = situation.Loc.LocationRoad;
    const int32_t current = pLine->getCurrentIndex();
    const int32_t count = pLine->getCount();
    const OptPathElem &elem = pLine->at( current );
    const AiRoadIt &it = elem.It;
    AiObstacleSlot &slot = slots[current - 1];
    const float maxRatio = GetMaxWidthRatio( it );
    const float minRatio = 0.0f - maxRatio;
    AiRoadBuckets::Bucket *pBucket =
        gpAI->getRoadBuckets().getRoadBucket( situation.LocationID );

    slot.RoadID = static_cast<int32_t>( it.pRoad->ID );
    slot.bWay = it.bWay;
    slot.FractionStart = situation.U;
    const AiRoadShape *pShape = it.pRoad->getShape();
    slot.FractionEnd = pShape->computeRoadFraction( it );
    slot.TimeStart = 0.0f;
    const float carSpeed =
        fabsf( situation.Loc.VelocityX * pShape->TotalLength );
    const float elemSpeed = ( elem.TurningSpeed != kNoTime )
                                ? fabsf( elem.TurningSpeed )
                                : pVehicle->getInfos().MaxSpeed;
    slot.TimeEnd = elem.Len / ( ( carSpeed + elemSpeed ) * 0.5f );
    slot.FreeEnd = slot.FractionEnd;
    slot.FreeStart = slot.FractionStart;

    if ( it.pRoad->isOncomingOvertakeForbidden() ) {
        const float ratioA = ComputeWidthRatioXZ( it, elem.LateralLimits[0],
                                                  elem.LateralLimits[1] );
        const float ratioB = ComputeWidthRatioXZ( it, elem.LateralLimits[2],
                                                  elem.LateralLimits[3] );
        const float lo = ( ratioB > ratioA ) ? ratioA : ratioB;
        const float hi = ( ratioA > ratioB ) ? ratioA : ratioB;
        Marker start;
        start.FractionMin = 0.0f;
        start.FractionMax = 1.0f;
        start.Type = Marker::MT_Start;
        Marker end = start;
        end.Type = Marker::MT_End;
        if ( lo > 0.0f ) {
            start.WidthRatio = minRatio;
            end.WidthRatio = lo;
        } else {
            start.WidthRatio = hi;
            end.WidthRatio = maxRatio;
        }
        for ( int32_t i = 0; i < count - 1 && i < kNumSlots; i++ ) {
            const OptPathElem &next = pLine->at( i + 1 );
            if ( next.Type == OptPathElem::ET_Road &&
                 static_cast<int32_t>( next.It.pRoad->ID ) == slot.RoadID ) {
                addInterval( i, start, end );
            }
        }
    }

    if ( pBucket == nullptr ) {
        return;
    }
    const uint32_t selfID = pVehicle->getCarState()->getID();
    const AiVehIndDPCPattern &pattern = pVehicle->getDPC().DPCPattern;
    const float steeringSecurity = pVehicle->getDPC().SteeringSecurityFactor;
    for ( GameObject *pObject = pBucket->pFirstObject; pObject != nullptr;
          pObject = pObject->getNextInBucket() ) {
        if ( selfID == pObject->getID() || selfID == pObject->getParentID() ) {
            continue;
        }
        const ObjectWorldLocation &location = pObject->getWorldLocation();
        const ObjectLocationRoad &road = location.LocationRoad;
        if ( location.LocationType != ObjectWorldLocation::OWL_Road ||
             road.LateralMin == road.LateralMax ) {
            continue;
        }
        if ( isTargetObjectIgnored( pObject, &slot ) ) {
            continue;
        }

        const float mid = ( road.FractionMax + road.FractionMin ) * 0.5f;
        const float length = road.FractionMax - road.FractionMin;
        bool bSideBlock = false;
        if ( slot.bWay ) {
            if ( 0.0f > situation.Loc.VelocityX ) {
                return;
            }
            if ( 0.0f > road.FractionMin - carRoad.FractionMax ) {
                if ( isBlockingPlayer( pObject ) &&
                     gAIConfig.PathOptTrackDistance * situation.TolRoute >
                         fabsf( carRoad.FractionMin - road.FractionMax ) &&
                     location.VelocityX > situation.Loc.VelocityX ) {
                    slot.TargetWidthRatio = GetMidLateral( location );
                    continue;
                }
                const float reference =
                    pattern.bAvoidCollision ? carRoad.FractionMin
                    : pattern.bBlockPlayer  ? carRoad.FractionMax
                                            : situation.U;
                if ( reference > road.FractionMax ) {
                    continue;
                }
                if ( carRoad.LateralMax > road.LateralMin &&
                     road.LateralMax > carRoad.LateralMin ) {
                    Marker start;
                    start.WidthRatio = road.LateralMin;
                    start.FractionMin = 1.0f;
                    start.FractionMax = 1.0f;
                    start.Type = Marker::MT_Start;
                    Marker end;
                    end.WidthRatio = road.LateralMax;
                    end.FractionMin = 0.0f;
                    end.FractionMax = 1.0f;
                    end.Type = Marker::MT_End;
                    for ( int32_t i = 0; i < kNumSlots; i++ ) {
                        if ( pObject->getType() == 0 ) {
                            slots[i].addObject( pObject );
                        }
                        addInterval( i, start, end );
                    }
                } else {
                    bSideBlock = true;
                }
            }
        } else {
            if ( situation.Loc.VelocityX > 0.0f ) {
                return;
            }
            if ( road.FractionMax - carRoad.FractionMin > 0.0f ) {
                if ( isBlockingPlayer( pObject ) &&
                     gAIConfig.PathOptTrackDistance * situation.TolRoute >
                         fabsf( carRoad.FractionMax - road.FractionMin ) &&
                     situation.Loc.VelocityX > location.VelocityX ) {
                    slot.TargetWidthRatio = GetMidLateral( location );
                }
                const float reference =
                    pattern.bAvoidCollision ? carRoad.FractionMax
                    : pattern.bBlockPlayer  ? carRoad.FractionMin
                                            : situation.U;
                if ( road.FractionMin > reference ) {
                    continue;
                }
                bSideBlock = true;
            }
        }

        if ( bSideBlock ) {
            if ( road.LateralMax > carRoad.LateralMax ) {
                Marker start;
                start.WidthRatio = road.LateralMin;
                start.FractionMin = 0.0f;
                start.FractionMax = 1.0f;
                start.Type = Marker::MT_Start;
                Marker end = start;
                end.WidthRatio = maxRatio;
                end.Type = Marker::MT_End;
                for ( int32_t i = 0; i < count - 1 && i < kNumSlots; i++ ) {
                    const OptPathElem &next = pLine->at( i + 1 );
                    if ( next.Type != OptPathElem::ET_Road ||
                         static_cast<int32_t>( next.It.pRoad->ID ) !=
                             slot.RoadID ) {
                        continue;
                    }
                    if ( pObject->getType() == 0 ) {
                        slots[i].addObject( pObject );
                    }
                    addInterval( i, start, end );
                }
            } else if ( carRoad.LateralMin > road.LateralMin ) {
                Marker start;
                start.WidthRatio = minRatio;
                start.FractionMin = 1.0f;
                start.FractionMax = 1.0f;
                start.Type = Marker::MT_Start;
                Marker end;
                end.WidthRatio = road.LateralMax;
                end.FractionMin = 0.0f;
                end.FractionMax = 1.0f;
                end.Type = Marker::MT_End;
                for ( int32_t i = 0; i < kNumSlots; i++ ) {
                    if ( pObject->getType() == 0 ) {
                        slots[i].addObject( pObject );
                    }
                    addInterval( i, start, end );
                }
            }
            continue;
        }

        const float fractionA = location.VelocityX * slot.TimeStart + mid;
        const float fractionB = location.VelocityX * slot.TimeEnd + mid;
        const float margin = situation.CarLengthU * steeringSecurity;
        float lo = ( ( fractionB > fractionA ) ? fractionA : fractionB ) -
                   length - margin;
        if ( 0.0f > lo ) {
            lo = 0.0f;
        }
        float hi = ( ( fractionA > fractionB ) ? fractionA : fractionB ) +
                   margin + length;
        if ( hi > 1.0f ) {
            hi = 1.0f;
        }

        Marker start;
        start.WidthRatio = road.LateralMin;
        start.FractionMin = lo;
        start.FractionMax = hi;
        start.Type = Marker::MT_Start;
        Marker end = start;
        end.WidthRatio = road.LateralMax;
        end.Type = Marker::MT_End;
        if ( location.VelocityZ > 0.0f ) {
            start.WidthRatio =
                location.VelocityZ * slot.TimeStart + road.LateralMin;
            end.WidthRatio =
                location.VelocityZ * slot.TimeEnd + road.LateralMax;
        } else if ( 0.0f > location.VelocityZ ) {
            start.WidthRatio =
                location.VelocityZ * slot.TimeEnd + road.LateralMin;
            end.WidthRatio =
                location.VelocityZ * slot.TimeStart + road.LateralMax;
        }

        if ( !updateSlotRange( current - 1, fractionA, fractionB,
                               location.VelocityX, length ) ) {
            continue;
        }
        if ( pObject->getType() == 0 ) {
            slot.addObject( pObject );
        }
        addInterval( current - 1, start, end );
    }
}

bool AiVehObstacles::initSpanSlot( const int32_t slotIndex,
                                   const int32_t indexA,
                                   const int32_t indexB ) {
    // FUN_00951030
    if ( slotIndex >= kNumSlots ) {
        return false;
    }
    const AiVehNavOpponent::Situation &situation = GetSituation( pSituation );
    const OptPathElem &elemA = pLine->at( indexA );
    const OptPathElem &elemB = pLine->at( indexB );
    if ( elemA.Marker != OptPathElem::EM_None ) {
        return false;
    }

    AiObstacleSlot &slot = slots[slotIndex];
    slot.RoadID = static_cast<int32_t>( elemA.It.pRoad->ID );
    slot.bWay = elemA.It.bWay;
    slot.FractionStart =
        elemA.It.pRoad->getShape()->computeRoadFraction( elemA.It );
    slot.FractionEnd =
        elemB.It.pRoad->getShape()->computeRoadFraction( elemB.It );
    const Eigen::Vector4f delta =
        pLine->at( pLine->getCurrentIndex() ).Position - situation.Position;
    slot.TimeStart = sqrtf( delta.dot( delta ) ) / situation.CurSpeed;
    for ( int32_t i = pLine->getCurrentIndex(); i <= indexA; i++ ) {
        slot.TimeStart += pLine->at( i ).Len / situation.CurSpeed;
    }
    slot.TimeEnd = slot.TimeStart;
    for ( int32_t i = indexA + 1; i <= indexB; i++ ) {
        slot.TimeEnd += pLine->at( i ).Len / situation.CurSpeed;
    }
    slot.FreeEnd = slot.FractionEnd;
    slot.FreeStart = slot.FractionStart;
    return true;
}

void AiVehObstacles::collectSpan( const int32_t indexA,
                                  const int32_t indexB,
                                  const int32_t slotIndex ) {
    // FUN_00952ac0
    if ( !initSpanSlot( slotIndex, indexA, indexB ) ) {
        return;
    }
    const AiVehNavOpponent::Situation &situation = GetSituation( pSituation );
    const ObjectLocationRoad &carRoad = situation.Loc.LocationRoad;
    AiObstacleSlot &slot = slots[slotIndex];
    const AiRoadIt &it = pLine->at( indexA ).It;
    const AiRoadShape *pShape = it.pRoad->getShape();
    AiRoadBuckets::Bucket *pBucket =
        gpAI->getRoadBuckets().getRoadBucket( it.pRoad->ID );

    float minSpeed = pVehicle->getInfos().MaxSpeed;
    int32_t first = indexA;
    int32_t numElems = ( indexB < pLine->getCount() - 1 ) ? 3 : 2;
    if ( indexA > 0 ) {
        first--;
        numElems++;
    }
    for ( int32_t i = 0; i < numElems; i++ ) {
        float speed = pLine->at( first + i ).TurningSpeed;
        if ( speed == kNoTime ) {
            speed = pVehicle->getInfos().MaxSpeed;
        }
        if ( minSpeed > speed ) {
            minSpeed = speed;
        }
    }

    if ( pBucket == nullptr ) {
        return;
    }
    const uint32_t selfID = pVehicle->getCarState()->getID();
    const float steeringSecurity = pVehicle->getDPC().SteeringSecurityFactor;
    for ( GameObject *pObject = pBucket->pFirstObject; pObject != nullptr;
          pObject = pObject->getNextInBucket() ) {
        if ( selfID == pObject->getID() || selfID == pObject->getParentID() ) {
            continue;
        }
        const ObjectWorldLocation &location = pObject->getWorldLocation();
        const ObjectLocationRoad &road = location.LocationRoad;
        if ( location.LocationType != ObjectWorldLocation::OWL_Road ||
             road.LateralMin == road.LateralMax ) {
            continue;
        }
        if ( location.VelocityX * pShape->TotalLength >
             minSpeed * kSlowObjectSpeedRatio ) {
            continue;
        }
        if ( isTargetObjectIgnored( pObject, &slot ) ) {
            continue;
        }

        float margin = steeringSecurity;
        bool bSameRoad = false;
        if ( situation.isOnRoad() && situation.LocationID == road.RoadID ) {
            bSameRoad = true;
            margin *= situation.CarLengthU;
            const bool bBehind = situation.Loc.bWay
                                     ? carRoad.FractionMax > road.FractionMax
                                     : road.FractionMin > carRoad.FractionMin;
            if ( bBehind ) {
                if ( isBlockingPlayer( pObject ) ) {
                    const float gap =
                        situation.Loc.bWay
                            ? fabsf( carRoad.FractionMin - road.FractionMax )
                            : fabsf( carRoad.FractionMax - road.FractionMin );
                    const bool bFaster =
                        situation.Loc.bWay
                            ? location.VelocityX > situation.Loc.VelocityX
                            : situation.Loc.VelocityX > location.VelocityX;
                    if ( gAIConfig.PathOptTrackDistance * situation.TolRoute >
                             gap &&
                         bFaster ) {
                        slot.TargetWidthRatio = GetMidLateral( location );
                    }
                }
                continue;
            }
        }
        if ( !bSameRoad ) {
            margin = margin / pShape->TotalLength *
                     pVehicle->getCarState()->getBody()->BodyDimensions.z();
        }

        const float mid = ( road.FractionMax + road.FractionMin ) * 0.5f;
        const float length = road.FractionMax - road.FractionMin;
        const float fractionA = slot.TimeStart * location.VelocityX + mid;
        const float fractionB = location.VelocityX * slot.TimeEnd + mid;
        float lo = ( ( fractionB > fractionA ) ? fractionA : fractionB ) -
                   length - margin;
        if ( 0.0f > lo ) {
            lo = 0.0f;
        }
        float hi = ( ( fractionA > fractionB ) ? fractionA : fractionB ) +
                   length + margin;
        if ( hi > 1.0f ) {
            hi = 1.0f;
        }

        Marker start;
        start.WidthRatio = road.LateralMin;
        start.FractionMin = lo;
        start.FractionMax = hi;
        start.Type = Marker::MT_Start;
        Marker end = start;
        end.WidthRatio = road.LateralMax;
        end.Type = Marker::MT_End;
        if ( location.VelocityZ > 0.0f ) {
            start.WidthRatio =
                slot.TimeStart * location.VelocityZ + road.LateralMin;
            end.WidthRatio =
                location.VelocityZ * slot.TimeEnd + road.LateralMax;
        } else if ( 0.0f > location.VelocityZ ) {
            start.WidthRatio =
                location.VelocityZ * slot.TimeEnd + road.LateralMin;
            end.WidthRatio =
                location.VelocityZ * slot.TimeStart + road.LateralMax;
        }
        start.Time = ( situation.LocationID == road.RoadID )
                         ? computeCatchUpTime( location )
                         : computeTimeToElem( indexA );
        end.Time = start.Time;

        if ( !updateSlotRange( slotIndex, fractionA, fractionB,
                               location.VelocityX, length ) ) {
            continue;
        }
        if ( pObject->getType() == 0 ) {
            slot.addObject( pObject );
        }
        addInterval( slotIndex, start, end );
    }
}

void AiVehObstacles::collectJunction( const uint32_t junctionID,
                                      const int32_t index ) {
    // FUN_00952160
    if ( index < 3 ) {
        return;
    }
    const AiVehNavOpponent::Situation &situation = GetSituation( pSituation );
    const bool bHasBefore = index >= 4;
    const bool bHasAfter = index < pLine->getCount() - 1;

    AiRoadIt itBefore;
    if ( bHasBefore ) {
        itBefore = pLine->at( index - 3 ).It;
        if ( slots[index - 4].RoadID == -1 ) {
            initSpanSlot( index - 4, index - 4, index - 3 );
        }
    }
    AiRoadIt itAfter;
    if ( bHasAfter ) {
        itAfter = pLine->at( index + 1 ).It;
        initSpanSlot( index, index, index + 1 );
    }

    float maxDistance = kJunctionMaxDistance;
    if ( situation.isInJunction() && bHasAfter ) {
        maxDistance =
            ComputeDistanceAlongRoad( pVehicle->getCarState(), itAfter );
    }

    AiRoadBuckets::Bucket *pBucket =
        gpAI->getRoadBuckets().getJunctionBucket( junctionID );
    if ( pBucket == nullptr ) {
        return;
    }
    const uint32_t selfID = pVehicle->getCarState()->getID();
    for ( GameObject *pObject = pBucket->pFirstObject; pObject != nullptr;
          pObject = pObject->getNextInBucket() ) {
        if ( selfID == pObject->getID() || selfID == pObject->getParentID() ) {
            continue;
        }
        if ( kJunctionMaxDistance > maxDistance && bHasAfter &&
             ComputeDistanceAlongRoad( pObject, itAfter ) > maxDistance ) {
            continue;
        }

        if ( pObject->getType() == 0 ) {
            const Eigen::Vector4f velocity = pObject->getLinearVelocity();
            const float speed = sqrtf( velocity.dot( velocity ) );
            const float timeToJunction = computeTimeToElem( index - 3 );
            const Eigen::Vector4f direction =
                pLine->at( index ).Position - pLine->at( index - 3 ).Position;
            const float angle = fabsf( atan2f(
                velocity.z() * direction.x() - direction.z() * velocity.x(),
                velocity.x() * direction.x() + velocity.z() * direction.z() ) );
            const Eigen::Vector4f delta =
                pObject->getWorldPosition() - situation.Position;
            const float distance = sqrtf( delta.dot( delta ) );
            if ( angle > kHalfPI ) {
                const float closingSpeed = situation.CurSpeed + speed;
                if ( closingSpeed == 0.0f ||
                     timeToJunction >
                         distance / closingSpeed + kJunctionTimeMargin ) {
                    continue;
                }
            } else if ( kHalfPI > angle ) {
                const float closingSpeed = situation.CurSpeed - speed;
                if ( 0.0f >= closingSpeed ||
                     distance / closingSpeed + kJunctionTimeMargin >
                         computeTimeToElem( index ) ) {
                    continue;
                }
            } else {
                if ( situation.CurSpeed == 0.0f || itBefore.pRoad == nullptr ) {
                    continue;
                }
                const float lateralGap =
                    distance / situation.CurSpeed * sinf( angle ) * speed;
                if ( lateralGap > itBefore.getEntity()->pProfile->InnerWidth ) {
                    continue;
                }
            }
        }
        if ( isTargetObjectIgnored( pObject, nullptr ) ) {
            continue;
        }

        Eigen::Vector2f corners[kNumCorners];
        ComputeCornersXZ( pObject, corners );
        if ( bHasBefore ) {
            collectJunctionObject( pObject, corners, itBefore,
                                   slots[index - 4] );
        }
        if ( bHasAfter ) {
            collectJunctionObject( pObject, corners, itAfter, slots[index] );
        }
    }
}

void AiVehObstacles::collectJunctionObject(
    GameObject *pObject,
    const Eigen::Vector2f ( &corners )[4],
    const AiRoadIt &it,
    AiObstacleSlot &slot ) {
    // FUN_00952160 (inlined)
    float lo = 0.0f;
    float hi = 0.0f;
    ProjectCorners( it, corners, lo, hi );
    if ( !( hi > lo ) ) {
        return;
    }
    Marker start;
    start.WidthRatio = lo;
    start.FractionMin = slot.FractionStart;
    start.FractionMax = slot.FractionEnd;
    start.Time = slot.TimeStart;
    start.Type = Marker::MT_Start;
    Marker end = start;
    end.WidthRatio = hi;
    end.Type = Marker::MT_End;
    if ( pObject->getType() == 0 ) {
        slot.addObject( pObject );
    }
    slot.insertInterval( start, end );
}

bool AiVehObstacles::updateSlotRange( const int32_t slotIndex,
                                      const float fractionA,
                                      const float fractionB,
                                      const float velocity,
                                      const float length ) {
    // FUN_009512a0
    if ( slotIndex >= kNumSlots ) {
        return false;
    }
    const AiVehNavOpponent::Situation &situation = GetSituation( pSituation );
    AiObstacleSlot &slot = slots[slotIndex];
    const float steeringSecurity = pVehicle->getDPC().SteeringSecurityFactor;

    float margin = 0.0f;
    if ( situation.isOnRoad() &&
         static_cast<int32_t>( situation.LocationID ) == slot.RoadID ) {
        margin = steeringSecurity * situation.CarLengthU;
    } else {
        const float carLength =
            pVehicle->getCarState()->getBody()->BodyDimensions.z();
        AiRoadMap::Road *pRoad =
            gpAI->getRoadMap().getRoad( static_cast<uint32_t>( slot.RoadID ) );
        margin = steeringSecurity / pRoad->getShape()->TotalLength * carLength;
    }

    if ( slot.bWay ) {
        if ( velocity > situation.Loc.VelocityX ) {
            return false;
        }
        const float hi = margin + length + slot.FractionEnd;
        const float lo = slot.FractionStart - length - margin;
        const bool bOverlap = ( hi > fractionA && fractionA > lo ) ||
                              ( hi > fractionB && fractionB > lo ) ||
                              ( fractionA > hi && lo > fractionB ) ||
                              ( fractionB > hi && lo > fractionA );
        if ( !bOverlap ) {
            return false;
        }
        const float freeEnd =
            ( ( fractionB > fractionA ) ? fractionA : fractionB ) - length -
            margin;
        const float freeStart =
            margin + ( ( fractionB > fractionA ) ? fractionB : fractionA ) +
            length;
        if ( freeEnd > slot.FractionStart ) {
            if ( slot.FreeEnd > freeEnd ) {
                slot.FreeEnd = freeEnd;
            }
        } else {
            slot.FreeEnd = slot.FractionStart;
        }
        if ( slot.FractionEnd > freeStart ) {
            if ( freeStart > slot.FreeStart ) {
                slot.FreeStart = freeStart;
            }
        } else {
            slot.FreeStart = slot.FractionEnd;
        }
        return true;
    }

    if ( situation.Loc.VelocityX > velocity ) {
        return false;
    }
    const float lo = slot.FractionEnd - length - margin;
    const float hi = slot.FractionStart + margin + length;
    const bool bOverlap = ( fractionA > lo && hi > fractionA ) ||
                          ( fractionB > lo && hi > fractionB ) ||
                          ( lo > fractionA && fractionB > hi ) ||
                          ( lo > fractionB && fractionA > hi );
    if ( !bOverlap ) {
        return false;
    }
    const float freeEnd =
        margin + ( ( fractionA > fractionB ) ? fractionA : fractionB ) + length;
    const float freeStart =
        ( ( fractionA > fractionB ) ? fractionB : fractionA ) - length - margin;
    if ( slot.FractionStart > freeEnd ) {
        if ( freeEnd > slot.FreeEnd ) {
            slot.FreeEnd = freeEnd;
        }
    } else {
        slot.FreeEnd = slot.FractionStart;
    }
    if ( freeStart > slot.FractionEnd ) {
        if ( slot.FreeStart > freeStart ) {
            slot.FreeStart = freeStart;
        }
    } else {
        slot.FreeStart = slot.FractionEnd;
    }
    return true;
}

void AiVehObstacles::addInterval( const int32_t slotIndex,
                                  const Marker &start,
                                  const Marker &end ) {
    // FUN_0094e2f0
    slots[slotIndex].insertInterval( start, end );
}

float AiVehObstacles::computeCatchUpTime(
    const ObjectWorldLocation &location ) const {
    // FUN_00950140
    const AiVehNavOpponent::Situation &situation = GetSituation( pSituation );
    if ( !situation.isOnRoad() ||
         situation.LocationID != location.LocationRoad.RoadID ) {
        return kNoTime;
    }
    AiRoadMap::Road *pRoad =
        gpAI->getRoadMap().getRoad( location.LocationRoad.RoadID );
    if ( pRoad != nullptr ) {
        pRoad->getShape();
    }
    if ( location.LocationType != ObjectWorldLocation::OWL_Road ) {
        return kNoTime;
    }

    const ObjectLocationRoad &carRoad = situation.Loc.LocationRoad;
    float carFraction = 0.0f;
    float objectFraction = 0.0f;
    if ( situation.Loc.bWay ) {
        carFraction = carRoad.FractionMax;
        objectFraction = location.LocationRoad.FractionMin;
        if ( carFraction > objectFraction ) {
            return kNoTime;
        }
    } else {
        carFraction = carRoad.FractionMin;
        objectFraction = location.LocationRoad.FractionMax;
        if ( objectFraction > carFraction ) {
            return kNoTime;
        }
    }

    const float relativeVelocity = situation.Loc.VelocityX - location.VelocityX;
    if ( relativeVelocity == 0.0f ) {
        return kNoTime;
    }
    const float time = ( objectFraction - carFraction ) / relativeVelocity;
    const float fraction = time * situation.Loc.VelocityX + carFraction;
    if ( 0.0f > time || fraction > 1.0f || 0.0f > fraction ) {
        return kNoTime;
    }
    return time;
}

float AiVehObstacles::computeTimeToElem( const int32_t index ) const {
    // FUN_0094ef60
    const int32_t current = pLine->getCurrentIndex();
    const Eigen::Vector4f delta =
        OptPath::GetEffectivePosition( pLine->at( current ) ) -
        pVehicle->getInfos().GroundMatrix.row( 3 ).transpose();
    const float invSpeed = 1.0f / GetSituation( pSituation ).CurSpeed;
    float time = invSpeed * sqrtf( delta.dot( delta ) );
    for ( int32_t i = current + 1; i < index; i++ ) {
        if ( i < 0 || i >= pLine->getCount() ) {
            return kUnreachableTime;
        }
        time += pLine->at( i ).Len * invSpeed;
    }
    return time;
}

bool AiVehObstacles::solve( bool &bOutStop ) {
    // FUN_00952a80
    if ( pSituation->pOptPath->getCount() != 0 ) {
        pSituation->pOptPath->updateCurrentIndex( &pSituation->LineDistance );
    }
    if ( pLine->getCurrentIndex() == 0 ) {
        return false;
    }
    return solveAhead( bOutStop );
}

bool AiVehObstacles::solveAhead( bool &bOutStop ) {
    // FUN_00950ea0
    const int32_t current = pLine->getCurrentIndex();
    if ( current == 0 ) {
        return true;
    }
    const AiVehNavOpponent::Situation &situation = GetSituation( pSituation );
    const float lookAhead =
        pVehicle->computeBrakingDistance( situation.CurSpeed, 0.0f, 0.0f ) *
        kLookAheadFactor;

    bool bCurrentSolved = true;
    float distance = 0.0f;
    int32_t last = 0;
    if ( situation.isOnRoad() &&
         pLine->at( current ).Type == OptPathElem::ET_Road &&
         pLine->at( current - 1 ).Type == OptPathElem::ET_Road ) {
        bCurrentSolved = solveCurrent( lookAhead, distance, last, bOutStop );
    }

    bool bSolved = true;
    while ( lookAhead > distance ) {
        const int32_t next = last + 3;
        if ( next >= pLine->getCount() ||
             pLine->at( next ).Type != OptPathElem::ET_Road ) {
            break;
        }
        float delta = 0.0f;
        bSolved =
            solveNext( next, lookAhead - distance, delta, last, bOutStop );
        distance += delta;
        if ( !bSolved ) {
            break;
        }
    }
    return bCurrentSolved && bSolved;
}

bool AiVehObstacles::solveCurrent( const float lookAhead,
                                   float &outDistance,
                                   int32_t &outLast,
                                   bool &bOutStop ) {
    // FUN_00950850
    const AiVehNavOpponent::Situation &situation = GetSituation( pSituation );
    OptPathElem start;
    start.Id = -1;
    start.Position = situation.Position;
    start.field_0x28 = -1.0f;
    start.Type = OptPathElem::ET_Road;
    start.It = situation.RoadIt;
    start.WidthRatio = situation.V;
    start.RouteIndex = 0;
    start.TurningSpeed = situation.CurSpeed;
    return solveSpan( pLine->getCurrentIndex() - 1, lookAhead, start,
                      gbSmoothCurrentAvoidance, outDistance, outLast,
                      bOutStop );
}

bool AiVehObstacles::solveNext( const int32_t first,
                                const float remaining,
                                float &outDistance,
                                int32_t &outLast,
                                bool &bOutStop ) {
    // FUN_00950240
    const AiVehNavOpponent::Situation &situation = GetSituation( pSituation );
    const OptPathElem &elem = pLine->at( first );
    OptPathElem start;
    start.Id = -1;
    start.Position = elem.Position;
    start.field_0x28 = -1.0f;
    start.Type = OptPathElem::ET_Road;
    start.It = situation.RoadIt;
    start.WidthRatio = elem.WidthRatio;
    start.RouteIndex = 0;
    start.TurningSpeed = situation.CurSpeed;
    return solveSpan( first, remaining, start, gbSmoothNextAvoidance,
                      outDistance, outLast, bOutStop );
}

bool AiVehObstacles::solveSpan( const int32_t first,
                                const float lookAhead,
                                const OptPathElem &start,
                                const bool bSmooth,
                                float &outDistance,
                                int32_t &outLast,
                                bool &bOutStop ) {
    // FUN_00950850 / FUN_00950240 (shared body)
    const AiVehNavOpponent::Situation &situation = GetSituation( pSituation );
    const int32_t lastOnRoad =
        pLine->findLastOnSameRoad( first, -1.0f, nullptr );
    float distance = 0.0f;
    int32_t last = pLine->findLastOnSameRoad( first, lookAhead, &distance );
    if ( last - first < 2 && last + 1 <= lastOnRoad ) {
        last++;
    }
    outLast = last;
    outDistance = distance;
    if ( distance == 0.0f ) {
        return true;
    }

    AiObstacleSlot merged = slots[first];
    for ( int32_t i = first + 1; i <= last - 1; i++ ) {
        mergeSlots( merged, slots[i], &merged );
    }

    const AiRoadIt it = pLine->at( first ).It;
    const float desired = start.WidthRatio;
    bool bBlocked = false;
    for ( int32_t i = first; i < last; i++ ) {
        bool bFitStart = false;
        bool bFitNext = false;
        if ( !isGapFree( i, start, pLine->at( i + 1 ), bFitStart, bFitNext ) ) {
            bBlocked = true;
            break;
        }
    }
    if ( !bBlocked ) {
        if ( 1.0f >= fabsf( merged.TargetWidthRatio ) ) {
            pLine->setLateralTarget( first, lastOnRoad,
                                     merged.TargetWidthRatio );
        }
        return true;
    }

    float target = OptPath::kUnsetWidthRatio;
    const bool bSolved = findGapTarget( merged, first, desired, target );
    if ( bSmooth ) {
        const float delta = target - desired;
        const float lateral =
            fabsf( delta * it.getEntity()->pProfile->InnerWidth * 0.5f );
        const float alongRoad = fabsf( ( merged.FreeEnd - situation.U ) *
                                       it.pRoad->getShape()->TotalLength );
        float ratio = target;
        if ( alongRoad > kMinAvoidDistance ) {
            const float slope = lateral / alongRoad;
            if ( 1.0f > slope * kAvoidSlopeFactor ) {
                ratio = slope * delta * kAvoidSlopeFactor + desired;
            }
        }
        if ( ratio > 1.0f ) {
            ratio = 1.0f;
        } else if ( -1.0f > ratio ) {
            ratio = -1.0f;
        }
        pLine->setLateralTarget( first, lastOnRoad, ratio );
    } else {
        pLine->setLateralTarget( first, lastOnRoad, target );
    }
    if ( !bSolved ) {
        bOutStop = merged.hasSoftCollisionCar();
    }
    return bSolved;
}

static float ComputeAvoidanceMargin( const AiVehicle *pVehicle,
                                     const float widthRatio,
                                     const bool bScalePlayer ) {
    const float steeringSecurity = pVehicle->getDPC().SteeringSecurityFactor;
    if ( pVehicle->isPlayerCar() ) {
        return ( bScalePlayer ? steeringSecurity * widthRatio : widthRatio ) *
               kPlayerMarginFactor;
    }
    return steeringSecurity * widthRatio *
           ( IsBike( pVehicle ) ? kBikeMarginFactor : kCarMarginFactor );
}

bool AiVehObstacles::isGapFree( const int32_t slotIndex,
                                const OptPathElem &elemA,
                                const OptPathElem &elemB,
                                bool &bOutFitA,
                                bool &bOutFitB ) const {
    // FUN_0094dcc0
    if ( slotIndex >= kNumSlots ) {
        return false;
    }
    const AiObstacleSlot &slot = slots[slotIndex];
    const AiRoadIt &it = ( elemA.It.pRoad != nullptr ) ? elemA.It : elemB.It;
    const float maxRatio = GetMaxWidthRatio( it );
    const float minRatio = 0.0f - maxRatio;
    if ( slot.Markers.empty() ) {
        return true;
    }

    const float widthRatio = pVehicle->getCarState()->getDimensions().x() /
                             it.getEntity()->pProfile->InnerWidth;
    const float margin = ComputeAvoidanceMargin( pVehicle, widthRatio, false );
    const float ratioA = GetEffectiveWidthRatio( elemA );
    const float ratioB = GetEffectiveWidthRatio( elemB );
    const float hiA = ratioA + margin;
    const float hiB = ratioB + margin;
    const float loA = ratioA - margin;
    const float loB = ratioB - margin;
    const std::vector<Marker> &markers = slot.Markers;

    bool bFitA = markers.front().WidthRatio > hiA;
    bool bFitB = markers.front().WidthRatio > hiB;
    bOutFitA |= bFitA;
    bOutFitB |= bFitB;
    if ( bFitA && bFitB ) {
        return true;
    }
    bFitA = loA > markers.back().WidthRatio;
    bFitB = loB > markers.back().WidthRatio;
    bOutFitA |= bFitA;
    bOutFitB |= bFitB;
    if ( bFitA && bFitB ) {
        return true;
    }

    const int32_t count = static_cast<int32_t>( markers.size() );
    for ( int32_t i = 0; i < count; i++ ) {
        float gapStart = minRatio;
        float gapEnd = markers[0].WidthRatio;
        if ( i != 0 ) {
            if ( ( i & 1 ) == 0 ) {
                continue;
            }
            gapStart = markers[i].WidthRatio;
            gapEnd = ( i + 1 < count ) ? markers[i + 1].WidthRatio : maxRatio;
        }
        bFitA = loA > gapStart && gapEnd > hiA;
        bFitB = loB > gapStart && gapEnd > hiB;
        bOutFitA |= bFitA;
        bOutFitB |= bFitB;
        if ( bFitA && bFitB ) {
            return true;
        }
    }
    return false;
}

bool AiVehObstacles::findGapTarget( const AiObstacleSlot &slot,
                                    const int32_t index,
                                    const float desired,
                                    float &outTarget ) const {
    // FUN_0094f180
    const AiRoadIt &it = pLine->at( index ).It;
    const float maxRatio = GetMaxWidthRatio( it );
    const float minRatio = 0.0f - maxRatio;
    const float widthRatio = pVehicle->getCarState()->getDimensions().x() /
                             ( it.getEntity()->pProfile->InnerWidth * 0.5f );
    const float margin = ComputeAvoidanceMargin( pVehicle, widthRatio, true );
    const float halfMargin = margin * 0.5f;

    float best = OptPath::kUnsetWidthRatio;
    float bestDistance = kNoGapDistance;
    const std::vector<Marker> &markers = slot.Markers;
    const int32_t count = static_cast<int32_t>( markers.size() );
    const auto getRatio = [&]( const int32_t i ) {
        return ( i < count ) ? markers[i].WidthRatio : maxRatio;
    };
    for ( int32_t i = 0; i < count; i++ ) {
        float gapStart = minRatio;
        float spanStart = minRatio;
        float gapEnd = getRatio( 0 );
        float spanEnd = getRatio( 1 );
        if ( i != 0 ) {
            if ( ( i & 1 ) == 0 ) {
                continue;
            }
            gapStart = markers[i].WidthRatio;
            spanStart = markers[i - 1].WidthRatio;
            gapEnd = getRatio( i + 1 );
            spanEnd = ( i + 1 < count ) ? getRatio( i + 2 ) : maxRatio;
        }
        if ( !( fabsf( gapEnd - gapStart ) > margin ) ) {
            continue;
        }

        float candidate =
            ( fabsf( gapStart - desired ) >= fabsf( gapEnd - desired ) )
                ? gapEnd - halfMargin
                : halfMargin + gapStart;
        if ( !( desired > spanStart && spanEnd > desired &&
                candidate > spanStart && spanEnd > candidate ) ) {
            continue;
        }

        const float targetRatio = slot.TargetWidthRatio;
        if ( 1.0f >= fabsf( targetRatio ) && targetRatio > gapStart &&
             gapEnd > targetRatio ) {
            if ( fabsf( targetRatio - gapStart ) > halfMargin &&
                 fabsf( gapEnd - targetRatio ) > halfMargin ) {
                candidate = targetRatio;
            }
            best = candidate;
            break;
        }
        const float distance = fabsf( candidate - desired );
        if ( bestDistance > distance ) {
            bestDistance = distance;
            best = candidate;
        }
    }

    if ( best != OptPath::kUnsetWidthRatio ) {
        outTarget = best;
        return true;
    }
    if ( it.bWay ) {
        outTarget = ( margin > desired ) ? halfMargin : desired;
    } else {
        outTarget = ( desired > 0.0f - margin ) ? -halfMargin : desired;
    }
    return false;
}

bool AiVehObstacles::mergeSlots( const AiObstacleSlot &slotA,
                                 const AiObstacleSlot &slotB,
                                 AiObstacleSlot *pOut ) const {
    // FUN_0094e6f0
    if ( slotA.RoadID != slotB.RoadID || slotA.bWay != slotB.bWay ||
         slotA.TargetWidthRatio != slotB.TargetWidthRatio ) {
        return false;
    }
    if ( pOut == nullptr ) {
        return true;
    }

    AiObstacleSlot merged;
    merged.RoadID = slotA.RoadID;
    merged.bWay = slotA.bWay;
    merged.TargetWidthRatio = slotA.TargetWidthRatio;
    merged.TimeStart = ( slotB.TimeStart > slotA.TimeStart ) ? slotA.TimeStart
                                                             : slotB.TimeStart;
    merged.TimeEnd =
        ( slotA.TimeEnd > slotB.TimeEnd ) ? slotA.TimeEnd : slotB.TimeEnd;
    if ( slotA.bWay ) {
        merged.FractionStart = ( slotB.FractionStart > slotA.FractionStart )
                                   ? slotA.FractionStart
                                   : slotB.FractionStart;
        merged.FreeEnd =
            ( slotB.FreeEnd > slotA.FreeEnd ) ? slotA.FreeEnd : slotB.FreeEnd;
        merged.FractionEnd = ( slotA.FractionEnd > slotB.FractionEnd )
                                 ? slotA.FractionEnd
                                 : slotB.FractionEnd;
        merged.FreeStart = ( slotA.FreeStart > slotB.FreeStart )
                               ? slotA.FreeStart
                               : slotB.FreeStart;
    } else {
        merged.FractionStart = ( slotA.FractionStart > slotB.FractionStart )
                                   ? slotA.FractionStart
                                   : slotB.FractionStart;
        merged.FreeEnd =
            ( slotA.FreeEnd > slotB.FreeEnd ) ? slotA.FreeEnd : slotB.FreeEnd;
        merged.FractionEnd = ( slotB.FractionEnd > slotA.FractionEnd )
                                 ? slotA.FractionEnd
                                 : slotB.FractionEnd;
        merged.FreeStart = ( slotB.FreeStart > slotA.FreeStart )
                               ? slotA.FreeStart
                               : slotB.FreeStart;
    }

    if ( slotA.Markers.empty() ) {
        merged.Markers = slotB.Markers;
    } else {
        merged.Markers = slotA.Markers;
        Marker start;
        for ( const Marker &marker : slotB.Markers ) {
            if ( marker.Type == Marker::MT_Start ) {
                start = marker;
            } else if ( marker.Type == Marker::MT_End ) {
                merged.insertInterval( start, marker );
            }
        }
    }

    for ( GameObject *pObject : slotA.Objects ) {
        merged.addObject( pObject );
    }
    for ( GameObject *pObject : slotB.Objects ) {
        merged.addObject( pObject );
    }
    *pOut = merged;
    return true;
}
