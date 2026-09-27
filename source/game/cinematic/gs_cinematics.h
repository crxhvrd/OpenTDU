#pragma once

#include "game/gs_base.h"

class AudioClient;
struct ClientConfig;
class PipelineStereo;

enum eCinematicType : int32_t; // mng_cinematics.h

class GSCinematics : public GameSystem {
public:
    const char *getName() const override { return "Service : Cinematics"; }
    inline bool isInitialized() const { return bInitialized; }

public:
    GSCinematics();
    ~GSCinematics();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void terminate() override;
    PipelineStereo *getMusicPipeline() const;
    ClientConfig *getMusicClientConfig() const;

    bool isCinematicPlaying( eCinematicType type,
                             bool bRequireAudio,
                             bool bIncludeRequested ) const;

private:
    AudioClient *pMusicClient;

    int32_t field_0x138;
    int32_t field_0x13c;

    bool bInitialized;
};

extern GSCinematics *gpCinematics;
