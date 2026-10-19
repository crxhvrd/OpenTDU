#include "shared.h"
#include "ai_debug_race.h"

#include "ai/ai_path.h"
#include "ai/ai_pool.h"
#include "ai/ai_road_query.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "game/mng_gps.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"

#ifdef OTDU_USE_IMGUI
#include "imgui.h"
#endif

AiDebugRace gAiDebugRace;

static constexpr int32_t kMissionTypeRace = 5;
static constexpr float kGridRowSpacing = 10.0f; // DAT_00f4166c
static constexpr float kGridLaneSpacing = 5.0f; // DAT_00f41750

AiDebugRace::AiDebugRace()
    : opponents(),
      checkpoints(),
      startLineFrame( Matrix4x4f_t::Identity() ),
      carID( -1 ),
      finishDistance( 1500.0f ),
      bReverseGrid( false ) {}

int32_t AiDebugRace::getNumCheckpoints() const {
    return static_cast<int32_t>( checkpoints.size() ) - 1;
}

bool AiDebugRace::getCheckpointRoadLocation( const int32_t index,
                                             AiRoadIt &outIt ) {
    if ( index < 0 || index >= static_cast<int32_t>( checkpoints.size() ) ) {
        return false;
    }
    outIt = checkpoints[index];
    return outIt.pRoad != nullptr;
}

void AiDebugRace::setCheckpointRoadLocation( const int32_t index,
                                             const AiRoadIt &it ) {
    if ( index >= 0 && index < static_cast<int32_t>( checkpoints.size() ) ) {
        checkpoints[index] = it;
    }
}

bool AiDebugRace::setupCheckpoints() {
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr || !gpAI->isRoadMapLoaded() ) {
        return false;
    }

    const Matrix4x4f_t &playerMatrix = pPlayerCar->getWorldMatrix();
    const Eigen::Vector4f startPosition =
        playerMatrix.row( 3 ).transpose() +
        playerMatrix.row( 2 ).transpose() *
            ( kGridRowSpacing * ( kMaxNumOpponents + 1 ) );
    AiRoadLocation start;
    if ( !ProjectOnRoad( startPosition, nullptr, nullptr, false, &start ) ) {
        return false;
    }

    AiRoadIt finish;
    if ( gpMngGPS != nullptr && gpMngGPS->getRoute().getNumElems() > 0 ) {
        finish = gpMngGPS->getRoute().getEnd();
    } else {
        const Eigen::Vector4f target =
            playerMatrix.row( 3 ).transpose() +
            playerMatrix.row( 2 ).transpose() * finishDistance;
        AiRoadLocation location;
        if ( !ProjectOnRoad( target, nullptr, nullptr, false, &location ) ) {
            return false;
        }
        finish = location.It;
    }

    if ( finish.pRoad == nullptr ) {
        return false;
    }

    AiPath route;
    route.setStart( start.It, 0 );
    route.setEnd( finish );
    if ( route.solve( true ) != AiPathSolver::S_Success ) {
        route.clear();
        return false;
    }
    const AiRoadIt startLine = route.getStart();
    route.clear();
    startLine.computeFrame( startLineFrame, -1 );

    checkpoints.clear();
    checkpoints.push_back( startLine );
    checkpoints.push_back( startLine );
    checkpoints.push_back( finish );
    return true;
}

void AiDebugRace::spawnOpponent() {
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr ) {
        return;
    }
    if ( checkpoints.empty() && !setupCheckpoints() ) {
        OTDU_LOG_WARN( "Debug race: no road around the player\n" );
        return;
    }

    for ( int32_t i = 0; i < kMaxNumOpponents; i++ ) {
        Opponent &opponent = opponents[i];
        if ( opponent.State != OS_Free ) {
            continue;
        }

        const int32_t opponentCarID =
            ( carID >= 0 ) ? carID : pPlayerCar->getCarID();
        VehicleBaseResources *pResource = gpCarState->grabFreeResourceEntry();
        if ( pResource == nullptr ||
             !pResource->load( opponentCarID, -1, 1, 0, 0, 0,
                               eVehicleResourceType::VRT_Traffic ) ) {
            if ( pResource != nullptr ) {
                gpCarState->releaseResourceEntry( pResource );
            }
            OTDU_LOG_WARN( "Debug race: failed to load car %d\n",
                           opponentCarID );
            return;
        }

        const float lateral =
            ( i % 2 == 0 ) ? -kGridLaneSpacing * 0.5f : kGridLaneSpacing * 0.5f;
        opponent.StartFrame = startLineFrame;
        opponent.StartFrame.row( 3 ) +=
            startLineFrame.row( 2 ) * ( -kGridRowSpacing * ( i + 1 ) ) +
            startLineFrame.row( 0 ) * lateral;
        if ( bReverseGrid ) {
            opponent.StartFrame.row( 0 ) = -opponent.StartFrame.row( 0 );
            opponent.StartFrame.row( 2 ) = -opponent.StartFrame.row( 2 );
        }
        opponent.pResource = pResource;
        opponent.State = OS_Loading;
        return;
    }
}

void AiDebugRace::tick() {
    for ( int32_t i = 0; i < kMaxNumOpponents; i++ ) {
        Opponent &opponent = opponents[i];
        if ( opponent.State != OS_Loading || !opponent.pResource->isLoaded() ) {
            continue;
        }

        VehicleSpawnParams params;
        params.Transform = opponent.StartFrame;
        params.bSetPosition = true;
        params.bCastToGround = true;
        opponent.pCarState = gpCarState->installVehicle(
            opponent.pResource, params, VIT_Opponent, -1 );
        if ( opponent.pCarState == nullptr ) {
            gpCarState->releaseResourceEntry( opponent.pResource );
            opponent = Opponent();
            continue;
        }

        {
            PhysicsLock lock;
            opponent.pCarState->getPhysicsVehicle()->resetToTransform(
                opponent.StartFrame, true );
        }
        opponent.pCarState->getWorldMatrix() = opponent.StartFrame;

        AiRoadIt startHint;
        getCheckpointRoadLocation( 1, startHint );
        opponent.pVehicle = gpAI->getAIPool()->addOpponent(
            opponent.pCarState, opponent.StartFrame, startHint,
            kMissionTypeRace );
        opponent.pCarState->setAIController( opponent.pVehicle );
        if ( opponent.pVehicle != nullptr ) {
            opponent.pVehicle->getOpponentBehaviour().setOpponentIndex( i + 1 );
            opponent.pVehicle->startFade( false );
        }
        opponent.State = OS_Installed;
    }
}

void AiDebugRace::sendMessage( const int32_t id ) {
    gpAI->setRace( this );
    AiRaceMessage message;
    message.ID = id;
    gpAI->broadcastRaceMessage( message );
}

void AiDebugRace::releaseAll() {
    for ( Opponent &opponent : opponents ) {
        if ( opponent.pCarState != nullptr ) {
            if ( opponent.pVehicle != nullptr ) {
                gpAI->getAIPool()->removeVehicle( opponent.pVehicle,
                                                  "RemoveOpponent", false );
            }
            opponent.pCarState->setAIController( nullptr );
            gpCarState->unloadVehicle( opponent.pCarState, false, false );
        } else if ( opponent.pResource != nullptr ) {
            opponent.pResource->unload();
            gpCarState->releaseResourceEntry( opponent.pResource );
        }
        opponent = Opponent();
    }
    checkpoints.clear();
    if ( gpAI->getRace() == this ) {
        gpAI->setRace( nullptr );
    }
}

void AiDebugRace::drawDebug() {
#ifdef OTDU_USE_IMGUI
    ImGui::SeparatorText( "Race opponents (debug race)" );
    ImGui::InputInt( "Opponent car ID (-1 = player car)", &carID );
    ImGui::InputFloat( "Finish distance (no GPS route)", &finishDistance );
    ImGui::Checkbox( "Reverse grid (cars face away from the route)",
                     &bReverseGrid );
    if ( ImGui::Button( "Spawn opponent" ) ) {
        spawnOpponent();
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Prepare" ) ) {
        sendMessage( ARM_Prepare );
        sendMessage( ARM_Countdown );
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Go" ) ) {
        sendMessage( ARM_CountdownLast );
        sendMessage( ARM_Go );
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Stop" ) ) {
        sendMessage( ARM_Stop );
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Release" ) ) {
        releaseAll();
    }

    ImGui::Text( "Checkpoints %d", getNumCheckpoints() );
    for ( int32_t i = 0; i < kMaxNumOpponents; i++ ) {
        const Opponent &opponent = opponents[i];
        if ( opponent.pVehicle == nullptr ) {
            ImGui::Text( "#%d state %d", i, opponent.State );
            continue;
        }
        AiVehicle *pVehicle = opponent.pVehicle;
        AiVehController *pController = pVehicle->getController();
        ImGui::Text( "#%d nav %d route %d | speed %.1f m/s target %.1f", i,
                     pVehicle->getOpponentNavigation().getState(),
                     pController != nullptr ? pController->getCount() : -1,
                     pVehicle->getInfos().Speed,
                     pVehicle->getLocomotion() != nullptr
                         ? pVehicle->getLocomotion()->getTargetSpeed()
                         : 0.0f );
    }
#endif
}
