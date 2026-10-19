#pragma once

#include "ed_audio_stream_entry.h"

// A decoded sound held in memory (.wav; mono at 44100Hz).
class edAudioSample {
public:
    inline int32_t getChannelCount() const { return channelCount; }
    inline int32_t getFrameCount() const { return frameCount; }
    inline int32_t getSampleRate() const { return sampleRate; }
    inline int32_t getLoopStart() const { return loopStart; }
    inline int32_t getLoopEnd() const { return loopEnd; }
    inline int32_t getMarkerCount() const {
        return static_cast<int32_t>( markers.size() );
    }
    inline int32_t getMarker( int32_t index ) const { return markers[index]; }
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
    void parseCuePoints( const uint8_t *pData, int32_t size );
    bool takeDecoded( struct SDL_AudioSpec &sourceSpec,
                      uint8_t *pDecoded,
                      uint32_t decodedSize,
                      const char *pDebugName );

private:
    std::vector<int16_t> samples;
    int32_t channelCount;
    int32_t frameCount;
    int32_t sampleRate;
    int32_t loopStart; // -1 when the sample has no "loop" cue
    int32_t loopEnd;
    std::vector<int32_t> markers;
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
    int32_t getMarker( int32_t index ) const override;

private:
    const edAudioSample *pSample;
    int32_t readPosition;
};
