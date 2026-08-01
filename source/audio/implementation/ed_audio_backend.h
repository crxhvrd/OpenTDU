#pragma once

// Low level audio output sink.

// Output sample rate, in Hz. FUN_00616fd0 fills its WAVEFORMATEXTENSIBLE with
// nSamplesPerSec = 48000 and wBitsPerSample = 16.
static constexpr int32_t kAudioOutputSampleRate = 48000;

// Number of frames the engine processes in one go.
static constexpr int32_t kAudioFramesPerBlock = 1024;
static constexpr int32_t kAudioRingBlockCount = 4;

class edAudioBackend {
public:
    inline int32_t getChannelCount() const { return channelCount; }

public:
    virtual ~edAudioBackend() {}

    // Opens the output device. 'numChannels' is the interleaved channel count
    // the engine will mix at (see GetPreferredAudioChannelCount).
    virtual bool initialize( int32_t numChannels ) = 0;
    virtual void shutdown() = 0;

    // Hands one block of interleaved frames over to the device. Expects
    // 'frameCount * getChannelCount()' floats.
    virtual void submit( const float *pInterleavedFrames,
                         int32_t frameCount ) = 0;

    // Frames already handed over but not played back yet. The engine paces
    // itself against this, the way the original waits on its DirectSound play
    // cursor notifications.
    virtual int32_t getQueuedFrames() const = 0;

protected:
    edAudioBackend() : channelCount( 0 ) {}

protected:
    int32_t channelCount;
};

edAudioBackend *CreateAudioBackend();
int32_t GetPreferredAudioChannelCount();
