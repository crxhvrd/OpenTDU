#include "shared.h"
#include "mng_actor.h"

#include "bank_scene_arrays.h"
#include "entity/base_entity.h"
#include "entity/entity_factory.h"
#include "game/gm_scene.h"
#include "render/gs_render.h"
#include "world/mng_zone.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "game/scenaric/scenaric_bank.h"
#include "game/scenaric/local_player.h"
#include "game/vehicle/car_state.h"
#include "physics/vehicle/physics_vehicle.h"

static inline bool IsGameModeRequestingDOFPass() {
    return gpActiveGMScene != nullptr && false;
}

MngActor::MngActor()
    : Manager(),
      StreamingCallback(),
      numActors( 0 ),
      pParent( nullptr ),
      pGameMech( nullptr ),
      bIsManager( false ),
      bRenderInCockpitRenderPass( false ) {
    // FUN_006deb10
    pActors.fill( nullptr );
}

MngActor::~MngActor() {
    // FUN_006deac0
}

void MngActor::tick( float, float ) {
    // FUN_006de8b0
    for ( int32_t i = 0; i < numActors; i++ ) {
        pActors[i]->tick();
    }
}

void MngActor::draw() {
    // FUN_006de8f0
    RenderScene *pScene = getRenderPass();
    for ( int32_t i = 0; i < numActors; i++ ) {
        Entity *pActor = pActors[i];
        if ( pActor->isStreamManaged() && !pActor->isStreamActive() ) {
            continue;
        }
        pActor->draw( pScene );
    }
}

void MngActor::terminate() {
    // FUN_006defd0
    int32_t i = 0;
    while ( i < numActors ) {
        Entity *pActor = pActors[i];
        if ( pActor != nullptr && pActor->getParent() != nullptr ) {
            Uninstanciate( this, pActor );
            i--;
        }
        i++;
    }
}

RenderScene *MngActor::getRenderPass() {
    // FUN_006dea60
    return gpRender->getRenderPass<eRenderPass::RP_Near>().pScene;
}

bool MngActor::onWorldStreamed( Eigen::Vector4f *param_1,
                                Eigen::Vector4f *param_2,
                                Eigen::Vector4f *param_3 ) {
    // FUN_006ded70
    if ( pParent != nullptr ) {
        pParent->shiftObjects( *param_3 );
    }

    for ( int32_t i = 0; i < numActors; i++ ) {
        pActors[i]->onWorldStreamed();
    }

    return true;
}

static constexpr const char *kPlayerParkingName = "PosCarParking"; // DAT_00f8e5b8 

Matrix4x4f_t MngActor::placePlayerVehicle( const int32_t spawnMode,
                                           const bool bApply ) {
    // FUN_006df7d0
    Matrix4x4f_t frame = Matrix4x4f_t::Identity();

    if ( spawnMode != 6 || gpScenaric == nullptr ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC (spawn modes 7 and 8)
        return frame;
    }

    Spots &spots = gpScenaric->getSpotList();
    SpotMap *pSpot =
        spots.getWorldSpotChecked( gpScenaric->getCurrentSpotIndex() );
    if ( pSpot == nullptr ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC (the no-spot fallback walks
                                 // the actor list for an ET_CARP, then falls
                                 // back to GSScenaric's default position)
        return frame;
    }

    Eigen::Vector4f fallbackPosition( pSpot->X, pSpot->Y, pSpot->Z, 1.0f );
    const SpotSector *pSector = spots.getSpotSector( pSpot->Hashcode );
    if ( pSector != nullptr ) {
        fallbackPosition =
            Eigen::Vector4f( pSector->X, pSector->Y, pSector->Z, 1.0f );

        ScenaricBank *pBank = FindStreamedSpotBank( pSector );
        Entity *pParking =
            ( pBank != nullptr )
                ? pBank->findEntity( eEntityType::ET_CARP, kPlayerParkingName )
                : nullptr;
        if ( pParking != nullptr ) {
            frame = pParking->getModelMatrix();
            OTDU_LOG_INFO( "Player spawn: '%s' of spot 0x%016llx at "
                           "%.1f %.1f %.1f\n",
                           kPlayerParkingName,
                           (unsigned long long)pSpot->Hashcode,
                           frame( 3, 0 ), frame( 3, 1 ), frame( 3, 2 ) );
            applyPlayerVehicleFrame( frame, bApply );
            return frame;
        }
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: GSAI (AiRoadMap snap for the spawn frame)
    OTDU_LOG_WARN( "Player spawn: no '%s' marker for spot 0x%016llx (bank %s) "
                   "- dropping the car at %.1f %.1f %.1f\n",
                   kPlayerParkingName, (unsigned long long)pSpot->Hashcode,
                   ( pSector != nullptr && FindStreamedSpotBank( pSector ) )
                       ? "streamed"
                       : "NOT streamed",
                   fallbackPosition.x(), fallbackPosition.y(),
                   fallbackPosition.z() );
    frame.col( 3 ) = fallbackPosition;
    applyPlayerVehicleFrame( frame, bApply );
    return frame;
}

void MngActor::applyPlayerVehicleFrame( const Matrix4x4f_t &frame,
                                        const bool bApply ) {
    // FUN_0083d900 
    if ( !bApply || gpScenaric == nullptr ) {
        return;
    }

    CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
    if ( pCarState == nullptr ) {
        return;
    }

    PhysicsVehicle *pVehicle = pCarState->getPhysicsVehicle();
    if ( pVehicle != nullptr ) {
        pVehicle->resetToTransform( frame );
    }
}

Entity *MngActor::instanciateActor( Entity *pEntity ) {
    // FUN_006def70
    const eEntityType type = pEntity->getType();
    EntityAllocation allocation = EntityFactory::DoAction(
        nullptr, type, nullptr, eInstantiationFlag::IF_CreateEntity );

    Entity *pInstance = allocation.pEntity;
    if ( pInstance != nullptr ) {
        pEntity->instanciate( this, pInstance );
        pInstance->initializeInstance( this, pEntity );
        pInstance->initialize();
    }
    return pInstance;
}

int32_t MngActor::refreshMissingModels() {
    int32_t rebuilt = 0;
    for ( int32_t i = 0; i < numActors; i++ ) {
        if ( pActors[i] != nullptr && pActors[i]->refreshModelFromTemplate() ) {
            rebuilt++;
        }
    }
    return rebuilt;
}

void MngActor::registerActor( Entity *pActor ) {
    // FUN_007fafe0 (inlined)
    if ( numActors >= kMaxNumEntity ) {
        return;
    }
    pActors[numActors] = pActor;
    numActors++;
}

void MngActor::removeActorFromList( Entity *pActor ) {
    // FUN_006de870
    if ( numActors <= 0 ) {
        return;
    }

    int32_t index = 0;
    while ( pActors[index] != pActor ) {
        index++;
        if ( index >= numActors ) {
            return;
        }
    }

    pActors[index] = pActors[numActors - 1];
    pActors[numActors - 1] = nullptr;
    numActors--;
}

void MngActor::Uninstanciate( MngActor *pMngActor, Entity *pActor ) {
    // FUN_006deed0
    if ( pActor == nullptr || pActor->getParent() == nullptr ) {
        return;
    }

    const eEntityType type = pActor->getType();
    if ( type == eEntityType::ET_VELU ) {
        if ( gpMngZone != nullptr ) {
            gpMngZone->unregisterVolume( pActor );
        }
    } else if ( type == eEntityType::ET_ZONE ) {
        if ( gpMngZone != nullptr ) {
            gpMngZone->unregisterZone( pActor );
        }
    } else if ( !Entity::IsAvatarEntityType( pActor ) &&
                pMngActor != nullptr ) {
        pMngActor->removeActorFromList( pActor );
    }

    pActor->unloadResources();
    pActor->deinstanciate();

    EntityFactory::DoAction( pActor, type, nullptr,
                             eInstantiationFlag::IF_DeleteEntity );
}

RenderScene *MngActorWorld::getRenderPass() {
    // FUN_006de980
    if ( IsGameModeRequestingDOFPass() ) {
        return gpRender->getRenderPass<eRenderPass::RP_FakeDOF>().pScene;
    }

    return gpRender->getRenderPass<eRenderPass::RP_Near>().pScene;
}
