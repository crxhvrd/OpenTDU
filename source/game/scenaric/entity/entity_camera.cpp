#include "shared.h"
#include "entity_camera.h"

#include "filesystem/binary.h"

EntityCamera::EntityCamera() : Entity(), FOV( 0.0f ), Ratio( 0.0f ) {
    // FUN_00424360
}

EntityCamera::~EntityCamera() {
    // FUN_00424120
}

void EntityCamera::deserialize( BinaryReader *param_2 ) {
    // FUN_009d8a00
    Entity::deserialize( param_2 );

    FOV = *reinterpret_cast<float *>( param_2->pCursor );
    param_2->pCursor += sizeof( float );

    Ratio = *reinterpret_cast<float *>( param_2->pCursor );
    param_2->pCursor += sizeof( float );
}

void EntityCamera::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_009d89d0
    Entity::instanciate( pMngActor, param_1 );

    EntityCamera *pInstance = static_cast<EntityCamera *>( param_1 );
    pInstance->FOV = FOV;
    pInstance->Ratio = Ratio;
}
