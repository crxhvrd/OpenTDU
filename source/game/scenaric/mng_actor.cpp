#include "shared.h"
#include "mng_actor.h"

#include "ai/ai_road_query.h"
#include "bank_scene_arrays.h"
#include "entity/base_entity.h"
#include "entity/entity_car_player.h"
#include "entity/entity_factory.h"
#include "entity/entity_volume.h"
#include "game/gm_scene.h"
#include "render/gs_render.h"
#include "world/mng_zone.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "game/scenaric/scenaric_bank.h"
#include "game/scenaric/local_player.h"
#include "game/vehicle/car_state.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_save.h"
#include "world/gs_world.h"
#include "world/streaming_manager.h"

static inline bool IsGameModeRequestingDOFPass() {
    return gpActiveGMScene != nullptr && gpActiveGMScene->isDrawnInFakeDOF();
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

void MngActor::tick( float deltaTime, float totalTime ) {
    // FUN_006de8b0
    for ( int32_t i = 0; i < numActors; i++ ) {
        pActors[i]->tick( pGameMech, deltaTime, totalTime );
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

Entity *MngActor::findActorByName( const char *pName ) const {
    // FUN_006de9a0
    for ( int32_t i = 0; i < numActors; i++ ) {
        if ( pActors[i]->getInstanceName() == pName ) {
            return pActors[i];
        }
    }
    return nullptr;
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

static constexpr float kHouseSpawnRoadBackOff = 10.0f;

static Matrix4x4f_t BuildSpawnFrame( const Eigen::Vector4f &position ) {
    Matrix4x4f_t frame;
    frame.row( 0 ) << -1.0f, 0.0f, 0.0f, 0.0f;
    frame.row( 1 ) << 0.0f, 1.0f, 0.0f, 0.0f;
    frame.row( 2 ) << 0.0f, 0.0f, -1.0f, 0.0f;
    frame.row( 3 ) = position.transpose();
    return frame;
}

static const SpotMap *FindCurrentHouseSpot() {
    Spots &spots = gpScenaric->getSpotList();
    const uint64_t houseHashcode =
        gpPlayerData->getLoadedSave()->getDriver()->getCurrentHouse();
    return spots.getWorldSpotChecked( spots.getSpotMapIndex( houseHashcode ) );
}

Entity *MngActor::findSpawnCarPark( const int32_t spawnMode ) const {
    // FUN_006df570 / FUN_006df7d0 (inlined)
    for ( int32_t i = 0; i < numActors; i++ ) {
        Entity *pActor = pActors[i];
        if ( pActor->getType() != eEntityType::ET_CARP ) {
            continue;
        }

        const EntityCarPlayer *pCarPark =
            static_cast<const EntityCarPlayer *>( pActor );
        if ( spawnMode == 0 || pCarPark->getSpawnTag() == spawnMode ) {
            return pActor;
        }
    }
    return nullptr;
}

static void SetPlayerSpawnPosition( const Eigen::Vector4f &position ) {
    Eigen::Vector4f streamingPosition = position;
    gpWorld->setPlayerPosition( &position );
    gpWorld->setStreamingPosition( &streamingPosition );
}

void MngActor::preparePlayerSpawnStreaming( const int32_t spawnMode ) {
    // FUN_006df570
    if ( spawnMode == 8 ) {
        if ( gpScenaric->hasDefaultStartPosition() ) {
            SetPlayerSpawnPosition( gpScenaric->getDefaultStartPosition() );
            return;
        }

        const SpotMap *pHouse = FindCurrentHouseSpot();
        if ( pHouse != nullptr ) {
            Matrix4x4f_t frame;
            ComputeRoadApproachFrame(
                Eigen::Vector4f( pHouse->X, pHouse->Y, pHouse->Z, 1.0f ), frame,
                kHouseSpawnRoadBackOff );
            SetPlayerSpawnPosition( frame.row( 3 ).transpose() );
            return;
        }
    } else {
        if ( spawnMode == 4 && gpScenaric->wasMapExited() ) {
            return;
        }
        if ( spawnMode == 6 || spawnMode == 7 ) {
            return;
        }

        const Entity *pCarPark = findSpawnCarPark( spawnMode );
        if ( pCarPark != nullptr ) {
            gpWorld->setPreviousTransform( pCarPark->getModelMatrix() );
            Eigen::Vector4f streamingPosition =
                pCarPark->getModelMatrix().row( 3 ).transpose();
            gpWorld->setStreamingPosition( &streamingPosition );
            return;
        }

        if ( gpScenaric->hasDefaultStartPosition() ) {
            SetPlayerSpawnPosition( gpScenaric->getDefaultStartPosition() );
            return;
        }
    }

    SetPlayerSpawnPosition( kDefaultPosition );
}

bool MngActor::IsInSpotDetectionVolume( const Matrix4x4f_t *pFrame ) {
    // FUN_006deb80
    Matrix4x4f_t frame;
    if ( pFrame == nullptr ) {
        CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
        PhysicsVehicle *pPhysicsVehicle =
            ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;
        if ( pPhysicsVehicle == nullptr ) {
            return false;
        }
        frame = pPhysicsVehicle->getChassisWorldMatrix();
    } else {
        frame = *pFrame;
    }
    const Eigen::Vector4f position = frame.row( 3 ).transpose();

    for ( SpotCell &cell : gpWorld->getStreamingManager()->spotStream.cells ) {
        ScenaricBank *pBank = cell.spot.pSCBank;
        if ( cell.pSector == nullptr || cell.pSpotSector == nullptr ||
             pBank == nullptr ) {
            continue;
        }

        for ( int32_t i = 0; i < pBank->getEntityCount(); i++ ) {
            Entity *pEntity = pBank->getEntity( i );
            if ( pEntity == nullptr ||
                 pEntity->getType() != eEntityType::ET_VELU ||
                 pEntity->getInstanceName() != "Spot_Detection" ) {
                continue;
            }

            Matrix4x4f_t volumeMatrix =
                pEntity->getModelMatrix() * pBank->getBankTransform();

            EntityVolume detectionVolume;
            detectionVolume.setVolumeType( VT_Box );
            detectionVolume.propagateTransform( volumeMatrix, true );
            if ( detectionVolume.isPointInside( position, nullptr ) ) {
                return true;
            }
        }
    }
    return false;
}

static constexpr const char *kPlayerParkingName =
    "PosCarParking"; // DAT_00f8e5b8

Matrix4x4f_t MngActor::placePlayerVehicle( const int32_t spawnMode,
                                           const bool bApply ) {
    // FUN_006df7d0
    Matrix4x4f_t frame = Matrix4x4f_t::Identity();

    if ( spawnMode == 8 ) {
        if ( gpScenaric->hasDefaultStartPosition() ) {
            frame = BuildSpawnFrame( gpScenaric->getDefaultStartPosition() );
        } else {
            const SpotMap *pHouse = FindCurrentHouseSpot();
            if ( pHouse != nullptr ) {
                ComputeRoadApproachFrame(
                    Eigen::Vector4f( pHouse->X, pHouse->Y, pHouse->Z, 1.0f ),
                    frame, kHouseSpawnRoadBackOff );
            } else {
                frame = BuildSpawnFrame( kDefaultPosition );
            }
        }
        applyPlayerVehicleFrame( frame, bApply );
        return frame;
    }

    if ( spawnMode == 7 ) {
        frame = gpWorld->getPreviousTransform();
        if ( !bApply ) {
            return frame;
        }

        CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
        PhysicsVehicle *pVehicle =
            ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;
        if ( pVehicle != nullptr && !IsInSpotDetectionVolume( &frame ) &&
             !pVehicle->isInWater() ) {
            pVehicle->resetToTransform( frame, true );
        }
        return frame;
    }

    Spots &spots = gpScenaric->getSpotList();
    SpotMap *pSpot =
        ( spawnMode == 6 )
            ? spots.getWorldSpotChecked( gpScenaric->getCurrentSpotIndex() )
            : nullptr;
    if ( pSpot == nullptr ) {
        Entity *pCarPark = findSpawnCarPark( spawnMode );
        if ( pCarPark == nullptr ) {
            frame = BuildSpawnFrame( gpScenaric->hasDefaultStartPosition()
                                         ? gpScenaric->getDefaultStartPosition()
                                         : kDefaultPosition );
            applyPlayerVehicleFrame( frame, bApply );
            return frame;
        }

        if ( gpScenaric->getSelectedEventParameters().getSource() ==
             EventParameters::ES_HardcodedMission ) {
            pCarPark->setTransformFromDesc();
        }
        frame = pCarPark->getModelMatrix();
        frame.row( 3 ).head<3>() += Eigen::Vector3f( 0.0f, 1.0f, 0.0f );
        applyPlayerVehicleFrame( frame, bApply );
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
                           (unsigned long long)pSpot->Hashcode, frame( 3, 0 ),
                           frame( 3, 1 ), frame( 3, 2 ) );
            applyPlayerVehicleFrame( frame, bApply );
            return frame;
        }
    }

    SnapToRoadFrame( fallbackPosition, frame,
                     eRoadSnapMode::RSM_KeepAwayFromEnds, true, true );
    OTDU_LOG_WARN( "Player spawn: no '%s' marker for spot 0x%016llx (bank %s) "
                   "- snapped the car to the road at %.1f %.1f %.1f\n",
                   kPlayerParkingName, (unsigned long long)pSpot->Hashcode,
                   ( pSector != nullptr && FindStreamedSpotBank( pSector ) )
                       ? "streamed"
                       : "NOT streamed",
                   frame( 3, 0 ), frame( 3, 1 ), frame( 3, 2 ) );
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
        pVehicle->resetToTransform( frame, true );
    }
}

Entity *MngActor::instanciateActor( Entity *pEntity ) {
    return InstanciateEntity( this, pEntity );
}

Entity *MngActor::InstanciateEntity( MngActor *pMngActor, Entity *pTemplate ) {
    // FUN_006def70
    if ( pTemplate == nullptr ) {
        return nullptr;
    }

    const eEntityType type = pTemplate->getType();
    EntityAllocation allocation = EntityFactory::DoAction(
        nullptr, type, nullptr, eInstantiationFlag::IF_CreateEntity );

    Entity *pInstance = allocation.pEntity;
    if ( pInstance != nullptr ) {
        pTemplate->instanciate( pMngActor, pInstance );
        pInstance->initializeInstance( pMngActor, pTemplate );
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
