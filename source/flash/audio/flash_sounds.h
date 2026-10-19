#pragma once

#include "filesystem/bank.h"

#include "audio/implementation/ed_audio_sample.h"
#include "audio/gs_audio.h"

#include "gps_voice.h"

#include <memory>
#include <unordered_map>

class edAudioStreamEntry;

class FlashSounds : public Bank {
public:
    static constexpr size_t kMaxGroupNameLength = 9;
    static constexpr int32_t kNumVoices = 10;

public:
    inline GPSVoice *getGPSVoice() { return &gpsVoice; }

public:
    FlashSounds();
    ~FlashSounds();

    bool initialize( void *pMemory );
    void terminate();

    void loadFrontEndSounds();

    bool
    playFrontEndSound( const char *param_2, int32_t param_3, bool param_4 );
    bool playFrontEndMusic( bool bFromCine );
    void stopFrontEndMusic();
    bool stopGroup( const char *pGroupName, float fadeTime );
    void stopAllGroups();
    void flushPendingSound();

public:
    // One AddFESound line out of the bank's sounds.ini
    struct FrontEndSound {
        std::string FileName;
        float Volume = 1.0f;
    };

    struct FrontEndMusic {
        char FileName[32] = {};
        char Key[12] = {};
        float Volume = 1.0f;
        bool bWaitCine = false;
    };
    static constexpr int32_t kMaxMusicEntries = 21;

    struct PendingSound {
        char Name[32] = {};
        bool bLoop = false;
    };
    static constexpr int32_t kMaxPendingSounds = 1;

private:
    void parseSoundList();
    const FrontEndSound *findSound( const char *pFlashMessage ) const;

#ifdef OTDU_USE_IMGUI // TODO: REMOVEME: SOUND TEST MENU FOR FRONTEND
public:
    inline const std::unordered_map<std::string, FrontEndSound> &
    getSoundList() const {
        return soundList;
    }
    const char *getVoiceGroup( int32_t voiceIndex ) const;
    bool isVoicePlaying( int32_t voiceIndex ) const;

private:
#endif

    bool queuePendingSound( const char *pName, bool bLoop );
    const edAudioSample *getSample( const char *pName );
    int32_t getFreeVoice();
    const FrontEndMusic *findMusic( const char *pKey ) const;
    static const char *ResolveGameModeMusicKey();

private:
    void *pMemoryPool;
    AudioClient *pAudioClient = nullptr;

    bool bInitialized = false;
    GPSVoice gpsVoice;
    PipelineMono pipeline;
    std::unique_ptr<edAudioStreamEntry> voices[kNumVoices];
    char voiceGroups[kNumVoices][kMaxGroupNameLength] = {};
    std::unordered_map<std::string, std::unique_ptr<edAudioSample>> samples;
    std::unordered_map<std::string, FrontEndSound> soundList;
    FrontEndMusic musicList[kMaxMusicEntries] = {};
    int32_t numMusicEntries = 0;
    PendingSound pendingSounds[kMaxPendingSounds] = {};
};
