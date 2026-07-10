#include "shared.h"
#include "entity_transporter.h"

#include "game/scenaric/gs_scenaric.h"
#include "filesystem/binary.h"

EntityTransporter::EntityTransporter()
    : EntityExtraChallenge(),
      pMapName( nullptr ),
      Reward( 0 ),
      Coupon( 0 ),
      GoodiesHashcode( 0 ),
      pGoodies( nullptr ),
      ConditionType( 0 ),
      Condition{ 0 } {
    // FUN_007dd530
}

EntityTransporter::~EntityTransporter() {
    // FUN_00424120
}

void EntityTransporter::loadResources( ScenaricBank *param_2 ) {
    // FUN_007dd840
    EntityExtraChallenge::loadResources( param_2 );
    pGoodies = gpScenaric->findEntityByHashcode( GoodiesHashcode );
}

void EntityTransporter::deserialize( BinaryReader *param_2 ) {
    // FUN_007dd380
    EntityExtraChallenge::deserialize( param_2 );

    pMapName = reinterpret_cast<const char *>( param_2->pCursor );
    while ( *param_2->pCursor != '\0' ) {
        param_2->pCursor++;
    }
    param_2->pCursor++;
    param_2->pCursor = reinterpret_cast<uint8_t *>(
        ( reinterpret_cast<uintptr_t>( param_2->pCursor ) + 3 ) &
        ~static_cast<uintptr_t>( 3 ) );

    Reward = *reinterpret_cast<uint32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint32_t );

    Coupon = *reinterpret_cast<uint32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint32_t );

    const char *pEndAvatarName =
        reinterpret_cast<const char *>( param_2->pCursor );
    while ( *param_2->pCursor != '\0' ) {
        param_2->pCursor++;
    }
    param_2->pCursor++;
    param_2->pCursor = reinterpret_cast<uint8_t *>(
        ( reinterpret_cast<uintptr_t>( param_2->pCursor ) + 3 ) &
        ~static_cast<uintptr_t>( 3 ) );
    (void)pEndAvatarName;

    OTDU_UNIMPLEMENTED; // TODO: AVATAR

    ConditionType = *reinterpret_cast<uint32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint32_t );

    if ( ConditionType == 2 ) {
        // Condition holds an in-buffer string.
        Condition.pValue = reinterpret_cast<const char *>( param_2->pCursor );
        while ( *param_2->pCursor != '\0' ) {
            param_2->pCursor++;
        }
        param_2->pCursor++;
        param_2->pCursor = reinterpret_cast<uint8_t *>(
            ( reinterpret_cast<uintptr_t>( param_2->pCursor ) + 3 ) &
            ~static_cast<uintptr_t>( 3 ) );
    } else {
        Condition.Value = *reinterpret_cast<uint32_t *>( param_2->pCursor );
        param_2->pCursor += sizeof( uint32_t );
    }

    GoodiesHashcode = *reinterpret_cast<uint64_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint64_t );
}
