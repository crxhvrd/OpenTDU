#pragma once

#include "audio_dispatcher_playback.h"
#include "core/ed_xmb.h"

class AudioClient;
class edAudioSample;

// A single positional sound made of several channels
class AudioSoundResource {
public:
    static constexpr int32_t kMaxChannels = 5;

    struct Channel {
        float Gain = 0.0f;
        Eigen::Vector4f Position = Eigen::Vector4f::Zero();
        AudioDispatcherPlaybackSoundInstance *pInstance = nullptr;
        std::unique_ptr<edAudioSample> pSample;
    };

public:
    inline int32_t getChannelCount() const { return ChannelCount; }
    inline float getVolume() const { return Volume; }
    inline float getEmittersDistance() const { return EmittersDistance; }
    inline Channel &getChannel( int32_t index ) { return Channels[index]; }
    inline const Channel &getChannel( int32_t index ) const {
        return Channels[index];
    }

public:
    explicit AudioSoundResource( int32_t channelCount );
    ~AudioSoundResource();

    static std::unique_ptr<AudioSoundResource>
    create( const edXMBNode &resourceNode, AudioClient *pClient );

    void setVolume( float volume );
    void setPitch( float pitch );
    void setLoopState( bool bLoop );
    void setClipDistances( float clipNear, float clipFar );
    void setEmittersDistance( float distance );
    void setAttenuation( float attenuation );
    void setDopplerFactor( float factor );
    void setPosition( const Eigen::Vector4f &position,
                      uint8_t coordinateSystem );
    void play( float fadeTime );
    void stop( float fadeTime );
    bool isPlaying() const;

private:
    bool load( const edXMBNode &resourceNode, AudioClient *pClient );

    void updateChannelLayout();
    void pushChannelPositions();

private:
    Channel Channels[kMaxChannels];
    int32_t ChannelCount;

    bool bLoopState = false;
    float Volume = 0.0f;
    float Pitch = 0.0f;
    float ClipNear = 0.0f;
    float ClipFar = 0.0f;
    float EmittersDistance = 0.0f;
    float Attenuation = 0.0f;
    float DopplerFactor = 0.0f;
    Eigen::Vector4f Position = Eigen::Vector4f::Zero();
    uint8_t CoordinateSystem =
        AudioDispatcherPlaybackInstanceValues::ADPCS_World;
};
