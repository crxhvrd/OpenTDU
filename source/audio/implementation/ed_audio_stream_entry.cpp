#include "shared.h"
#include "ed_audio_stream_entry.h"

#include "core/time.h"
#include "ed_audio_sample.h"
#include "ed_audio_backend.h"

static inline float GetAudioTime() {
    // FUN_00610eb0
    return GetTimePrecise();
}

edAudioFader::edAudioFader()
    : Mode( AFM_Idle ), Duration( 0.0f ), StartTime( 0.0f ), Gain( 1.0f ) {
    // FUN_00618d50
}

float edAudioFader::evaluate() {
    // FUN_00618d90
    if ( Mode == AFM_Idle ) {
        return 1.0f;
    }

    const float elapsed = ( GetAudioTime() - StartTime ) / Duration;
    const float gain = ( Mode == AFM_FadeIn ) ? ( 1.0f - Gain ) * elapsed + Gain
                                              : Gain - Gain * elapsed;

    if ( gain > 1.0f ) {
        Duration = 0.0f;
        Mode = AFM_Idle;
        return 1.0f;
    }

    if ( gain < 0.0f ) {
        Duration = 0.0f;
        Mode = AFM_Idle;
        return 0.0f;
    }

    return gain;
}

void edAudioFader::start( eMode mode, float duration, float idleGain ) {
    if ( duration == 0.0f ) {
        Mode = AFM_Idle;
        Duration = 0.0f;
        return;
    }

    const float now = GetAudioTime();

    if ( Mode == AFM_Idle ) {
        Gain = idleGain;
    } else {
        const float elapsed = ( now - StartTime ) / Duration;
        Gain = ( Mode == AFM_FadeIn ) ? elapsed * ( 1.0f - Gain ) + Gain
                                      : Gain - elapsed * Gain;
    }

    Gain = ( Gain >= 0.0f ) ? Min( Gain, 1.0f ) : 0.0f;

    Mode = mode;
    StartTime = now;
    Duration = duration;
}

void edAudioFader::startFadeIn( float duration ) {
    // FUN_00618f40
    start( AFM_FadeIn, duration, 0.0f );
}

void edAudioFader::startFadeOut( float duration ) {
    // FUN_00619020
    start( AFM_FadeOut, duration, 1.0f );
}

edAudioStreamEntry::edAudioStreamEntry()
    : edAudioObject( 0, 0 ),
      Name{},
      SourceChannelCount( 0 ),
      SourcePlaneStride( 0 ),
      SourceSampleRate( kAudioOutputSampleRate ),
      CodecFlags( 0 ),
      EndSample( 0 ),
      LoopEndSample( 0 ),
      MarkerCount( 0 ),
      pSampleData( nullptr ),
      pReader( nullptr ),
      pData( nullptr ),
      pPendingData( nullptr ),
      NextMarkerIndex( 0 ),
      PendingSeek( 0 ),
      TotalSampleCount( 0 ),
      ConsumedSampleCount( 0 ),
      SeekDiscardCount( 0 ),
      AvailableSampleCount( 0 ),
      Position( 0 ),
      PreviousSample{},
      pListeners{},
      PlayingState( APES_Stop ),
      FadeTarget( APEFT_None ),
      PreviousVolume( 0.0f ),
      PreviousPitch( 0 ),
      LoopCount( 0 ),
      LoopsDone( 0 ),
      bResetRamp( true ) {
    // FUN_00613700
}

edAudioStreamEntry::~edAudioStreamEntry() { releaseReader(); }

void edAudioStreamEntry::onEntryAttached( edAudioObject *pInParent ) {
    // FUN_00614050
    if ( pInParent != nullptr ) {
        OutputChannelCount = pInParent->getInputChannelCount();
    }
}

bool edAudioStreamEntry::setSample( const edAudioSample *pInSample ) {
    // FUN_00614700
    pSampleData = pInSample;

    if ( pInSample == nullptr ) {
        return setReader( nullptr );
    }

    return setReader( std::make_unique<edAudioSampleReader>( pInSample ) );
}

bool edAudioStreamEntry::setReader(
    std::unique_ptr<edAudioStreamReader> pInReader ) {
    // FUN_00614540
    ScopedMutexLock lock( &GetAudioGraphMutex() );

    resetStream( -1, 0 );

    Markers.clear();

    releaseReader();

    pReader = std::move( pInReader );
    ConsumedSampleCount = 0;

    if ( pReader == nullptr ) {
        Name[0] = '\0';
        SourceChannelCount = 0;
        SourcePlaneStride = 0;
        SourceSampleRate = kAudioOutputSampleRate;
        EndSample = 0;
        CodecFlags = 0;
        LoopEndSample = 0;
        MarkerCount = 0;
        PlayingState = APES_Stop;
        FadeTarget = APEFT_None;
        return false;
    }

    if ( PendingSeek == 0 ) {
        PendingSeek = -1;
    }

    cacheReaderFormat();
    return true;
}

void edAudioStreamEntry::cacheReaderFormat() {
    // FUN_00613ae0
    edAudioStreamReader::Format format = {};
    if ( pReader == nullptr || !pReader->getFormat( format ) ) {
        return;
    }

    snprintf( Name, sizeof( Name ), "%s", "" );

    SourceChannelCount = format.ChannelCount;
    SourcePlaneStride = format.PlaneStride;
    SourceSampleRate = format.SampleRate;
    CodecFlags = format.CodecFlags;
    EndSample = format.EndSample;
    LoopEndSample = format.LoopEndSample;
    MarkerCount = format.MarkerCount;

    Markers.resize( MarkerCount > 0 ? MarkerCount : 0 );
    for ( int32_t i = 0; i < MarkerCount; i++ ) {
        Markers[i] = pReader->getMarker( i );
    }
}

void edAudioStreamEntry::releaseReader() {
    pReader.reset();
    pData = nullptr;
    pPendingData = nullptr;
}

void edAudioStreamEntry::setLoopCount( int32_t loopCount ) {
    LoopCount = loopCount;
}

void edAudioStreamEntry::seek( float seconds, bool bWrapIntoLoop ) {
    // FUN_00613ea0
    int32_t position = int32_t( seconds * float( SourceSampleRate ) );

    if ( bWrapIntoLoop && LoopCount != 0 ) {
        OTDU_UNIMPLEMENTED;
    }

    if ( position > EndSample ) {
        position = EndSample;
    }

    resetStream( position, 0 );
}

bool edAudioStreamEntry::addListener( edAudioStreamEntryListener *pListener ) {
    for ( int32_t i = 0; i < kMaxListeners; i++ ) {
        if ( pListeners[i] == nullptr ) {
            pListeners[i] = pListener;
            return true;
        }
    }

    return false;
}

void edAudioStreamEntry::removeListener(
    edAudioStreamEntryListener *pListener ) {
    for ( int32_t i = 0; i < kMaxListeners; i++ ) {
        if ( pListeners[i] == pListener ) {
            pListeners[i] = nullptr;
        }
    }
}

void edAudioStreamEntry::notifyListeners( ListenerEvent event ) {
    // FUN_00613e70
    for ( int32_t i = 0; i < kMaxListeners; i++ ) {
        if ( pListeners[i] != nullptr ) {
            ( pListeners[i]->*event )( this );
        }
    }
}

void edAudioStreamEntry::resetStream( int32_t seekPosition,
                                      int32_t loopsDone ) {
    // FUN_00613bd0
    ScopedMutexLock lock( &GetAudioGraphMutex() );

    PendingSeek = seekPosition;

    pData = nullptr;
    pPendingData = nullptr;
    NextMarkerIndex = 0;
    TotalSampleCount = 0;
    ConsumedSampleCount = 0;
    SeekDiscardCount = 0;
    AvailableSampleCount = 0;
    Position = 0;

    for ( int32_t i = 0; i < kMaxSourceChannels; i++ ) {
        PreviousSample[i] = 0.0f;
    }

    LoopsDone = loopsDone;
    bResetRamp = true;
}

void edAudioStreamEntry::play( float fadeTime ) {
    // FUN_00613f20
    switch ( PlayingState ) {
    case APES_Stop:
        PlayingState = APES_Play;
        resetStream( PendingSeek != -1 ? PendingSeek : 0, 0 );
        notifyListeners( &edAudioStreamEntryListener::onPipeEntryPlayed );
        Fader.startFadeIn( fadeTime );
        break;

    case APES_Play:
        // Playing again from the top rather than ignoring the call
        resetStream( PendingSeek != -1 ? PendingSeek : 0, 0 );
        notifyListeners( &edAudioStreamEntryListener::onPipeEntryPlayed );
        Fader.startFadeIn( fadeTime );
        break;

    case APES_Pause:
        PlayingState = APES_Play;
        notifyListeners( &edAudioStreamEntryListener::onPipeEntryResumed );
        Fader.startFadeIn( fadeTime );
        break;

    case APES_FadeoutToStop:
        PlayingState = APES_Stop;
        play( fadeTime );
        return;

    case APES_FadeoutToPause:
        PlayingState = APES_Pause;
        play( fadeTime );
        return;
    }

    FadeTarget = ( fadeTime == 0.0f ) ? APEFT_None : APEFT_Play;
}

void edAudioStreamEntry::stop( float fadeTime ) {
    // FUN_006144b0
    if ( PlayingState == APES_Play ) {
        PlayingState = ( fadeTime == 0.0f ) ? APES_FadeoutToStop : APES_Play;
        Fader.startFadeOut( fadeTime );
    } else if ( PlayingState == APES_Pause ) {
        stopInternal();
    } else if ( PlayingState == APES_FadeoutToPause ) {
        PlayingState = APES_FadeoutToStop;
    }

    FadeTarget = ( fadeTime == 0.0f ) ? APEFT_None : APEFT_Stop;
}

void edAudioStreamEntry::pause( float fadeTime ) {
    // FUN_006139a0
    if ( PlayingState == APES_Play ) {
        PlayingState = ( fadeTime == 0.0f ) ? APES_FadeoutToPause : APES_Play;
        Fader.startFadeOut( fadeTime );
    } else if ( PlayingState == APES_Pause ) {
        Fader.startFadeOut( fadeTime );
    }

    FadeTarget = ( fadeTime == 0.0f ) ? APEFT_None : APEFT_Pause;
}

void edAudioStreamEntry::stopInternal() {
    // FUN_00614420
    PlayingState = APES_Stop;
    notifyListeners( &edAudioStreamEntryListener::onPipeEntryStopped );
}

void edAudioStreamEntry::pauseInternal() {
    // FUN_00614460
    PlayingState = APES_Pause;
    PreviousVolume = 0.0f;
    PreviousPitch = 0;
    notifyListeners( &edAudioStreamEntryListener::onPipeEntryPaused );
}

bool edAudioStreamEntry::prepare( edAudioDSPNode & ) {
    // FUN_00613a20
    return pReader != nullptr && PlayingState != APES_Stop &&
           PlayingState != APES_Pause;
}

float edAudioStreamEntry::evaluateVolume( const edAudioDSPNode &node ) {
    // FUN_00613c90
    const float gain = Fader.evaluate();

    if ( !Fader.isFading() ) {
        if ( FadeTarget == APEFT_Stop ) {
            PlayingState = APES_FadeoutToStop;
        } else if ( FadeTarget == APEFT_Pause ) {
            PlayingState = APES_FadeoutToPause;
        }

        FadeTarget = APEFT_None;
    }

    if ( PlayingState == APES_FadeoutToStop ||
         PlayingState == APES_FadeoutToPause ) {
        return 0.0f;
    }

    return node.Volume * gain;
}

void edAudioStreamEntry::internalProcess( edAudioDSPNode &node,
                                          const edAudioProcessInputs & ) {
    // FUN_00614ad0
    const float volume = evaluateVolume( node );

    float ratio = float( SourceSampleRate ) * node.Pitch *
                  ( 1.0f / float( kAudioOutputSampleRate ) );
    ratio = ( ratio >= 0.0f ) ? Min( ratio, kMaxPlaybackRatio ) : -ratio;

    const int32_t pitch = int32_t( ratio * kPitchScale );

    const int32_t channelCount =
        ( pParent != nullptr ) ? pParent->getInputChannelCount() : 0;
    const int32_t frameCount = int32_t( node.FrameCount );

    if ( bResetRamp ) {
        if ( LoopCount < 1 || CodecFlags != 0 ) {
            PreviousVolume = volume;
            PreviousPitch = pitch;
        } else {
            PreviousVolume = 0.0f;
            PreviousPitch = 0;
        }

        bResetRamp = false;
    }

    float volumeDelta = volume - PreviousVolume;
    if ( fabsf( volumeDelta ) <= kFractionScale ) {
        volumeDelta = 0.0f;
    }

    if ( node.pWorkingBuffer != nullptr && frameCount > 0 &&
         channelCount > 0 ) {
        const int32_t previousPitch = PreviousPitch;
        const float volumeStep = volumeDelta / float( frameCount );
        const int32_t pitchStep = ( pitch - previousPitch ) / frameCount;

        int32_t written = 0;
        int32_t refillsLeft = frameCount + 1;

        while ( refillsLeft-- > 0 ) {
            written += mixBlock( channelCount, frameCount, frameCount - written,
                                 node.pWorkingBuffer + written, volumeStep,
                                 pitchStep );

            if ( written >= frameCount ) {
                break;
            }

            if ( !advanceBlock() && PlayingState == APES_Stop ) {
                break;
            }
        }
    }

    if ( PlayingState == APES_Stop ) {
        releaseReader();
    } else if ( PlayingState == APES_FadeoutToStop ) {
        stopInternal();
    } else if ( PlayingState == APES_FadeoutToPause ) {
        pauseInternal();
    }
}

int32_t edAudioStreamEntry::mixBlock( int32_t channelCount,
                                      int32_t planeStride,
                                      int32_t frameCount,
                                      float *pDestination,
                                      float volumeStep,
                                      int32_t pitchStep ) {
    // FUN_006140a0
    if ( channelCount <= 0 || SourceChannelCount <= 0 || pData == nullptr ) {
        return 0;
    }

    const int32_t availableSamples = AvailableSampleCount;

    int32_t position = Position;
    float volume = PreviousVolume;
    int32_t pitch = PreviousPitch;
    int32_t written = 0;

    for ( int32_t channel = 0; channel < channelCount; channel++ ) {
        position = Position;
        volume = PreviousVolume;
        pitch = PreviousPitch;

        float previousSample = PreviousSample[channel];
        const int16_t *pPlane =
            pData + ( channel % SourceChannelCount ) * SourcePlaneStride;

        int32_t index = position >> kPositionShift;
        written = 0;

        while ( written < frameCount && index < availableSamples ) {
            const float currentSample = float( pPlane[index] );
            const float fraction =
                float( position & kFractionMask ) * kFractionScale;

            pDestination[written] +=
                ( ( currentSample - previousSample ) * fraction +
                  previousSample ) *
                volume;

            pitch += pitchStep;
            position += pitch >> kPitchShift;
            volume += volumeStep;

            if ( index < ( position >> kPositionShift ) ) {
                index = position >> kPositionShift;
                previousSample = currentSample;
            }

            written++;
        }

        PreviousSample[channel] = previousSample;
        pDestination += planeStride;
    }

    Position = position;
    PreviousVolume = volume;
    PreviousPitch = pitch;

    return written;
}

bool edAudioStreamEntry::advanceBlock() {
    // FUN_006149d0
    ConsumedSampleCount += AvailableSampleCount;

    if ( pPendingData != nullptr ) {
        onDataSwap();
        pData = pPendingData;
        pPendingData = nullptr;
    }

    AvailableSampleCount = TotalSampleCount - ConsumedSampleCount;
    if ( AvailableSampleCount == 0 && PlayingState != APES_Stop ) {
        refill();
    }

    // Clip the block short at the loop point so the rewind lands exactly.
    int32_t limit = EndSample;
    if ( limit > 0 ) {
        if ( LoopCount == -1 || LoopsDone < LoopCount ) {
            limit = LoopEndSample;
        }

        if ( limit <= TotalSampleCount ) {
            AvailableSampleCount = limit - ConsumedSampleCount;
            pPendingData = pData + AvailableSampleCount;
        }
    }

    if ( NextMarkerIndex < MarkerCount &&
         NextMarkerIndex < int32_t( Markers.size() ) ) {
        const int32_t marker = Markers[NextMarkerIndex];
        if ( marker < AvailableSampleCount + ConsumedSampleCount ) {
            AvailableSampleCount = marker - ConsumedSampleCount;
            pPendingData = pData + AvailableSampleCount;
            NextMarkerIndex++;
        }
    }

    Position &= kFractionMask;

    return AvailableSampleCount > 0;
}

void edAudioStreamEntry::refill() {
    // FUN_006147b0
    if ( pReader == nullptr ) {
        stopInternal();
        return;
    }

    pReader->bLoop = ( LoopCount == -1 || LoopsDone < LoopCount );

    if ( PendingSeek >= 0 ) {
        while ( NextMarkerIndex < MarkerCount &&
                NextMarkerIndex < int32_t( Markers.size() ) &&
                Markers[NextMarkerIndex] < PendingSeek ) {
            NextMarkerIndex++;
        }

        const int32_t reached = pReader->seek( PendingSeek );
        ConsumedSampleCount = reached;
        TotalSampleCount = reached;
        SeekDiscardCount = PendingSeek - reached;
        PendingSeek = -1;
    }

    int32_t decodedCount = 0;
    const int16_t *pDecoded = pReader->decode( decodedCount );

    if ( pDecoded == nullptr || decodedCount <= 0 ) {
        AvailableSampleCount = 0;
        onEndOfStream();
        return;
    }

    pData = pDecoded;
    TotalSampleCount += decodedCount;

    if ( decodedCount < SeekDiscardCount ) {
        ConsumedSampleCount += decodedCount;
        AvailableSampleCount = 0;
        SeekDiscardCount -= decodedCount;
        return;
    }

    ConsumedSampleCount += SeekDiscardCount;
    AvailableSampleCount = decodedCount - SeekDiscardCount;
    pData = pDecoded + SeekDiscardCount;
    SeekDiscardCount = 0;
}

void edAudioStreamEntry::onDataSwap() {
    // FUN_00614910
    if ( PlayingState != APES_Stop && ConsumedSampleCount == LoopEndSample &&
         ( LoopCount == -1 || LoopsDone < LoopCount ) ) {
        loopRewind();
    } else if ( ConsumedSampleCount == EndSample ) {
        onEndOfStream();
    }

    if ( bResetRamp && PendingSeek < 1 ) {
        bResetRamp = false;
    }
}

void edAudioStreamEntry::loopRewind() {
    // FUN_006143c0
    LoopsDone++;
    resetStream( 0, LoopsDone );
}

void edAudioStreamEntry::onEndOfStream() {
    // FUN_006146b0
    stopInternal();
}
