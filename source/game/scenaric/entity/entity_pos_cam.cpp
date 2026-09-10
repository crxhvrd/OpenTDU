#include "shared.h"
#include "entity_pos_cam.h"

#include "game/scenaric/gs_scenaric.h"
#include "filesystem/binary.h"

EntityPosCam::EntityPosCam()
    : Entity(),
      pPathRef( nullptr ),
      pEntityRefs{},
      pZoneVolumeRef( nullptr ),
      pLateEntityRef( nullptr ) {
    // FUN_007eab40
}

EntityPosCam::~EntityPosCam() {
    // FUN_00424120
}

void EntityPosCam::loadResources( ScenaricBank *param_2 ) {
    // FUN_007ea910
    for ( int32_t i = 0; i < kNumEntityRefs; i++ ) {
        if ( pEntityRefs[i] != nullptr ) {
            ResolveEntityRef( pEntityRefs[i], param_2 );
        }
    }

    if ( pLateEntityRef != nullptr ) {
        ResolveEntityRef( pLateEntityRef, param_2 );
    }

    ResolvePathBlock( pPathRef, param_2 );
    ResolveVolumeBlock( pZoneVolumeRef, param_2 );
}

void EntityPosCam::deserialize( BinaryReader *param_2 ) {
    // FUN_007eaa10
    Entity::deserialize( param_2 );

    const uint32_t version = gpScenaric->getVersion();

    pPathRef = ReadStreamRef( param_2 );
    pEntityRefs[0] = ReadStreamRef( param_2 );

    if ( version >= 0x0b ) {
        pEntityRefs[1] = ReadStreamRef( param_2 );
    }

    if ( version >= 0x0e ) {
        pEntityRefs[2] = ReadStreamRef( param_2 );

        if ( version < 0x16 ) {
            pEntityRefs[3] = ReadStreamRef( param_2 );
        } else {
            pEntityRefs[3] = nullptr;
            pLateEntityRef = ReadStreamRef( param_2 );
        }

        pEntityRefs[4] = ReadStreamRef( param_2 );
        pEntityRefs[5] = ReadStreamRef( param_2 );
    }

    if ( version < 0x15 ) {
        Mode = 1;
    } else {
        pEntityRefs[6] = ReadStreamRef( param_2 );
        pEntityRefs[7] = ReadStreamRef( param_2 );
        pEntityRefs[8] = ReadStreamRef( param_2 );
        pEntityRefs[9] = ReadStreamRef( param_2 );
        Mode = static_cast<int32_t>( ReadStreamU32( param_2 ) );
    }

    pZoneVolumeRef = ReadStreamRef( param_2 );
}

void EntityPosCam::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_007ea850
    Entity::instanciate( pMngActor, param_1 );

    EntityPosCam *pTarget = static_cast<EntityPosCam *>( param_1 );
    pTarget->pPathRef = pPathRef;
    for ( int32_t i = 0; i < kNumEntityRefs; i++ ) {
        pTarget->pEntityRefs[i] = pEntityRefs[i];
    }
    pTarget->pZoneVolumeRef = pZoneVolumeRef;
    pTarget->pLateEntityRef = pLateEntityRef;
}
