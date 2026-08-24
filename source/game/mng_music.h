#pragma once

#include "game/mng_base.h"
#include "core/mutex.h"

#include <memory>

class TestDriveGameInstance;
class AudioClient;
class AudioStereoSource;
class edAudioSample;

class MngMusic : public Manager {
public:
    const char *getName() const override { // FUN_00430f50
        return "Music";
    }
    inline void setPlayFlags( uint32_t flags ) { playFlags = flags; }
    inline uint32_t getPlayFlags() const { return playFlags; }

public:
    MngMusic();
    ~MngMusic();

    void tick( float totalTime, float deltaTime ) override;
    bool initialize( TestDriveGameInstance *pInstance ) override;
    void terminate() override;

    void loadMusic( const char *pFilename, float volume );
    void stop();

private:
    enum eState : int32_t {
        MMS_None = 0,
        MMS_Loading = 1,
        MMS_Ready = 2,
    };

private:
    void onMusicInstalled();

private:
    AudioClient *pAudioClient;
    uint32_t playFlags;
    void *pMusicData;
    uint32_t musicDataSize;
    std::shared_ptr<bool> pInstallGuard;
    std::unique_ptr<AudioStereoSource> pSource;
    std::unique_ptr<edAudioSample> pChannelSamples[2];
    bool bPlaying;
    char musicFilename[32];
    float currentVolume;
    eState state;
    TestDriveMutex loadingMutex;
};

extern MngMusic *gpMngMusic;
