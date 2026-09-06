#include "shared.h"
#include "ed_audio_reverb.h"

#include "ed_audio_backend.h"
#include "ed_audio_memory.h"

static constexpr float kSampleMin = -32768.0f;              // DAT_00ef1b5c
static constexpr float kSampleMax = 32767.0f;               // DAT_00f419b8
static constexpr float kDenormalThreshold = 3.0517578e-05f; // DAT_00f41658

static constexpr float kDefaultRatio = 0.5f; // FLOAT_00f41674
static constexpr float kMaxRoomSize = 25.0f; // DAT_00ef1b50, meters

static constexpr float kInverseSampleRate =
    1.0f / float( kAudioOutputSampleRate ); // DAT_00ef1b54
static constexpr float kRoundTripSampleRate =
    2.0f * float( kAudioOutputSampleRate ); // DAT_00ef1b60

static constexpr float kRoomWeightBase = 10.0f;       // DAT_00ef1b68
static constexpr float kRoomWeightExponent = -0.005f; // DAT_00ef1b64
static constexpr float kInverseSoundTravel =
    1.0f /
    ( 340.0f * float( edAudioReverbChannel::kDelayLineSize ) ); // DAT_00f47a00

static constexpr float kInverseRandMax = 1.0f / 32767.0f; // DAT_00f41714
static constexpr float kTwoPi = 6.2831855f;               // -DAT_00f417e0

static inline float RandomUnit() {
    return float( rand() & 0x7fff ) * kInverseRandMax;
}

void edAudioReverbChannel::initialize() {
    // FUN_00611c80
    WriteCursor = 0;
    memset( DelayLine, 0, sizeof( DelayLine ) );

    DryWetRatio = kDefaultRatio;
    LowPassCutoff = kDefaultRatio;
    TapCount = 0;
    DryVolume = 1.0f;
    WetVolume = 1.0f;
    FilterState = 0.0f;

    memset( Taps, 0, sizeof( Taps ) );

    normalizeTaps();
}

void edAudioReverbChannel::reset() {
    // FUN_006119b0
    FilterState = 0.0f;
    memset( DelayLine, 0, sizeof( DelayLine ) );
}

void edAudioReverbChannel::normalizeTaps() {
    // FUN_00611820
    float weightSum = 0.0f;
    for ( int32_t i = 0; i < TapCount; i++ ) {
        weightSum += Taps[i].Weight;
    }

    if ( weightSum <= 0.0f ) {
        weightSum = 1.0f;
    }

    const float scale = 1.0f / weightSum;
    for ( int32_t i = 0; i < TapCount; i++ ) {
        Taps[i].Gain = Taps[i].Weight * scale;
    }
}

void edAudioReverbChannel::setTapCount( int32_t count ) {
    // FUN_00611900
    count = Clamp( count, 0, kMaxTaps );

    for ( int32_t i = count; i < TapCount; i++ ) {
        Taps[i] = Tap{};
    }

    TapCount = count;
    normalizeTaps();
}

void edAudioReverbChannel::setDryVolume( float volume ) {
    // FUN_00611720
    DryVolume = volume;
}

void edAudioReverbChannel::setWetVolume( float volume ) {
    // FUN_00611740
    WetVolume = volume;
}

void edAudioReverbChannel::setDryWetRatio( float ratio ) {
    // FUN_00611760
    DryWetRatio = Clamp( ratio, 0.0f, 1.0f );
}

void edAudioReverbChannel::setLowPassCutoff( float cutoffHz ) {
    // FUN_00611790
    LowPassCutoff = cutoffHz * kInverseSampleRate;
}

void edAudioReverbChannel::setRoom( float size,
                                    int32_t complexity,
                                    float diffusion ) {
    // FUN_00611d20
    setTapCount( complexity );

    if ( complexity <= 0 ) {
        return;
    }

    size = Clamp( size, 0.0f, kMaxRoomSize );
    diffusion = Clamp( diffusion, 0.0f, 1.0f );

    Taps[0].Weight =
        Saturate( powf( kRoomWeightBase, size * kRoomWeightExponent ) );
    normalizeTaps();
    Taps[0].Delay = kInverseSoundTravel * size * kRoundTripSampleRate;

    for ( int32_t i = 1; i < TapCount; i++ ) {
        const float distance = size - RandomUnit() * diffusion * size;

        Taps[i].Weight =
            Saturate( powf( kRoomWeightBase, distance * kRoomWeightExponent ) );
        normalizeTaps();
        Taps[i].Delay = kInverseSoundTravel * distance * kRoundTripSampleRate;
    }
}

void edAudioReverbChannel::process( const float *pSource,
                                    float *pDestination,
                                    int32_t frameCount ) {
    // FUN_006119f0
    if ( std::isnan( FilterState ) ) {
        FilterState = 0.0f;
        memset( DelayLine, 0, sizeof( DelayLine ) );
    } else if ( fabsf( FilterState ) >= kDenormalThreshold ) {
        if ( FilterState > kSampleMax ) {
            FilterState = kSampleMax;
        } else if ( FilterState < kSampleMin ) {
            FilterState = kSampleMin;
        }
    } else {
        FilterState = 0.0f;
    }

    for ( int32_t i = 0; i < kDelayLineGuard; i++ ) {
        DelayLine[kDelayLineSize + i] = DelayLine[i];
    }

    int32_t tapOffsets[kMaxTaps] = {};
    for ( int32_t tap = 0; tap < TapCount; tap++ ) {
        tapOffsets[tap] = kDelayLineGuard -
                          int32_t( Taps[tap].Delay * -float( kDelayLineSize ) );
    }

    const float filterFeedback = expf( LowPassCutoff * -kTwoPi );
    const float filterGain = 1.0f - filterFeedback;

    for ( int32_t frame = 0; frame < frameCount; frame++ ) {
        const float drySample = pSource[frame];

        float wetSample = 0.0f;
        for ( int32_t tap = 0; tap < TapCount; tap++ ) {
            const int32_t readIndex =
                ( WriteCursor - tapOffsets[tap] + frame ) & kDelayLineMask;
            wetSample += DelayLine[readIndex] * Taps[tap].Gain;
        }

        pDestination[frame] += WetVolume * wetSample + DryVolume * drySample;

        FilterState =
            ( ( 1.0f - DryWetRatio ) * wetSample + drySample * DryWetRatio ) *
                filterGain +
            filterFeedback * FilterState;

        if ( fabsf( FilterState ) < kDenormalThreshold ) {
            FilterState = 0.0f;
        }

        DelayLine[( WriteCursor + frame ) & kDelayLineMask] = FilterState;
    }

    WriteCursor = ( WriteCursor + frameCount ) & kDelayLineMask;
}

edAudioReverb::edAudioReverb( int32_t channelCount, const char *pName )
    : edAudioObject( channelCount, channelCount ),
      pChannels( nullptr ),
      Buffer( channelCount ) {
    // FUN_006116f0 + FUN_00402490
    Buffer.debugSetDescription( "Reverb buffer" );
    debugSetDescription( pName );
    addNewEntry( &Buffer );
}

edAudioReverb::~edAudioReverb() {
    // FUN_00402510
    unlinkEntry( &Buffer );
    Buffer.unlinkAllEntries();
    unlinkAllEntries();
    releaseChannels();
}

void edAudioReverb::onEntryAttached( edAudioObject * ) {
    // FUN_00611ed0
    releaseChannels();

    pChannels = static_cast<edAudioReverbChannel *>( edAudioHeap::Allocate(
        OutputChannelCount * int32_t( sizeof( edAudioReverbChannel ) ) ) );

    if ( pChannels == nullptr ) {
        OTDU_LOG_ERROR( "Out of audio memory for reverb '%s' (%i channels)\n",
                        getDescription(), OutputChannelCount );
        return;
    }

    for ( int32_t channel = 0; channel < OutputChannelCount; channel++ ) {
        pChannels[channel].initialize();
    }
}

void edAudioReverb::onEntryDetached( edAudioObject * ) {
    // FUN_00611800
    releaseChannels();
}

void edAudioReverb::releaseChannels() {
    edAudioHeap::Free( pChannels );
    pChannels = nullptr;
}

void edAudioReverb::internalProcess( edAudioDSPNode &node,
                                     const edAudioProcessInputs &inputs ) {
    // FUN_00611f20
    if ( inputs.Count == 0 || pChannels == nullptr ||
         node.pWorkingBuffer == nullptr || inputs.ppBuffers[0] == nullptr ) {
        return;
    }

    const float *pSource = inputs.ppBuffers[0];
    float *pDestination = node.pWorkingBuffer;
    const int32_t frameCount = int32_t( node.FrameCount );

    for ( int32_t channel = 0; channel < OutputChannelCount; channel++ ) {
        const int32_t offset = frameCount * channel;
        pChannels[channel].process( pSource + offset, pDestination + offset,
                                    frameCount );
    }
}

void edAudioReverb::reset() {
    // FUN_006119b0
    if ( pChannels == nullptr ) {
        return;
    }

    for ( int32_t channel = 0; channel < OutputChannelCount; channel++ ) {
        pChannels[channel].reset();
    }
}
