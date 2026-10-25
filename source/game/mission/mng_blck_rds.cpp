#include "shared.h"
#include "mng_blck_rds.h"

#include "ai/ai_road_map.h"
#include "ai/gs_ai.h"
#include "core/maths_helper.h"
#include "game/entity_mechanism.h"
#include "game/game_mechanism.h"
#include "game/gm_world.h"
#include "game/mission/game_mech_race_hud.h"
#include "game/scenaric/entity/entity_check_point.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/scenaric_bank.h"
#include "game/vehicle/car_state.h"

static AiRoadMap::Entity *GetFirstEntity( const AiRoadMap::Road *pRoad ) {
    return pRoad->pDetail->pEntities[0];
}

static AiRoadMap::Entity *GetLastEntity( const AiRoadMap::Road *pRoad ) {
    return pRoad->pDetail->pEntities[pRoad->pDetail->NumEntities - 1];
}

static bool IsSameRoadPosition( const AiRoadIt &it,
                                const AiRoadIt &other,
                                const float tolerance ) {
    if ( other.pRoad == nullptr || it.pRoad->ID != other.pRoad->ID ||
         it.IndEntity != other.IndEntity || it.IndWay != other.IndWay ) {
        return false;
    }
    const float distance =
        ( it.bWay == other.bWay ) ? it.T - other.T : 1.0f - ( other.T + it.T );
    return std::abs( distance ) <= tolerance;
}

MngBlckRds::MngBlckRds()
    : Manager(),
      pTemplateLeft( nullptr ),
      pTemplateRight( nullptr ),
      mngActor(),
      bRouteBuilt( false ),
      route(),
      bReleaseRequested( false ),
      bEnabled( false ) {
    // FUN_00727620
    pBlockerPool.fill( nullptr );
    bBlockerActive.fill( false );
}

MngBlckRds::~MngBlckRds() {}

void MngBlckRds::tick( float deltaTime, float totalTime ) {
    // FUN_007280e0
    if ( !bEnabled ) {
        return;
    }
    if ( bReleaseRequested ) {
        bReleaseRequested = false;
        releaseAll();
    }
    if ( bRouteBuilt ) {
        update();
    }
}

void MngBlckRds::pause() {
    // FUN_00728030
    releaseActiveBlockers();
}

void MngBlckRds::terminate() {
    // FUN_00728120
    junctionExits.clear();
    blockedJunctions.clear();
    for ( Entity *&pBlocker : pBlockerPool ) {
        if ( pBlocker != nullptr ) {
            MngActor::Uninstanciate( nullptr, pBlocker );
            pBlocker = nullptr;
        }
    }
}

void MngBlckRds::enable() {
    // FUN_007283c0
    if ( buildRoute() ) {
        const int32_t numJunctions = gpAI->getRoadMap().getNumJunctions();
        blockedJunctions.resize( numJunctions );
        junctionExits.resize( numJunctions );
        bRouteBuilt = true;
        markRouteJunctions();
        compactJunctions();
        junctionExits.clear();
    }

    if ( !bEnabled ) {
        return;
    }

    ScenaricBank &worldActorsBank = gpScenaric->getWorldActorsBank();
    pTemplateLeft =
        worldActorsBank.findEntity( eEntityType::ET_ACTS, "SS_VirG_act" );
    pTemplateRight =
        worldActorsBank.findEntity( eEntityType::ET_ACTS, "SS_VirD_act" );
    for ( int32_t i = 0; i < kNumBlockers; i++ ) {
        bBlockerActive[i] = false;
        pBlockerPool[i] = MngActor::InstanciateEntity(
            nullptr,
            ( i < kNumBlockersPerSide ) ? pTemplateLeft : pTemplateRight );
    }
}

bool MngBlckRds::buildRoute() {
    // FUN_00728180
    GameMechRaceHUD *pRaceHUD = static_cast<GameMechRaceHUD *>(
        gpActiveGM->getGameMech()->getComponent( GameMechanism::CS_RaceHUD ) );
    if ( pRaceHUD == nullptr ) {
        return false;
    }

    for ( int32_t step = 2; step - 1 < pRaceHUD->getNumSteps(); step++ ) {
        EntityCheckPoint *pFrom =
            pRaceHUD->findCheckpointForStep( pRaceHUD->wrapStep( step - 1 ) );
        EntityCheckPoint *pTo =
            pRaceHUD->findCheckpointForStep( pRaceHUD->wrapStep( step ) );
        if ( pFrom == nullptr || pTo == nullptr ) {
            continue;
        }

        AiPath leg;
        Matrix4x4f_t frame = pFrom->getModelMatrix();
        MatrixPreRotateY( frame, kPI );
        leg.setStartFromFrame( frame );
        frame = pTo->getModelMatrix();
        MatrixPreRotateY( frame, kPI );
        leg.setEndFromFrame( frame );
        leg.solve( true );
        route.append( leg, true );
        leg.clear();
    }
    return true;
}

void MngBlckRds::markRouteJunctions() {
    // FUN_00727b30
    for ( AiRouteElem *pElem = route.getHead(); pElem != nullptr;
          pElem = pElem->pNext ) {
        if ( pElem->pNext == nullptr ) {
            continue;
        }

        AiRoadIt routeEntry;
        AiRoadIt routeExit;
        pElem->pNext->From.unpack( routeEntry );
        pElem->To.unpack( routeExit );
        if ( routeExit.pRoad == routeEntry.pRoad ||
             routeExit.pRoad == nullptr ) {
            continue;
        }

        AiRoadMap::Junction *pJunction = routeExit.bWay
                                             ? routeExit.pRoad->pJunctionEnd
                                             : routeExit.pRoad->pJunctionBegin;
        if ( pJunction == nullptr ) {
            continue;
        }
        markJunction( int32_t( pJunction->ID ) - 1, routeExit, routeEntry );
    }
    route.clear();
}

void MngBlckRds::markJunction( const int32_t junctionIndex,
                               const AiRoadIt &routeExit,
                               const AiRoadIt &routeEntry ) {
    // FUN_00727740
    AiRoadMap::Junction *pJunction =
        gpAI->getRoadMap().getJunctionByIndex( junctionIndex );
    if ( pJunction == nullptr ) {
        return;
    }
    JunctionExits &exits = junctionExits[junctionIndex];

    const Eigen::Vector4f exitPosition =
        routeExit.computeOffsetPosition( 0.0f, 0.0f );
    const Eigen::Vector4f entryPosition =
        routeEntry.computeOffsetPosition( 0.0f, 0.0f );
    const Eigen::Vector4f routeDirection =
        Normalize( entryPosition - exitPosition );

    AiRoadIt junctionExits[kMaxJunctionExits];
    const int32_t numExits = CollectJunctionExits( pJunction, junctionExits );
    for ( int32_t i = 0; i < numExits; i++ ) {
        exits.Exits[i].pack( junctionExits[i] );
    }

    for ( int32_t i = 0; i < numExits; i++ ) {
        const AiRoadIt &exit = junctionExits[i];
        uint8_t side = ES_Route;
        if ( !IsSameRoadPosition( exit, routeExit, kSameExitTolerance ) &&
             !IsSameRoadPosition( exit, routeEntry, kSameExitTolerance ) ) {
            const Eigen::Vector4f toExit = Normalize(
                exit.computeOffsetPosition( 0.0f, 0.0f ) - exitPosition );
            side = ( Vector4DCross( toExit, routeDirection ).y() > 0.0f )
                       ? ES_BlockedLeft
                       : ES_BlockedRight;
        }

        uint8_t &state = exits.States[i];
        if ( state == ES_BlockedLeft || state == ES_BlockedRight ) {
            if ( side == ES_Route ) {
                state = ES_Route;
            }
        } else if ( state == ES_Untouched ) {
            state = side;
        }
    }
}

int32_t MngBlckRds::CollectJunctionExits( const AiRoadMap::Junction *pJunction,
                                          AiRoadIt *pOutExits ) {
    // FUN_007274d0
    int32_t numExits = 0;
    for ( int32_t i = 0; i < pJunction->NumRoads; i++ ) {
        if ( i >= kMaxJunctionExits ) {
            return numExits;
        }

        AiRoadMap::Road *pRoad = pJunction->getRoad( i );
        for ( int32_t end = 0; end < 2; end++ ) {
            const bool bEnd = ( end == 0 );
            const AiRoadMap::Junction *pRoadJunction =
                bEnd ? pRoad->pJunctionEnd : pRoad->pJunctionBegin;
            if ( pRoadJunction != pJunction ) {
                continue;
            }

            bool bAlreadyListed = false;
            for ( int32_t j = 0; j < numExits; j++ ) {
                if ( pOutExits[j].pRoad == pRoad &&
                     pOutExits[j].bWay == bEnd ) {
                    bAlreadyListed = true;
                    break;
                }
            }
            if ( bAlreadyListed || numExits >= kMaxJunctionExits ) {
                continue;
            }

            AiRoadIt &exit = pOutExits[numExits++];
            exit.pRoad = pRoad;
            if ( bEnd ) {
                exit.IndEntity = pRoad->pDetail->NumEntities - 1;
                exit.IndWay = GetLastEntity( pRoad )->getNumCurves() - 1;
            } else {
                exit.IndEntity = 0;
                exit.IndWay = 0;
            }
            exit.bWay = bEnd;
            exit.T = 1.0f;
        }
    }
    return numExits;
}

void MngBlckRds::compactJunctions() {
    // FUN_00727300
    const AiRoadMap &roadMap = gpAI->getRoadMap();
    std::vector<BlockedJunction> visited;
    for ( int32_t i = 0; i < roadMap.getNumJunctions(); i++ ) {
        if ( junctionExits[i].States[0] == ES_Untouched ) {
            continue;
        }

        BlockedJunction &blockedJunction = visited.emplace_back();
        blockedJunction.Exits = junctionExits[i];
        blockedJunction.pJunction = roadMap.getJunctionByIndex( i );
        blockedJunction.DistanceSq = 0.0f;
        blockedJunction.field_0xcc = 0.0f;
        blockedJunction.bReleased = true;
        blockedJunction.PositionX = blockedJunction.pJunction->Position.X;
        blockedJunction.PositionZ = blockedJunction.pJunction->Position.Z;
    }
    blockedJunctions = std::move( visited );
}

void MngBlckRds::update() {
    // FUN_00727ea0
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr ) {
        return;
    }

    const Eigen::Vector4f playerPosition =
        pPlayerCar->getBody()->WorldMatrix.row( 3 ).transpose();
    for ( BlockedJunction &blockedJunction : blockedJunctions ) {
        const float deltaX = blockedJunction.PositionX - playerPosition.x();
        const float deltaZ = blockedJunction.PositionZ - playerPosition.z();
        blockedJunction.DistanceSq = deltaX * deltaX + deltaZ * deltaZ;

        if ( blockedJunction.DistanceSq < kSpawnDistanceSq ) {
            if ( !blockedJunction.bReleased ) {
                continue;
            }
            blockedJunction.bReleased = false;
            for ( int32_t i = 0; i < kMaxJunctionExits; i++ ) {
                const uint8_t state = blockedJunction.Exits.States[i];
                if ( state == ES_BlockedLeft || state == ES_BlockedRight ) {
                    spawnBlockers( blockedJunction.Exits.Exits[i], state,
                                   blockedJunction.pBlockers );
                }
            }
        } else if ( blockedJunction.DistanceSq < kReleaseDistanceSq &&
                    !blockedJunction.bReleased ) {
            blockedJunction.bReleased = true;
            for ( Entity *&pBlocker : blockedJunction.pBlockers ) {
                if ( pBlocker != nullptr ) {
                    releaseBlocker( pBlocker );
                    pBlocker = nullptr;
                }
            }
        }
    }
}

void MngBlckRds::releaseAll() {
    // FUN_00728040
    if ( !bEnabled ) {
        return;
    }
    for ( BlockedJunction &blockedJunction : blockedJunctions ) {
        if ( blockedJunction.bReleased ) {
            continue;
        }
        blockedJunction.bReleased = true;
        for ( Entity *&pBlocker : blockedJunction.pBlockers ) {
            if ( pBlocker != nullptr ) {
                releaseBlocker( pBlocker );
                pBlocker = nullptr;
            }
        }
    }
}

void MngBlckRds::releaseActiveBlockers() {
    // FUN_00727ff0
    for ( int32_t i = 0; i < kNumBlockers; i++ ) {
        if ( bBlockerActive[i] ) {
            releaseBlocker( pBlockerPool[i] );
        }
    }
}

void MngBlckRds::spawnBlockers( const AiCompressedRoadIt &exit,
                                const uint8_t state,
                                Entity **ppBlockers ) {
    // FUN_00727db0
    AiRoadIt it;
    exit.unpack( it );
    const int32_t numLanes = GetLastEntity( it.pRoad )->NumRightLanes +
                             GetFirstEntity( it.pRoad )->NumLeftLanes;

    int32_t slot = 0;
    while ( slot < kMaxBlockersPerJunction && ppBlockers[slot] != nullptr ) {
        slot++;
    }

    for ( int32_t lane = 0; lane < numLanes; lane++ ) {
        Matrix4x4f_t frame;
        ComputeLaneFrame( it, lane, frame );
        Entity *pBlocker = acquireBlocker( state );
        if ( pBlocker == nullptr ) {
            continue;
        }

        if ( slot < kMaxBlockersPerJunction ) {
            ppBlockers[slot++] = pBlocker;
        }
        pBlocker->propagateTransform( frame, true );
        if ( pBlocker->getMechanism() != nullptr ) {
            pBlocker->getMechanism()->setInitialTransformMatrix( frame );
        }
    }
}

void MngBlckRds::ComputeLaneFrame( AiRoadIt it,
                                   const int32_t lane,
                                   Matrix4x4f_t &outFrame ) {
    // FUN_00727c00
    const int32_t numLanesOnSide =
        it.bWay ? GetLastEntity( it.pRoad )->NumRightLanes
                : GetFirstEntity( it.pRoad )->NumLeftLanes;
    if ( lane < numLanesOnSide ) {
        it.computeFrame( outFrame, lane );
        return;
    }

    it.flip();
    it.computeFrame( outFrame, lane - numLanesOnSide );
    const Eigen::Vector4f position = outFrame.row( 3 ).transpose();
    outFrame.row( 3 ) = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ).transpose();
    MatrixRotateY( outFrame, kPI );
    outFrame.row( 3 ) = position.transpose();
}

Entity *MngBlckRds::acquireBlocker( const uint8_t state ) {
    // FUN_00727260
    const int32_t first = ( state == ES_BlockedLeft ) ? 0 : kNumBlockersPerSide;
    for ( int32_t i = first; i < first + kNumBlockersPerSide; i++ ) {
        if ( bBlockerActive[i] ) {
            continue;
        }
        bBlockerActive[i] = true;
        mngActor.registerActor( pBlockerPool[i] );
        return pBlockerPool[i];
    }
    return nullptr;
}

void MngBlckRds::releaseBlocker( Entity *pBlocker ) {
    // FUN_00727d30
    for ( int32_t i = 0; i < kNumBlockers; i++ ) {
        if ( pBlockerPool[i] != pBlocker ) {
            continue;
        }
        bBlockerActive[i] = false;
        mngActor.removeActorFromList( pBlocker );
        if ( pBlocker->getMechanism() != nullptr ) {
            pBlocker->getMechanism()->reInit();
        }
        return;
    }
}
