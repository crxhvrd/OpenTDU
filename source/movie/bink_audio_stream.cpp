#include "shared.h"
#include "bink_audio_stream.h"

#include "audio/implementation/ed_audio_object.h"

#include "ed_movie.h"

BinkAudioRing::BinkAudioRing()
    : storage(),
      channelCount( 0 ),
      sampleRate( 0 ),
      capacityFrames( 0 ),
      writeCursor( 0u ),
      readCursor( 0u ),
      bFinished( false ),
      droppedFrames( 0u ),
      bDropReported( false ) {}

bool BinkAudioRing::initialize( int32_t inChannelCount,
                                int32_t inSampleRate,
                                int32_t inCapacityFrames ) {
    if ( inChannelCount <= 0 ||
         inChannelCount > edAudioStreamEntry::kMaxSourceChannels ||
         inSampleRate <= 0 || inCapacityFrames <= 0 ) {
        return false;
    }

    channelCount = inChannelCount;
    sampleRate = inSampleRate;
    capacityFrames = inCapacityFrames;

    storage.assign( static_cast<size_t>( capacityFrames ) * channelCount, 0 );

    writeCursor.store( 0u, std::memory_order_relaxed );
    readCursor.store( 0u, std::memory_order_relaxed );
    bFinished.store( false, std::memory_order_relaxed );

    droppedFrames = 0u;
    bDropReported = false;

    return true;
}

void BinkAudioRing::destroy() {
    storage.clear();
    storage.shrink_to_fit();

    channelCount = 0;
    sampleRate = 0;
    capacityFrames = 0;

    writeCursor.store( 0u, std::memory_order_relaxed );
    readCursor.store( 0u, std::memory_order_relaxed );
    bFinished.store( false, std::memory_order_relaxed );
}

int32_t BinkAudioRing::getAvailableFrames() const {
    const uint32_t write = writeCursor.load( std::memory_order_acquire );
    const uint32_t read = readCursor.load( std::memory_order_acquire );

    return static_cast<int32_t>( write - read );
}

void BinkAudioRing::markFinished() {
    bFinished.store( true, std::memory_order_release );
}

void BinkAudioRing::pushInterleaved( const int16_t *pFrames,
                                     int32_t frameCount ) {
    if ( pFrames == nullptr || frameCount <= 0 || capacityFrames <= 0 ) {
        return;
    }

    const uint32_t write = writeCursor.load( std::memory_order_relaxed );
    const uint32_t read = readCursor.load( std::memory_order_acquire );

    const int32_t usedFrames = static_cast<int32_t>( write - read );
    const int32_t freeFrames = capacityFrames - usedFrames;

    if ( frameCount > freeFrames ) {
        droppedFrames += static_cast<uint32_t>( frameCount - freeFrames );
        frameCount = freeFrames;

        if ( !bDropReported ) {
            OTDU_LOG_WARN( "Movie audio ring overflowed (%u frames dropped); "
                           "playback will lag the video\n",
                           droppedFrames );
            bDropReported = true;
        }
    }

    if ( frameCount <= 0 ) {
        return;
    }

    const int32_t offset =
        static_cast<int32_t>( write % static_cast<uint32_t>( capacityFrames ) );
    const int32_t firstRun = Min( frameCount, capacityFrames - offset );

    memcpy(
        storage.data() + static_cast<size_t>( offset ) * channelCount, pFrames,
        static_cast<size_t>( firstRun ) * channelCount * sizeof( int16_t ) );

    if ( firstRun < frameCount ) {
        memcpy( storage.data(),
                pFrames + static_cast<size_t>( firstRun ) * channelCount,
                static_cast<size_t>( frameCount - firstRun ) * channelCount *
                    sizeof( int16_t ) );
    }

    writeCursor.store( write + static_cast<uint32_t>( frameCount ),
                       std::memory_order_release );
}

static inline void DeinterleaveRun( const int16_t *pSource,
                                    int16_t *pPlanes,
                                    int32_t planeStride,
                                    int32_t channelCount,
                                    int32_t firstFrame,
                                    int32_t frameCount ) {
    for ( int32_t channel = 0; channel < channelCount; channel++ ) {
        int16_t *pPlane = pPlanes + channel * planeStride + firstFrame;

        for ( int32_t frame = 0; frame < frameCount; frame++ ) {
            pPlane[frame] = pSource[frame * channelCount + channel];
        }
    }
}

int32_t BinkAudioRing::popDeinterleaved( int16_t *pPlanes,
                                         int32_t planeStride,
                                         int32_t frameCount ) {
    if ( pPlanes == nullptr || frameCount <= 0 || capacityFrames <= 0 ) {
        return 0;
    }

    const uint32_t read = readCursor.load( std::memory_order_relaxed );
    const uint32_t write = writeCursor.load( std::memory_order_acquire );

    frameCount = Min( frameCount, static_cast<int32_t>( write - read ) );
    if ( frameCount <= 0 ) {
        return 0;
    }

    const int32_t offset =
        static_cast<int32_t>( read % static_cast<uint32_t>( capacityFrames ) );
    const int32_t firstRun = Min( frameCount, capacityFrames - offset );

    DeinterleaveRun( storage.data() +
                         static_cast<size_t>( offset ) * channelCount,
                     pPlanes, planeStride, channelCount, 0, firstRun );

    if ( firstRun < frameCount ) {
        DeinterleaveRun( storage.data(), pPlanes, planeStride, channelCount,
                         firstRun, frameCount - firstRun );
    }

    readCursor.store( read + static_cast<uint32_t>( frameCount ),
                      std::memory_order_release );

    return frameCount;
}

BinkAudioStreamReader::BinkAudioStreamReader( BinkAudioRing *pInRing )
    : pRing( pInRing ), planes() {
    if ( pRing != nullptr && pRing->getChannelCount() > 0 ) {
        planes.assign( static_cast<size_t>( pRing->getChannelCount() ) *
                           kDecodeChunkFrames,
                       0 );
    }
}

bool BinkAudioStreamReader::getFormat( Format &format ) const {
    if ( pRing == nullptr || pRing->getChannelCount() <= 0 ||
         pRing->getSampleRate() <= 0 ) {
        return false;
    }

    format.ChannelCount = pRing->getChannelCount();
    format.PlaneStride = kDecodeChunkFrames;
    format.SampleRate = pRing->getSampleRate();
    format.CodecFlags = 0;
    format.EndSample = 0;
    format.LoopStartSample = 0;
    format.LoopEndSample = 0;
    format.MarkerCount = 0;

    return true;
}

const int16_t *BinkAudioStreamReader::decode( int32_t &sampleCount ) {
    if ( pRing == nullptr || planes.empty() ) {
        sampleCount = 0;
        return nullptr;
    }

    const int32_t channelCount = pRing->getChannelCount();

    const int32_t frameCount = pRing->popDeinterleaved(
        planes.data(), kDecodeChunkFrames, kDecodeChunkFrames );
    if ( frameCount > 0 ) {
        sampleCount = frameCount;
        return planes.data();
    }

    if ( pRing->isFinished() ) {
        sampleCount = 0;
        return nullptr;
    }

    for ( int32_t channel = 0; channel < channelCount; channel++ ) {
        memset( planes.data() + channel * kDecodeChunkFrames, 0,
                kSilenceChunkFrames * sizeof( int16_t ) );
    }

    sampleCount = kSilenceChunkFrames;
    return planes.data();
}

int32_t BinkAudioStreamReader::seek( int32_t position ) {
    // Nothing to seek in a live stream
    return position;
}

MovieAudioPipeline::MovieAudioPipeline()
    : pRoot( nullptr ), pAdapter( nullptr ), pBuffer( nullptr ) {}

MovieAudioPipeline::~MovieAudioPipeline() { destroy(); }

bool MovieAudioPipeline::create( edAudioObject *pParent ) {
    if ( pParent == nullptr || pRoot != nullptr ) {
        return false;
    }

    const int32_t channelCount = pParent->getInputChannelCount();

    pRoot = new edAudioObject( channelCount, channelCount );
    pRoot->debugSetDescription( "MoviePipeline" );

    pAdapter = new edAudioChannelAdapter( 2, channelCount );
    pAdapter->debugSetDescription( "MoviePipeline Adapter" );

    pBuffer = new edAudioBuffer( 2 );
    pBuffer->debugSetDescription( "MoviePipeline Buffer" );

    pAdapter->addNewEntry( pBuffer );
    pRoot->addNewEntry( pAdapter );
    pParent->addNewEntry( pRoot );

    return true;
}

void MovieAudioPipeline::destroy() {
    if ( pRoot != nullptr && pRoot->getParent() != nullptr ) {
        pRoot->getParent()->unlinkEntry( pRoot );
    }

    if ( pBuffer != nullptr && pBuffer->getEntryCount() > 0 ) {
        OTDU_LOG_WARN( "Movie audio pipeline still had %d voice(s) attached\n",
                       pBuffer->getEntryCount() );
        pBuffer->unlinkAllEntries();
    }

    if ( pRoot != nullptr ) {
        pRoot->unlinkAllEntries();
    }
    if ( pAdapter != nullptr ) {
        pAdapter->unlinkAllEntries();
    }

    delete pBuffer;
    delete pAdapter;
    delete pRoot;

    pBuffer = nullptr;
    pAdapter = nullptr;
    pRoot = nullptr;
}

BinkMovieAudio::BinkMovieAudio()
    : ring(),
      decodeScratch(),
      trackIndex( 0u ),
      pVoice( nullptr ),
      bPlaybackRequested( false ),
      bStarted( false ) {}

BinkMovieAudio::~BinkMovieAudio() { destroy(); }

#if OTDU_USE_LIBBINKDEC
bool BinkMovieAudio::initialize( BinkHandle &handle, const char *pDebugName ) {
    if ( pVoice != nullptr ) {
        return true;
    }

    if ( !handle.isValid || Bink_GetNumAudioTracks( handle ) == 0 ) {
        return false;
    }

    trackIndex = 0u;

    const AudioInfo info = Bink_GetAudioTrackDetails( handle, trackIndex );
    if ( info.nChannels == 0u || info.sampleRate == 0u ||
         info.idealBufferSize == 0u ) {
        OTDU_LOG_WARN( "Movie '%s' has an unusable audio track "
                       "(%u channels, %u Hz, %u byte buffer)\n",
                       pDebugName, info.nChannels, info.sampleRate,
                       info.idealBufferSize );
        return false;
    }

    const int32_t channelCount = static_cast<int32_t>( info.nChannels );
    const int32_t sampleRate = static_cast<int32_t>( info.sampleRate );

    const int32_t capacityFrames =
        static_cast<int32_t>( sampleRate * kRingSeconds );

    if ( !ring.initialize( channelCount, sampleRate, capacityFrames ) ) {
        OTDU_LOG_WARN( "Failed to size the audio ring for movie '%s'\n",
                       pDebugName );
        return false;
    }

    // Bink_GetAudioData fills the caller's buffer with up to idealBufferSize
    // BYTES of interleaved int16
    decodeScratch.assign( info.idealBufferSize / sizeof( int16_t ), 0 );

    pVoice = std::make_unique<edAudioStreamEntry>();

    char description[32];
    snprintf( description, sizeof( description ), "Movie %s", pDebugName );
    pVoice->debugSetDescription( description );

    if ( !pVoice->setReader(
             std::make_unique<BinkAudioStreamReader>( &ring ) ) ) {
        pVoice.reset();
        ring.destroy();
        decodeScratch.clear();
        return false;
    }

    pVoice->setLoopCount( 0 );
    pVoice->Volume = 1.0f;
    pVoice->Pitch = 1.0f;

    if ( !gEdMovie.attachAudioVoice( pVoice.get() ) ) {
        pVoice.reset();
        ring.destroy();
        decodeScratch.clear();
        return false;
    }

    OTDU_LOG_INFO( "Movie '%s' audio track: %d channel(s) at %d Hz\n",
                   pDebugName, channelCount, sampleRate );

    return true;
}

void BinkMovieAudio::pumpDecodedFrame( BinkHandle &handle ) {
    if ( pVoice == nullptr || decodeScratch.empty() || !handle.isValid ) {
        return;
    }

    const uint32_t reportedBytes =
        Bink_GetAudioData( handle, trackIndex, decodeScratch.data() );
    if ( reportedBytes == 0u ) {
        return;
    }

    // Bink_GetAudioData returns the bytes the packet DECODED
    const size_t scratchBytes = decodeScratch.size() * sizeof( int16_t );
    const size_t copiedBytes =
        Min( static_cast<size_t>( reportedBytes ), scratchBytes );

    const size_t frameBytes =
        sizeof( int16_t ) * static_cast<size_t>( ring.getChannelCount() );
    const int32_t frameCount = static_cast<int32_t>( copiedBytes / frameBytes );

    ring.pushInterleaved( decodeScratch.data(), frameCount );
}
#endif

void BinkMovieAudio::destroy() {
    if ( pVoice != nullptr ) {
        pVoice->stop();
        gEdMovie.detachAudioVoice( pVoice.get() );
        pVoice.reset();
    }

    ring.destroy();

    decodeScratch.clear();
    decodeScratch.shrink_to_fit();

    bPlaybackRequested = false;
    bStarted = false;
}

void BinkMovieAudio::requestPlayback() { bPlaybackRequested = true; }

void BinkMovieAudio::markFinished() { ring.markFinished(); }

void BinkMovieAudio::tick() {
    if ( pVoice == nullptr || bStarted || !bPlaybackRequested ) {
        return;
    }

    if ( ring.getAvailableFrames() < kPrimeFrames && !ring.isFinished() ) {
        return;
    }

    pVoice->play();
    bStarted = true;
}
