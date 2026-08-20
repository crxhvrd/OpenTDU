#include "shared.h"
#include "entity_avatar_convoy.h"

#include "game/scenaric/gs_scenaric.h"

EntityAvatarConvoy::EntityAvatarConvoy()
    : EntityAvatarPassenger(), CurrentFreerideAnim( -1 ) {
    // FUN_007f6fe0
    OTDU_UNIMPLEMENTED;
}

EntityAvatarConvoy::~EntityAvatarConvoy() {
    // FUN_00424900
}

void EntityAvatarConvoy::loadResources( ScenaricBank *param_2 ) {
    // FUN_007f70c0
    EntityAvatarPassenger::loadResources( param_2 );

    if ( AnimHi.File != 0 ) {
        AnimHi.pClip = gpScenaric->resolveClip( param_2, AnimHi.pRefBas );
    }

    if ( AnimPeepin.File != 0 ) {
        AnimPeepin.pClip =
            gpScenaric->resolveClip( param_2, AnimPeepin.pRefBas );
    }
}

void EntityAvatarConvoy::deserialize( BinaryReader *param_2 ) {
    // FUN_007f7070
    deserializeAnims( param_2, 0x1c );
}

void EntityAvatarConvoy::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_007f7020
    EntityAvatarPassenger::instanciate( pMngActor, param_1 );

    EntityAvatarConvoy *pTarget = static_cast<EntityAvatarConvoy *>( param_1 );

    pTarget->AnimHi.ID = AnimHi.ID;
    pTarget->AnimHi.File = AnimHi.File;
    pTarget->AnimHi.pRefBas = AnimHi.pRefBas;

    pTarget->AnimPeepin.ID = AnimPeepin.ID;
    pTarget->AnimPeepin.File = AnimPeepin.File;
    pTarget->AnimPeepin.pRefBas = AnimPeepin.pRefBas;
}
