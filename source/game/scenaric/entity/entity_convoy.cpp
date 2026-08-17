#include "shared.h"
#include "entity_convoy.h"

#include "filesystem/binary.h"

EntityConvoy::EntityConvoy()
    : EntityExtraChallenge(),
      pMapName( nullptr ),
      OffRoad( 0 ),
      Reward( 0 ),
      MinReward( 0 ),
      Coupon( 0 ),
      CarID( 0 ),
      CarColorID( 0 ),
      CarRimID( 0 ),
      CarInteriorColorID( 0 ),
      EndAvatarConfig() {
    // FUN_007dd350
}

EntityConvoy::~EntityConvoy() {
    // FUN_00424120
}

void EntityConvoy::deserialize( BinaryReader *param_2 ) {
    // FUN_007dd1c0
    EntityExtraChallenge::deserialize( param_2 );

    OffRoad = ReadStreamU32( param_2 );
    pMapName = ReadStreamString( param_2 );

    Reward = ReadStreamU32( param_2 );
    MinReward = ReadStreamU32( param_2 );
    Coupon = ReadStreamU32( param_2 );

    CarID = ReadStreamU32( param_2 );
    CarColorID = ReadStreamU32( param_2 );
    CarRimID = ReadStreamU32( param_2 );
    CarInteriorColorID = ReadStreamU32( param_2 );

    readAvatarConfigFromPNJ( param_2, EndAvatarConfig );

    TimeLimit = 0.0f;
}
