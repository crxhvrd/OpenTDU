#include "shared.h"
#include "entity_avatar_transporter.h"

#include "game/scenaric/gs_scenaric.h"

EntityAvatarTransporter::EntityAvatarTransporter()
    : EntityAvatarPassenger(), CurrentFreerideAnim( -1 ) {
    // FUN_007f2da0
    OTDU_UNIMPLEMENTED;
}

EntityAvatarTransporter::~EntityAvatarTransporter() {
    // FUN_00424900
    OTDU_UNIMPLEMENTED;
}

void EntityAvatarTransporter::loadResources( ScenaricBank *param_2 ) {
    // FUN_007f2e80
    EntityAvatarPassenger::loadResources( param_2 );

    if ( AnimHi.File != 0 ) {
        AnimHi.pClip = gpScenaric->resolveClip( param_2, AnimHi.pRefBas );
    }

    if ( AnimPeepin.File != 0 ) {
        AnimPeepin.pClip =
            gpScenaric->resolveClip( param_2, AnimPeepin.pRefBas );
    }
}

void EntityAvatarTransporter::deserialize( BinaryReader *param_2 ) {
    // FUN_007f2e30
    deserializeAnims( param_2, 0x1d );
}

void EntityAvatarTransporter::instanciate( MngActor *pMngActor,
                                           Entity *param_1 ) {
    // FUN_007f2de0
    EntityAvatarPassenger::instanciate( pMngActor, param_1 );

    EntityAvatarTransporter *pTarget =
        static_cast<EntityAvatarTransporter *>( param_1 );

    pTarget->AnimHi.ID = AnimHi.ID;
    pTarget->AnimHi.File = AnimHi.File;
    pTarget->AnimHi.pRefBas = AnimHi.pRefBas;

    pTarget->AnimPeepin.ID = AnimPeepin.ID;
    pTarget->AnimPeepin.File = AnimPeepin.File;
    pTarget->AnimPeepin.pRefBas = AnimPeepin.pRefBas;
}
