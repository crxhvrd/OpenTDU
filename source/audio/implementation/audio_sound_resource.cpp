#include "shared.h"
#include "audio_sound_resource.h"

#include "audio/gs_audio.h"
#include "ed_audio_sample.h"
#include "ed_audio.h"

AudioSoundResource::AudioSoundResource( int32_t channelCount )
    : ChannelCount( channelCount ) {
    // FUN_00402600 / FUN_00402650 / FUN_00402700 / FUN_004027e0
}

AudioSoundResource::~AudioSoundResource() {
    for ( int32_t i = 0; i < ChannelCount; i++ ) {
        Channel &channel = Channels[i];
        if ( channel.pInstance != nullptr ) {
            channel.pInstance->release();
            channel.pInstance = nullptr;
        }
        channel.pSample.reset();
    }
}

std::unique_ptr<AudioSoundResource>
AudioSoundResource::create( const edXMBNode &resourceNode,
                            AudioClient *pClient ) {
    // FUN_009b15d0
    const edXMBNode channels = resourceNode.findChild( "Channels" );
    const int32_t channelCount =
        ( channels.getValue() != nullptr ) ? channels.getArrayCount() : 0;

    switch ( channelCount ) {
    case 1:
    case 2:
    case 4:
    case 5:
        break;
    default:
        return nullptr;
    }

    auto pResource = std::make_unique<AudioSoundResource>( channelCount );
    if ( !pResource->load( resourceNode, pClient ) ) {
        return nullptr;
    }

    return pResource;
}

bool AudioSoundResource::load( const edXMBNode &resourceNode,
                               AudioClient *pClient ) {
    // FUN_009b1220
    if ( pClient == nullptr ) {
        return false;
    }

    const edXMBNode channels = resourceNode.findChild( "Channels" );

    for ( int32_t i = 0; i < ChannelCount; i++ ) {
        Channel &channel = Channels[i];

        channel.pInstance = pClient->acquireSoundInstance();
        if ( channel.pInstance == nullptr ) {
            return false;
        }

        const edXMBNode sample = channels.getElement( i ).findChild( "Sample" );
        const void *pSampleData = sample.getValue();
        if ( pSampleData != nullptr && sample.isArray() ) {
            auto pDecoded = std::make_unique<edAudioSample>();
            if ( pDecoded->loadFromMemory( pSampleData,
                                           sample.getArrayCount() ) ) {
                channel.pSample = std::move( pDecoded );
            }
        }

        channel.pInstance->setSample( channel.pSample.get() );
        channel.pInstance->debugSetDescription( "SoundEmitterChannel" );
    }

    setLoopState( resourceNode.readBool( "LoopState", false ) );
    setVolume( resourceNode.readFloat( "Volume", 0.0f ) );
    setPitch( resourceNode.readFloat( "Pitch", 0.0f ) );
    setClipDistances( resourceNode.readFloat( "ClipNear", 0.0f ),
                      resourceNode.readFloat( "ClipFar", 0.0f ) );
    setEmittersDistance( resourceNode.readFloat( "EmittersDistance", 0.0f ) );
    setAttenuation( resourceNode.readFloat( "Attenuation", 0.0f ) );

    return true;
}

void AudioSoundResource::setVolume( float volume ) {
    // FUN_009b0970
    Volume = volume;
    for ( int32_t i = 0; i < ChannelCount; i++ ) {
        Channels[i].pInstance->setVolume( Channels[i].Gain * Volume );
    }
}

void AudioSoundResource::setPitch( float pitch ) {
    Pitch = pitch;
    for ( int32_t i = 0; i < ChannelCount; i++ ) {
        Channels[i].pInstance->setPitch( Pitch );
    }
}

void AudioSoundResource::setLoopState( bool bLoop ) {
    // FUN_00613620
    bLoopState = bLoop;
    for ( int32_t i = 0; i < ChannelCount; i++ ) {
        Channels[i].pInstance->setLoopCount( bLoopState ? -1 : 0 );
    }
}

void AudioSoundResource::setClipDistances( float clipNear, float clipFar ) {
    // FUN_009b0880
    ClipNear = clipNear;
    ClipFar = clipFar;
    for ( int32_t i = 0; i < ChannelCount; i++ ) {
        Channels[i].pInstance->setClipDistances( ClipNear, ClipFar );
    }
}

void AudioSoundResource::setEmittersDistance( float distance ) {
    // FUN_009b0f70
    EmittersDistance = distance;
    updateChannelLayout();
    pushChannelPositions();
}

void AudioSoundResource::setAttenuation( float attenuation ) {
    Attenuation = attenuation;
    for ( int32_t i = 0; i < ChannelCount; i++ ) {
        Channels[i].pInstance->setRefractionFactor( Attenuation );
    }
}

void AudioSoundResource::setDopplerFactor( float factor ) {
    // FUN_009b07e0
    DopplerFactor = factor;
    for ( int32_t i = 0; i < ChannelCount; i++ ) {
        Channels[i].pInstance->setDopplerFactor( DopplerFactor );
    }
}

void AudioSoundResource::setPosition( const Eigen::Vector4f &position,
                                      uint8_t coordinateSystem ) {
    // FUN_009b1090
    Position = position;
    CoordinateSystem = coordinateSystem;
    pushChannelPositions();
}

void AudioSoundResource::pushChannelPositions() {
    for ( int32_t i = 0; i < ChannelCount; i++ ) {
        const Eigen::Vector4f world = Position + Channels[i].Position;
        Channels[i].pInstance->setPosition( world.head<3>(), CoordinateSystem );
    }
}

void AudioSoundResource::updateChannelLayout() {
    const float d = EmittersDistance;
    const float back = -kDiagonal * d;
    const float front = kDiagonal * d;

    switch ( ChannelCount ) {
    case 1: // FUN_009b0f00
        Channels[0].Position = Eigen::Vector4f::Zero();
        break;

    case 2: // FUN_009b0e30
        Channels[0].Position = Eigen::Vector4f( -d, 0.0f, 0.0f, 0.0f );
        Channels[1].Position = Eigen::Vector4f( d, 0.0f, 0.0f, 0.0f );
        break;

    case 4: // FUN_009b0cb0
    case 5: // FUN_009b0ad0 (the same four corners + a centre speaker)
        Channels[0].Position = Eigen::Vector4f( back, 0.0f, front, 0.0f );
        Channels[1].Position = Eigen::Vector4f( front, 0.0f, front, 0.0f );
        Channels[2].Position = Eigen::Vector4f( back, 0.0f, back, 0.0f );
        Channels[3].Position = Eigen::Vector4f( front, 0.0f, back, 0.0f );
        if ( ChannelCount == 5 ) {
            Channels[4].Position = Eigen::Vector4f( 0.0f, 0.0f, d, 0.0f );
        }
        break;

    default:
        break;
    }
}

void AudioSoundResource::play( float fadeTime ) {
    // FUN_009b06a0
    for ( int32_t i = 0; i < ChannelCount; i++ ) {
        Channels[i].pInstance->play( fadeTime );
    }
}

void AudioSoundResource::stop( float fadeTime ) {
    // FUN_009b0670
    for ( int32_t i = 0; i < ChannelCount; i++ ) {
        Channels[i].pInstance->stop( fadeTime );
    }
}

bool AudioSoundResource::isPlaying() const {
    // FUN_009b0760
    for ( int32_t i = 0; i < ChannelCount; i++ ) {
        const AudioDispatcherPlaybackSoundInstance *pInstance =
            Channels[i].pInstance;
        if ( pInstance->isStopped() || pInstance->isPaused() ) {
            return false;
        }
    }

    return true;
}
