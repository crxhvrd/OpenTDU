#include "shared.h"
#include "entity_house_data.h"

#include "game/scenaric/gs_scenaric.h"
#include "filesystem/binary.h"

EntityHouseData::EntityHouseData()
    : Entity(),
      pEntityRef0( nullptr ),
      pEntityRef1( nullptr ),
      pPathList( nullptr ) {
    // FUN_00424180
}

EntityHouseData::~EntityHouseData() {
    // FUN_00424120
}

void EntityHouseData::loadResources( ScenaricBank *param_2 ) {
    // FUN_007ea4f0
    (void)param_2;

    ResolveEntityRef( pEntityRef0 );
    ResolveEntityRef( pEntityRef1 );
    ResolvePathList( pPathList );
}

void EntityHouseData::deserialize( BinaryReader *param_2 ) {
    // FUN_007ea550
    Entity::deserialize( param_2 );

    pEntityRef0 = param_2->pCursor;
    param_2->pCursor += 8;

    pEntityRef1 = param_2->pCursor;
    param_2->pCursor += 8;

    pPathList = reinterpret_cast<uint32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint32_t );
    if ( *pPathList != 0 ) {
        param_2->pCursor += *pPathList * 8;
    }
}
