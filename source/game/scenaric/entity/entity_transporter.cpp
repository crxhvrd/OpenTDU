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
      EndAvatarConfig(),
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

    pMapName = ReadStreamString( param_2 );

    Reward = ReadStreamU32( param_2 );
    Coupon = ReadStreamU32( param_2 );

    readAvatarConfigFromPNJ( param_2, EndAvatarConfig );

    ConditionType = ReadStreamU32( param_2 );
    if ( ConditionType == 2 ) {
        Condition.pValue = ReadStreamString( param_2 );
    } else {
        Condition.Value = ReadStreamU32( param_2 );
    }

    GoodiesHashcode = *reinterpret_cast<uint64_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint64_t );
}
