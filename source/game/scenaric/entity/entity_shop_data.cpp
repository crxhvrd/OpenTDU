#include "shared.h"
#include "entity_shop_data.h"

#include "game/scenaric/gs_scenaric.h"
#include "filesystem/binary.h"

EntityShopData::EntityShopData()
    : Entity(),
      pPathRef( nullptr ),
      pEntityRef( nullptr ),
      pEntityRefList( nullptr ) {
    // FUN_00424150
}

EntityShopData::~EntityShopData() {
    // FUN_00424120
}

void EntityShopData::loadResources( ScenaricBank *param_2 ) {
    // FUN_007ea5a0
    (void)param_2;

    ResolveEntityRefList( pEntityRefList );
    ResolveEntityRef( pEntityRef );
    ResolvePathBlock( pPathRef );
}

void EntityShopData::deserialize( BinaryReader *param_2 ) {
    // FUN_007ea640
    Entity::deserialize( param_2 );

    pPathRef = param_2->pCursor;
    param_2->pCursor += 8;

    pEntityRef = param_2->pCursor;
    param_2->pCursor += 8;

    pEntityRefList = reinterpret_cast<uint32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint32_t );
    if ( *pEntityRefList != 0 ) {
        param_2->pCursor += *pEntityRefList * 8;
    }
}

void EntityShopData::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_007ea600
    Entity::instanciate( pMngActor, param_1 );

    EntityShopData *pInstance = static_cast<EntityShopData *>( param_1 );
    pInstance->pPathRef = pPathRef;
    pInstance->pEntityRef = pEntityRef;
    pInstance->pEntityRefList = pEntityRefList;
}
