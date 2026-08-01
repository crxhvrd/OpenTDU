#pragma once

#include "ed_audio_object.h"

#include <memory>

class edAudioStreamEntry;
class edAudioSample;

class edAudioFader {
public:
    enum eMode : int32_t {
        AFM_Idle = 0,
        AFM_FadeIn = 1,
        AFM_FadeOut = 2,
    };

public:
    inline bool isFading() const { return Mode != AFM_Idle; }

public:
    edAudioFader();

    float evaluate();
    void startFadeIn( float duration );
    void startFadeOut( float duration );

private:
    void start( eMode mode, float duration, float idleGain );

public:
    int32_t Mode;
    float Duration;
    float StartTime;
    float Gain;
};

class edAudioStreamEntryListener {
public:
    virtual ~edAudioStreamEntryListener() = default;

    virtual void onPipeEntryPlayed( edAudioStreamEntry * ) { /* FUN_00415350 */
    }
    virtual void onPipeEntryResumed( edAudioStreamEntry * ) { /* FUN_004019d0 */
    }
    virtual void onPipeEntryStopped( edAudioStreamEntry * ) { /* FUN_00614cc0 */
    }
    virtual void onPipeEntryPaused( edAudioStreamEntry * ) { /* FUN_004019c0 */
    }
};

class edAudioStreamReader {
public:
    struct Format {
        int32_t ChannelCount;
        int32_t PlaneStride;
        int32_t SampleRate;
        int32_t CodecFlags;
        int32_t EndSample;
        int32_t LoopEndSample;
        int32_t MarkerCount;
    };

public:
    virtual ~edAudioStreamReader() = default;

    virtual bool getFormat( Format &format ) const = 0;
    virtual const int16_t *decode( int32_t &sampleCount ) = 0;
    virtual int32_t seek( int32_t position ) = 0;
    virtual int32_t getMarker( int32_t index ) const {
        // FUN_00619100
        return 0;
    }

public:
    bool bLoop = false;
};

class edAudioStreamEntry : public edAudioObject {
public:
    enum eState : int32_t {
        APES_Stop = 0,
        APES_Play = 1,
        APES_Pause = 2,
        APES_FadeoutToStop = 3,
        APES_FadeoutToPause = 4,
    };

    enum eFadeTarget : int32_t {
        APEFT_None = 0,
        APEFT_Play = 1,
        APEFT_Stop = 2,
        APEFT_Pause = 3,
    };

    static constexpr int32_t kMaxListeners = 4;
    static constexpr int32_t kMaxSourceChannels = 8;
    static constexpr int32_t kPositionShift = 15;
    static constexpr int32_t kPitchShift = 11;
    static constexpr int32_t kFractionMask = 0x7fff;
    static constexpr float kFractionScale = 1.0f / 32768.0f; // DAT_00ef1c44
    static constexpr float kPitchScale = 67108864.0f;        // DAT_00ef1c40
    static constexpr float kMaxPlaybackRatio = 16.0f;        // DAT_00f418d8

public:
    inline eState getPlayingState() const { return PlayingState; }
    inline bool isStopped() const {
        // FUN_00613880
        return PlayingState == APES_Stop;
    }
    inline bool isPaused() const {
        // FUN_00613890
        return PlayingState == APES_Pause;
    }
    inline const char *getName() const { return Name; }
    inline float getDuration() const {
        // FUN_00613940
        return ( SourceSampleRate > 0 )
                   ? float( EndSample ) / float( SourceSampleRate )
                   : 0.0f;
    }

    inline float getPosition() const {
        // FUN_006138b0
        const int32_t position =
            ( PendingSeek >= 1 ) ? PendingSeek : ConsumedSampleCount;
        return ( SourceSampleRate > 0 )
                   ? float( position ) / float( SourceSampleRate )
                   : 0.0f;
    }

public:
    edAudioStreamEntry();
    ~edAudioStreamEntry() override;

    bool setSample( const edAudioSample *pInSample );
    bool setReader( std::unique_ptr<edAudioStreamReader> pInReader );

    void play( float fadeTime = 0.0f );
    void stop( float fadeTime = 0.0f );
    void pause( float fadeTime = 0.0f );
    void setLoopCount( int32_t loopCount );
    void seek( float seconds, bool bWrapIntoLoop = false );
    bool addListener( edAudioStreamEntryListener *pListener );
    void removeListener( edAudioStreamEntryListener *pListener );
    void onEntryAttached( edAudioObject *pParent ) override;
    bool prepare( edAudioDSPNode &node ) override;
    void internalProcess( edAudioDSPNode &node,
                          const edAudioProcessInputs &inputs ) override;

private:
    using ListenerEvent =
        void ( edAudioStreamEntryListener::* )( edAudioStreamEntry * );

private:
    float evaluateVolume( const edAudioDSPNode &node );
    int32_t mixBlock( int32_t channelCount,
                      int32_t planeStride,
                      int32_t frameCount,
                      float *pDestination,
                      float volumeStep,
                      int32_t pitchStep );

    bool advanceBlock();
    void refill();
    void onDataSwap();
    void loopRewind();
    void onEndOfStream();

    void resetStream( int32_t seekPosition, int32_t loopsDone );
    void cacheReaderFormat();
    void stopInternal();
    void pauseInternal();
    void releaseReader();
    void notifyListeners( ListenerEvent event );

private:
    char Name[32];
    int32_t SourceChannelCount;
    int32_t SourcePlaneStride;
    int32_t SourceSampleRate;
    int32_t CodecFlags;
    int32_t EndSample;
    int32_t LoopEndSample;
    int32_t MarkerCount;
    std::vector<int32_t> Markers;
    const edAudioSample *pSampleData;
    std::unique_ptr<edAudioStreamReader> pReader;
    const int16_t *pData;        // current block
    const int16_t *pPendingData; // promoted on the next advance
    int32_t NextMarkerIndex;
    int32_t PendingSeek; // -1 when there is none
    int32_t TotalSampleCount;
    int32_t ConsumedSampleCount;
    int32_t SeekDiscardCount;
    int32_t AvailableSampleCount;
    int32_t Position;
    float PreviousSample[kMaxSourceChannels];
    edAudioStreamEntryListener *pListeners[kMaxListeners];
    eState PlayingState;
    edAudioFader Fader;
    eFadeTarget FadeTarget;
    float PreviousVolume;
    int32_t PreviousPitch;
    int32_t LoopCount; // -1 loops forever
    int32_t LoopsDone;
    bool bResetRamp;
};
