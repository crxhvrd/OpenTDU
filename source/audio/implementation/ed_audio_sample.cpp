#include "shared.h"
#include "ed_audio_sample.h"

#include "ed_audio_backend.h"

#if defined( OTDU_USE_SDL )
#include <SDL3/SDL.h>
#endif

edAudioSample::edAudioSample()
    : channelCount( 0 ), frameCount( 0 ), sampleRate( 0 ) {}

edAudioSample::~edAudioSample() { unload(); }

void edAudioSample::unload() {
    samples.clear();
    samples.shrink_to_fit();
    channelCount = 0;
    frameCount = 0;
    sampleRate = 0;
}

bool edAudioSample::loadFromFile( const char *pFilePath ) {
#if defined( OTDU_USE_SDL )
    unload();

    SDL_AudioSpec sourceSpec = {};
    uint8_t *pDecoded = nullptr;
    uint32_t decodedSize = 0;
    if ( !SDL_LoadWAV( pFilePath, &sourceSpec, &pDecoded, &decodedSize ) ) {
        OTDU_LOG_ERROR( "Failed to load '%s': '%s'!\n", pFilePath,
                        SDL_GetError() );
        return false;
    }

    return takeDecoded( sourceSpec, pDecoded, decodedSize, pFilePath );
#else
    return false;
#endif
}

bool edAudioSample::loadFromMemory( const void *pData, int32_t size ) {
#if defined( OTDU_USE_SDL )
    unload();

    if ( pData == nullptr || size <= 0 ) {
        return false;
    }

    SDL_IOStream *pStream = SDL_IOFromConstMem( pData, size_t( size ) );
    if ( pStream == nullptr ) {
        OTDU_LOG_ERROR( "Failed to wrap sample memory: '%s'!\n",
                        SDL_GetError() );
        return false;
    }

    SDL_AudioSpec sourceSpec = {};
    uint8_t *pDecoded = nullptr;
    uint32_t decodedSize = 0;

    if ( !SDL_LoadWAV_IO( pStream, true, &sourceSpec, &pDecoded,
                          &decodedSize ) ) {
        OTDU_LOG_ERROR( "Failed to decode in-memory sample: '%s'!\n",
                        SDL_GetError() );
        return false;
    }

    return takeDecoded( sourceSpec, pDecoded, decodedSize, "<bank entry>" );
#else
    return false;
#endif
}

bool edAudioSample::takeDecoded( SDL_AudioSpec &sourceSpec,
                                 uint8_t *pDecoded,
                                 uint32_t decodedSize,
                                 const char *pDebugName ) {
#if defined( OTDU_USE_SDL )
    // Bring it to signed 16bit but keep the sample's own rate
    SDL_AudioSpec targetSpec = {};
    targetSpec.format = SDL_AUDIO_S16;
    targetSpec.channels = sourceSpec.channels;
    targetSpec.freq = sourceSpec.freq;

    uint8_t *pConverted = nullptr;
    int32_t convertedSize = 0;
    const bool bConverted = SDL_ConvertAudioSamples(
        &sourceSpec, pDecoded, static_cast<int32_t>( decodedSize ), &targetSpec,
        &pConverted, &convertedSize );
    SDL_free( pDecoded );

    if ( !bConverted ) {
        OTDU_LOG_ERROR( "Failed to convert '%s': '%s'!\n", pDebugName,
                        SDL_GetError() );
        return false;
    }

    channelCount = targetSpec.channels;
    sampleRate = targetSpec.freq;
    frameCount = convertedSize / ( channelCount * (int32_t)sizeof( int16_t ) );

    // Interleaved to planar
    const int16_t *pInterleaved =
        reinterpret_cast<const int16_t *>( pConverted );
    samples.resize( channelCount * frameCount );
    for ( int32_t frame = 0; frame < frameCount; frame++ ) {
        for ( int32_t channel = 0; channel < channelCount; channel++ ) {
            samples[channel * frameCount + frame] =
                pInterleaved[frame * channelCount + channel];
        }
    }

    SDL_free( pConverted );
    return true;
#else
    return false;
#endif
}

edAudioSampleReader::edAudioSampleReader( const edAudioSample *pInSample )
    : pSample( pInSample ), readPosition( 0 ) {}

void edAudioSampleReader::setSample( const edAudioSample *pInSample ) {
    pSample = pInSample;
    readPosition = 0;
}

bool edAudioSampleReader::getFormat( Format &format ) const {
    if ( pSample == nullptr || !pSample->isLoaded() ) {
        return false;
    }

    format.ChannelCount = pSample->getChannelCount();
    format.PlaneStride = pSample->getFrameCount();
    format.SampleRate = pSample->getSampleRate();
    format.CodecFlags = 0;
    format.EndSample = pSample->getFrameCount();
    format.LoopEndSample = pSample->getFrameCount();
    format.MarkerCount = 0;

    return true;
}

const int16_t *edAudioSampleReader::decode( int32_t &sampleCount ) {
    // FUN_00619140
    if ( pSample == nullptr || !pSample->isLoaded() ||
         readPosition >= pSample->getFrameCount() ) {
        sampleCount = 0;
        return nullptr;
    }

    sampleCount =
        Min( kDecodeChunkSamples, pSample->getFrameCount() - readPosition );

    const int16_t *pBlock = pSample->getSamples() + readPosition;
    readPosition += sampleCount;

    return pBlock;
}

int32_t edAudioSampleReader::seek( int32_t position ) {
    // FUN_00619130
    if ( pSample == nullptr ) {
        readPosition = 0;
        return 0;
    }

    readPosition = Clamp( position, 0, pSample->getFrameCount() );
    return readPosition;
}
