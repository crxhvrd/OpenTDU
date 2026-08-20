#include "shared.h"
#include "entity_avatar_hitch_hiker.h"

#include "game/scenaric/gs_scenaric.h"

EntityAvatarHitchHiker::EntityAvatarHitchHiker()
    : EntityAvatarPassenger(),
      RandomSeed( 0x3039 ),
      CumulatedProba( 0.0f ),
      CurrentFreerideAnim( -1 ),
      bPlaySound( false ) {
    // FUN_007f6bd0
    OTDU_UNIMPLEMENTED;
}

EntityAvatarHitchHiker::~EntityAvatarHitchHiker() {
    // FUN_00424930
    OTDU_UNIMPLEMENTED;
}

void EntityAvatarHitchHiker::loadResources( ScenaricBank *param_2 ) {
    // FUN_007f6d10
    EntityAvatarPassenger::loadResources( param_2 );

    if ( AnimHi.File != 0 ) {
        AnimHi.pClip = gpScenaric->resolveClip( param_2, AnimHi.pRefBas );
    }

    if ( AnimPeepin.File != 0 ) {
        AnimPeepin.pClip =
            gpScenaric->resolveClip( param_2, AnimPeepin.pRefBas );
    }
}

void EntityAvatarHitchHiker::deserialize( BinaryReader *param_2 ) {
    // FUN_007f6cc0
    deserializeAnims( param_2, 0x1b );
}

void EntityAvatarHitchHiker::instanciate( MngActor *pMngActor,
                                          Entity *param_1 ) {
    // FUN_007f6c70
    EntityAvatarPassenger::instanciate( pMngActor, param_1 );

    EntityAvatarHitchHiker *pTarget =
        static_cast<EntityAvatarHitchHiker *>( param_1 );

    pTarget->AnimHi.ID = AnimHi.ID;
    pTarget->AnimHi.File = AnimHi.File;
    pTarget->AnimHi.pRefBas = AnimHi.pRefBas;

    pTarget->AnimPeepin.ID = AnimPeepin.ID;
    pTarget->AnimPeepin.File = AnimPeepin.File;
    pTarget->AnimPeepin.pRefBas = AnimPeepin.pRefBas;
}
