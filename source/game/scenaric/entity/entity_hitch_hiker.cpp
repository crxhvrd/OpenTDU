#include "shared.h"
#include "entity_hitch_hiker.h"

#include "game/scenaric/gs_scenaric.h"
#include "filesystem/binary.h"

EntityHitchHiker::EntityHitchHiker()
    : EntityExtraChallenge(),
      OffRoad( 0 ),
      pFirstName( nullptr ),
      GoodiesHashcode( 0 ),
      pGoodies( nullptr ),
      Reward( 0 ),
      Coupon( 0 ),
      ConditionType( 0 ),
      Condition{ 0 } {
    // FUN_007dd640
}

EntityHitchHiker::~EntityHitchHiker() {
    // FUN_00424120
}

void EntityHitchHiker::loadResources( ScenaricBank *param_2 ) {
    // FUN_007dd870
    EntityExtraChallenge::loadResources( param_2 );
    pGoodies = gpScenaric->findEntityByHashcode( GoodiesHashcode );
}

void EntityHitchHiker::deserialize( BinaryReader *param_2 ) {
    // FUN_007dd560
    EntityExtraChallenge::deserialize( param_2 );

    OffRoad = ReadStreamU32( param_2 );
    pFirstName = ReadStreamString( param_2 );

    Reward = ReadStreamU32( param_2 );
    Coupon = ReadStreamU32( param_2 );

    ConditionType = ReadStreamU32( param_2 );
    if ( ConditionType == 2 ) {
        Condition.pValue = ReadStreamString( param_2 );
    } else {
        Condition.Value = ReadStreamU32( param_2 );
    }

    GoodiesHashcode = *reinterpret_cast<uint64_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint64_t );
}
