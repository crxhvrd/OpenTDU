#include "shared.h"
#include "entity_321go.h"

#include "game/scenaric/gs_scenaric.h"
#include "filesystem/binary.h"

Entity321Go::Entity321Go()
    : Entity(),
      Field0( 0 ),
      pPathList( nullptr ),
      pPathRef1( nullptr ),
      pPathRef2( nullptr ) {
    // FUN_009da130
}

Entity321Go::~Entity321Go() {
    // FUN_00424120
}

void Entity321Go::loadResources( ScenaricBank *param_2 ) {
    // FUN_009d3cd0
    (void)param_2;

    ResolvePathList( pPathList );
    ResolvePathBlock( pPathRef1 );
    ResolvePathBlock( pPathRef2 );
}

void Entity321Go::deserialize( BinaryReader *param_2 ) {
    // FUN_009d3d70
    Entity::deserialize( param_2 );

    Field0 = *reinterpret_cast<uint32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint32_t );

    pPathList = reinterpret_cast<uint32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint32_t );
    if ( *pPathList != 0 ) {
        param_2->pCursor += *pPathList * 8;
    }

    pPathRef1 = param_2->pCursor;
    param_2->pCursor += 8;

    pPathRef2 = param_2->pCursor;
    param_2->pCursor += 8;
}

void Entity321Go::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_009d3d30
    Entity::instanciate( pMngActor, param_1 );

    Entity321Go *pInstance = static_cast<Entity321Go *>( param_1 );
    pInstance->Field0 = Field0;
    pInstance->pPathList = pPathList;
    pInstance->pPathRef1 = pPathRef1;
    pInstance->pPathRef2 = pPathRef2;
}
