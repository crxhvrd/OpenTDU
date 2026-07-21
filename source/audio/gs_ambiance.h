#pragma once

#include "game/gs_base.h"
#include "core/mutex.h"

class GSAmbiance : public GameSystem {
public:
    const char *getName() const override { return "Service : Ambiance"; }

public:
    GSAmbiance();
    ~GSAmbiance();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void terminate() override;

private:
    TestDriveMutex Mutex;
    float field_0x78;
    float field_0x7c;
    int32_t ReverbModule; // SETAMBIANCEREVERBMODULE
    void *pCommonConfig;  // common.xmb buffer
    bool bOwnsCommonConfig;
    void *pEnvironmentClient;
    void *pEnvironmentPool;
    void *pEmitterInstancePool;
    void *pReverbPresetPool;
    void *pActiveEnvironment;
    int32_t field_0xa0;
    float field_0xa4;
};

extern GSAmbiance *gpAmbiance;
