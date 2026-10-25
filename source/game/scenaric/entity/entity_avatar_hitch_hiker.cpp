#include "shared.h"
#include "entity_avatar_hitch_hiker.h"

#include "game/scenaric/gs_scenaric.h"

static constexpr const char *kHitchHikerVoiceNames[] = {
    "standard", "idiot", "hippy" }; // PTR_s_standard_00f97020
static constexpr const char *kHitchHikerSoundNameFormat =
    "V_M_HitchHiker_%s_%03d.wav";
static constexpr uint32_t kVoiceRandomSeed = 0x3039;

EntityAvatarHitchHiker::EntityAvatarHitchHiker()
    : EntityAvatarPassenger(),
      SoundsBank(
          "hh", kHitchHikerSoundNameFormat, kNumPassengerDrivingSounds ),
      voice(),
      DrivingSoundPlayCounts{},
      CurrentSoundIndex( -1 ),
      Random(),
      CumulatedProba( 0.0f ),
      CurrentFreerideAnim( -1 ),
      bPlaySound( false ) {
    // FUN_007f6bd0
    Random.setSeed( kVoiceRandomSeed );
}

EntityAvatarHitchHiker::~EntityAvatarHitchHiker() {
    // FUN_00424930
    voice.destroy();
    SoundsBank.release();
}

void EntityAvatarHitchHiker::tick( GameMechanism *pGameMech,
                                   float deltaTime,
                                   float totalTime ) {
    // FUN_007f6d60
    EntityAvatarPassenger::tick( pGameMech, deltaTime, totalTime );
    if ( CurrentBehavior == PB_Passenger ) {
        voice.tick();
    }
}

void EntityAvatarHitchHiker::loadResources( ScenaricBank *param_2 ) {
    // FUN_007f6d10
    EntityAvatarPassenger::loadResources( param_2 );

    if ( AnimHi.File != 0 ) {
        AnimHi.pClip = gpScenaric->resolveClip( param_2, AnimHi.pRefBas );
    }

    if ( AnimPeepin.File != 0 ) {
        AnimPeepin.pClip =
            gpScenaric->resolveClip( param_2, AnimPeepin.pRefBas );
    }
}

void EntityAvatarHitchHiker::deserialize( BinaryReader *param_2 ) {
    // FUN_007f6cc0
    deserializeAnims( param_2, 0x1b );
}

void EntityAvatarHitchHiker::instanciate( MngActor *pMngActor,
                                          Entity *param_1 ) {
    // FUN_007f6c70
    EntityAvatarPassenger::instanciate( pMngActor, param_1 );

    EntityAvatarHitchHiker *pTarget =
        static_cast<EntityAvatarHitchHiker *>( param_1 );

    pTarget->AnimHi.ID = AnimHi.ID;
    pTarget->AnimHi.File = AnimHi.File;
    pTarget->AnimHi.pRefBas = AnimHi.pRefBas;

    pTarget->AnimPeepin.ID = AnimPeepin.ID;
    pTarget->AnimPeepin.File = AnimPeepin.File;
    pTarget->AnimPeepin.pRefBas = AnimPeepin.pRefBas;
}

void EntityAvatarHitchHiker::resetVoice() {
    // FUN_007f65a0
    voice.requestStop();
    voice.clear();
    CurrentSoundIndex = -1;
    for ( uint32_t &playCount : DrivingSoundPlayCounts ) {
        playCount = 0u;
    }
}

void EntityAvatarHitchHiker::initializeVoice( const uint32_t voiceType ) {
    // FUN_007f6e70
    const uint32_t numVoices =
        sizeof( kHitchHikerVoiceNames ) / sizeof( kHitchHikerVoiceNames[0] );
    SoundsBank.load(
        kHitchHikerVoiceNames[voiceType < numVoices ? voiceType : 0] );
    voice.create( "SCAvatarHitchHiker" );
    CurrentSoundIndex = -1;
}

void EntityAvatarHitchHiker::reactToPenalty( const float pendingRatio ) {
    // FUN_007f6880
    static constexpr float kAlwaysReactRatio = 1.0f; // FLOAT_00f4165c
    if ( voice.hasPendingSound() ) {
        return;
    }

    const float proba = RandomUnit( Random ) + CumulatedProba;
    if ( kAlwaysReactRatio - pendingRatio < proba ) {
        const int32_t soundIndex =
            PickLeastPlayedDrivingSound( DrivingSoundPlayCounts, Random );
        if ( SoundsBank.isReady() ) {
            voice.queue( SoundsBank.getSound( soundIndex ),
                         SoundsBank.getSoundName( soundIndex ) );
        }
        CurrentSoundIndex = soundIndex;
        CumulatedProba = 0.0f;
        return;
    }

    CumulatedProba += proba;
}
