#pragma once

#include "base_entity.h"

#include "audio/implementation/ed_audio_stream_entry.h"
#include "core/random/bcpl.h"

#include <memory>

class edAudioSample;
class AudioDispatcherPlaybackSoundInstance;

// SCSoundEmiter (ET_SDEM) - a positional sound placed by the scenaric editor.
class EntitySoundEmitter : public Entity, public edAudioStreamEntryListener {
public:
    const char *GetName() override { return "Actor"; /* FUN_00423f40 */ }

public:
    EntitySoundEmitter();
    ~EntitySoundEmitter();

    bool onMessage( FlashMessage *pMessage ) override;
    void initializeForCinematicKeyframed( void *param_2,
                                          void *param_3 ) override;

    void initialize() override;
    void loadResources( ScenaricBank *param_2 ) override;
    void unloadResources() override;
    void tick() override;
    void deserialize( BinaryReader *param_2 ) override;
    void setTransform( Matrix4x4f_t &transformMatrix, bool param_1 ) override;
    void setPosition( Eigen::Vector4f &position, bool param_1 ) override;
    void reset() override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;
    void onPipeEntryPlayed( edAudioStreamEntry *pEntry ) override;
    void onPipeEntryStopped( edAudioStreamEntry *pEntry ) override;
    void play( bool bInLoop );

protected:
    static constexpr uint32_t kMessagePlay = 0x706c6179;     // "play"
    static constexpr uint32_t kMessagePlayLoop = 0x706c6c6f; // "pllo"
    static constexpr uint32_t kMessagePlayOnce = 0x706c6f6e; // "plon"
    static constexpr uint32_t kMessageStop = 0x73746f70;     // "stop"

protected:
    void deserializeSoundParameters( BinaryReader *pReader );
    void updatePlayback();
    void reRandomizeVolumeAndPitch();
    float computeRandomPitch( float pitch, float randomPitch );
    float computeRandomVolume( float randomVolume, float volume );

protected:
    std::unique_ptr<edAudioSample> sampleStorage;
    const edAudioSample *pSample;
    uint8_t *pWavHeader;
    float Volume;
    float ClipDistMin;
    float ClipDistMax;
    float Doppler;
    bool bLoop;
    float RandomPitch;
    float Pitch;
    float RandomVolume;
    AudioDispatcherPlaybackSoundInstance *pSoundInstance;
    RandomGen RndGen;
    bool bSoundPlay;
    float OldTime;
    bool bAmbiance;
    bool bAmbianceLeft;
};
