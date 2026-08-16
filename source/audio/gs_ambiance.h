#pragma once

#include "game/gs_base.h"
#include "core/mutex.h"
#include "ambiance_environment.h"

class AudioClient;

class GSAmbiance : public GameSystem {
public:
    const char *getName() const override { return "Service : Ambiance"; }

    inline float getInternalViewVolumeFactor() const {
        return InternalViewVolumeFactor;
    }

public:
    GSAmbiance();
    ~GSAmbiance();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void terminate() override;

#ifdef OTDU_USE_IMGUI
    void drawDebug( float deltaTime ) override;
#endif

    bool isAmbianceRegionActive( int32_t classID );
    void registerSectorEmitters( const void *pBlob, const void *pOwner );
    void unregisterSectorEmitters( const void *pOwner );

private:
    bool isAmbianceRegionActiveUnlocked( int32_t classID ) const;
    float computeInternalViewVolumeFactor();
    void clearEmitterLists();
    void gatherEmitters( const Eigen::Vector4f &listener );
    void updateEnvironments( const Eigen::Vector4f &listener,
                             const Eigen::Vector3f &listenerLeftToRight );
    bool isShowcaseInteriorActive() const;
    void resetReverbModule();
    void applyReverbPreset( AmbianceReverbPreset *pPreset );
    void updateReverbModule();
    bool loadCommonConfig();

private:
    TestDriveMutex Mutex;

    float InteriorVolumeMin = 0.1f; // FLOAT_00f41668
    float InteriorVolumeMax = 0.7f; // DAT_00f41828

    void *pReverbModule = nullptr; // TODO:

    void *pCommonConfig = nullptr; // the whole common.xmb document
    bool bOwnsCommonConfig = false;

    AudioClient *pEnvironmentClient = nullptr;
    AmbiancePool<AmbianceEnvironment> Environments;
    AmbiancePool<AmbianceEmitter> Emitters;
    AmbiancePool<AmbianceReverbPreset> ReverbPresets;

    AmbianceEnvironment *pActiveEnvironment = nullptr;
    AmbianceReverbPreset *pActiveReverbPreset = nullptr;
    float InternalViewVolumeFactor = 1.0f;
};

extern GSAmbiance *gpAmbiance;
