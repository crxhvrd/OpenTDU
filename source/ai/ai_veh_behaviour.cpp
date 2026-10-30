#include "shared.h"
#include "ai_veh_behaviour.h"

#include "ai/ai_config.h"
#include "ai/ai_path.h"
#include "ai/ai_pool.h"
#include "ai/ai_road_shape.h"
#include "ai/ai_veh_nav_traffic.h"
#include "ai/ai_vehicle.h"
#include "ai/ai_bot_manager.h"
#include "ai/gs_ai.h"
#include "core/random/bcpl.h"
#include "game/gm_base.h"
#include "game/mission/gm_mission.h"
#include "game/scenaric/game_intro_tutorial.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "gs_timer.h"
#include "physics/collision_event.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_data/achievements_data.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_road_discovery.h"

static constexpr float kMaxBlockedTime = 6.0f; // DAT_00f41840

AiVehBhvPassive::AiVehBhvPassive() : pVehicle( nullptr ) {}

void AiVehBhvPassive::attach( AiVehicle *pOwner ) {
    // FUN_004adc30
    pVehicle = pOwner;
}

void AiVehBhvPassive::onRaceMessage( const AiRaceMessage &message ) {
    // FUN_009d74d0
    if ( message.Type != 0 ) {
        return;
    }

    AiVehNavOpponent *pNav = ( pVehicle->getNavigationType() == 1 )
                                 ? &pVehicle->getOpponentNavigation()
                                 : nullptr;
    switch ( message.ID ) {
    case ARM_Prepare:
        if ( pNav != nullptr ) {
            pNav->requestState( AiVehNavOpponent::NS_TrainingStart );
        }
        pVehicle->applyBotPersonality( 2, 2, 2 );
        break;
    case ARM_Go:
        if ( pNav != nullptr ) {
            pNav->requestState( AiVehNavOpponent::NS_TrainingFollow );
        }
        break;
    case ARM_Restart:
        pVehicle->setController( 2 );
        pVehicle->reset( true, true );
        pVehicle->setRemovable( false );
        if ( pVehicle->getNavigationType() == 1 ) {
            pVehicle->getOpponentNavigation().requestState(
                AiVehNavOpponent::NS_RaceOver );
        }
        break;
    default:
        break;
    }
}

AiVehBhvTraffic::AiVehBhvTraffic() : pVehicle( nullptr ) {}

void AiVehBhvTraffic::attach( AiVehicle *pOwner ) {
    // FUN_004adc30
    pVehicle = pOwner;
}

void AiVehBhvTraffic::reset( const bool bFull ) {
    // FUN_00614df0
    (void)bFull;
}

void AiVehBhvTraffic::tick( const float deltaTime, const float totalTime ) {
    // FUN_009cb960
    (void)totalTime;
    if ( deltaTime <= 0.0f ) {
        return;
    }
    if ( pVehicle->getInfos().BlockedTime > kMaxBlockedTime ) {
        pVehicle->onNavigationLost();
    }
}

void AiVehBhvTraffic::release() {
    // FUN_0063f2b0
}

void AiVehBhvTraffic::onCollision( CarState *pCarA,
                                   CarState *pCarB,
                                   const CollisionEvent &event ) {
    // FUN_009cb990
    (void)pCarA;
    (void)pCarB;
    if ( event.Energy > gAIConfig.Unknown0x1BC ) {
        pVehicle->onNavigationLost();
    }
}

static constexpr float kPersonalityResampleDelay = 3.0f; // DAT_00f416b4

AiVehBhvOpponent::AiVehBhvOpponent()
    : pVehicle( nullptr ),
      opponentIndex( -1 ),
      field_0xc( 0.0f ),
      raceStartTime( 0.0f ) {}

void AiVehBhvOpponent::attach( AiVehicle *pOwner ) {
    // FUN_004adc30
    pVehicle = pOwner;
}

void AiVehBhvOpponent::reset( const bool bFull ) {
    // FUN_00614df0
}

void AiVehBhvOpponent::tick( const float deltaTime, const float totalTime ) {
    // FUN_009cd850
    if ( gpTimer->GameTotalTime - raceStartTime > kPersonalityResampleDelay ) {
        pVehicle->getDPC().randomizeFactors( gpAI->getRandom() );
    }
}

void AiVehBhvOpponent::release() {
    // FUN_0063f2b0
}

void AiVehBhvOpponent::onCollision( CarState *pCarA,
                                    CarState *pCarB,
                                    const CollisionEvent &event ) {
    // FUN_0049cd30
}

void AiVehBhvOpponent::onRaceMessage( const AiRaceMessage &message ) {
    // FUN_009cd880
    if ( message.Type != 0 ) {
        return;
    }

    CarState *pCarState = pVehicle->getCarState();
    AiVehLocomotion *pLoco = pVehicle->getLocomotion();
    AiVehNavOpponent *pNav = ( pVehicle->getNavigationType() == 1 )
                                 ? &pVehicle->getOpponentNavigation()
                                 : nullptr;
    switch ( message.ID ) {
    case ARM_Prepare:
        prepareRace();
        break;
    case ARM_CountdownLast:
        if ( pNav != nullptr ) {
            pNav->setGridThrottle( 1.0f, 0.0f );
        }
        break;
    case ARM_Go:
        pCarState->getPhysicsVehicle()->setDrivingMode(
            PhysicsVehicle::DM_Player );
        if ( pLoco != nullptr ) {
            pLoco->setDrivenByNavigation( true );
        }
        if ( pNav != nullptr ) {
            pNav->requestState( AiVehNavOpponent::NS_Race );
        }
        break;
    case ARM_Restart: {
        VehicleBase *pDisplay = pCarState->getVehicleInstance();
        pDisplay->setShouldDraw( true );
        pDisplay->setAlpha( 1.0f );
        pVehicle->setController( 2 );
        const int32_t botID = pVehicle->getDPC().BotID;
        pVehicle->reset( true, true );
        pVehicle->getDPC().BotID = botID;
        pVehicle->setRemovable( false );
        pLoco = pVehicle->getLocomotion();
        if ( pLoco != nullptr ) {
            pLoco->setDrivenByNavigation( false );
            pLoco->setManualInputs( 0.0f, 1.0f, 1.0f, 0.0f, 0.0f );
        }
        OTDU_UNIMPLEMENTED_ONCE; // TODO: physics and engine restart fields
        if ( pNav != nullptr ) {
            pNav->requestState( AiVehNavOpponent::NS_RaceOver );
        }
        break;
    }
    case ARM_Stop:
        if ( pLoco != nullptr ) {
            pLoco->setDrivenByNavigation( true );
        }
        if ( pNav != nullptr ) {
            pNav->requestState( AiVehNavOpponent::NS_Halt );
        }
        pVehicle->getInfos().bHandbrake = true;
        pVehicle->getInfos().Speed = 0.0f;
        break;
    default:
        break;
    }
}

void AiVehBhvOpponent::prepareRace() {
    // FUN_009cd880 (message 0)
    const bool bMissionRace =
        gpActiveGameMode != nullptr &&
        gpActiveGameMode->getCurrentGameMode() == eGameMode::GM_Mission;
    if ( bMissionRace ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: GMMission race flag (FUN_00801030)
    }
    const bool bMissionFlag = false;

    raceStartTime = gpTimer->GameTotalTime;
    CarState *pCarState = pVehicle->getCarState();
    pCarState->getVehicleInstance()->setShouldDraw( true );
    if ( pVehicle->getNavigationType() == 1 ) {
        pVehicle->getOpponentNavigation().requestState(
            AiVehNavOpponent::NS_Grid );
    }
    pCarState->getPhysicsVehicle()->setDrivingMode( PhysicsVehicle::DM_Parked );
    if ( pVehicle->getLocomotion() != nullptr ) {
        pVehicle->getLocomotion()->setDrivenByNavigation( false );
    }

    AiVehIndDPC &dpc = pVehicle->getDPC();
    if ( dpc.BotID != -1 && !gAIConfig.bBotBehaviorOverride ) {
        pVehicle->assignBotPersonality( dpc.BotID );
        pVehicle->applyBotPersonality( 2, bMissionFlag ? 0 : dpc.Personality,
                                       dpc.Difficulty );
    } else {
        int32_t difficulty = 2;
        if ( gAIConfig.BotsWantedDifficulty == 0 ) {
            difficulty = 0;
        } else if ( gAIConfig.BotsWantedDifficulty == 1 ) {
            difficulty = 1;
        }

        const int32_t personality = ( gAIConfig.BotsWantedBehavior >= 0 &&
                                      gAIConfig.BotsWantedBehavior <= 3 )
                                        ? gAIConfig.BotsWantedBehavior
                                        : 2;

        dpc.BotID = -1;
        dpc.BotSectorIndex = -1;
        strncpy( dpc.pName, "Pas dans le DVD", AiVehIndDPC::kNameLength );
        dpc.Personality = personality;
        dpc.Difficulty = difficulty;
        pVehicle->applyBotPersonality( 2, personality, difficulty );
    }

    if ( pCarState == gpScenaric->getLocalPlayer().getCarState() ) {
        pVehicle->applyBotPersonality( 2, bMissionFlag ? 0 : 2, 2 );
    }

    if ( !gpScenaric->isInstantChallenge() ) { // DAT_01214750
        applyCatchUp();
    }
}

void AiVehBhvOpponent::applyCatchUp() {
    // FUN_009cd690
    if ( gpActiveGameMode == nullptr ||
         gpActiveGameMode->getCurrentGameMode() != eGameMode::GM_Mission ) {
        return;
    }

    static constexpr double kCatchUpExponent = 1.3;     // DAT_00f41ab8
    static constexpr float kRookieCatchUpScale = 0.75f; // DAT_00f416a4
    static constexpr float kRookieCatchUpMin = 0.5f;    // DAT_00f41674
    static constexpr float kNoviceCatchUpScale = 0.88f; // DAT_00f41ab4
    static constexpr float kNoviceCatchUpMin = 0.75f;   // DAT_00f416a4
    static constexpr float kInvalidRating = -1.0f;      // DAT_00f41664

    const MissionInfo &missionInfo =
        static_cast<GMMission *>( gpActiveGameMode )->getMissionInfo();
    const float playerRating = missionInfo.Participants[0].Rating;
    const float opponentRating =
        ( opponentIndex >= 0 && opponentIndex < missionInfo.NumEntries &&
          opponentIndex < MissionInfo::kMaxNumParticipants )
            ? missionInfo.Participants[opponentIndex].Rating
            : kInvalidRating;

    AiVehIndDPC &dpc = pVehicle->getDPC();
    const int32_t playerLevel =
        gpPlayerData->getLoadedSave()->getAchievements()->getPlayerLevel();
    if ( playerLevel == 0 ) {
        if ( dpc.Difficulty == 0 ) {
            CarState *pCarState = pVehicle->getCarState();
            if ( pCarState != nullptr &&
                 pCarState->getPhysicsVehicle() != nullptr ) {
                pCarState->getPhysicsVehicle()->setElectronicMode( 0 );
            }
            if ( dpc.Personality == 2 ) {
                dpc.NumOutsideWanted = 1;
            } else if ( dpc.Personality == 1 ) {
                dpc.NumOutsideWanted = 2;
            }
        }
        const float catchUp =
            static_cast<float>(
                pow( playerRating / opponentRating, kCatchUpExponent ) ) *
            kRookieCatchUpScale;
        dpc.DPCPattern.MaxAccel = ( catchUp < kRookieCatchUpMin )
                                      ? kRookieCatchUpMin
                                      : std::min( catchUp, 1.0f );
    } else if ( playerLevel == 1 ) {
        if ( dpc.Difficulty == 0 ) {
            if ( dpc.Personality == 2 ) {
                dpc.NumOutsideWanted = 0;
            } else if ( dpc.Personality == 1 ) {
                dpc.NumOutsideWanted = 1;
            }
        }
        if ( opponentRating > playerRating ) {
            const float catchUp =
                static_cast<float>(
                    pow( playerRating / opponentRating, kCatchUpExponent ) ) *
                kNoviceCatchUpScale;
            dpc.DPCPattern.MaxAccel = ( catchUp < kNoviceCatchUpMin )
                                          ? kNoviceCatchUpMin
                                          : std::min( catchUp, 1.0f );
        }
    }
}

static constexpr float kBotFarDistance = 100000.0f; // DAT_00f418e0
static constexpr float kBotMaxDistance = 4000.0f;   // DAT_00f41cbc
static constexpr float kBotTauntDistance = 100.0f;  // DAT_00f41708
static constexpr float kBotTauntInterval = 10.0f;   // DAT_00f4166c
static constexpr float kBotChaseCooldown = 20.0f;   // DAT_00f03ee0
static constexpr float kBotNoChaseEndTime = -2.4f;  // DAT_00f41ac0
static constexpr int32_t kBotNoSpawnZone = 5;

static void QueueHeadlightFlash( AiVehSignals &signals ) {
    // FUN_00920a90
    if ( signals.bHeadlightsActivated ) {
        return;
    }

    float *pQueue = signals.HeadlightCommandQueue;
    pQueue[0] = 0.25f;
    pQueue[1] = 0.5f;
    pQueue[2] = 1.0f;
    pQueue[3] = 0.0f;
    signals.HeadlightCommandIndex = 0;
    signals.bHeadlightsActivated = true;
    signals.bHeadlightOn = !signals.bHeadlightActivated;
    signals.HeadlightLastTime = gpTimer->GameTotalTime;
}

AiVehBhvBot::AiVehBhvBot()
    : AiVehBehaviour(),
      AiFsm(),
      pVehicle( nullptr ),
      field_0x2c( 0.0f ),
      field_0x30( true ),
      field_0x31( false ),
      distanceToPlayer( kBotFarDistance ),
      field_0x38( 0.0f ),
      field_0x40( Eigen::Vector4f::Zero() ),
      lastTauntTime( 0.0f ),
      chaseEndTime( kBotNoChaseEndTime ) {}

void AiVehBhvBot::attach( AiVehicle *pOwner ) {
    // FUN_0092bc60
    pVehicle = pOwner;
    initialize();
}

void AiVehBhvBot::reset( const bool bFull ) {
    // FUN_00614df0
}

void AiVehBhvBot::tick( const float deltaTime, const float totalTime ) {
    // FUN_0092bc50
    process( FE_Update );
}

void AiVehBhvBot::release() {
    // FUN_0063f2b0
}

void AiVehBhvBot::onCollision( CarState *pCarA,
                               CarState *pCarB,
                               const CollisionEvent &event ) {
    // FUN_0049cd30
}

void AiVehBhvBot::initialize() {
    // FUN_0092bbc0
    field_0x2c = 0.0f;
    field_0x38 = 0.0f;
    distanceToPlayer = kBotFarDistance;
    field_0x31 = false;
    field_0x30 = true;
    field_0x40 = Eigen::Vector4f::Zero();
    lastTauntTime = 0.0f;
    chaseEndTime = kBotNoChaseEndTime;
    restart( BS_Start );
}

bool AiVehBhvBot::handle( const int32_t event, const int32_t state ) {
    // FUN_0092c150
    AiVehIndDPC &dpc = pVehicle->getDPC();
    switch ( state ) {
    case BS_Idle:
        return event <= FE_Exit;

    case BS_Start:
        if ( event == FE_Update && pVehicle->getNavigationType() == 1 &&
             pVehicle->getNavigation() != nullptr ) {
            AiVehNavOpponent &nav = pVehicle->getOpponentNavigation();
            const int32_t navState = nav.getState();
            if ( navState != AiVehNavOpponent::NS_LineEnded &&
                 navState != AiVehNavOpponent::NS_BotCruise &&
                 navState != AiVehNavOpponent::NS_BotFollow ) {
                nav.requestState( AiVehNavOpponent::NS_LineEnded );
                goTo( BS_Cruise );
            }
        }
        return event <= FE_Exit;

    case BS_Cruise:
        if ( event == FE_Enter ) {
            if ( chaseEndTime == kBotNoChaseEndTime ) {
                pVehicle->applyBotPersonality( 0, dpc.Personality,
                                               dpc.Difficulty );
            }
            return true;
        }
        if ( event == FE_Update ) {
            if ( chaseEndTime != kBotNoChaseEndTime &&
                 gpTimer->GameTotalTime - chaseEndTime > kBotChaseCooldown ) {
                pVehicle->applyBotPersonality( 0, dpc.Personality,
                                               dpc.Difficulty );
                chaseEndTime = kBotNoChaseEndTime;
            }
            if ( isTooFar() ) {
                removeTooFar();
            } else if ( shouldStartChase() ) {
                pVehicle->setController( 1 );
                pVehicle->getChaseController().setTarget(
                    gpScenaric->getLocalPlayer().getCarState() );
                goTo( BS_Chase );
                if ( pVehicle->getNavigationType() == 1 ) {
                    pVehicle->getOpponentNavigation().requestState(
                        AiVehNavOpponent::NS_LineEnded );
                }
            }
            if ( dpc.DPCPattern.bTaunt ) {
                taunt();
            }
            return true;
        }
        if ( event == FE_Exit ) {
            chaseEndTime = kBotNoChaseEndTime;
            return true;
        }
        return false;

    case BS_Chase:
        if ( event == FE_Enter ) {
            pVehicle->applyBotPersonality( 1, dpc.Personality, dpc.Difficulty );
            return true;
        }
        if ( event == FE_Update ) {
            if ( isTooFar() ) {
                removeTooFar();
            } else if ( shouldStopChase() ) {
                pVehicle->setController( 7 );
                goTo( BS_Cruise );
                if ( pVehicle->getNavigationType() == 1 ) {
                    pVehicle->getOpponentNavigation().requestState(
                        AiVehNavOpponent::NS_LineEnded );
                }
            }
            if ( dpc.DPCPattern.bTaunt ) {
                taunt();
            }
            return true;
        }
        if ( event == FE_Exit ) {
            chaseEndTime = gpTimer->GameTotalTime;
            return true;
        }
        return false;

    default:
        return false;
    }
}

bool AiVehBhvBot::isTooFar() {
    // FUN_0092bdb0
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr || !gpAI->getBotManager().isActive() ) {
        distanceToPlayer = kBotFarDistance;
        return true;
    }
    if ( gpScenaric->getIntroTutorial().isActive() ) {
        return true;
    }

    const Eigen::Vector4f delta = pVehicle->getCarState()->getWorldPosition() -
                                  pPlayerCar->getWorldPosition();
    distanceToPlayer = delta.norm();
    return distanceToPlayer > kBotMaxDistance;
}

bool AiVehBhvBot::shouldStartChase() const {
    // FUN_0092be80
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr ) {
        return false;
    }
    const Eigen::Vector4f playerPosition = pPlayerCar->getWorldPosition();
    if ( gpAI->getBotManager().computeZone( &playerPosition ) ==
         kBotNoSpawnZone ) {
        return false;
    }

    CarState *pCarState = pVehicle->getCarState();
    const AiVehIndDPCPattern &pattern = pVehicle->getDPC().DPCPattern;
    const Eigen::Vector4f delta =
        playerPosition - pCarState->getWorldPosition();
    if ( delta.squaredNorm() >= pattern.ChaseDistMin * pattern.ChaseDistMin ) {
        return false;
    }

    const ObjectWorldLocation &playerLoc = pPlayerCar->getWorldLocation();
    if ( playerLoc.LocationType != ObjectWorldLocation::OWL_Road ||
         !IsRoadAvailable( playerLoc.LocationRoad.RoadID ) ) {
        return false;
    }
    const ObjectWorldLocation &botLoc = pCarState->getWorldLocation();
    if ( botLoc.LocationType != ObjectWorldLocation::OWL_Road ) {
        return false;
    }
    if ( botLoc.LocationRoad.RoadID == playerLoc.LocationRoad.RoadID ) {
        return botLoc.isObjectAhead( playerLoc ) &&
               !playerLoc.isObjectAhead( botLoc );
    }

    AiRoadMap &roadMap = gpAI->getRoadMap();
    AiRoadMap::Road *pBotRoad = roadMap.getRoad( botLoc.LocationRoad.RoadID );
    if ( pBotRoad == nullptr ) {
        return false;
    }
    AiRoadIt botIt;
    pBotRoad->getShape()->computeIt(
        ( botLoc.LocationRoad.FractionMin + botLoc.LocationRoad.FractionMax ) *
            0.5f,
        botIt );
    botIt.setWay( botLoc.bWay );
    AiRoadIt playerIt;
    playerIt.setRoadEntry( roadMap.getRoad( playerLoc.LocationRoad.RoadID ),
                           playerLoc.bWay );

    AiPath path;
    path.setStart( botIt, 0 );
    path.setEnd( playerIt );
    path.solve( true );
    const bool bChase =
        path.getStatus() == AiPathSolver::S_Success &&
        pattern.ChaseDistMax > static_cast<float>( path.getTotalDistance() ) &&
        path.getHead()->From.getWay() == ( botLoc.bWay != 0 ) &&
        path.getTail()->To.getWay() == ( playerLoc.bWay != 0 );
    path.clear();
    return bChase;
}

bool AiVehBhvBot::shouldStopChase() const {
    // FUN_0092bc70
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr ) {
        return true;
    }

    CarState *pCarState = pVehicle->getCarState();
    const float chaseDistMax = pVehicle->getDPC().DPCPattern.ChaseDistMax;
    const Eigen::Vector4f delta =
        pPlayerCar->getWorldPosition() - pCarState->getWorldPosition();
    if ( delta.squaredNorm() >= chaseDistMax * chaseDistMax ) {
        return true;
    }

    const ObjectWorldLocation &botLoc = pCarState->getWorldLocation();
    if ( botLoc.LocationType != ObjectWorldLocation::OWL_Road ) {
        return false;
    }
    const ObjectWorldLocation &playerLoc = pPlayerCar->getWorldLocation();
    if ( playerLoc.LocationType != ObjectWorldLocation::OWL_Road ) {
        return playerLoc.LocationType == ObjectWorldLocation::OWL_OffRoad;
    }
    if ( !IsRoadAvailable( playerLoc.LocationRoad.RoadID ) ) {
        return true;
    }
    if ( playerLoc.LocationRoad.RoadID != botLoc.LocationRoad.RoadID ) {
        return false;
    }
    if ( playerLoc.bWay != botLoc.bWay ) {
        return true;
    }
    return playerLoc.isObjectAhead( botLoc );
}

void AiVehBhvBot::taunt() {
    // FUN_0092bb00
    if ( gpTimer->GameTotalTime - lastTauntTime <= kBotTauntInterval ||
         distanceToPlayer >= kBotTauntDistance ) {
        return;
    }

    RandomGen &random = pVehicle->getRandom();
    AiVehSignals &signals = pVehicle->getSignals();
    if ( random.getRandomUint32() < 0x7fffffffu ) {
        QueueHorn( signals, RandomUnit( random ) );
    } else {
        RandomUnit( random );
        QueueHeadlightFlash( signals );
    }
    lastTauntTime = gpTimer->GameTotalTime;
}

void AiVehBhvBot::removeTooFar() {
    // FUN_0092c150 (states 2 / 4 update)
    pVehicle->setRemovable( true );
    gpAI->getAIPool()->removeVehicle( pVehicle, "bot too far", true );
}
