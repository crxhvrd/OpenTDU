#pragma once

#include "filesystem/bank.h"

#include "audio/gs_audio.h"

#include <memory>

class edAudioSample;
class edAudioStreamEntry;

class GPSVoice : public Bank {
public:
    static constexpr uint32_t kMemoryPoolSize = 0x120002;

public:
    inline void setMemoryPool( void *pMemory ) { pMemoryPool = pMemory; }

public:
    GPSVoice();
    ~GPSVoice();

    bool initialize();
    void initializeFromDatabase();
    void terminate();
    bool play( const int32_t index );

    void loadContent() override;

private:
    enum class eAudioState {
        GVAS_Unknown = 0,
        GVAS_WaitingForParsing = 1,
        GVAS_Ready = 2
    };

    struct Sample {
        std::unique_ptr<edAudioSample> pContent;
        char *pName = nullptr;
    };

private:
    void *pMemoryPool;
    eAudioState audioState;
    std::array<Sample, 0x11> audioSamples;
    AudioClient *pAudioClient = nullptr;
    PipelineMono pipeline;
    std::unique_ptr<edAudioStreamEntry> pSoundInstance;

    uint8_t bEnabled : 1;
};
