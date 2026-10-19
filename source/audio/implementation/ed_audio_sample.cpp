#include "shared.h"
#include "ed_audio_sample.h"

#include "ed_audio_backend.h"

#if defined( OTDU_USE_SDL )
#include <SDL3/SDL.h>
#endif

static constexpr uint32_t MakeFourCC( const char *pFourCC ) {
    return uint32_t( uint8_t( pFourCC[0] ) ) |
           ( uint32_t( uint8_t( pFourCC[1] ) ) << 8 ) |
           ( uint32_t( uint8_t( pFourCC[2] ) ) << 16 ) |
           ( uint32_t( uint8_t( pFourCC[3] ) ) << 24 );
}

static constexpr uint32_t kRiffChunkID = MakeFourCC( "RIFF" );
static constexpr uint32_t kCueChunkID = MakeFourCC( "cue " );
static constexpr uint32_t kListChunkID = MakeFourCC( "LIST" );
static constexpr uint32_t kAdtlListType = MakeFourCC( "adtl" );
static constexpr uint32_t kLablChunkID = MakeFourCC( "labl" );

static constexpr int32_t kRiffHeaderSize = 12;
static constexpr int32_t kChunkHeaderSize = 8;

struct RiffChunk {
    const uint8_t *pData = nullptr;
    int32_t Size = 0;
};

static uint32_t ReadU32( const uint8_t *pData ) {
    uint32_t value = 0;
    memcpy( &value, pData, sizeof( value ) );
    return value;
}

static bool FindRiffChunk( const uint8_t *pData,
                           const int32_t size,
                           const uint32_t chunkID,
                           RiffChunk &outChunk ) {
    // FUN_0061f8c0
    int32_t offset = 0;
    while ( offset + kChunkHeaderSize <= size ) {
        const uint32_t id = ReadU32( pData + offset );
        const int32_t chunkSize = int32_t( ReadU32( pData + offset + 4 ) );
        const int32_t bodyOffset = offset + kChunkHeaderSize;
        if ( chunkSize < 0 || chunkSize > size - bodyOffset ) {
            return false;
        }

        if ( id == chunkID ) {
            outChunk.pData = pData + bodyOffset;
            outChunk.Size = chunkSize;
            return true;
        }
        offset = bodyOffset + chunkSize + ( chunkSize & 1 );
    }
    return false;
}

struct CuePoint {
    uint32_t ID;
    int32_t Position;
};

static bool IsLoopCue( const RiffChunk &adtl, const uint32_t cueID ) {
    static constexpr int32_t kLablCueIDSize = 4;

    int32_t offset = 4; // skip the list type
    while ( offset + kChunkHeaderSize <= adtl.Size ) {
        const uint32_t id = ReadU32( adtl.pData + offset );
        const int32_t chunkSize = int32_t( ReadU32( adtl.pData + offset + 4 ) );
        const int32_t bodyOffset = offset + kChunkHeaderSize;
        if ( chunkSize < 0 || chunkSize > adtl.Size - bodyOffset ) {
            return false;
        }

        if ( id == kLablChunkID && chunkSize >= kLablCueIDSize &&
             ReadU32( adtl.pData + bodyOffset ) == cueID ) {
            // Only the first four characters of the label are compared.
            const char *pLabel = reinterpret_cast<const char *>(
                adtl.pData + bodyOffset + kLablCueIDSize );
            const int32_t labelLength = chunkSize - kLablCueIDSize;
            return labelLength >= 4 && strncmp( pLabel, "loop", 4 ) == 0;
        }
        offset = bodyOffset + chunkSize + ( chunkSize & 1 );
    }
    return false;
}

edAudioSample::edAudioSample()
    : channelCount( 0 ),
      frameCount( 0 ),
      sampleRate( 0 ),
      loopStart( -1 ),
      loopEnd( -1 ) {}

edAudioSample::~edAudioSample() { unload(); }

void edAudioSample::unload() {
    samples.clear();
    samples.shrink_to_fit();
    channelCount = 0;
    frameCount = 0;
    sampleRate = 0;
    loopStart = -1;
    loopEnd = -1;
    markers.clear();
}

void edAudioSample::parseCuePoints( const uint8_t *pData, int32_t size ) {
    // FUN_006178a0 (cue chunk) / FUN_0061f490 (labels)
    static constexpr int32_t kCuePointSize = 0x18;

    if ( pData == nullptr || size < kRiffHeaderSize ||
         ReadU32( pData ) != kRiffChunkID ) {
        return;
    }

    const uint8_t *pChunks = pData + kRiffHeaderSize;
    const int32_t chunksSize = size - kRiffHeaderSize;

    RiffChunk cue;
    if ( !FindRiffChunk( pChunks, chunksSize, kCueChunkID, cue ) ||
         cue.Size < 4 ) {
        return;
    }

    const int32_t cueCount = Min( int32_t( ReadU32( cue.pData ) ),
                                  ( cue.Size - 4 ) / kCuePointSize );
    if ( cueCount <= 0 ) {
        return;
    }

    std::vector<CuePoint> cuePoints( cueCount );
    for ( int32_t i = 0; i < cueCount; i++ ) {
        const uint8_t *pCuePoint = cue.pData + 4 + i * kCuePointSize;
        cuePoints[i].ID = ReadU32( pCuePoint );
        cuePoints[i].Position = int32_t( ReadU32( pCuePoint + 4 ) );
    }

    // FUN_0061fa70
    std::sort( cuePoints.begin(), cuePoints.end(),
               []( const CuePoint &a, const CuePoint &b ) {
                   return a.Position < b.Position;
               } );

    RiffChunk list;
    const bool bHasLabels =
        FindRiffChunk( pChunks, chunksSize, kListChunkID, list ) &&
        list.Size >= 4 && ReadU32( list.pData ) == kAdtlListType;

    markers.reserve( cueCount );
    for ( const CuePoint &cuePoint : cuePoints ) {
        if ( bHasLabels && IsLoopCue( list, cuePoint.ID ) ) {
            if ( loopStart == -1 ) {
                loopStart = cuePoint.Position;
                continue;
            }
            if ( loopEnd == -1 ) {
                loopEnd = cuePoint.Position;
                continue;
            }
        }
        markers.push_back( cuePoint.Position );
    }
}

bool edAudioSample::loadFromFile( const char *pFilePath ) {
#if defined( OTDU_USE_SDL )
    unload();

    size_t fileSize = 0;
    void *pFileData = SDL_LoadFile( pFilePath, &fileSize );
    if ( pFileData == nullptr ) {
        OTDU_LOG_ERROR( "Failed to load '%s': '%s'!\n", pFilePath,
                        SDL_GetError() );
        return false;
    }

    const bool bLoaded =
        loadFromMemory( pFileData, static_cast<int32_t>( fileSize ) );
    SDL_free( pFileData );
    return bLoaded;
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

    if ( !takeDecoded( sourceSpec, pDecoded, decodedSize, "<bank entry>" ) ) {
        return false;
    }

    parseCuePoints( static_cast<const uint8_t *>( pData ), size );
    return true;
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
    format.LoopStartSample = pSample->getLoopStart();
    format.LoopEndSample = pSample->getLoopEnd();
    format.MarkerCount = pSample->getMarkerCount();

    // FUN_00619150
    if ( format.LoopEndSample <= format.LoopStartSample ) {
        format.LoopStartSample = 0;
        format.LoopEndSample = format.EndSample;
    }

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

int32_t edAudioSampleReader::getMarker( int32_t index ) const {
    // FUN_00619100
    return pSample->getMarker( index );
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
