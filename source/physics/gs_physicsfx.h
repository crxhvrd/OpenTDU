#pragma once

#include "game/gs_base.h"
#include "filesystem/bank.h"

class AudioClient;
class AudioDispatcherPlaybackSoundInstance;
class edAudioSample;
struct ParticleInstance;

enum ePhysicsFXEventKind : int32_t {
    PFXEK_Impact = 0,
    PFXEK_ImpactLooped = 1, // looped
    PFXEK_Slide = 2,        // looped
    PFXEK_Removed = 4,

    PFXEK_Count = 4
};

struct PhysicsFXEvent {
    int32_t FXMaterialA = 0;
    float MassA = 0.0f;
    Eigen::Vector4f VelocityA;
    const void *pBodyA = nullptr;
    int32_t FXMaterialB = 0;
    float MassB = 0.0f;
    Eigen::Vector4f VelocityB;
    const void *pBodyB = nullptr;

    int32_t Kind = PFXEK_Impact;

    Eigen::Vector4f ContactPosition; // ABSOLUTE world space
    Eigen::Vector4f Normal;          // normalised
    float Energy = 0.0f;
};

class PhysicsFXEventQueue {
public:
    static constexpr int32_t kMaxEventsPerFrame = 64;

public:
    void push( const PhysicsFXEvent &event );
    void swapBuffers();
    void reset();

    inline int32_t getReadCount() const { return counts[readIndex]; }
    inline const PhysicsFXEvent &getReadEvent( int32_t index ) const {
        return events[readIndex][index];
    }

private:
    std::array<std::array<PhysicsFXEvent, kMaxEventsPerFrame>, 2> events = {};
    std::array<int32_t, 2> counts = { 0, 0 };
    int32_t writeIndex = 0;
    int32_t readIndex = 1;
    std::mutex mutex;
};

// Chunk header shared by every node of Sound/Physics/SFXConfig.bin
struct SFXChunkHeader {
    char Name[8];
    int32_t Size;        // size of the whole chunk (header included)
    int16_t NumChildren; // children start at (this + HeaderSize)
    int16_t HeaderSize;  // payload always starts at (this + 0x10)
};
OTDU_SIZE_MUST_MATCH( SFXChunkHeader, 16 );

// One energy band inside a SAMPLE chunk
struct SFXSampleRecord {
    int32_t SampleIndex; // index into the FILELIST chunk
    float VolumeMin;
    float VolumeMax;
    float PitchMin;
    float PitchMax;
    float Unknown_0x14; // TODO: never read at runtime
    float Unknown_0x18; // TODO: never read at runtime
    float EnergyMin;
    float EnergyMax;
    char ParticleName[2][32];
    float ParticleDensity[2]; //  ParticleInstance::Density
    float ParticleSize[2];    // ParticleInstance::Scale
    float ParticleSpeed[2];   //  ParticleInstance::Speed
    char __PADDING__[4];
};
OTDU_SIZE_MUST_MATCH( SFXSampleRecord, 128 );

class GSPhysicsFX : public GameSystem, public Bank {
public:
    const char *getName() const override { return "Service : PhysicsFX"; }

    inline PhysicsFXEventQueue &getEventQueue() { return eventQueue; }
    inline bool isReady() const { return bLoaded; }
    inline bool hasEffectFor( int32_t fxMaterialA,
                              int32_t fxMaterialB,
                              int32_t kind ) const {
        return getBinomial( fxMaterialA, fxMaterialB, kind ) != nullptr;
    }

public:
    GSPhysicsFX();
    ~GSPhysicsFX();

    bool initialize( TestDriveGameInstance * ) override;
    void terminate() override;
    void tick( float totalTime, float deltaTime ) override;
    void draw( float deltaTime ) override;

#ifdef OTDU_USE_IMGUI
    void drawDebug( float deltaTime ) override;
#endif

    static int32_t RemapMaterial( int32_t materialType );

private:
    static constexpr int32_t kNumFXMaterials = 22;
    static constexpr int32_t kInvalidFXMaterial = 22;
    static constexpr int32_t kNumMaterialTypes = 48;
    static constexpr int32_t kNumSlots = 32;

    // A slot drives at most two SAMPLE layers at once (e.g. scrape + metal)
    static constexpr int32_t kNumLayersPerSlot = 2;
    static constexpr int32_t kNumParticlesPerLayer = 2;

private:
    struct FXLayer {
        bool bActive = false;
        std::array<ParticleInstance *, kNumParticlesPerLayer> pParticles = {};
        AudioDispatcherPlaybackSoundInstance *pSound = nullptr;
        const SFXSampleRecord *pRecord = nullptr;
    };

    struct FXSlot {
        PhysicsFXEvent Event;
        std::array<FXLayer, kNumLayersPerSlot> Layers;
        float Timer = 0.0f;
        bool bRefreshedThisFrame = false;
        bool bInUse = false;
    };

private:
    void loadConfig();
    void releaseConfig();
    void stopAllEffects();

    const SFXChunkHeader *
    getBinomial( int32_t fxMaterialA, int32_t fxMaterialB, int32_t kind ) const;
    static const SFXChunkHeader *selectMassBucket(
        const SFXChunkHeader *pBinomial, float massA, float massB );
    static const SFXSampleRecord *
    selectEnergyBand( const SFXChunkHeader *pSample, float energy );

    bool loadSampleTable( const SFXChunkHeader *pFileList );

    FXSlot *findActiveSlot( const PhysicsFXEvent &event );
    FXSlot *acquireSlot();

    void startEffect( const PhysicsFXEvent &event );
    void updateSlot( FXSlot &slot, float deltaTime );
    void reapLayer( FXLayer &layer );

    const edAudioSample *resolveSample( const void *pWaveData );

private:
    std::vector<const void *> sampleData;
    std::unordered_map<const void *, std::unique_ptr<edAudioSample>> samples;

    std::vector<uint8_t> configBlob;
    const SFXChunkHeader *pBinoList = nullptr;
    const float *pSettings = nullptr;

    std::array<const SFXChunkHeader *,
               kNumFXMaterials * kNumFXMaterials * PFXEK_Count>
        materialLUT = {};

    std::array<FXSlot, kNumSlots> slots;

    AudioClient *pAudioClient = nullptr;
    float masterVolumeScale = 1.0f;
    bool bMuted = false;
    bool bLoaded = false;

    PhysicsFXEventQueue eventQueue;
};

extern GSPhysicsFX *gpPhysicsFX;
