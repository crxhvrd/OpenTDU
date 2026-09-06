#pragma once

#include "game/gs_base.h"
#include "audio/implementation/ed_audio_sample.h"
#include "audio/gs_audio.h"
#include "gs_vehicle_sound_model.h"
#include "vst_physics_wrapper.h"

class CarState;
class edAudioReverb;

class VehicleVoiceSource final : public IVSTSoundInstanceSource {
public:
    explicit VehicleVoiceSource( AudioClient *pInClient )
        : pClient( pInClient ) {}
    ~VehicleVoiceSource() override;

    AudioDispatcherPlaybackSoundInstance *
    acquireSoundInstance( const void *pWaveData,
                          const char *pName,
                          bool bLoop,
                          float &duration ) override;
    void releaseSoundInstance(
        AudioDispatcherPlaybackSoundInstance *pInstance ) override;
    int32_t forgetSamplesIn( const void *pBase, uint32_t size );

private:
    const edAudioSample *resolveSample( const void *pWaveData );

private:
    AudioClient *pClient = nullptr;
    std::unordered_map<const void *, std::unique_ptr<edAudioSample>> samples;
};

struct VehicleSoundInstance {
    VSTPhysicsWrapper Wrapper;
    VehicleSoundModel Model;
    CarState *pCarState = nullptr;

    VehicleSoundBank *pBank = nullptr;

    bool bIsCar = true;
    bool bEngineBuilt = false;
    float ReverbOpening = 0.0f;
    bool bReverbPresetDirty = false;
};

class GSVehicleSound : public GameSystem {
public:
    static constexpr uint32_t kMaxInstances = 32;
    static constexpr int32_t kMaxCachedBanks =
        static_cast<int32_t>( kMaxInstances );

    enum eVehicleSoundCategory : uint32_t {
        VSC_LocalPlayerCar = 1,
        VSC_Indoor = 2,
        VSC_Car = 4,
        VSC_Bike = 8,
        VSC_Showcase = 0x10,
    };

public:
    const char *getName() const override { return "Service : Vehicle Sound"; }
    inline AudioClient *getClient() const { return pClient; }
    inline edAudioReverb *getReverbModule() const { return pReverbModule; }
    inline uint32_t getMutedCategoryMask() const { return mutedCategoryMask; }
    inline void setMutedCategoryMask( uint32_t mask ) {
        if ( mutedCategoryMask != mask ) {
            OTDU_LOG_DEBUG( "Vehicle sound muted categories 0x%02x -> 0x%02x\n",
                            mutedCategoryMask, mask );
        }
        mutedCategoryMask = mask;
    }

public:
    GSVehicleSound();
    ~GSVehicleSound();

    bool initialize( TestDriveGameInstance * ) override;
    void terminate() override;
    void tick( float totalTime, float deltaTime ) override;

    bool isVehicleMuted( const CarState &carState ) const;
    VehicleSoundInstance *createInstance( CarState *pCarState );
    void destroyInstance( CarState *pCarState );
    void tickInstances( const Eigen::Vector3f &listenerPosition );

private:
    using BankCache = std::vector<std::unique_ptr<VehicleSoundBank>>;

private:
    AudioClient *pClient = nullptr;
    edAudioReverb *pReverbModule = nullptr;
    std::unique_ptr<VehicleVoiceSource> pVoiceSource;
    std::array<std::unique_ptr<VehicleSoundInstance>, kMaxInstances> instances;
    uint32_t mutedCategoryMask = 0;
    BankCache carBanks;
    BankCache bikeBanks;

private:
    BankCache &getBankCache( bool bIsBike );
    VehicleSoundInstance *findInstance( const CarState *pCarState );
    void releaseInstance( VehicleSoundInstance &instance );
    void updateInteriorReverb( VehicleSoundInstance &instance );
    bool buildEngine( VehicleSoundInstance &instance );
    VehicleSoundBank *
    acquireBank( int32_t carID, bool bIsBike, const char *pBankName );
    void releaseBank( VehicleSoundBank *pBank );
    void sweepBankCaches();
};

extern GSVehicleSound *gpVehicleSound;
