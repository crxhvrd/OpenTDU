#pragma once

#include "core/ed_xmb.h"

class AudioClient;
class AudioSoundResource;
class AmbianceEnvironment;

template <typename T> class AmbiancePool {
public:
    inline int32_t getCapacity() const { return Capacity; }
    inline int32_t getAllocatedCount() const { return AllocatedCount; }

    inline bool isAllocated( int32_t index ) const {
        return index >= 0 && index < Capacity &&
               ( Words[index >> 5] & ( 1u << ( index & 31 ) ) ) != 0;
    }

    inline T *get( int32_t index ) {
        return isAllocated( index ) ? &Elements[index] : nullptr;
    }
    inline const T *get( int32_t index ) const {
        return isAllocated( index ) ? &Elements[index] : nullptr;
    }

public:
    void create( int32_t capacity ) {
        destroy();
        if ( capacity <= 0 ) {
            return;
        }

        Elements.resize( size_t( capacity ) );
        Words.assign( size_t( ( capacity + 31 ) >> 5 ), 0u );
        Capacity = capacity;
        AllocatedCount = 0;
    }

    void destroy() {
        Elements.clear();
        Words.clear();
        Capacity = 0;
        AllocatedCount = 0;
    }

    T *allocate() {
        if ( AllocatedCount == Capacity ) {
            return nullptr;
        }

        for ( int32_t i = 0; i < Capacity; i++ ) {
            if ( isAllocated( i ) ) {
                continue;
            }

            Words[i >> 5] |= ( 1u << ( i & 31 ) );
            AllocatedCount++;
            return &Elements[i];
        }

        return nullptr;
    }

    void free( const T *pElement ) {
        if ( pElement == nullptr || AllocatedCount == 0 ) {
            return;
        }

        const int32_t index = int32_t( pElement - Elements.data() );
        if ( index < 0 || index >= Capacity ) {
            return;
        }

        Words[index >> 5] &= ~( 1u << ( index & 31 ) );
        AllocatedCount--;
    }

private:
    std::vector<T> Elements;
    std::vector<uint32_t> Words;
    int32_t Capacity = 0;
    int32_t AllocatedCount = 0;
};

// One entry of the common.xmb "ReverbPresets" array
struct AmbianceReverbPreset {
    std::string Name = "";
    float DryVolume = 0.0f;
    float WetVolume = 0.0f;
    float DryWetRatio = 0.0f;
    float LowPassCutoffFrequency = 0.0f;
    float RoomSize = 0.0f;
    int32_t RoomComplexity = 0;
    float RoomDiffusion = 0.0f;
    int32_t Priority = 0;

    void load( const edXMBNode &node );
};

// A convex zone, streamed in with its sector, that pulls one environment in
class AmbianceEmitter {
public:
    // distance a fresh emitter reports until it is ticked.
    static constexpr float kInitialDistance = 2.0f; // DAT_00f416b0
    // a tunnel zone reports this while the car is outside one.
    static constexpr float kInfiniteDistance =
        3.4028234663852886e+38f;                           // DAT_00f4171c
    static constexpr float kCrossProductEpsilon = 1.0e-6f; // FLOAT_00f41888
    static constexpr float kParameterEpsilon = -1.0e-9f;   // DAT_00f41c9c

public:
    inline const void *getOwner() const { return pOwner; }
    inline AmbianceEnvironment *getEnvironment() const { return pEnvironment; }
    inline float getDistance() const { return Distance; }
    inline float getAttenuation() const { return Attenuation; }
    inline float getChannelAttenuation( int32_t channel ) const {
        return ChannelAttenuation[channel];
    }

public:
    void deserialize( const uint8_t *&pCursor,
                      AmbiancePool<AmbianceEnvironment> &environments,
                      const void *pInOwner );

    void updateDistance( const Eigen::Vector4f &listener );
    void computeAttenuations( const Eigen::Vector4f &listener,
                              const Eigen::Vector3f &listenerLeftToRight );

private:
    Eigen::Vector3f getPoint( int32_t index ) const;
    bool containsPoint( const Eigen::Vector3f &local ) const;
    bool findBoundaryCrossing( const Eigen::Vector3f &local,
                               Eigen::Vector4f &out ) const;

private:
    const void *pOwner = nullptr;
    Eigen::Vector4f Position = Eigen::Vector4f::Zero();
    int32_t PointCount = 0;
    const float *pPoints = nullptr;
    AmbianceEnvironment *pEnvironment = nullptr;
    float Distance = kInitialDistance;
    Eigen::Vector4f BoundaryPoint = Eigen::Vector4f::Zero();
    float Attenuation = 0.0f;
    float ChannelAttenuation[2] = { 0.0f, 0.0f };
};

// One entry of the common.xmb "Environments" array
class AmbianceEnvironment {
public:
    static constexpr int32_t kMaxEmitters = 16;
    static constexpr int32_t kInteriorClassID = 7;

public:
    inline const char *getTypeName() const { return TypeName.c_str(); }
    inline int32_t getClassID() const { return ClassID; }
    inline int32_t getPriorityRank() const { return PriorityRank; }
    inline float getPriorityFactor() const { return PriorityFactor; }
    inline bool isTunnel() const { return bIsTunnel; }
    inline float getFadeStart() const { return FadeStart; }
    inline float getFadeStop() const { return FadeStop; }
    inline float getEnvironmentAttenuation() const { return Attenuation; }
    inline AmbianceReverbPreset *getReverbPreset() const {
        return pReverbPreset;
    }
    AudioSoundResource *getResource() const;
    inline int32_t getEmitterCount() const { return EmitterCount; }
    inline float getWeight() const { return Weight; }
    inline bool isActive() const { return bActive; }
    inline void setWeight( float weight ) { Weight = weight; }
    inline void clearEmitters() { EmitterCount = 0; }

public:
    void load( const edXMBNode &node,
               AudioClient *pClient,
               AmbiancePool<AmbianceReverbPreset> &reverbPresets );
    void destroy();
    bool addEmitter( AmbianceEmitter *pEmitter );
    void update( const Eigen::Vector4f &listener,
                 const Eigen::Vector3f &listenerLeftToRight,
                 float volume );

private:
    std::string TypeName = "";
    int32_t TypeID = 0;
    int32_t ClassID = 0;
    int32_t PriorityRank = 0;
    float PriorityFactor = 0.0f;
    bool bIsTunnel = false;
    std::string SampleName = "";
    float ResourceVolume = 0.0f;
    float FadeStart = 0.0f;
    float FadeStop = 0.0f;
    float Attenuation = 0.0f;
    std::unique_ptr<AudioSoundResource> pResource;
    AmbianceReverbPreset *pReverbPreset = nullptr;
    AmbianceEmitter *pEmitters[kMaxEmitters] = {};
    int32_t EmitterCount = 0;
    float Weight = 0.0f;
    bool bActive = false;
};
