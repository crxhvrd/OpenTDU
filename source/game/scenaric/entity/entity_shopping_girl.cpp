#include "shared.h"
#include "entity_shopping_girl.h"

#include "game/scenaric/gs_scenaric.h"
#include "filesystem/binary.h"

EntityShoppingGirl::EntityShoppingGirl()
    : EntityExtraChallenge(),
      pGirlName( nullptr ),
      Reward( 0 ),
      Coupon( 0 ),
      ConditionType( 0 ),
      Condition{ 0 },
      GoodiesBinHashcode( 0 ),
      pGoodiesBin( nullptr ) {
    // FUN_007dd730
}

EntityShoppingGirl::~EntityShoppingGirl() {
    // FUN_00424120
}

void EntityShoppingGirl::loadResources( ScenaricBank *param_2 ) {
    // FUN_007dd8a0
    EntityExtraChallenge::loadResources( param_2 );
    pGoodiesBin = gpScenaric->getSCVolume( GoodiesBinHashcode );
}

void EntityShoppingGirl::deserialize( BinaryReader *param_2 ) {
    // FUN_007dd660
    EntityExtraChallenge::deserialize( param_2 );

    Reward = *reinterpret_cast<uint32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint32_t );

    pGirlName = reinterpret_cast<const char *>( param_2->pCursor );
    while ( *param_2->pCursor != '\0' ) {
        param_2->pCursor++;
    }
    param_2->pCursor++;
    param_2->pCursor = reinterpret_cast<uint8_t *>(
        ( reinterpret_cast<uintptr_t>( param_2->pCursor ) + 3 ) &
        ~static_cast<uintptr_t>( 3 ) );

    Coupon = *reinterpret_cast<uint32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint32_t );

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

    GoodiesBinHashcode = *reinterpret_cast<uint64_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint64_t );
}
