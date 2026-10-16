#include "shared.h"
#include "mng_gps.h"

#include "core/maths_helper.h"
#include "ai/ai_pool.h"
#include "ai/ai_road_shape.h"
#include "ai/gs_ai.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/gm_base.h"
#include "game/mng_hud.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "player_data/gs_playerdata.h"
#include "render/draw_list.h"
#include "tdu_instance.h"

#include <cmath>

MngGPS *gpMngGPS = nullptr; // DAT_010e8008

static constexpr float kKmToMiles = 0.621504f;            // DAT_00f418c0
static constexpr float kSameObjectiveDistanceSq = 0.001f; // DAT_00f4167c
static constexpr float kArrivalRadiusSq = 3600.0f;        // DAT_00f41740
static constexpr float kRouteUpdateDelay = 0.5f;          // DAT_00f41674
static constexpr float kRouteMargin = 40.0f;              // DAT_00f41b70
static constexpr float kWrongWayDelay = 2.0f;             // DAT_00f416b0
static constexpr float kWrongWayVoiceDelay = 10.0f;       // DAT_00f4166c
static constexpr float kTurnAnnounceDistance = 200.0f;    // DAT_00f4170c
static constexpr float kTurnAnnounceNear = 50.0f;         // DAT_00f41848
static constexpr float kRejoinDistanceSq = 90000.0f;      // DAT_00f41ca4
static constexpr float kRouteStep = 20.0f;
static constexpr uint32_t kRouteExhaustedPoints = 300;
static constexpr uint32_t kRouteColor = 0xff00ff00u;

static constexpr int32_t kVoiceWrongWay = 0;
static constexpr int32_t kVoiceNewDestination = 16;

static bool sbDestinationAnnounced = false;        // BYTE_010e8005
static Eigen::Vector4f sAnnouncedDestination = {}; // DAT_01698d20

// PTR_s_up_00f9c21c
static const char *kArrowLabels[MngGPS::kArrowNone + 1] = {
    "up",       "rightup", "right",  "rightdown", "down",
    "leftdown", "left",    "leftup", "empty" };

static GPSVoice *GetGPSVoice() {
    return gpFlash->getFlashSound()->getGPSVoice();
}

static AiRoadMap &GetRoadMap() { return gpAI->getRoadMap(); }

static bool IsObjectiveSet( const ObjectiveMarker &marker ) {
    return marker.Type != nullptr && marker.Type[0] != '\0';
}

static void ComputeJunctionTurn( const AiRoadIt &arrive,
                                 float &outTurn,
                                 int32_t &outArrow,
                                 const AiRoadIt &leave ) {
    // FUN_006c2ee0
    const AiRoadMap::Junction *pJunction =
        arrive.bWay ? arrive.pRoad->pJunctionEnd : arrive.pRoad->pJunctionBegin;
    const float inAngle = arrive.computeAngleAtJunction( pJunction );
    const float outAngle = leave.computeAngleAtJunction( pJunction );

    int32_t numNearOut = 0;
    int32_t numRightSharp = 0;
    int32_t numRight = 0;
    int32_t numLeft = 0;
    int32_t numLeftSharp = 0;
    for ( int32_t i = 0; i < pJunction->NumRoads; i++ ) {
        const float angle = pJunction->computeRoadAngle( i );
        float delta = std::fabs( outAngle - angle );
        if ( delta > 180.0f ) {
            delta = 360.0f - delta;
        }
        if ( 91.0f > delta ) {
            numNearOut++;
        }
        float relative = angle - inAngle;
        if ( 0.0f > relative ) {
            relative += 360.0f;
        }
        if ( relative > 44.0f && 91.0f > relative ) {
            numRightSharp++;
        }
        if ( relative > 89.0f && 136.0f > relative ) {
            numRight++;
        }
        if ( relative > 224.0f && 271.0f > relative ) {
            numLeft++;
        }
        if ( relative > 269.0f && 316.0f > relative ) {
            numLeftSharp++;
        }
    }

    float turn = outAngle - inAngle;
    if ( 0.0f > turn ) {
        turn += 360.0f;
    }
    outTurn = turn;

    if ( 61.0f > turn ) {
        outArrow = 3;
    } else if ( 119.0f > turn ) {
        if ( 89.0f > turn && numRight >= 1 ) {
            outArrow = 3;
        } else if ( turn > 91.0f && numRightSharp >= 1 ) {
            outArrow = 1;
        } else {
            outArrow = 2;
        }
    } else if ( 151.0f > turn ) {
        outArrow = ( turn > 149.0f && numNearOut <= 1 ) ? 0 : 1;
    } else if ( 209.0f > turn ) {
        outArrow = 0;
    } else if ( 241.0f > turn ) {
        outArrow = ( 211.0f > turn && numNearOut <= 1 ) ? 0 : 7;
    } else if ( 299.0f > turn ) {
        if ( 269.0f > turn && numLeftSharp >= 1 ) {
            outArrow = 7;
        } else if ( turn > 271.0f && numLeft >= 1 ) {
            outArrow = 5;
        } else {
            outArrow = 6;
        }
    } else {
        outArrow = 5;
    }
}

MngGPS::MngGPS()
    : Manager(),
      mutex( "MngGPS" ),
      bRouteIgnoreOneWay( false ),
      bHadObjective( false ),
      lastObjectivePosition( Eigen::Vector4f::Zero() ),
      lastSecondaryObjectivePosition( Eigen::Vector4f::Zero() ),
      carRoadFraction( -1.0f ),
      carDisplayPosition( Eigen::Vector4f::Zero() ),
      carRoadPosition( 0.0f, 0.0f, 0.0f, -1.0f ),
      bCarOnRoad( false ),
      wrongWayTimer( 0.0f ),
      bWrongWayAnnounced( false ),
      objectiveIt(),
      secondaryIt(),
      bBigArrowDisabled( true ),
      bVoiceEnabled( true ),
      bDistanceCalloutsEnabled( true ),
      nextJunctionArrow( kArrowNone ),
      distanceToNextJunction( FLT_MAX ),
      route(),
      secondaryRoute(),
      updateAccumulator( 0.0f ),
      routePoints(),
      numRoutePoints( 0 ),
      bRouteExhausted( false ),
      lastCalloutDistance( 0.0f ),
      lastTurnCalloutDistance( 0.0f ),
      wrongWayVoiceTimer( 0.0f ),
      bObjectiveInRange( false ),
      cachedSessionPlayerCount( 0 ) {
    // FUN_006c2280
    gpMngGPS = this;
}

MngGPS::~MngGPS() {
    // FUN_006c2230
    gpMngGPS = nullptr;
    secondaryRoute.clear();
    route.clear();
}

bool MngGPS::initialize( TestDriveGameInstance * ) {
    // FUN_006c1d50
    mutex.initialize( "MngGPS" );
    cachedSessionPlayerCount = 0;
    return true;
}

void MngGPS::reset() {
    // FUN_006c1d40
}

void MngGPS::draw() {
    // FUN_0063f2b0
}

void MngGPS::pause() {
    // FUN_006c1d30
}

void MngGPS::terminate() {
    // FUN_006c1d20
    mutex.terminate();
}

void MngGPS::projectOnNearestRoad( const Eigen::Vector4f &position,
                                   AiRoadIt &outIt ) {
    AiRoadMap::Road *pRoad = GetRoadMap().findNearestRoad( position, true );
    // NOTE: retail does not check the road.
    if ( pRoad == nullptr || pRoad->getShape() == nullptr ) {
        return;
    }
    Matrix4x4f_t frame = Matrix4x4f_t::Identity();
    frame.row( 3 ) =
        Eigen::Vector4f( position.x(), position.y(), position.z(), 1.0f )
            .transpose();
    ProjectOnRoadShape( frame, outIt, nullptr, false, pRoad->getShape() );
}

bool MngGPS::computeRouteDirectionOnRoad( const uint32_t roadID,
                                          const Eigen::Vector4f &position,
                                          Eigen::Vector4f &outDirection ) {
    // FUN_006c3530
    ScopedMutexLock lock( &mutex );
    bool bFound = false;
    for ( const AiRouteElem *pElem = route.getHead(); pElem != nullptr;
          pElem = pElem->pNext ) {
        if ( pElem->To.getRoadID() != roadID ) {
            continue;
        }
        AiRoadMap::Road *pRoad = GetRoadMap().getRoad( roadID );
        Matrix4x4f_t frame = Matrix4x4f_t::Identity();
        frame.row( 3 ) = position.transpose();
        AiRoadIt it;
        ProjectOnRoadShape( frame, it, nullptr, false, pRoad->getShape() );
        it.bWay = pElem->To.getWay();
        const Eigen::Vector4f tangent = it.getEntity()->computeTangent(
            it.IndWay, it.bWay ? it.T : 1.0f - it.T );
        outDirection = tangent / tangent.norm();
        if ( !it.bWay ) {
            outDirection *= -1.0f;
        }
        bFound = true;
    }
    return bFound;
}

void MngGPS::tick( float deltaTime, float ) {
    // FUN_006c38d0
    updateAccumulator += deltaTime;

    const eGameMode activeGameMode = gpTestDriveInstance->getActiveGameMode();
    int32_t numPlayers = 1;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    if ( activeGameMode == GM_Mission ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: GMMission
    } else {
        numPlayers = gpAI->getAIPool()->getNumBots() + 1;
    }
    if ( !( gpScenaric->isICEnabled() && gpActiveGameMode != nullptr &&
            gpActiveGameMode->hasRequestedExit() ) ) {
        if ( cachedSessionPlayerCount < numPlayers ) {
            FlashPlayer *pHudPlayer = gpMngHud->getHudPlayer();
            if ( pHudPlayer != nullptr ) {
                cachedSessionPlayerCount = numPlayers;
                pHudPlayer->setVariableValue( "/:nb_FRplayers", numPlayers );
                FlashClip clip;
                if ( pHudPlayer->getClipPlayer(
                         "/HUD/minimap/gps_track/nb_players/in_out", &clip ) ) {
                    clip.goToLabelAndPlay( "in", false );
                }
            }
        } else if ( cachedSessionPlayerCount > numPlayers ) {
            onPlayerLeft( numPlayers, -1 );
        }
    }

    // Objective selection
    Eigen::Vector4f target;
    Eigen::Vector4f secondary = Eigen::Vector4f::Zero();
    const char *pType = "";
    bool bHasObjective = false;
    bool bHasSecondary = false;
    bool bNoExtraChallenge = false;
    float arrivalRadiusSq = 0.0f;
    if ( activeGameMode == GM_Mission ) {
        const ObjectiveMarker &primary = gpScenaric->getObjective( 3 );
        const ObjectiveMarker &next = gpScenaric->getObjective( 4 );
        target = primary.Position;
        pType = primary.Type;
        bHasObjective = IsObjectiveSet( primary );
        bNoExtraChallenge = gpScenaric->getActiveExtraChallenge() == nullptr;
        secondary = next.Position;
        bHasSecondary = IsObjectiveSet( next );
    } else {
        for ( int32_t slot = 2;; slot-- ) {
            const ObjectiveMarker &marker = gpScenaric->getObjective( slot );
            target = marker.Position;
            pType = marker.Type;
            bHasObjective = IsObjectiveSet( marker );
            if ( bHasObjective || slot == 0 ) {
                break;
            }
        }
        arrivalRadiusSq = kArrivalRadiusSq;
    }
    if ( pType == nullptr ) {
        pType = "";
    }

    const float objectiveMoveSq = LengthSq4( target - lastObjectivePosition );
    const bool bSameObjective = bHasObjective == bHadObjective &&
                                objectiveMoveSq < kSameObjectiveDistanceSq &&
                                bRouteIgnoreOneWay == bNoExtraChallenge;
    bHadObjective = bHasObjective;

    const bool bAlreadyAnnounced =
        sbDestinationAnnounced &&
        LengthSq4( target - sAnnouncedDestination ) < kSameObjectiveDistanceSq;
    if ( bHasObjective && bVoiceEnabled && !bAlreadyAnnounced &&
         GetGPSVoice()->play( kVoiceNewDestination ) ) {
        sAnnouncedDestination = target;
        sbDestinationAnnounced = true;
    }

    if ( bSameObjective && updateAccumulator <= kRouteUpdateDelay ) {
        wrongWayTimer += deltaTime;
        return;
    }

    FlashPlayer *pHudPlayer = gpMngHud->getHudPlayer();
    CarState *pCar = gpScenaric->getLocalPlayer().getCarState();
    if ( !bHasObjective || pCar == nullptr ) {
        route.clear();
        carRoadPosition.w() = -1.0f;
        wrongWayTimer = 0.0f;
        bWrongWayAnnounced = false;
        wrongWayVoiceTimer = kWrongWayVoiceDelay;
        numRoutePoints = 0;
        FlashClip trackClip;
        if ( pHudPlayer != nullptr &&
             pHudPlayer->getClipPlayer( "/HUD/minimap/gps_track",
                                        &trackClip ) ) {
            trackClip.goToLabelAndPlay( "off", false );
        }
        wrongWayTimer += deltaTime;
        return;
    }

    if ( !( objectiveMoveSq < kSameObjectiveDistanceSq ) ||
         objectiveIt.pRoad == nullptr ) {
        projectOnNearestRoad( target, objectiveIt );
        lastObjectivePosition = target;
    }
    if ( bHasSecondary &&
         ( !( LengthSq4( secondary - lastSecondaryObjectivePosition ) <
              kSameObjectiveDistanceSq ) ||
           secondaryIt.pRoad == nullptr ) ) {
        projectOnNearestRoad( secondary, secondaryIt );
        lastSecondaryObjectivePosition = secondary;
    }

    const ObjectWorldLocation &carLocation = pCar->getWorldLocation();
    const bool bAddSecondLeg = strcmp( pType, "start" ) == 0;
    int32_t arrow = kArrowNone;
    float distance = 0.0f;
    if ( carLocation.LocationType == ObjectWorldLocation::OWL_Road ) {
        bCarOnRoad = true;
        const float carFraction = ( carLocation.LocationRoad.FractionMin +
                                    carLocation.LocationRoad.FractionMax ) *
                                  0.5f;
        carRoadFraction = carFraction;

        bool bReroute = true;
        AiRouteElem *pHead = route.getHead();
        if ( bSameObjective && pHead != nullptr &&
             carLocation.LocationRoad.RoadID == pHead->To.getRoadID() ) {
            bReroute = false;
            AiRoadIt itTo;
            AiRoadIt itFrom;
            pHead->To.unpack( itTo );
            pHead->From.unpack( itFrom );
            const AiRoadMap::Road *pRoad =
                GetRoadMap().getRoad( pHead->To.getRoadID() );
            const float margin =
                kRouteMargin / static_cast<float>( pRoad->Distance );
            const AiRoadShape *pShape =
                const_cast<AiRoadMap::Road *>( pRoad )->getShape();
            const float toFraction = pShape->computeRoadFraction( itTo );
            const float fromFraction = pShape->computeRoadFraction( itFrom );
            if ( pHead->To.getWay() ) {
                bReroute = carFraction > toFraction ||
                           fromFraction - margin > carFraction;
            } else {
                bReroute = toFraction > carFraction ||
                           carFraction > fromFraction + margin;
            }
        }

        Eigen::Vector4f carOnRoad = Eigen::Vector4f::Zero();
        carLocation.computeWorldPosition( carOnRoad );
        const bool bInRange = LengthSq4( carOnRoad - target ) < arrivalRadiusSq;
        bObjectiveInRange = bInRange;
        if ( ( bReroute || bRouteExhausted ) && !bInRange ) {
            AiRoadIt start;
            AiRoadMap::Road *pRoad =
                GetRoadMap().getRoad( carLocation.LocationRoad.RoadID );
            pRoad->getShape()->computeIt( carFraction, start );
            start.setWay( carLocation.bWay != 0 );
            const float lateral = ( carLocation.LocationRoad.LateralMin +
                                    carLocation.LocationRoad.LateralMax ) *
                                  0.5f;
            recomputeRoute( start, ( 0.0f > lateral ) ? 1 : 2,
                            bNoExtraChallenge, bHasSecondary );
        }

        if ( !bInRange ) {
            distance = computeRouteDistance( carFraction, bAddSecondLeg );
            arrow = computeArrow( carLocation, carFraction );
            pHead = route.getHead();
            if ( pHead != nullptr &&
                 ( carLocation.bWay != 0 ) != pHead->To.getWay() ) {
                if ( wrongWayTimer > kWrongWayDelay ) {
                    wrongWayVoiceTimer += updateAccumulator;
                    if ( wrongWayVoiceTimer >= kWrongWayVoiceDelay &&
                         !bWrongWayAnnounced ) {
                        GetGPSVoice()->play( kVoiceWrongWay );
                        wrongWayVoiceTimer = 0.0f;
                        bWrongWayAnnounced = true;
                    }
                }
            } else {
                wrongWayTimer = 0.0f;
                bWrongWayAnnounced = false;
            }
        }
        carLocation.computeWorldPosition( carRoadPosition );
        carDisplayPosition = carRoadPosition;
    } else {
        if ( carLocation.LocationType == ObjectWorldLocation::OWL_Junction &&
             distanceToNextJunction < kTurnAnnounceDistance ) {
            arrow = nextJunctionArrow;
        }
        wrongWayTimer = 0.0f;
        bWrongWayAnnounced = false;
        wrongWayVoiceTimer = kWrongWayVoiceDelay;
        bCarOnRoad = false;

        const Eigen::Vector4f position = pCar->getWorldPosition();
        const bool bInRange = LengthSq4( position - target ) < arrivalRadiusSq;
        const bool bReproject = !(
            carRoadPosition.w() != -1.0f &&
            !( LengthSq4( carRoadPosition - position ) > kRejoinDistanceSq ) &&
            bSameObjective );
        if ( bReproject && !bInRange ) {
            AiRoadMap::Road *pRoad =
                GetRoadMap().findNearestRoad( position, true );
            if ( pRoad != nullptr && pRoad->getShape() != nullptr ) {
                AiRoadIt it;
                Matrix4x4f_t frame = Matrix4x4f_t::Identity();
                frame.row( 3 ) = Eigen::Vector4f( position.x(), position.y(),
                                                  position.z(), 1.0f )
                                     .transpose();
                ProjectOnRoadShape( frame, it, nullptr, false,
                                    pRoad->getShape() );
                carRoadFraction = pRoad->getShape()->computeRoadFraction( it );
                recomputeRoute( it, 0, bNoExtraChallenge, bHasSecondary );
                carRoadPosition = it.computeOffsetPosition( 0.0f, 0.0f );
            }
        }
        carDisplayPosition = position;

        if ( !bInRange ) {
            distance = computeRouteDistance( carRoadFraction, bAddSecondLeg );
            bool bAddRejoin = true;
            if ( carLocation.LocationType ==
                 ObjectWorldLocation::OWL_Junction ) {
                int32_t junctionID = -1;
                AiRouteElem *pHead = route.getHead();
                if ( pHead != nullptr ) {
                    const AiRoadMap::Road *pRoad =
                        GetRoadMap().getRoad( pHead->To.getRoadID() );
                    if ( pRoad != nullptr ) {
                        const AiRoadMap::Junction *pJunction =
                            pHead->To.getWay() ? pRoad->pJunctionEnd
                                               : pRoad->pJunctionBegin;
                        if ( pJunction != nullptr ) {
                            junctionID = static_cast<int32_t>( pJunction->ID );
                        }
                    }
                }
                if ( static_cast<int32_t>( carLocation.LocationOther.RoadID ) ==
                     junctionID ) {
                    bAddRejoin = false;
                }
            }
            if ( bAddRejoin ) {
                // FUN_00404b40
                distance += ( position - carRoadPosition ).norm();
            }
        }
        lastTurnCalloutDistance = FLT_MAX;
    }

    if ( pHudPlayer != nullptr ) {
        const char *pUnit = "km";
        float displayDistance = 0.0f;
        if ( gpPlayerData->isUsingImperialUnits() ) {
            displayDistance =
                static_cast<float>( std::ceil( static_cast<double>(
                                        distance * kKmToMiles * 0.01f ) ) *
                                    static_cast<double>( 0.1f ) );
            pUnit = "mi.";
        } else {
            displayDistance = static_cast<float>(
                std::ceil( static_cast<double>( distance * 0.01f ) ) *
                static_cast<double>( 0.1f ) );
        }
        pHudPlayer->setVariableValue( "obj_Distance", displayDistance );
        pHudPlayer->setVariableValue( "gps_Unit", pUnit );
    }
    announceDistance( distance );

    if ( pHudPlayer != nullptr ) {
        FlashClip trackClip;
        if ( pHudPlayer->getClipPlayer( "/HUD/minimap/gps_track",
                                        &trackClip ) ) {
            trackClip.setProperty( eFlashProperty::FP_Visible, true );
            trackClip.goToLabelAndPlay( "on", false );
        }
        FlashClip objectiveClip;
        if ( pHudPlayer->getClipPlayer( "HUD/minimap/gps_track/objectif_gps",
                                        &objectiveClip ) ) {
            objectiveClip.goToLabelAndPlay( pType, false );
        }
        FlashClip arrowClip;
        if ( pHudPlayer->getClipPlayer( "HUD/minimap/gps_track/arrow_gps",
                                        &arrowClip ) ) {
            arrowClip.setProperty( eFlashProperty::FP_Visible, true );
            arrowClip.goToLabelAndPlay( kArrowLabels[arrow], false );
        }
        FlashClip bigArrowClip;
        if ( !bBigArrowDisabled &&
             pHudPlayer->getClipPlayer( "HUD/minimap/gps_track/arrow_gps/_big",
                                        &bigArrowClip ) ) {
            bigArrowClip.goToLabelAndPlay( "empty", false );
        }
    }
    updateAccumulator = 0.0f;
    wrongWayTimer += deltaTime;
}

void MngGPS::recomputeRoute( const AiRoadIt &start,
                             const int32_t startSide,
                             const bool bIgnoreOneWay,
                             const bool bSecondLeg ) {
    // FUN_006c3440
    mutex.lock();
    route.setStart( start, startSide );
    route.setEnd( objectiveIt );
    route.setIgnoreOneWay( bIgnoreOneWay );
    route.setIgnoreRoadLocks( true );
    route.solve( true );
    bRouteIgnoreOneWay = bIgnoreOneWay;
    if ( bSecondLeg ) {
        if ( route.getTail() != nullptr ) {
            objectiveIt.setWay( route.getTail()->To.getWay() );
        }
        secondaryRoute.setStart( objectiveIt, 0 );
        secondaryRoute.setEnd( secondaryIt );
        secondaryRoute.setIgnoreOneWay( bIgnoreOneWay );
        secondaryRoute.setIgnoreRoadLocks( true );
        secondaryRoute.solve( true );
    } else {
        secondaryRoute.clear();
    }
    mutex.unlock();
    buildRoutePolyline();
}

void MngGPS::buildRoutePolyline() {
    // FUN_006c28b0
    numRoutePoints = 0;
    for ( const AiPath *pPath : { &route, &secondaryRoute } ) {
        const AiRouteElem *pElem = pPath->getHead();
        for ( int32_t n = pPath->getNumElems(); n > 0;
              n--, pElem = pElem->pNext ) {
            const uint32_t roadID = pElem->From.getRoadID();
            AiRoadMap::Road *pFromRoad = GetRoadMap().getRoad( roadID );
            AiRoadIt it;
            AiRoadIt to;
            pElem->From.unpack( it );
            pElem->To.unpack( to );

            bool bAdvanced = true;
            do {
                if ( numRoutePoints >= kMaxNumRoutePoints ) {
                    break;
                }
                const Eigen::Vector4f position =
                    it.computeOffsetPosition( 0.0f, 0.0f );
                RoutePoint &point = routePoints[numRoutePoints];
                point.X = position.x();
                point.Z = position.z();
                point.RoadID = roadID;
                const AiRoadShape *pShape =
                    ( pFromRoad != nullptr ) ? pFromRoad->getShape() : nullptr;
                point.T = ( pShape != nullptr )
                              ? pShape->computeRoadFraction( it )
                              : 0.0f;
                numRoutePoints++;

                bAdvanced = it.advanceByDistance( kRouteStep, true );
                if ( to.pRoad == it.pRoad && to.bWay == it.bWay ) {
                    bool bReached = false;
                    if ( to.IndEntity != it.IndEntity ) {
                        bReached = ( to.IndEntity < it.IndEntity ) == to.bWay;
                    } else if ( to.IndWay != it.IndWay ) {
                        bReached = ( to.IndWay < it.IndWay ) == to.bWay;
                    } else {
                        bReached = it.T > to.T;
                    }
                    if ( bReached ) {
                        break;
                    }
                }
            } while ( bAdvanced );
        }
    }
    bRouteExhausted = false;
}

float MngGPS::computeRouteDistance( const float carFraction,
                                    const bool bAddSecondLeg ) const {
    // FUN_006c26a0
    float distance = 0.0f;
    const AiRouteElem *pHead = route.getHead();
    const uint32_t numElems = static_cast<uint32_t>( route.getNumElems() );
    if ( numElems > 1 ) {
        AiRoadIt it;
        pHead->From.unpack( it );
        AiRoadMap::Road *pRoad = GetRoadMap().getRoad( pHead->To.getRoadID() );
        const float length = static_cast<float>( pRoad->Distance );
        const float fraction = pRoad->getShape()->computeRoadFraction( it );
        const bool bWay = pHead->To.getWay();
        const float covered = ( bWay ? 1.0f - fraction : fraction ) * length;
        const float remaining =
            ( bWay ? 1.0f - carFraction : carFraction ) * length;
        distance = remaining +
                   ( static_cast<float>( route.getTotalDistance() ) - covered );
    } else if ( numElems == 1 ) {
        AiRoadIt it;
        pHead->To.unpack( it );
        AiRoadMap::Road *pRoad = GetRoadMap().getRoad( pHead->To.getRoadID() );
        const float length = static_cast<float>( pRoad->Distance );
        const float fraction = pRoad->getShape()->computeRoadFraction( it );
        distance = std::fabs( carFraction - fraction ) * length;
    }
    if ( bAddSecondLeg ) {
        distance += static_cast<float>( secondaryRoute.getTotalDistance() );
    }
    return distance;
}

int32_t MngGPS::computeArrow( const ObjectWorldLocation &carLocation,
                              const float carFraction ) {
    // FUN_006c31e0
    const AiRouteElem *pHead = route.getHead();
    const int32_t numElems = route.getNumElems();
    if ( numElems == 0 || pHead == nullptr ) {
        return kArrowNone;
    }

    int32_t arrow = kArrowNone;
    const bool bWay = pHead->To.getWay();
    const float t = bWay ? 1.0f - carFraction : carFraction;
    const float distance =
        static_cast<float>(
            GetRoadMap().getRoad( pHead->To.getRoadID() )->Distance ) *
        t;
    if ( ( carLocation.bWay != 0 ) == bWay ) {
        if ( numElems > 1 && distance < kTurnAnnounceDistance ) {
            AiRoadIt leave;
            AiRoadIt arrive;
            pHead->pNext->From.unpack( leave );
            pHead->To.unpack( arrive );
            if ( arrive.pRoad != leave.pRoad && arrive.pRoad != nullptr ) {
                float turn = 0.0f;
                ComputeJunctionTurn( arrive, turn, arrow, leave );
                announceTurn( turn, distance );
            }

            nextJunctionArrow = kArrowNone;
            distanceToNextJunction = FLT_MAX;
            if ( numElems <= 2 ) {
                return arrow;
            }
            const AiRouteElem *pNext = pHead->pNext;
            pNext->To.unpack( arrive );
            pNext->pNext->From.unpack( leave );
            distanceToNextJunction = static_cast<float>(
                GetRoadMap().getRoad( pNext->From.getRoadID() )->Distance );
            float unusedTurn = 0.0f;
            ComputeJunctionTurn( arrive, unusedTurn, nextJunctionArrow, leave );
            return arrow;
        }
        arrow = kArrowNone;
    } else {
        arrow = ( wrongWayTimer > kWrongWayDelay ) ? 4 : kArrowNone;
    }
    lastTurnCalloutDistance = distance;
    return arrow;
}

void MngGPS::announceTurn( const float turnAngle, const float distance ) {
    // FUN_006c1d70
    int32_t voice = -1;
    if ( lastTurnCalloutDistance > kTurnAnnounceNear &&
         kTurnAnnounceNear >= distance &&
         std::fabs( distance - kTurnAnnounceNear ) < kTurnAnnounceNear ) {
        if ( 119.0f > turnAngle ) {
            voice = 2;
        } else if ( turnAngle > 241.0f ) {
            voice = 4;
        }
    } else if ( lastTurnCalloutDistance > kTurnAnnounceDistance &&
                kTurnAnnounceDistance >= distance &&
                std::fabs( distance - kTurnAnnounceDistance ) <
                    kTurnAnnounceNear ) {
        if ( 119.0f > turnAngle ) {
            voice = 1;
        } else if ( turnAngle > 241.0f ) {
            voice = 3;
        }
    }
    if ( voice != -1 ) {
        GetGPSVoice()->play( voice );
    }
    lastTurnCalloutDistance = distance;
}

void MngGPS::announceDistance( const float distance ) {
    // FUN_006c1e40
    static constexpr float kThresholds[5] = { 3000.0f, 2000.0f, 1500.0f,
                                              1000.0f, 500.0f };
    if ( bDistanceCalloutsEnabled ) {
        float current = distance;
        float previous = lastCalloutDistance;
        int32_t firstVoice = 9;
        if ( gpPlayerData->isUsingImperialUnits() ) {
            current *= kKmToMiles;
            previous *= kKmToMiles;
            firstVoice = 14;
        }
        for ( int32_t i = 0; i < 5; i++ ) {
            const float threshold = kThresholds[i];
            if ( previous > threshold && threshold >= current &&
                 std::fabs( current - threshold ) < kTurnAnnounceNear ) {
                GetGPSVoice()->play( firstVoice - i );
                break;
            }
        }
    }
    lastCalloutDistance = distance;
}

void MngGPS::announceArrival() {
    // FUN_0042c3f0
    static constexpr int32_t kVoiceArrived = 15;
    if ( bVoiceEnabled ) {
        GetGPSVoice()->play( kVoiceArrived );
    }
}

void MngGPS::drawRoute( Matrix4x4f_t &worldToMap, DrawList &drawList ) {
    // FUN_006c20b0
    if ( numRoutePoints == 0 ) {
        return;
    }
    if ( bObjectiveInRange && secondaryRoute.getNumElems() == 0 ) {
        return;
    }

    drawList.setActiveTransformMatrix( worldToMap );
    drawList.setActiveMaterial( nullptr );
    drawList.setDiffuse( 0, ColorRGBA( kRouteColor ) );
    if ( !bCarOnRoad && drawList.beginPrimitive( PT_Line, 2 ) ) {
        drawList.pushVertex( carDisplayPosition.x(), carDisplayPosition.z(),
                             0.0f );
        drawList.pushVertex( carRoadPosition.x(), carRoadPosition.z(), 0.0f );
        drawList.commitPrimitive();
    }

    uint32_t i = 0;
    const AiRouteElem *pHead = route.getHead();
    if ( pHead != nullptr ) {
        const uint32_t roadID = pHead->To.getRoadID();
        const bool bWay = pHead->From.getWay();
        for ( ; i < numRoutePoints; i++ ) {
            if ( routePoints[i].RoadID != roadID ) {
                break;
            }
            if ( bWay ) {
                if ( !( carRoadFraction > routePoints[i].T ) ) {
                    break;
                }
            } else if ( !( routePoints[i].T > carRoadFraction ) ) {
                break;
            }
        }
        if ( i > kRouteExhaustedPoints ) {
            bRouteExhausted = true;
        }
    }

    i++;
    if ( i < numRoutePoints &&
         drawList.beginPrimitive( PT_LineStrip, numRoutePoints - i + 1 ) ) {
        drawList.pushVertex( routePoints[i - 1].X, routePoints[i - 1].Z, 0.0f );
        for ( ; i < numRoutePoints; i++ ) {
            drawList.pushVertex( routePoints[i].X, routePoints[i].Z, 0.0f );
        }
        drawList.commitPrimitive();
    }
}

void MngGPS::onPlayerLeft( const int32_t numPlayers,
                           const int32_t playerIndex ) {
    // FUN_006c23c0
    FlashPlayer *pHudPlayer = gpMngHud->getHudPlayer();
    if ( pHudPlayer == nullptr ) {
        return;
    }
    cachedSessionPlayerCount = numPlayers;
    pHudPlayer->setVariableValue( "/:nb_FRplayers", numPlayers );
    FlashClip clip;
    if ( pHudPlayer->getClipPlayer( "/HUD/minimap/gps_track/nb_players/in_out",
                                    &clip ) ) {
        clip.goToLabelAndPlay( "out", false );
    }
    if ( playerIndex != -1 ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    }
}

void MngGPS::onPlayerJoined( const int32_t numPlayers,
                             const int32_t playerIndex ) {
    // FUN_006c24b0
    FlashPlayer *pHudPlayer = gpMngHud->getHudPlayer();
    if ( pHudPlayer == nullptr ) {
        return;
    }
    cachedSessionPlayerCount = numPlayers;
    pHudPlayer->setVariableValue( "/:nb_FRplayers", numPlayers );
    FlashClip clip;
    if ( pHudPlayer->getClipPlayer( "/HUD/minimap/gps_track/nb_players/in_out",
                                    &clip ) ) {
        clip.goToLabelAndPlay( "in", false );
    }
    if ( playerIndex != -1 ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    }
}
