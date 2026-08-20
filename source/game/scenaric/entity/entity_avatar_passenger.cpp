#include "shared.h"
#include "entity_avatar_passenger.h"

#include "entity_bytecode.h"
#include "filesystem/binary.h"
#include "game/scenaric/gs_scenaric.h"

EntityAvatarPassenger::EntityAvatarPassenger()
    : EntityAvatar(),
      pAnimCar(),
      NumAnimCar( 0 ),
      CurrentBehavior( 0 ),
      AnimHi(),
      AnimPeepin() {
    // FUN_007f4bd0
    OTDU_UNIMPLEMENTED;
}

EntityAvatarPassenger::~EntityAvatarPassenger() {
    // FUN_00424900
    OTDU_UNIMPLEMENTED;
}

void EntityAvatarPassenger::loadResources( ScenaricBank *param_2 ) {
    // FUN_007f5640
    EntityActor::loadResources( param_2 );

    for ( uint32_t i = 0; i < kNumAnimCar; i++ ) {
        if ( pAnimCar[i].File != 0 ) {
            pAnimCar[i].pClip =
                gpScenaric->resolveClip( param_2, pAnimCar[i].pRefBas );
        }
    }
}

void EntityAvatarPassenger::instanciate( MngActor *pMngActor,
                                         Entity *param_1 ) {
    // FUN_007f54e0
    EntityAvatar::instanciate( pMngActor, param_1 );

    EntityAvatarPassenger *pTarget =
        static_cast<EntityAvatarPassenger *>( param_1 );

    pTarget->NumAnimCar = NumAnimCar;
    for ( uint32_t i = 0; i < NumAnimCar && i < kNumAnimCar; i++ ) {
        pTarget->pAnimCar[i].ID = pAnimCar[i].ID;
        pTarget->pAnimCar[i].File = pAnimCar[i].File;
        pTarget->pAnimCar[i].pRefBas = pAnimCar[i].pRefBas;
    }
}

void EntityAvatarPassenger::deserializeAnims( BinaryReader *pReader,
                                              uint32_t minVersion ) {
    EntityActor::deserialize( pReader );
    NumAnimCar = DecodeBytecodeAnimCarTable( pReader, pAnimCar, kNumAnimCar );
    if ( gpScenaric->getVersion() > minVersion ) {
        AnimHi.deserialize( pReader );
        AnimPeepin.deserialize( pReader );
    }
}
