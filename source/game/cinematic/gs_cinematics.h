#pragma once

#include "game/gs_base.h"

class AudioClient;
class PipelineStereo;

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

private:
    AudioClient *pMusicClient;
    bool bInitialized;   
};

extern GSCinematics *gpCinematics;
