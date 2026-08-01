#pragma once

#include "audio/implementation/ed_audio_stream_entry.h"

#if OTDU_USE_LIBBINKDEC
#include "BinkDecoder.h"
#endif

class edAudioBuffer;
class edAudioChannelAdapter;
class edAudioObject;

class BinkAudioRing {
public:
    inline int32_t getChannelCount() const { return channelCount; }
    inline int32_t getSampleRate() const { return sampleRate; }
    inline bool isFinished() const {
        return bFinished.load( std::memory_order_acquire );
    }

public:
    BinkAudioRing();

    bool initialize( int32_t inChannelCount,
                     int32_t inSampleRate,
                     int32_t inCapacityFrames );
    void destroy();

    int32_t getAvailableFrames() const;

    // Game thread.
    void pushInterleaved( const int16_t *pFrames, int32_t frameCount );
    void markFinished();

    // Audio thread.
    int32_t popDeinterleaved( int16_t *pPlanes,
                              int32_t planeStride,
                              int32_t frameCount );

private:
    std::vector<int16_t> storage; // interleaved, capacityFrames * channelCount
    int32_t channelCount;
    int32_t sampleRate;
    int32_t capacityFrames;

    std::atomic<uint32_t> writeCursor;
    std::atomic<uint32_t> readCursor;
    std::atomic<bool> bFinished;

    uint32_t droppedFrames;
    bool bDropReported;
};

class BinkAudioStreamReader final : public edAudioStreamReader {
public:
    // Frames handed over per decode() call.
    static constexpr int32_t kDecodeChunkFrames = 2048;
    // Frames of silence emitted when the ring has run dry mid-movie.
    static constexpr int32_t kSilenceChunkFrames = 256;

public:
    explicit BinkAudioStreamReader( BinkAudioRing *pInRing );

    bool getFormat( Format &format ) const override;
    const int16_t *decode( int32_t &sampleCount ) override;
    int32_t seek( int32_t position ) override;

private:
    BinkAudioRing *pRing;
    std::vector<int16_t> planes; // channelCount * kDecodeChunkFrames
};

class MovieAudioPipeline {
public:
    inline edAudioBuffer *getBuffer() { return pBuffer; }

public:
    MovieAudioPipeline();
    ~MovieAudioPipeline();

    bool create( edAudioObject *pParent );
    void destroy();

private:
    edAudioObject *pRoot;
    edAudioChannelAdapter *pAdapter;
    edAudioBuffer *pBuffer;
};

class BinkMovieAudio {
public:
    inline bool hasAudio() const { return pVoice != nullptr; }

public:
    BinkMovieAudio();
    ~BinkMovieAudio();

#if OTDU_USE_LIBBINKDEC
    bool initialize( BinkHandle &handle, const char *pDebugName );
    void pumpDecodedFrame( BinkHandle &handle );
#endif

    void destroy();

    void requestPlayback();
    void markFinished();
    void tick();

private:
    static constexpr float kRingSeconds = 1.0f;
    static constexpr int32_t kPrimeFrames = 2048;

private:
    BinkAudioRing ring;
    std::vector<int16_t> decodeScratch; // interleaved, Bink's ideal buffer size
    uint32_t trackIndex;
    std::unique_ptr<edAudioStreamEntry> pVoice;
    bool bPlaybackRequested;
    bool bStarted;
};
