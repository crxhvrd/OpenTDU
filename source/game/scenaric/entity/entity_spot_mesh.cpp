#include "shared.h"
#include "entity_spot_mesh.h"

#include "core/maths_helper.h"
#include "filesystem/binary.h"
#include "game/gm_world.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "game/vehicle/car_state.h"
#include "tdu_instance.h"

EntitySpotMesh::EntitySpotMesh()
    : EntityActor(),
      pData( nullptr ),
      volumeRoad(),
      volumeSpot(),
      pSpotWorld( nullptr ),
      pSpotSector( nullptr ),
      bInZone( false ) {
    // FUN_004241c0
}

EntitySpotMesh::~EntitySpotMesh() {
    // FUN_00424220
}

void EntitySpotMesh::setupZoneVolumes() {
    // FUN_007d7b00 (inlined)
    if ( pSpotSector == nullptr ) {
        return;
    }

    Eigen::Vector3f roadZoneExtent( kRoadZoneExtentX, kRoadZoneExtentY,
                                    kRoadZoneExtentZ );
    Matrix4x4f_t roadZone = MakeScaleMatrix( roadZoneExtent );
    roadZone.row( 3 ) =
        Eigen::Vector4f( pSpotSector->X, pSpotSector->Y, pSpotSector->Z, 1.0f );

    volumeRoad.propagateTransform( roadZone, true );
    volumeRoad.setVolumeType( VT_Sphere );

    if ( pSpotWorld != nullptr && pSpotWorld->Type == ST_Challenge ) {
        volumeSpot.propagateTransform( roadZone, true );
        volumeSpot.setVolumeType( VT_Sphere );
    } else {
        volumeSpot.setVolumeType( VT_Box );

        Matrix4x4f_t spotZone = pSpotSector->Zone;
        volumeSpot.propagateTransform( spotZone, true );
    }
}

static bool IsFreeRideActive() {
    return gpTestDriveInstance != nullptr &&
           gpTestDriveInstance->getActiveGameMode() == GM_FreeRide;
}

void EntitySpotMesh::notifySpotZoneEvent( eSpotZoneEvent eventCode ) {
    // FUN_009d9c10 / FUN_009d9b30 (inlined)
    (void)eventCode;

    if ( !IsFreeRideActive() ) {
        return;
    }
    if ( gpActiveGM == nullptr || gpActiveGM->getGameMech() == nullptr ) {
        return;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: GAMELOGIC
}

void EntitySpotMesh::unloadResources() {
    // FUN_009d9b30
    Entity::unloadResources();
    Model.uninstanciate();
    animation.terminate();

    if ( bInZone ) {
        notifySpotZoneEvent( SZE_Unloaded );
        bInZone = false;
    }
}

void EntitySpotMesh::tick() {
    // FUN_009d9c10
    EntityActor::tick();

    const CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr ) {
        return;
    }

    const uint32_t entryFlags = flags;
    if ( ( entryFlags & kSpotCandidateFlag ) == 0 ) {
        return;
    }

    const CarBodyState *pBody = pLocalCar->getBody();
    const Eigen::Vector4f worldPosition = pBody->WorldMatrix.row( 3 );
    const Eigen::Vector4f previousWorldPosition =
        pBody->PreviousWorldMatrix.row( 3 );
    const bool bArrested = pLocalCar->getPoliceState().isArrested();

    if ( pSpotWorld != nullptr ) {
        const bool bAvailable = IsSpotAvailableInCurrentMode( pSpotWorld );
        const bool bWasAvailable = ( entryFlags & kSpotAvailableFlag ) != 0;

        if ( bAvailable && !bWasAvailable ) {
            flags = entryFlags | kSpotAvailableFlag;
        } else if ( !bAvailable && bWasAvailable ) {
            flags = entryFlags & ~kSpotAvailableFlag;

            if ( bInZone ) {
                notifySpotZoneEvent( SZE_Left );
                bInZone = false;
            }
        }
    }

    if ( ( flags & kSpotAvailableFlag ) == 0 ) {
        return;
    }

    const bool bInsideZone =
        volumeRoad.isPointInside( worldPosition, &previousWorldPosition ) ||
        volumeSpot.isPointInside( worldPosition, &previousWorldPosition );

    if ( !bInsideZone || bArrested || !IsFreeRideActive() ) {
        if ( bInZone ) {
            notifySpotZoneEvent( SZE_Left );
            bInZone = false;
        }
        return;
    }

    notifySpotZoneEvent( SZE_Entered );
    bInZone = true;
}

void EntitySpotMesh::deserialize( BinaryReader *param_2 ) {
    // FUN_009d9be0
    EntityActor::deserialize( param_2 );

    pData = reinterpret_cast<SerializedData *>( param_2->pCursor );
    param_2->pCursor += sizeof( SerializedData );
}

void EntitySpotMesh::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_009d9bb0
    EntityActor::instanciate( pMngActor, param_1 );

    static_cast<EntitySpotMesh *>( param_1 )->pData = pData;
}
