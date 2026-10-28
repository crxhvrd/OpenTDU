#pragma once

#include "audio/implementation/ed_audio_stream_entry.h"

class AudioDispatcherPlaybackSoundInstance;
class CinResCollection;
class EntityAvatar;
class edAudioSample;

// Cinematic voice line ("Sound/Voices/<language>/<name>.wav") ("plvo")
class CinematicVoiceEmitter : public edAudioStreamEntryListener {
public:
    static constexpr int32_t kMaxEmittersPerCinematic = 20; // 0x14
    static constexpr int32_t kNameLength = 0x20;

    enum eState : int32_t {
        CVES_None = 0,
        CVES_Loading = 1,
        CVES_Ready = 2,
    };

public:
    inline bool isReady() const { return State == CVES_Ready; }

public:
    CinematicVoiceEmitter();
    ~CinematicVoiceEmitter();

    void install( char *pVoiceName, float volume, CinResCollection *pOwner );
    void play();
    void playFrom( EntityAvatar *pAvatar );
    void setPosition( const Eigen::Vector3f &position );
    void stop();
    void uninstall();

    void onPipeEntryStopped( edAudioStreamEntry *pEntry ) override;
    void onPipeEntryEnded( edAudioStreamEntry *pEntry ) override;

private:
    bool loadSample( const char *pPath );
    void createInstance();

private:
    eState State;
    AudioDispatcherPlaybackSoundInstance *pInstance;
    std::unique_ptr<edAudioSample> pSample;
    float Volume;
    EntityAvatar *pOwnerAvatar;
    char Name[kNameLength];
};
