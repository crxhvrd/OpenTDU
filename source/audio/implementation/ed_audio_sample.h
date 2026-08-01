#pragma once

#include "ed_audio_stream_entry.h"

// A decoded sound held in memory (.wav; mono at 44100Hz).
class edAudioSample {
public:
    inline int32_t getChannelCount() const { return channelCount; }
    inline int32_t getFrameCount() const { return frameCount; }
    inline int32_t getSampleRate() const { return sampleRate; }
    inline bool isLoaded() const { return !samples.empty(); }
    inline const int16_t *getChannel( int32_t channel ) const {
        return samples.data() + channel * frameCount;
    }

    inline const int16_t *getSamples() const { return samples.data(); }

public:
    edAudioSample();
    ~edAudioSample();

    bool loadFromFile( const char *pFilePath );
    bool loadFromMemory( const void *pData, int32_t size );
    void unload();

private:
    bool takeDecoded( struct SDL_AudioSpec &sourceSpec,
                      uint8_t *pDecoded,
                      uint32_t decodedSize,
                      const char *pDebugName );

private:
    std::vector<int16_t> samples;
    int32_t channelCount;
    int32_t frameCount;
    int32_t sampleRate;
};

class edAudioSampleReader : public edAudioStreamReader {
public:
    static constexpr int32_t kDecodeChunkSamples = 2048;

public:
    explicit edAudioSampleReader( const edAudioSample *pInSample = nullptr );

    void setSample( const edAudioSample *pInSample );

    bool getFormat( Format &format ) const override;
    const int16_t *decode( int32_t &sampleCount ) override;
    int32_t seek( int32_t position ) override;

private:
    const edAudioSample *pSample;
    int32_t readPosition;
};
