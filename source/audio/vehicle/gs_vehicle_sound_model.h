#pragma once

#include "core/ed_xmb.h"
#include "filesystem/bank.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class AudioDispatcherPlaybackSoundInstance;

enum eVSTInstanceRequest : int32_t {
    VSTIR_None = 0,
    VSTIR_Start = 1,
    VSTIR_Stop = 2,
    VSTIR_Held = 3,
};

enum eVSTPhysicsChannel : int32_t {
    // Engine. RPM drives the on/off-load layer pitch, torque their crossfade.
    VSTPC_EngineRPM = 0,      // rad/s * 60/2pi
    VSTPC_SpeedKmh = 1,       // m/s * 3.6
    VSTPC_EnginePowerPS = 2,  // torque * omega / 735.5, traffic vehicles
    VSTPC_EngineTorqueNm = 3, // signed: > 0 on throttle, < 0 engine braking

    VSTPC_AirDensityNorm = 4, // density normalised over [0.7, 1.7]
    VSTPC_WindowAperture = 5, // aperture * 0.7, cars only
    VSTPC_Zero0 = 6,          // always 0.0
    VSTPC_Zero1 = 7,          // always 0.0
    VSTPC_Unwritten = 8,      // 8..13, see the note above

    VSTPC_WheelSpeedKmh = 14,     // 14..17
    VSTPC_WheelLoadNorm = 18,     // 18..21, load / (mass * 9.81), unused
    VSTPC_WheelSlipLong = 22,     // 22..25, signed
    VSTPC_WheelSlipLat = 26,      // 26..29, absolute
    VSTPC_WheelSpeedLatKmh = 30,  // 30..33, unused
    VSTPC_WheelSlipLatRatio = 34, // 34..37, unused

    VSTPC_TurboPressureBar = 38, // 0..3 bar
    VSTPC_TurboRPM = 39,
    VSTPC_TurboAccelRPM = 40, // unused
    VSTPC_LandingImpact = 41, // mean over wheels, 0..1
    VSTPC_ReverseRPM = 42,    // EngineRPM while CurrentGear == 0, else 0
    VSTPC_DoorAperture = 43,  // mean of both doors, unused
};

enum eVSTStateFlag : uint32_t {
    VSTSF_ViewInterior = 1u << 0,
    VSTSF_ViewExterior = 1u << 1,
    VSTSF_SurfaceOther = 1u << 2, // unrecognised material, or wheel airborne
    VSTSF_SurfaceRoad = 1u << 3,  // asphalt, rock, concrete, pavement, rubber
    VSTSF_SurfaceGrass = 1u << 4, // grass, foliage 3/4
    VSTSF_SurfaceDirt = 1u << 5,  // dirt, wood, vegetation 1..3
};

struct VSTPhysicsSettings {
    static constexpr int32_t kPhysicsValueCount = 44;
    static constexpr int32_t kStateFlagCount = 4;
    float PhysicsValues[kPhysicsValueCount] = {};
    uint32_t StateFlags[kStateFlagCount] = {};
    float VolumeFactor = 1.0f;
    int32_t CurrentGear = 0;
    Eigen::Vector3f Speed = Eigen::Vector3f::Zero();
    Matrix4x4f_t Transform = Matrix4x4f_t::Identity();
    int32_t TrackingMode = 0;
    bool bIsPlayerState = false;
    bool bCurrentHornDown = false;
    bool bCurrentEngineIgnition = false;
    bool bCurrentViewInternal = false;
    bool bCurrentDoorLeftOpened = false;
    bool bCurrentDoorRightOpened = false;
    bool bCurrentWindowMoving = false;

    // One-shot events
    bool bEventHornPushed = false;
    bool bEventHornReleased = false;
    bool bEventStartEnginePushed = false;
    bool bEventStartEngineReleased = false;
    bool bEventGearUpChanged = false;
    bool bEventGearDownChanged = false;
    bool bEventViewChanged = false;
    bool bEventDoorLeftClose = false;
    bool bEventDoorLeftOpen = false;
    bool bEventDoorRightClose = false;
    bool bEventDoorRightOpen = false;
    bool bEventWindowMovingStart = false;
    bool bEventWindowMovingStop = false;
};

class IVSTSoundInstanceSource {
public:
    virtual ~IVSTSoundInstanceSource() = default;

    virtual AudioDispatcherPlaybackSoundInstance *
    acquireSoundInstance( const void *pWaveData,
                          const char *pName,
                          bool bLoop,
                          float &duration ) = 0;

    virtual void
    releaseSoundInstance( AudioDispatcherPlaybackSoundInstance *pInstance ) = 0;
};

float EvaluateVSTCurve( const float *pCurveSet,
                        int32_t pointCount,
                        float input );

struct VehicleSoundLink {
    const int32_t *pPhysic = nullptr;
    int32_t PhysicCount = 0;
    int32_t Audio = 0;
    int32_t CurvePointCount = 0;
    const float *pCurveSet = nullptr;
};

struct VehicleSoundSample {
    static constexpr int32_t kMaxInstancePositions = 5;
    static constexpr int32_t kMaxLinks = 5;

    const void *pWaveData = nullptr;
    const char *pSampleName = nullptr;
    float SampleDuration = 0.0f;
    bool bIsTrigger = false;
    bool bMustBeLooped = false;
    float BaseVolume = 0.0f;
    float ClipNear = 0.0f;
    float ClipFar = 0.0f;
    float RefractionFactor = 0.0f;
    float TrackingClipNear = 0.0f;
    float TrackingClipFar = 0.0f;
    float TrackingRefractionFactor = 0.0f;
    int32_t TrackingClippingFactorCount = 0;
    const float *pTrackingClippingFactors = nullptr;
    int32_t SoundCount = 0;
    std::array<AudioDispatcherPlaybackSoundInstance *, kMaxInstancePositions>
        pSoundInstances = {};
    std::array<float[4], kMaxInstancePositions> SoundPositions = {};
    std::array<int32_t, kMaxInstancePositions> InstanceRequest = {};

    int32_t LinkCount = 0;
    std::array<VehicleSoundLink, kMaxLinks> Links = {};
    const float *pPlaybackStateVolume = nullptr;
    int32_t SampleID = 0;
    int32_t PanelID = 0;
    float CurrentVolume = 0.0f;
    float CurrentPitch = 0.0f;
    float CurrentClipNear = 0.0f;
    float CurrentClipFar = 0.0f;
    float CurrentRefractionFactor = 0.0f;
    float CurrentDopplerFactor = 0.0f;
    bool bValid = false;
};

// The shared table Common.bnk
class CommonVehicleSounds : public Bank {
public:
    inline bool isReady() const { return document.isValid(); }
    inline int32_t getWaveCount() const {
        return static_cast<int32_t>( waveData.size() );
    }
    inline const edXMBNode &getDocument() const { return document; }

public:
    bool load( const char *pResRootPath );
    bool loadFromMemory( const void *pBankMemory, uint32_t bankSize );
    const void *getWaveData( int32_t waveIndex ) const;

private:
    void parse();

private:
    edXMBNode document;
    std::vector<const void *> waveData;
};

class VehicleSoundBank final : public Bank {
public:
    static constexpr int32_t kMaxSamples = 0x2b; // 43
public:
    inline bool isReady() const { return document.isValid(); }
    inline const edXMBNode &getDocument() const { return document; }
    inline const char *getName() const { return name.c_str(); }
    inline const void *getWaveData( int32_t index ) const {
        return ( index >= 0 && index < kMaxSamples ) ? pWaveData[index]
                                                     : nullptr;
    }
    inline int32_t getWaveCount() const { return waveCount; }
    inline const void *getBankMemory() const { return pBankFile; }
    inline uint32_t getBankMemorySize() const { return bankSize; }
    inline int32_t getCarID() const { return carID; }
    inline void setCarID( int32_t id ) { carID = id; }
    inline int32_t getRefCount() const { return refCount; }
    inline void addRef() { refCount++; }
    inline int32_t release() { return --refCount; }

public:
    ~VehicleSoundBank() { unload(); }

    bool load( const char *pResRootPath, const char *pName );
    bool loadFromMemory( const void *pBankMemory, uint32_t bankSize );

private:
    void parse();

private:
    edXMBNode document;
    uint32_t bankSize = 0;
    std::string name;
    const void *pWaveData[kMaxSamples] = {};
    int32_t waveCount = 0;
    int32_t carID = -1;
    int32_t refCount = 0;
};

class VehicleSoundModel {
public:
    static constexpr int32_t kMaxSamples = VehicleSoundBank::kMaxSamples;

public:
    inline const VehicleSoundBank *getBank() const { return pBank; }

    inline const VehicleSoundSample &getSample( int32_t sampleID ) const {
        return samples[( sampleID >= 0 && sampleID < kMaxSamples ) ? sampleID
                                                                   : 0];
    }
    inline bool isSampleValid( int32_t sampleID ) const {
        return sampleID >= 0 && sampleID < kMaxSamples &&
               samples[sampleID].bValid;
    }
    inline const char *getName() const {
        return ( pBank != nullptr ) ? pBank->getName() : "";
    }
    inline bool isPaused() const { return bPaused; }

public:
    bool parse( const VehicleSoundBank &sharedBank );
    int32_t getValidSampleCount() const;
    int32_t releaseVoices( IVSTSoundInstanceSource *pInstanceSource );
    void update( const VSTPhysicsSettings &settings,
                 const float *pPanelVolumeFactors,
                 const Eigen::Vector3f &listenerPosition,
                 IVSTSoundInstanceSource *pInstanceSource );
    void pause();
    void resume();
    void updateSampleInstance( VehicleSoundSample &sample,
                               int32_t soundIndex,
                               const Eigen::Vector3f &worldPosition,
                               const Eigen::Vector3f &speed,
                               IVSTSoundInstanceSource *pInstanceSource ) const;
    void dispatchEvents( const VSTPhysicsSettings &settings );
    void computeSampleGain( VehicleSoundSample &sample,
                            int32_t soundIndex,
                            const VSTPhysicsSettings &settings,
                            const float *pPanelVolumeFactors ) const;
    void updateSampleClipping( VehicleSoundSample &sample,
                               const VSTPhysicsSettings &settings ) const;

private:
    void parseSample( VehicleSoundSample &sample,
                      const edXMBNode &node,
                      const VehicleSoundBank &sharedBank ) const;

private:
    std::array<VehicleSoundSample, kMaxSamples> samples = {};
    bool bPaused = false;
    const VehicleSoundBank *pBank = nullptr;
};

extern CommonVehicleSounds gCommonVehicleSounds;
