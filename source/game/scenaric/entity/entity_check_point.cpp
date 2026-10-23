#include "shared.h"
#include "entity_check_point.h"

#include "filesystem/binary.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/mng_actor.h"
#include "ai/ai_game_session.h"
#include "entity_volume.h"
#include "game/game_mechanism.h"
#include "game/gm_world.h"
#include "game/mission/game_mech_race_hud.h"
#include "game/scenaric/event_parameters.h"
#include "game/vehicle/car_state.h"
#include "ai/ai_road_map.h"
#include "ai/ai_road_query.h"
#include "core/maths_helper.h"
#include "game/entity_mechanism.h"
#include "render/hiearchy_node.h"
#include "render/serialized_skinned_model.h"

static constexpr float kGateRoadEdgeMargin = 0.6f; // DAT_00f416c8
static constexpr float kZoneRoadEdgeMargin = 3.0f; // DAT_00f416b4

static bool IsCarRaceEligible( CarState *pCarState ) {
    // FUN_008aeb20
    if ( pCarState->getSimulationType() !=
         CarState::eSimulationType::CSST_ReplicatedFromNetwork ) {
        return true;
    }
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (CarState +0x39d4 == local P2P
                             // player)
    return true;
}

static Entity *InstanciateEntity( MngActor *pMngActor, Entity *pTemplate ) {
    // FUN_006def70
    if ( pTemplate == nullptr ) {
        return nullptr;
    }

    const eEntityType type = pTemplate->getType();
    EntityAllocation allocation = EntityFactory::DoAction(
        nullptr, type, nullptr, eInstantiationFlag::IF_CreateEntity );

    Entity *pInstance = allocation.pEntity;
    if ( pInstance == nullptr ) {
        return nullptr;
    }

    pTemplate->instanciate( pMngActor, pInstance );
    pInstance->initializeInstance( pMngActor, pTemplate );
    pInstance->initialize();
    return pInstance;
}

static uint8_t *ReadOptionalWavRef( BinaryReader *pReader ) {
    // FUN_007f9930 (inlined)
    if ( ReadStreamU32( pReader ) == 0 ) {
        return nullptr;
    }
    return ReadStreamRef( pReader );
}

static void ResolveWavRef( uint8_t *pBlock ) {
    // FUN_007f9a70 (inlined)
    if ( pBlock == nullptr ) {
        return;
    }

    const uint64_t hashcode = *reinterpret_cast<uint64_t *>( pBlock );
    *reinterpret_cast<void **>( pBlock ) = gpScenaric->findWavData( hashcode );
}

static void PropagatePositionToChild( Entity *pChild,
                                      const Entity *pParent,
                                      const Eigen::Vector4f &position,
                                      bool param_1 ) {
    // FUN_007fb3c0
    Eigen::Vector4f childPosition = pChild->getModelMatrix().row( 3 );
    childPosition += position;
    childPosition -= Eigen::Vector4f( pParent->getModelMatrix().row( 3 ) );

    pChild->propagatePosition( childPosition, param_1 );
}

static void PropagateTransformToChild( Entity *pChild,
                                       const Entity *pParent,
                                       const Matrix4x4f_t &transformMatrix,
                                       bool param_1 ) {
    // FUN_007fb700
    Matrix4x4f_t childMatrix = pChild->getModelMatrix() *
                               pParent->getModelMatrix().inverse() *
                               transformMatrix;

    pChild->propagateTransform( childMatrix, param_1 );
}

EntityCheckPoint::EntityCheckPoint()
    : EntityActor(),
      NbTimesEventReached{},
      pZoneTrigger( nullptr ),
      PassedData( 0.0f ),
      pLEDData( nullptr ),
      pSoundGood( nullptr ),
      pSoundBad( nullptr ),
      WasIn( 0 ),
      RoadWidth( 0.0f ),
      Index( 0 ),
      serializedIndex( &Index ),
      pBinActors{ nullptr, nullptr },
      pInstanciatedActors{ nullptr, nullptr },
      AIComputedRoadIt() {
    // FUN_007f9830
}

EntityCheckPoint::~EntityCheckPoint() {
    // FUN_00424120
}

void EntityCheckPoint::initialize() {
    // FUN_007fa2e0
    EntityActor::initialize();

    AIComputedRoadIt.pRoad = nullptr;
    AIComputedRoadIt.bWay = true;
    AIComputedRoadIt.T = 0.0f;
    AIComputedRoadIt.IndEntity = -1;
    AIComputedRoadIt.IndWay = -1;

    OTDU_ASSERT( pLEDData );
    Index = pLEDData->Index;

    for ( int32_t i = 0; i < kNumEventSlots; i++ ) {
        NbTimesEventReached[i] = 0;
    }
}

void EntityCheckPoint::loadResources( ScenaricBank *param_2 ) {
    // FUN_007f9a70
    EntityActor::loadResources( param_2 );

    pZoneTrigger = gpScenaric->getSCVolume( pLEDData->ZoneTriggerHashcode );

    for ( int32_t i = 0; i < kNumGateActors; i++ ) {
        if ( pBinActors[i] != nullptr ) {
            ResolveEntityRef( pBinActors[i], param_2 );
        }
    }

    ResolveWavRef( pSoundGood );
    ResolveWavRef( pSoundBad );
}

void EntityCheckPoint::unloadResources() {
    // FUN_007f99c0
    Entity::unloadResources();
    Model.uninstanciate();
    animation.terminate();
}

void EntityCheckPoint::tick( GameMechanism *pGameMech,
                             float deltaTime,
                             float totalTime ) {
    // FUN_007f9ef0
    for ( int32_t i = 0; i < kNumGateActors; i++ ) {
        if ( pInstanciatedActors[i] != nullptr ) {
            pInstanciatedActors[i]->tick( pGameMech, deltaTime, totalTime );
        }
    }

    if ( ( flags & 2 ) == 0 ) {
        return;
    }

    EntityActor::tick( pGameMech, deltaTime, totalTime );
    if ( pGameMech == nullptr ) {
        return;
    }

    GameModeWorld *pOwner = pGameMech->getOwner();
    if ( pOwner == nullptr || ( pOwner->getGameModeFlags() & 1 ) != 0 ) {
        return;
    }

    const EntityVolume *pZone =
        static_cast<const EntityVolume *>( pZoneTrigger );
    switch ( pOwner->getGameMechType() ) {
    case 4: {
        CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
        if ( pLocalCar == nullptr || pZone == nullptr ) {
            return;
        }
        const Eigen::Vector4f position =
            pLocalCar->getBody()->WorldMatrix.row( 3 ).transpose();
        const Eigen::Vector4f previousPosition =
            pLocalCar->getBody()->PreviousWorldMatrix.row( 3 ).transpose();
        if ( pZone->isPointInside( position, &previousPosition ) ) {
            CheckpointPassedParam param{ Index, 0u, 0 };
            pGameMech->onMessage( this, GameMechanism::kMsgCheckpointPassed,
                                  &param );
        }
        return;
    }
    case -1:
    case 0:
    case 1:
    case 2:
    case 3:
    case 6: {
        GameSession *pSession = pGameMech->getGameSession();
        if ( Index < 0 || pSession == nullptr ) {
            return;
        }
        const int32_t capacity = pSession->getCapacity();
        for ( int32_t slot = 0; slot < capacity; slot++ ) {
            GameParticipant *pParticipant = pSession->getParticipant( slot );
            if ( pParticipant == nullptr || !pParticipant->isActive() ||
                 pParticipant->getCarState() == nullptr ||
                 isAlreadyPassed( pGameMech, slot ) ) {
                continue;
            }

            CarState *pCarState = pParticipant->getCarState();
            if ( !IsCarRaceEligible( pCarState ) || pZone == nullptr ) {
                continue;
            }
            const Eigen::Vector4f position =
                pCarState->getBody()->WorldMatrix.row( 3 ).transpose();
            const Eigen::Vector4f previousPosition =
                pCarState->getBody()->PreviousWorldMatrix.row( 3 ).transpose();
            if ( pZone->isPointInside( position, &previousPosition ) ) {
                CheckpointPassedParam param{ Index, pCarState->getID(), slot };
                pGameMech->onMessage( this, GameMechanism::kMsgCheckpointPassed,
                                      &param );
            }
        }
        return;
    }
    default:
        return;
    }
}

bool EntityCheckPoint::isAlreadyPassed( GameMechanism *pGameMech,
                                        const int32_t slot ) const {
    // FUN_007f95b0
    GameMechRaceHUD *pRaceHUD = static_cast<GameMechRaceHUD *>(
        pGameMech->getComponent( GameMechanism::CS_RaceHUD ) );
    if ( pRaceHUD == nullptr ) {
        return false;
    }

    GameParticipant *pParticipant =
        pGameMech->getGameSession()->getParticipant( slot );
    const int32_t lap = ( pParticipant != nullptr )
                            ? pParticipant->getSessionParams().CurrentLap
                            : 0;
    if ( lap == 0 ) {
        return NbTimesEventReached[slot] > 0;
    }

    const int32_t loopCheckpointIndex =
        int32_t( gpActiveGM->getMissionDescription()
                     ->getLoopCheckpointIndex()
                     .getValue() );
    if ( Index == loopCheckpointIndex ) {
        return !pRaceHUD->isOnLapCheckpoint( slot );
    }
    if ( Index > loopCheckpointIndex ) {
        return NbTimesEventReached[slot] >= lap;
    }
    return true;
}

bool EntityCheckPoint::draw( RenderScene *pScene ) {
    // FUN_007f9a00
    if ( ( flags & 1 ) && ( flags & 4 ) ) {
        if ( !bLightingValid ) {
            updateSkinningMatrices();
        }

        Model.draw( pScene );
    }

    if ( ( flags & 1 ) && ( flags & 4 ) ) {
        for ( int32_t i = 0; i < kNumGateActors; i++ ) {
            if ( pInstanciatedActors[i] != nullptr ) {
                pInstanciatedActors[i]->draw( pScene );
            }
        }
    }

    return true;
}

void EntityCheckPoint::deserialize( BinaryReader *param_2 ) {
    // FUN_007f9930
    EntityActor::deserialize( param_2 );

    pLEDData = reinterpret_cast<SerializedCheckPoint *>( param_2->pCursor );
    param_2->pCursor += sizeof( SerializedCheckPoint );

    const uint32_t version = gpScenaric->getVersion();
    if ( version > 0x17 ) {
        for ( int32_t i = 0; i < kNumGateActors; i++ ) {
            pBinActors[i] = ReadStreamRef( param_2 );
        }
    }

    if ( version > 0x0e ) {
        pSoundGood = ReadOptionalWavRef( param_2 );
        pSoundBad = ReadOptionalWavRef( param_2 );
    }
}

void EntityCheckPoint::propagateTransform( Matrix4x4f_t &transformMatrix,
                                           bool param_1 ) {
    // FUN_007f9c40
    Matrix4x4f_t checkpointMatrix = transformMatrix;
    AiRoadProfile *pProfile = nullptr;

    if ( ( pInstanciatedActors[0] != nullptr ||
           pInstanciatedActors[1] != nullptr ) &&
         ProjectFrameOnRoad( transformMatrix, checkpointMatrix, &pProfile,
                             true ) &&
         pProfile != nullptr ) {
        const Eigen::Vector4f up( 0.0f, 1.0f, 0.0f, 0.0f );
        checkpointMatrix.row( 1 ) = up.transpose();
        checkpointMatrix.row( 2 ) =
            Vector4DCross( checkpointMatrix.row( 0 ).transpose(), up )
                .transpose();

        const float gateOffset =
            ( pProfile->OuterWidth + pProfile->InnerWidth ) * 0.5f * 0.5f -
            kGateRoadEdgeMargin;
        const Eigen::Vector4f lateral =
            checkpointMatrix.row( 0 ).transpose() * gateOffset;

        for ( int32_t i = 0; i < kNumGateActors; i++ ) {
            Entity *pGate = pInstanciatedActors[i];
            if ( pGate == nullptr ) {
                continue;
            }

            Matrix4x4f_t gateMatrix = checkpointMatrix;
            if ( i == 0 ) {
                gateMatrix.row( 3 ) -= lateral.transpose();
            } else {
                gateMatrix.row( 3 ) += lateral.transpose();
            }
            pGate->propagateTransform( gateMatrix, param_1 );

            if ( pGate->getMechanism() != nullptr ) {
                pGate->getMechanism()->setInitialTransformMatrix( gateMatrix );
            }

            HiearchyNode *pRootNode = static_cast<SerializedSkinnedModel *>(
                                          pGate->getGeometryData() )
                                          ->pHiearchNode;
            if ( pRootNode == nullptr ) {
                continue;
            }
            HiearchyNode *pHiddenNode =
                pRootNode->findChild( GetIdentifier64bit( "R_D" ) );
            if ( pHiddenNode == nullptr ) {
                pHiddenNode =
                    pRootNode->findChild( GetIdentifier64bit( "R_G" ) );
            }
            if ( pHiddenNode != nullptr ) {
                pHiddenNode->setAndPropagateFlags( 4 );
            }
        }
    }

    if ( pZoneTrigger != nullptr ) {
        if ( pProfile != nullptr ) {
            EntityVolume *pZone = static_cast<EntityVolume *>( pZoneTrigger );
            Matrix4x4f_t zoneMatrix = pZone->getLocalTransform().inverse();
            zoneMatrix( 0, 0 ) = pProfile->OuterWidth + kZoneRoadEdgeMargin;
            pZone->setTransform( zoneMatrix, true );
        }
        PropagateTransformToChild( pZoneTrigger, this, checkpointMatrix, true );
    }

    Entity::propagateTransform( checkpointMatrix, param_1 );
    unknownActorFunc();
}

void EntityCheckPoint::propagatePosition( Eigen::Vector4f &position,
                                          bool param_1 ) {
    // FUN_007f97f0
    if ( pZoneTrigger != nullptr ) {
        PropagatePositionToChild( pZoneTrigger, this, position, true );
    }

    Entity::propagatePosition( position, param_1 );
    unknownActorFunc();
}

void EntityCheckPoint::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_007f9b70
    EntityActor::instanciate( pMngActor, param_1 );

    EntityCheckPoint *pInstance = static_cast<EntityCheckPoint *>( param_1 );
    pInstance->pLEDData = pLEDData;

    if ( pZoneTrigger != nullptr ) {
        pInstance->pZoneTrigger = InstanciateEntity( pMngActor, pZoneTrigger );
    }

    for ( int32_t i = 0; i < kNumGateActors; i++ ) {
        pInstance->pBinActors[i] = nullptr;

        Entity *pTemplate = ( pBinActors[i] != nullptr )
                                ? *reinterpret_cast<Entity **>( pBinActors[i] )
                                : nullptr;
        pInstance->pInstanciatedActors[i] =
            InstanciateEntity( nullptr, pTemplate );
    }

    pInstance->pSoundGood = pSoundGood;
    pInstance->pSoundBad = pSoundBad;
}

void EntityCheckPoint::deinstanciate() {
    // FUN_007f9b10
    EntityActor::deinstanciate();

    if ( pZoneTrigger != nullptr ) {
        MngActor::Uninstanciate( nullptr, pZoneTrigger );
        pZoneTrigger = nullptr;
    }

    for ( int32_t i = 0; i < kNumGateActors; i++ ) {
        if ( pInstanciatedActors[i] != nullptr ) {
            MngActor::Uninstanciate( nullptr, pInstanciatedActors[i] );
        }
    }
}

int32_t EntityCheckPoint::gmeEditionFactory( const int32_t controlIndex,
                                             const int32_t request,
                                             void *pData ) {
    // FUN_007f9690
    if ( request == GCR_NumControls ) {
        return 2;
    }
    if ( controlIndex < 1 ) {
        return Entity::gmeEditionFactory( controlIndex, request, pData );
    }
    if ( controlIndex != 1 ) {
        return 0;
    }
    return GMEControlAttribute( &serializedIndex, 1u, request, pData );
}

void EntityCheckPoint::onWorldStreamed() {
    // FUN_007f97c0
    EntityActor::onWorldStreamed();

    for ( int32_t i = 0; i < kNumGateActors; i++ ) {
        if ( pInstanciatedActors[i] != nullptr ) {
            pInstanciatedActors[i]->onWorldStreamed();
        }
    }
}
