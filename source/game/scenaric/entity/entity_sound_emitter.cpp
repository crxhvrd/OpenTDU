#include "shared.h"
#include "entity_sound_emitter.h"

#include "audio/implementation/audio_dispatcher_playback.h"
#include "audio/implementation/ed_audio_sample.h"
#include "core/time.h"
#include "filesystem/binary.h"
#include "game/game_mechanism.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/scenaric_bank.h"
#include "world/gs_world_streaming.h"

template <typename T> static inline T ReadValue( BinaryReader *pReader ) {
    const T value = *reinterpret_cast<const T *>( pReader->pCursor );
    pReader->pCursor += sizeof( T );
    return value;
}

EntitySoundEmitter::EntitySoundEmitter()
    : Entity(),
      edAudioStreamEntryListener(),
      sampleStorage( nullptr ),
      pSample( nullptr ),
      pWavHeader( nullptr ),
      Volume( 1.0f ),         // FLOAT_00f4165c
      ClipDistMin( 5.0f ),    // DAT_00f41750
      ClipDistMax( 1000.0f ), // DAT_00f41718
      Doppler( 0.0f ),
      bLoop( false ),
      RandomPitch( 0.0f ),
      Pitch( 1.0f ), // FLOAT_00f4165c
      RandomVolume( 0.0f ),
      pSoundInstance( nullptr ),
      bSoundPlay( true ),
      OldTime( 0.0f ),
      bAmbiance( false ),
      bAmbianceLeft( false ) {
    // FUN_007dfdc0
    RndGen.setSeed( 0x3039 );
    RndGen.setSeed( static_cast<uint32_t>( GetDeltaTimeSeconds() ) );
}

EntitySoundEmitter::~EntitySoundEmitter() {
    // FUN_004268c0
}

bool EntitySoundEmitter::onMessage( FlashMessage *pMessage ) {
    // FUN_007dfbb0
    const char *pTag = pMessage->pAsChar;
    const uint32_t tag = ( static_cast<uint32_t>( (uint8_t)pTag[0] ) << 24 ) |
                         ( static_cast<uint32_t>( (uint8_t)pTag[1] ) << 16 ) |
                         ( static_cast<uint32_t>( (uint8_t)pTag[2] ) << 8 ) |
                         static_cast<uint32_t>( (uint8_t)pTag[3] );

    switch ( tag ) {
    case kMessagePlay:
        updatePlayback();
        return true;

    case kMessagePlayOnce:
        bLoop = false;
        updatePlayback();
        return true;

    case kMessagePlayLoop:
        bLoop = true;
        updatePlayback();
        return true;

    case kMessageStop:
        if ( pSoundInstance != nullptr && !pSoundInstance->isStopped() ) {
            pSoundInstance->stop( 0.0f );
        }
        bSoundPlay = true;
        return true;

    default:
        break;
    }

    return false;
}

void EntitySoundEmitter::initializeForCinematicKeyframed( void *param_2,
                                                          void *param_3 ) {
    // FUN_007dfaf0
    OTDU_UNIMPLEMENTED; // TODO: CINEMATIC
}

void EntitySoundEmitter::initialize() {
    // FUN_007e0230
    setTransformFromDesc();

    if ( pMechanism != nullptr ) {
        pMechanism->setEntity( this );
    }
}

void EntitySoundEmitter::loadResources( ScenaricBank *param_2 ) {
    // FUN_007dffb0
    if ( pMechanism != nullptr ) {
        pMechanism->loadResources( param_2 );
    }

    resolveSoundReference();

    pSample = nullptr;

    if ( pWavHeader == nullptr ) {
        return;
    }

    const uint64_t hashcode = *reinterpret_cast<const uint64_t *>( pWavHeader );

    int32_t wavSize = 0;
    void *pWavData = param_2 != nullptr
                         ? param_2->getWavData( hashcode, &wavSize )
                         : nullptr;
    if ( pWavData == nullptr && gpScenaric != nullptr ) {
        pWavData = gpScenaric->findWavData( hashcode, &wavSize );
    }

    if ( pWavData != nullptr && wavSize > 0 ) {
        auto pNewSample = std::make_unique<edAudioSample>();
        if ( pNewSample->loadFromMemory( pWavData, wavSize ) ) {
            sampleStorage = std::move( pNewSample );
            pSample = sampleStorage.get();
        }
    }

    if ( pSample == nullptr ) {
        OTDU_LOG_WARN( "SoundEmitter '%s': wav %016llx could not be resolved "
                       "from the bank\n",
                       name.c_str(), (unsigned long long)hashcode );
    }
}

void EntitySoundEmitter::unloadResources() {
    // FUN_007dfef0
    Entity::unloadResources();

    if ( pSoundInstance != nullptr ) {
        if ( bAmbianceLeft ) {
            pSoundInstance->removeListener( this );
        }

        pSoundInstance->setSample( nullptr );
        pSoundInstance->release();
        pSoundInstance = nullptr;
    }

    sampleStorage.reset();
    pSample = nullptr;
}

void EntitySoundEmitter::tick() {
    // FUN_007e01c0
    Entity::tick();

    if ( pSoundInstance == nullptr || pSoundInstance->isStopped() ) {
        return;
    }

    if ( pSoundInstance->getPosition() < OldTime ) {
        reRandomizeVolumeAndPitch();
    }

    OldTime = pSoundInstance->getPosition();
}

void EntitySoundEmitter::deserialize( BinaryReader *param_2 ) {
    // FUN_007dfce0
    Entity::deserialize( param_2 );

    if ( ReadValue<uint32_t>( param_2 ) != 0 ) {
        pWavHeader = param_2->pCursor;
        param_2->pCursor += sizeof( uint64_t );
    }

    deserializeSoundParameters( param_2 );
}

void EntitySoundEmitter::deserializeSoundParameters( BinaryReader *pReader ) {
    // FUN_007df580
    Volume = ReadValue<float>( pReader );
    RandomVolume = ReadValue<float>( pReader );
    Pitch = ReadValue<float>( pReader );
    RandomPitch = ReadValue<float>( pReader );
    Doppler = ReadValue<float>( pReader );
    ClipDistMin = ReadValue<float>( pReader );
    ClipDistMax = ReadValue<float>( pReader );
    bLoop = ReadValue<uint32_t>( pReader ) != 0;
}

void EntitySoundEmitter::setTransform( Matrix4x4f_t &transformMatrix,
                                       bool param_1 ) {
    // FUN_007df3b0
    bShifted = !param_1;
    modelMatrix = transformMatrix;
}

void EntitySoundEmitter::setPosition( Eigen::Vector4f &position,
                                      bool param_1 ) {
    // FUN_007df900
    bShifted = !param_1;
    modelMatrix.row( 3 ) = position;
}

void EntitySoundEmitter::reset() {
    // FUN_007dffa0
    if ( pMechanism != nullptr ) {
        pMechanism->reInit();
    }
}

void EntitySoundEmitter::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_007dfc50
    Entity::instanciate( pMngActor, param_1 );

    EntitySoundEmitter *pTarget = static_cast<EntitySoundEmitter *>( param_1 );
    pTarget->pWavHeader = pWavHeader;
    pTarget->Volume = Volume;
    pTarget->ClipDistMin = ClipDistMin;
    pTarget->ClipDistMax = ClipDistMax;
    pTarget->Doppler = Doppler;
    pTarget->bLoop = bLoop;
    pTarget->RandomPitch = RandomPitch;
    pTarget->Pitch = Pitch;
    pTarget->RandomVolume = RandomVolume;
}

void EntitySoundEmitter::onPipeEntryPlayed( edAudioStreamEntry * ) {
    // FUN_007df6c0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: FLASH AUDIO
}

void EntitySoundEmitter::onPipeEntryStopped( edAudioStreamEntry * ) {
    // FUN_007e0250
    OTDU_UNIMPLEMENTED_ONCE; // TODO: FLASH AUDIO
}

void EntitySoundEmitter::play( bool bInLoop ) {
    // FUN_007df8f0
    bLoop = bInLoop;
    updatePlayback();
}

void EntitySoundEmitter::updatePlayback() {
    // FUN_007df730
    if ( bAmbiance ) {
        if ( pSample != nullptr && pSoundInstance != nullptr ) {
            pSoundInstance->setLoopCount( bLoop ? -1 : 0 );
            pSoundInstance->play( 0.0f );
        }
        return;
    }

    if ( pSample == nullptr ) {
        return;
    }

    if ( pSoundInstance == nullptr ) {
        const Eigen::Vector4f shift = gpWorldStreaming->isEnabled()
                                          ? gpWorldStreaming->getPosition()
                                          : Eigen::Vector4f::Zero();
        const Eigen::Vector3f position( modelMatrix( 3, 0 ) - shift.x(),
                                        modelMatrix( 3, 1 ) - shift.y(),
                                        modelMatrix( 3, 2 ) - shift.z() );

        const float pitch = computeRandomPitch( Pitch, RandomPitch );
        const float volume = computeRandomVolume( RandomVolume, Volume );

        if ( pSoundInstance != nullptr ) {
            // DebugSetDescription( inst, "SCSoundEmitter" )
            pSoundInstance->setSample( pSample );
            pSoundInstance->setPosition(
                position, AudioDispatcherPlaybackInstanceValues::ADPCS_World );
            pSoundInstance->setVolume( volume );
            pSoundInstance->setDopplerFactor( Doppler );
            pSoundInstance->setClipDistances( ClipDistMin, ClipDistMax );
            pSoundInstance->setPitch( pitch );
        }
    }

    if ( pSoundInstance == nullptr ) {
        return;
    }

    if ( bLoop != pSoundInstance->isLooping() ) {
        pSoundInstance->setLoopCount( bLoop ? -1 : 0 );
        bSoundPlay = true;
    }

    if ( pSoundInstance->isStopped() && bSoundPlay ) {
        pSoundInstance->play( 0.0f );
        bSoundPlay = false;
    }
}

void EntitySoundEmitter::reRandomizeVolumeAndPitch() {
    // FUN_007df520
    if ( bAmbiance ) {
        return;
    }

    pSoundInstance->setVolume( computeRandomVolume( RandomVolume, Volume ) );
    pSoundInstance->setPitch( computeRandomPitch( Pitch, RandomPitch ) );
}

float EntitySoundEmitter::computeRandomPitch( float pitch, float randomPitch ) {
    // FUN_007df470
    if ( randomPitch == 0.0f ) {
        return pitch;
    }

    // [0, 1) -> [-1, 1)
    const float t = RandomUnit( RndGen ) * 2.0f - 1.0f;

    if ( t >= 0.0f ) {
        return pitch * ( 1.0f + t * randomPitch );
    }

    return ( pitch / ( 1.0f + randomPitch ) ) * ( 1.0f - t ) + pitch * t;
}

float EntitySoundEmitter::computeRandomVolume( float randomVolume,
                                               float volume ) {
    // FUN_007df3e0
    if ( randomVolume == 0.0f ) {
        return volume;
    }

    return RandomUnit( RndGen ) * randomVolume - randomVolume * 0.5f + volume;
}
