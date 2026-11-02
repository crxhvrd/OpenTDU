#include "shared.h"
#include "ai_veh_behaviour.h"

#include "ai/ai_config.h"
#include "ai/ai_cop_manager.h"
#include "ai/ai_path.h"
#include "ai/ai_pool.h"
#include "ai/ai_road_shape.h"
#include "ai/ai_veh_nav_traffic.h"
#include "ai/ai_vehicle.h"
#include "ai/ai_bot_manager.h"
#include "ai/gs_ai.h"
#include "core/random/bcpl.h"
#include "debug/ed_debug_text.h"
#include "game/freeride/gm_freeride.h"
#include "game/game_mechanism.h"
#include "game/gm_base.h"
#include "game/gm_world.h"
#include "game/mission/game_mech_penalty.h"
#include "game/mission/gm_mission.h"
#include "game/scenaric/game_intro_tutorial.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "gs_timer.h"
#include "physics/collision_event.h"
#include "physics/gs_physics.h"
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
        if ( PhysicsVehicle *pPhysics = pCarState->getPhysicsVehicle();
             pPhysics != nullptr ) {
            {
                PhysicsLock lock;
                pPhysics->setVelocity( Eigen::Vector3f::Zero(), false, -1 );
            }
            pPhysics->setEngineState( eEngineState::ES_Off );
        }
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

static bool IsCollisionPenaltyOnlyRace( const GMMission &mission ) {
    // FUN_00801030
    const GameMechPenalty *pPenalty = static_cast<const GameMechPenalty *>(
        mission.getGameMech()->getComponent( GameMechanism::CS_Penalty ) );
    return pPenalty != nullptr &&
           pPenalty->getPenaltyFlags() == GameMechPenalty::kPenaltyCollisions;
}

void AiVehBhvOpponent::prepareRace() {
    // FUN_009cd880 (message 0)
    bool bMissionFlag = false;
    if ( gpActiveGameMode != nullptr &&
         gpActiveGameMode->getCurrentGameMode() == eGameMode::GM_Mission ) {
        bMissionFlag = IsCollisionPenaltyOnlyRace(
            *static_cast<GMMission *>( gpActiveGameMode ) );
    }

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

static constexpr float kPoliceNoDistance = -2000.0f;         // DAT_00f421d8
static constexpr float kPoliceNoTime = -1.0f;                // FLOAT_00f41664
static constexpr float kPoliceRandomRange = 0.9f;            // DAT_00f416a0
static constexpr float kPoliceMinAltitude = -50.0f;          // DAT_00f41cb8
static constexpr float kPoliceScanInterval = 0.5f;           // FLOAT_00f41674
static constexpr float kPoliceSearchReachRatio = 0.5f;       // FLOAT_00f41674
static constexpr float kPoliceLoseTargetRatio = 1.2f;        // DAT_00f41dc4
static constexpr float kPoliceMaxViewAngle = 1.5707964f;     // DAT_00f418a4
static constexpr float kPoliceViewAngleFalloff = 0.7853982f; // DAT_00f41cb0
static constexpr float kPoliceBlinkOnTime = 0.05f;           // DAT_00f41778
static constexpr float kPoliceBlinkOffTime = 0.25f;          // DAT_00f416dc

static bool gbDisableCopViewingAngle = false; // DAT_010e73d7

static bool IsPoliceAllowedByGameMech() {
    if ( gpActiveGM == nullptr || gpActiveGM->getGameMech() == nullptr ) {
        return true;
    }
    return gpActiveGM->getGameMech()->getCurrentState() ==
           GameMechanism::MS_Running;
}

AiVehBhvPolice::AiVehBhvPolice()
    : AiVehBehaviour(),
      AiFsm(),
      pVehicle( nullptr ),
      randomFactor( 0.0f ),
      distanceToTarget( kPoliceNoTime ),
      pTarget( nullptr ),
      pSearchedCar( nullptr ),
      field_0x3c( 0 ),
      captureStartTime( kPoliceNoTime ),
      lastTargetScanTime( 0.0f ) {}

void AiVehBhvPolice::attach( AiVehicle *pOwner ) {
    // FUN_009cbc00
    pVehicle = pOwner;
    randomFactor = RandomUnit( pOwner->getRandom() ) * kPoliceRandomRange;
    initialize();
}

void AiVehBhvPolice::reset( const bool bFull ) {
    // FUN_00614df0
}

void AiVehBhvPolice::tick( const float deltaTime, const float totalTime ) {
    // FUN_009cbbf0
    process( FE_Update );
}

void AiVehBhvPolice::release() {
    // FUN_0063f2b0
}

void AiVehBhvPolice::debugText() {
    // FUN_009cba90
    static constexpr const char *kStateNames[] = {
        "Lost",  "Init",       "Patrol", "Search",
        "Chase", "Aggressive", "Arrest", "Police Dam" };

    gEdDebugText.drawText( "Behavior : Police" );
    const int32_t state = getCurrentState();
    if ( state >= PS_Lost && state <= PS_PoliceDam ) {
        gEdDebugText.drawText( "Behavior State : %s", kStateNames[state] );
    }
}

void AiVehBhvPolice::onCollision( CarState *pCarA,
                                  CarState *pCarB,
                                  const CollisionEvent &event ) {
    // FUN_0049cd30
}

void AiVehBhvPolice::initialize() {
    // FUN_009cbb90
    distanceToTarget = kPoliceNoTime;
    captureStartTime = kPoliceNoTime;
    field_0x3c = 0;
    lastTargetScanTime = gpTimer->GameTotalTime;
    pTarget = nullptr;
    pSearchedCar = nullptr;
    restart( PS_Init );
}

bool AiVehBhvPolice::handle( const int32_t event, const int32_t state ) {
    // FUN_009ccc80
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    AiVehIndDPC &dpc = pVehicle->getDPC();
    switch ( state ) {
    case PS_Init:
        if ( event == FE_Update ) {
            checkRemoval();
            return true;
        }
        return event <= FE_Exit;

    case PS_Lost:
    case PS_Arrest:
        if ( event == FE_Update ) {
            if ( pPlayerCar != nullptr ) {
                checkRemoval();
            }
            return true;
        }
        return event <= FE_Exit;

    case PS_Patrol:
        if ( event == FE_Enter ) {
            pVehicle->setRemovable( false );
            if ( pVehicle->getControllerType() != 0 ||
                 pVehicle->getController() == nullptr ) {
                setRouteController();
            }
            if ( pVehicle->getNavigationType() != 0 ||
                 pVehicle->getNavigation() == nullptr ) {
                setTrafficNavigation();
            }
            if ( pVehicle->getLocomotionType() != 0 ||
                 pVehicle->getLocomotion() == nullptr ) {
                setTrafficLocomotion();
            }
            distanceToTarget = kPoliceNoDistance;
            captureStartTime = kPoliceNoTime;
            return true;
        }
        if ( event == FE_Update ) {
            if ( pPlayerCar == nullptr ) {
                return true;
            }
            checkRemoval();
            if ( pVehicle->getControllerType() != 0 ||
                 pVehicle->getController() == nullptr ) {
                setRouteController();
            } else if ( pVehicle->getNavigationType() != 0 ||
                        pVehicle->getNavigation() == nullptr ) {
                setTrafficNavigation();
            } else if ( pVehicle->getLocomotionType() == 0 &&
                        pVehicle->getLocomotion() != nullptr ) {
                updatePatrolOrSearch();
            } else {
                setTrafficLocomotion();
            }
            return true;
        }
        return event == FE_Exit;

    case PS_Search:
        if ( event == FE_Enter ) {
            pVehicle->setRemovable( false );
            setOpponentNavigation();
            pVehicle->applyBotPersonality( 0, 0, 2 );
            dpc.DPCPattern.bRespectWay = true;
            return true;
        }
        if ( event == FE_Update ) {
            if ( pPlayerCar == nullptr ) {
                return true;
            }
            checkRemoval();
            if ( pSearchedCar == nullptr ||
                 !pSearchedCar->getPoliceState().isWanted() ) {
                pSearchedCar = nullptr;
                goTo( PS_Patrol );
                return true;
            }
            if ( pVehicle->getControllerType() == 1 ) {
                bool bReached = true;
                AiRoadIt from;
                AiRoadIt to;
                if ( pVehicle->getController()->getFront( from, to ) &&
                     to.pRoad != nullptr ) {
                    const Eigen::Vector4f goalPosition =
                        to.computeOffsetPosition( 0.0f, 0.0f );
                    const float reachDistance = gAIConfig.PoliceSearchDistance *
                                                kPoliceSearchReachRatio;
                    const Eigen::Vector4f delta =
                        goalPosition -
                        pVehicle->getCarState()->getWorldPosition();
                    bReached =
                        delta.squaredNorm() < reachDistance * reachDistance;
                }
                if ( bReached ) {
                    setRouteController();
                    if ( pVehicle->getNavigationType() == 1 ) {
                        AiVehNavOpponent &nav =
                            pVehicle->getOpponentNavigation();
                        if ( nav.getState() !=
                             AiVehNavOpponent::NS_PoliceWaitRoute ) {
                            nav.requestState(
                                AiVehNavOpponent::NS_PoliceWaitRoute );
                        }
                    }
                }
            }
            updatePatrolOrSearch();
            if ( distanceToTarget > gAIConfig.PoliceDeleteDistance ) {
                pSearchedCar = nullptr;
                goTo( PS_Patrol );
            }
            return true;
        }
        return event == FE_Exit;

    case PS_Chase:
        if ( event == FE_Enter ) {
            pVehicle->setRemovable( false );
            enableSirens();
            captureStartTime = kPoliceNoTime;
            setOpponentNavigation();
            setOpponentLocomotion();
            pVehicle->applyBotPersonality( 1, 0, 2 );
            dpc.DPCPattern.bRespectWay = true;
            return true;
        }
        if ( event == FE_Update ) {
            updatePursuit();
            return true;
        }
        if ( event == FE_Exit ) {
            AiVehSignals &signals = pVehicle->getSignals();
            if ( signals.bIsCops ) {
                signals.bHornOn = false;
                signals.bFlashlightOn = false;
                signals.bHeadlightBlinking = false;
            }
            distanceToTarget = kPoliceNoDistance;
            captureStartTime = kPoliceNoTime;
            return true;
        }
        return false;

    case PS_Aggressive:
        if ( event == FE_Enter ) {
            pVehicle->setRemovable( false );
            pVehicle->applyBotPersonality( 2, 0, 2 );
            dpc.DPCPattern.bRespectWay = true;
            return true;
        }
        if ( event == FE_Update ) {
            updatePursuit();
            return true;
        }
        return event == FE_Exit;

    case PS_PoliceDam:
        if ( event == FE_Enter ) {
            pVehicle->setRemovable( false );
            AiVehSignals &signals = pVehicle->getSignals();
            if ( signals.bIsCops ) {
                signals.bHornOn = true;
                signals.bFlashlightOn = true;
            }
            pTarget = gAiCopMng.getPoliceDamTarget();
            return true;
        }
        if ( event == FE_Update ) {
            if ( pPlayerCar == nullptr ) {
                return true;
            }
            checkRemoval();
            pTarget = gAiCopMng.getPoliceDamTarget();
            const Eigen::Vector4f targetPosition =
                ( pTarget != nullptr ) ? pTarget->getWorldPosition()
                                       : pPlayerCar->getWorldPosition();
            const Eigen::Vector4f delta =
                targetPosition - pVehicle->getCarState()->getWorldPosition();
            const float deleteDistance = gAIConfig.PoliceDeleteDistance;
            if ( delta.squaredNorm() > deleteDistance * deleteDistance ) {
                pVehicle->setRemovable( true );
                pVehicle->requestRemoval( kPoliceNoTime,
                                          "police vehicle too far" );
                return true;
            }
            if ( pTarget != nullptr ) {
                bool bArrestPending = false;
                checkArrest( bArrestPending );
                if ( bArrestPending ) {
                    pTarget->getPoliceState().setArrestAfterBackOnTrack( true );
                }
            }
            return true;
        }
        return event == FE_Exit;

    default:
        return false;
    }
}

bool AiVehBhvPolice::checkRemoval() {
    // FUN_009cbea0
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr ) {
        return false;
    }

    const char *pReason = nullptr;
    const Eigen::Vector4f position =
        pVehicle->getCarState()->getWorldPosition();
    if ( gpScenaric->getIntroTutorial().isActive() ) {
        pReason = "no police during tutorial";
    } else if ( position.y() < kPoliceMinAltitude ) {
        pReason = "police vehicle below sea level";
    } else {
        Eigen::Vector4f delta = pPlayerCar->getWorldPosition() - position;
        delta.y() = 0.0f;
        const float deleteDistance = gAIConfig.PoliceDeleteDistance;
        if ( delta.squaredNorm() <= deleteDistance * deleteDistance ) {
            return false;
        }
        pReason = "police vehicle too far 2";
    }
    pVehicle->setRemovable( true );
    pVehicle->requestRemoval( kPoliceNoTime, pReason );
    return true;
}

bool AiVehBhvPolice::canSeeCar( CarState *pCar ) const {
    // FUN_009cbd00
    if ( pCar == nullptr || gAIConfig.PoliceSearchDistance <= 0.0f ) {
        return false;
    }

    // FUN_009cbc50
    const Eigen::Vector4f copPosition =
        pVehicle->getCarState()->getWorldPosition();
    const float distance = ( pCar->getWorldPosition() - copPosition ).norm();
    if ( distance <= gAIConfig.PoliceCaptureDistance ) {
        return true;
    }
    if ( distance > gAIConfig.PoliceSearchDistance ) {
        return false;
    }
    if ( gbDisableCopViewingAngle ) {
        return true;
    }

    const float maxAngle =
        kPoliceMaxViewAngle - ( ( distance - gAIConfig.PoliceCaptureDistance ) /
                                ( gAIConfig.PoliceSearchDistance -
                                  gAIConfig.PoliceCaptureDistance ) ) *
                                  kPoliceViewAngleFalloff;
    const Eigen::Vector4f forward =
        pVehicle->getCarState()->getWorldMatrix().row( 2 );
    const Eigen::Vector4f delta = pCar->getWorldPosition() - copPosition;
    const float angle =
        std::atan2( delta.z() * forward.x() - delta.x() * forward.z(),
                    delta.z() * forward.z() + delta.x() * forward.x() );
    return std::abs( angle ) < maxAngle;
}

void AiVehBhvPolice::checkTargetVisibility( CarState *pCar,
                                            bool &bOutVisible,
                                            bool &bOutEligible ) const {
    // FUN_009cbe10
    bOutVisible = true;
    bOutEligible = true;
    if ( pCar == nullptr ) {
        bOutVisible = false;
        bOutEligible = false;
        return;
    }

    const AiVehicle *pCarVehicle =
        static_cast<const AiVehicle *>( pCar->getAIController() );
    const bool bTargetable =
        pCarVehicle == nullptr ||
        ( pCarVehicle->getBehaviourType() != 0 &&
          pCar->getOwner() != CarState::eOwner::CSO_Traffic );
    const eGameMode gameMode = gpActiveGameMode->getCurrentGameMode();
    const bool bGameModeAllowed = gameMode == GM_FreeRide ||
                                  gameMode == GM_TestCar ||
                                  IsPoliceAllowedByGameMech();
    if ( bTargetable &&
         pCar->getPoliceState().getAfterCaptureGraceTime() <=
             gpTimer->GameTotalTime &&
         bGameModeAllowed ) {
        if ( !canSeeCar( pCar ) ) {
            bOutVisible = false;
        }
        return;
    }
    bOutVisible = false;
    bOutEligible = false;
}

void AiVehBhvPolice::onTargetWitnessed( CarState *pCar ) const {
    // FUN_009cc0c0
    if ( !gAIConfig.Unknown0x5B || pCar == nullptr ) {
        return;
    }

    PoliceState &policeState = pCar->getPoliceState();
    const float now = gpTimer->GameTotalTime;
    if ( now - policeState.getLastInfTimeWitness() >=
         gAIConfig.WitnessedOffenceTime ) {
        return;
    }

    const bool bWasWanted = policeState.isWanted();
    policeState.setWitnessedOffence( gAIConfig.WitnessedOffenceValue );
    policeState.setLastInfTimeWitness( 0.0f );
    gAiCopMng.onVoiceEvent( eCopVoiceEvent::CVE_WitnessedOffence );
    if ( !bWasWanted && policeState.isWanted() ) {
        const float malus = policeState.getMinTimeToStopSearchMalus();
        policeState.setMinTimeToStopSearch(
            now + malus + gAIConfig.PoliceSearchMinTime,
            gAIConfig.PoliceSearchMinTimeStep + malus );
    }
}

bool AiVehBhvPolice::shouldChase( CarState *pCar, bool &bOutWanted ) const {
    // FUN_009cc180
    bOutWanted = false;
    if ( pCar == nullptr ) {
        return false;
    }

    PoliceState &policeState = pCar->getPoliceState();
    if ( policeState.isChased() ) {
        return policeState.getNumChasers() < policeState.getNumWantedChaser();
    }

    bool bVisible = true;
    bool bEligible = true;
    checkTargetVisibility( pCar, bVisible, bEligible );
    if ( bVisible ) {
        const Eigen::Vector4f delta =
            pCar->getWorldPosition() -
            Eigen::Vector4f( pVehicle->getInfos().GroundMatrix.row( 3 ) );
        const float searchDistance = gAIConfig.PoliceSearchDistance;
        if ( delta.squaredNorm() < searchDistance * searchDistance ) {
            if ( !policeState.isWanted() ) {
                onTargetWitnessed( pCar );
            }
            if ( policeState.isWanted() ) {
                return policeState.getNumChasers() <
                       policeState.getNumWantedChaser();
            }
        }
    }
    if ( bEligible && policeState.isWanted() ) {
        bOutWanted = true;
    }
    return false;
}

void AiVehBhvPolice::findTarget( CarState *&pOutWantedCar ) {
    // FUN_009cc350
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr ) {
        return;
    }
    pOutWantedCar = nullptr;

    bool bWanted = false;
    if ( shouldChase( pPlayerCar, bWanted ) ) {
        pTarget = pPlayerCar;
        return;
    }
    if ( bWanted ) {
        pOutWantedCar = pPlayerCar;
    }
}

void AiVehBhvPolice::updateDistanceToTarget() {
    // FUN_009cc000
    if ( pTarget == nullptr ) {
        distanceToTarget = 0.0f;
        return;
    }
    distanceToTarget = ( pTarget->getWorldPosition() -
                         pVehicle->getCarState()->getWorldPosition() )
                           .norm();
}

void AiVehBhvPolice::loseTarget() {
    // FUN_009cb9c0
    if ( getCurrentState() == PS_Patrol ) {
        return;
    }
    if ( !pVehicle->isSoftCollision() ) {
        pTarget = nullptr;
        goTo( PS_Patrol );
    }
    captureStartTime = kPoliceNoTime;
}

void AiVehBhvPolice::startArrest() {
    // FUN_009cc3b0
    if ( pTarget == nullptr ) {
        return;
    }

    CarState *pArrestedCar = pTarget;
    captureStartTime = kPoliceNoTime;
    pTarget = nullptr;
    const AiVehicle *pArrestedVehicle =
        static_cast<const AiVehicle *>( pArrestedCar->getAIController() );
    if ( pArrestedVehicle == nullptr ) {
        gpAI->getAIPool()->updateArrestedCar( AiPool::AS_TakeOver,
                                              pArrestedCar );
        if ( !pVehicle->isSoftCollision() ) {
            goTo( PS_Arrest );
        }
    } else if ( pArrestedVehicle->isPlayerCar() &&
                !pVehicle->isSoftCollision() ) {
        goTo( PS_Patrol );
    }
}

bool AiVehBhvPolice::checkArrest( bool &bOutArrestPending ) {
    // FUN_009cc430
    bOutArrestPending = false;
    if ( GSPhysics::peekFadeRequest() != eFadeRequest::FR_Idle ||
         pTarget == nullptr ) {
        return false;
    }
    PoliceState &policeState = pTarget->getPoliceState();
    if ( policeState.computeOffenceLevel() < gAIConfig.InfLimit ) {
        return false;
    }

    const eGameMode gameMode = gpActiveGameMode->getCurrentGameMode();
    if ( gameMode == GM_FreeRide ) {
        const GMFreeride *pFreeride =
            static_cast<const GMFreeride *>( gpActiveGameMode );
        if ( pFreeride->getMngFRMeetingIC().getState() != ICState_None ) {
            return false;
        }
    } else if ( gameMode != GM_TestCar && !IsPoliceAllowedByGameMech() ) {
        return false;
    }

    const bool bBackOnTrack = pTarget->getBody()->BackOnTrackState != 0;
    if ( !bBackOnTrack && policeState.isArrestAfterBackOnTrack() ) {
        startArrest();
        return true;
    }

    const float now = gpTimer->GameTotalTime;
    bool bOffRoadCapture = false;
    bool bCaptured = false;
    if ( pTarget->getWorldLocation().LocationType ==
             ObjectWorldLocation::OWL_OffRoad &&
         !gAIConfig.bPoliceOutsideAllowed ) {
        bOffRoadCapture = true;
        if ( captureStartTime < 0.0f ) {
            captureStartTime = now;
        } else if ( now - captureStartTime >= gAIConfig.PoliceCaptureTime ) {
            bCaptured = true;
        }
    }

    if ( !bCaptured ) {
        updateDistanceToTarget();
        const float loseDistance =
            gAIConfig.PoliceSearchDistance * kPoliceLoseTargetRatio;
        if ( distanceToTarget > loseDistance ) {
            if ( gAiCopMng.getPlayerToCopMinDistance() > loseDistance &&
                 !bOffRoadCapture ) {
                loseTarget();
            }
            return false;
        }
        if ( distanceToTarget >= gAIConfig.PoliceCaptureDistance ) {
            if ( !bOffRoadCapture ) {
                captureStartTime = kPoliceNoTime;
            }
            return false;
        }
        if ( bBackOnTrack ) {
            bOutArrestPending = true;
            return false;
        }

        if ( gAiCopMng.canPlayWarnVoice() ) {
            gAiCopMng.onVoiceEvent( eCopVoiceEvent::CVE_WarnChased );
        }
        bool bTimeElapsed = false;
        if ( gAIConfig.bNeedSpeedAndTime || gAIConfig.bCaptureByTime ) {
            if ( captureStartTime <= 0.0f ) {
                captureStartTime = now;
            } else if ( gAIConfig.PoliceCaptureTime <=
                        now - captureStartTime ) {
                bTimeElapsed = true;
            }
        }
        const bool bSlowEnough =
            gAIConfig.bCaptureBySpeed &&
            pTarget->getLinearVelocity().norm() < gAIConfig.PoliceCaptureSpeed;
        if ( gAIConfig.bNeedSpeedAndTime ) {
            if ( ( gAIConfig.bCaptureByTime && !bTimeElapsed ) ||
                 ( gAIConfig.bCaptureBySpeed && !bSlowEnough ) ) {
                return false;
            }
        } else if ( !bTimeElapsed && !bSlowEnough ) {
            return false;
        }
    }

    if ( pTarget->getBody()->BackOnTrackState == 0 ) {
        startArrest();
        return true;
    }
    bOutArrestPending = true;
    return false;
}

void AiVehBhvPolice::updatePatrolOrSearch() {
    // FUN_009cc700
    if ( gpScenaric->getLocalPlayer().getCarState() == nullptr ) {
        return;
    }

    distanceToTarget = kPoliceNoDistance;
    captureStartTime = kPoliceNoTime;
    const ObjectWorldLocation &copLocation =
        pVehicle->getCarState()->getWorldLocation();
    CarState *pWantedCar = nullptr;
    if ( gpTimer->GameTotalTime - lastTargetScanTime > kPoliceScanInterval ) {
        if ( !gAIConfig.bPoliceForceChase ) {
            findTarget( pWantedCar );
        } else {
            CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
            if ( pPlayerCar != nullptr &&
                 pPlayerCar->getWorldLocation().LocationType ==
                     ObjectWorldLocation::OWL_Road ) {
                pTarget = pPlayerCar;
            }
        }
    }

    if ( pTarget == nullptr ) {
        if ( pSearchedCar != nullptr || pWantedCar == nullptr ) {
            return;
        }
        if ( pWantedCar->getWorldLocation().LocationType !=
             ObjectWorldLocation::OWL_Road ) {
            pSearchedCar = nullptr;
            return;
        }
        setChaseController();
        pSearchedCar = pWantedCar;
        pVehicle->getChaseController().setGoal( pWantedCar );
        pWantedCar->getPoliceState().setSearched( true );
        goTo( PS_Search );
        return;
    }

    const ObjectWorldLocation &targetLocation = pTarget->getWorldLocation();
    bool bCanChase = false;
    if ( targetLocation.LocationType == ObjectWorldLocation::OWL_Road &&
         copLocation.LocationType == ObjectWorldLocation::OWL_Road ) {
        bool bSameWay = false;
        if ( copLocation.LocationRoad.RoadID !=
             targetLocation.LocationRoad.RoadID ) {
            AiRoadMap &roadMap = gpAI->getRoadMap();
            AiRoadMap::Road *pCopRoad =
                roadMap.getRoad( copLocation.LocationRoad.RoadID );
            AiRoadMap::Road *pTargetRoad =
                roadMap.getRoad( targetLocation.LocationRoad.RoadID );
            if ( pCopRoad != nullptr && pTargetRoad != nullptr ) {
                AiRoadIt copIt;
                pCopRoad->getShape()->computeIt(
                    ( copLocation.LocationRoad.FractionMin +
                      copLocation.LocationRoad.FractionMax ) *
                        0.5f,
                    copIt );
                AiRoadIt targetIt;
                pTargetRoad->getShape()->computeIt(
                    ( targetLocation.LocationRoad.FractionMin +
                      targetLocation.LocationRoad.FractionMax ) *
                        0.5f,
                    targetIt );

                AiPath path;
                path.setStart( copIt, 0 );
                path.setEnd( targetIt );
                path.solve( true );
                bSameWay =
                    path.getTail() != nullptr &&
                    path.getTail()->To.getWay() == ( targetLocation.bWay != 0 );
                path.clear();
            }
        }
        bCanChase = bSameWay || gAIConfig.bPoliceCanChaseFromAhead;
    }

    PoliceState &policeState = pTarget->getPoliceState();
    if ( bCanChase &&
         policeState.getNumChasers() < policeState.getNumWantedChaser() ) {
        setChaseController();
        pVehicle->getChaseController().setTarget( pTarget );
        pSearchedCar = nullptr;
        policeState.setChased( true );
        policeState.stopSearch();
        goTo( PS_Chase );
        gAiCopMng.onVoiceEvent( eCopVoiceEvent::CVE_SearchToChase );
        return;
    }

    bool bArrestPending = false;
    checkArrest( bArrestPending );
    if ( bArrestPending ) {
        pTarget->getPoliceState().setArrestAfterBackOnTrack( true );
    }
    pTarget = nullptr;
}

void AiVehBhvPolice::updatePursuit() {
    // FUN_009ccc80 (states 4 / 5 update)
    if ( gpScenaric->getLocalPlayer().getCarState() == nullptr ) {
        return;
    }
    checkRemoval();
    if ( pVehicle->getControllerType() != 1 ||
         pVehicle->getController() == nullptr ) {
        setChaseController();
    } else if ( pVehicle->getNavigationType() != 1 ||
                pVehicle->getNavigation() == nullptr ) {
        setOpponentNavigation();
    } else if ( pVehicle->getLocomotionType() == 1 &&
                pVehicle->getLocomotion() != nullptr ) {
        // FUN_009cca60
        if ( pTarget != nullptr ) {
            PoliceState &policeState = pTarget->getPoliceState();
            if ( policeState.computeOffenceLevel() >= gAIConfig.InfLimit ) {
                const AiVehicle *pTargetVehicle =
                    static_cast<const AiVehicle *>(
                        pTarget->getAIController() );
                if ( policeState.getAfterCaptureGraceTime() >
                     gpTimer->GameTotalTime ) {
                    loseTarget();
                    return;
                }
                if ( pTargetVehicle != nullptr &&
                     pTargetVehicle->isPlayerCar() &&
                     policeState.isArrested() ) {
                    policeState.reset();
                    loseTarget();
                    return;
                }
                if ( gAiCopMng.canDoPoliceDams() &&
                     policeState.computeOffenceLevel() >=
                         gAIConfig.InfPoliceDamLimit &&
                     gAiCopMng.getNumPoliceDams() == 0 &&
                     !gAiCopMng.canSpawnPoliceDam() &&
                     !gpScenaric->getIntroTutorial().isActive() ) {
                    gAiCopMng.requestPoliceDam( pTarget );
                    gAiCopMng.onVoiceEvent(
                        eCopVoiceEvent::CVE_AskForPoliceDam );
                }
                if ( gAIConfig.bPoliceForceChase ) {
                    updateDistanceToTarget();
                    if ( distanceToTarget > 0.0f &&
                         distanceToTarget > gAIConfig.PoliceSearchDistance *
                                                kPoliceLoseTargetRatio &&
                         distanceToTarget > gAIConfig.PoliceDeleteDistance ) {
                        loseTarget();
                    }
                    return;
                }
                if ( pVehicle->getControllerType() ==
                     AiVehicle::kControllerNone ) {
                    loseTarget();
                    return;
                }
                bool bArrestPending = false;
                checkArrest( bArrestPending );
                if ( bArrestPending ) {
                    pTarget->getPoliceState().setArrestAfterBackOnTrack( true );
                }
                return;
            }
        }
        if ( getCurrentState() == PS_Patrol ) {
            return;
        }
        if ( !pVehicle->isSoftCollision() ) {
            pTarget = nullptr;
            goTo( PS_Patrol );
        }
        captureStartTime = kPoliceNoTime;
    } else {
        setOpponentLocomotion();
    }
}

void AiVehBhvPolice::setRouteController() {
    // FUN_00923a10 (EDI = 0)
    pVehicle->setController( 0 );
}

void AiVehBhvPolice::setChaseController() {
    // FUN_00923a10 (EDI = 1)
    pVehicle->setController( 1 );
}

void AiVehBhvPolice::setTrafficNavigation() {
    // FUN_009ccc80 (FUN_009236b0 inlined, type 0)
    pVehicle->setNavigation( 0 );
}

void AiVehBhvPolice::setOpponentNavigation() {
    // FUN_009ccc80 (FUN_009236b0 inlined, type 1)
    pVehicle->setNavigation( 1 );
}

void AiVehBhvPolice::setTrafficLocomotion() {
    // FUN_009ccc80 (FUN_009235b0 inlined, type 0)
    pVehicle->setLocomotion( 0, true, true );
}

void AiVehBhvPolice::setOpponentLocomotion() {
    // FUN_009ccc80 (FUN_009235b0 inlined, type 1)
    pVehicle->setLocomotion( 1, true, true );
}

void AiVehBhvPolice::enableSirens() {
    // FUN_009ccc80 (state 4 enter)
    AiVehSignals &signals = pVehicle->getSignals();
    if ( !signals.bIsCops ) {
        return;
    }
    signals.bHornOn = true;
    signals.bFlashlightOn = true;
    float *pQueue = signals.HeadlightCommandQueue;
    for ( int32_t i = 0; i < 5; i++ ) {
        pQueue[i] = kPoliceBlinkOnTime;
    }
    pQueue[5] = kPoliceBlinkOffTime;
    signals.HeadlightCommandIndex = 0;
    signals.bHeadlightsActivated = true;
    signals.bHeadlightOn = true;
    signals.HeadlightLastTime = gpTimer->GameTotalTime;
    signals.bHeadlightBlinking = true;
}
