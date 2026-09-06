#pragma once

#include "ed_audio_object.h"

// Per channel state of a reverb module
struct edAudioReverbChannel {
    static constexpr int32_t kDelayLineSize = 8192;
    static constexpr int32_t kDelayLineMask = kDelayLineSize - 1;
    static constexpr int32_t kDelayLineGuard = 8;
    static constexpr int32_t kMaxTaps = 100;

    struct Tap {
        float Weight;
        float Gain;
        float Delay; // seconds * (1 / (speed of sound * delay line size))
        float Padding;
    };

    int32_t WriteCursor;
    float DelayLine[kDelayLineSize + kDelayLineGuard];
    float DryWetRatio;
    float DryVolume;
    float WetVolume;
    float LowPassCutoff; // normalized over the output sample rate
    float FilterState;
    int32_t TapCount;
    Tap Taps[kMaxTaps];

    void initialize();
    void reset();
    void normalizeTaps();
    void setTapCount( int32_t count );

    void setDryVolume( float volume );
    void setWetVolume( float volume );
    void setDryWetRatio( float ratio );
    void setLowPassCutoff( float cutoffHz );
    void setRoom( float size, int32_t complexity, float diffusion );
    void
    process( const float *pSource, float *pDestination, int32_t frameCount );
};
OTDU_SIZE_MUST_MATCH( edAudioReverbChannel, 0x867c );

class edAudioReverb : public edAudioObject {
public:
    inline edAudioBuffer *getBuffer() { return &Buffer; }

    inline int32_t getChannelCount() const {
        // FUN_006119a0
        return OutputChannelCount;
    }
    inline edAudioReverbChannel *getChannel( int32_t channel ) {
        // FUN_006117d0
        return ( pChannels != nullptr && channel >= 0 &&
                 channel < OutputChannelCount )
                   ? &pChannels[channel]
                   : nullptr;
    }
    inline bool isAttached() const { return getParent() != nullptr; }

public:
    edAudioReverb( int32_t channelCount, const char *pName );
    ~edAudioReverb() override;

    void onEntryAttached( edAudioObject *pParent ) override;
    void onEntryDetached( edAudioObject *pParent ) override;
    void internalProcess( edAudioDSPNode &node,
                          const edAudioProcessInputs &inputs ) override;

    void reset();

private:
    void releaseChannels();

private:
    edAudioReverbChannel *pChannels;
    edAudioBuffer Buffer;
};
