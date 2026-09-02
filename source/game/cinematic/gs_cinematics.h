#pragma once

#include "game/gs_base.h"

class AudioClient;
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
    void terminate() override;
    PipelineStereo *getMusicPipeline() const;

    bool isCinematicPlaying( eCinematicType type,
                             bool bRequireAudio,
                             bool bIncludeRequested ) const;

private:
    AudioClient *pMusicClient;
    bool bInitialized;   
};

extern GSCinematics *gpCinematics;
