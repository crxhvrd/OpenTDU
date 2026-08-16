#include "shared.h"
#include "entity_music_desc.h"

#include "filesystem/binary.h"

EntityMusicDesc::EntityMusicDesc() : Entity(), pMusicName( nullptr ) {
    // FUN_009d8950
}

EntityMusicDesc::~EntityMusicDesc() {
    // FUN_00424120
}

void EntityMusicDesc::deserialize( BinaryReader *param_2 ) {
    // FUN_009d8910
    Entity::deserialize( param_2 );

    pMusicName = reinterpret_cast<const char *>( param_2->pCursor );
    while ( *param_2->pCursor != '\0' ) {
        param_2->pCursor++;
    }

    param_2->pCursor = reinterpret_cast<uint8_t *>(
        ( reinterpret_cast<uintptr_t>( param_2->pCursor ) + 4 ) &
        ~static_cast<uintptr_t>( 3 ) );
}
