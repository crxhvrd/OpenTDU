#pragma once

#include "ed_audio.h"
#include "ed_audio_stream_entry.h"

struct AudioDispatcherPlaybackInstanceValues {
    enum eCoordinateSystem : uint8_t {
        ADPCS_World =
            0, // world space position (has to be resolved against the listener)
        ADPCS_ListenerRelative = 1,
    };

    float UserVolume = 1.0f; // FLOAT_00ef1b70
    float UserPitch = 1.0f;  // FLOAT_00ef1b74
    edAudioListener Listener;
    Eigen::Vector3f Position = Eigen::Vector3f::Zero();
    uint8_t CoordinateSystem = ADPCS_World;
    Eigen::Vector3f Speed = Eigen::Vector3f::Zero();
    float ClipNear = 10.0f; //  FLOAT_00ef1bd0
    float ClipFar = 500.0f; // FLOAT_00ef1bd4
    float EffectiveClipFar = 500.0f;
    float MinVolume = 0.0f;
    Eigen::Vector3f ConeOrientation = Eigen::Vector3f::Zero();
    float InsideConeAngle = 360.0f;  // FLOAT_00ef1bdc
    float OutsideConeAngle = 360.0f; // FLOAT_00ef1bdc
    float OutsideConeVolume = 0.0f;
    float DopplerFactor = 1.0f; // FLOAT_00f4165c
    float RefractionFactor = 0.0f;

    Eigen::Vector3f resolveRelativePosition() const;
    float computeVolumeAttenuation( const Eigen::Vector3f &relative ) const;
    float computeDopplerPitch( const Eigen::Vector3f &relative ) const;
};

// One positional sound instance (used by GSBoat, GSAircraft, etc.)
class AudioDispatcherPlaybackSoundInstance : public edAudioStreamEntry {
public:
    static constexpr float kMinAudibleVolume = 0.001f; // FLOAT_00f4167c
    static constexpr float kSpeedOfSound = 340.0f;     // 1/DAT_00ef1c20
    static constexpr float kMaxRadialSpeed = 339.0f;   // DAT_00ef1c24
    static constexpr float kMaxDopplerRatio = 16.0f;   // DAT_00f418d8

public:
    AudioDispatcherPlaybackSoundInstance *pNextInstance = nullptr;

public:
    inline const Eigen::Vector3f &getRelativePosition() const {
        return RelativePosition;
    }
    inline AudioDispatcherPlaybackInstanceValues &getNextValues() {
        return Values[1];
    }
    inline const AudioDispatcherPlaybackInstanceValues &
    getCurrentValues() const {
        return Values[0];
    }
    inline bool isReleaseRequested() const { /* FUN_00613690 */
        return bReleaseRequested;
    }

public:
    AudioDispatcherPlaybackSoundInstance();

    void setVolume( float volume );
    void setPitch( float pitch );
    void setPosition( const Eigen::Vector3f &position,
                      uint8_t coordinateSystem );
    void setSpeed( const Eigen::Vector3f &speed );
    void setClipDistances( float clipNear, float clipFar );
    void setDopplerFactor( float factor );
    void setRefractionFactor( float factor );
    void setListener( const edAudioListener &listener );
    void update();
    void release();

    virtual bool
    computeAttenuation( AudioDispatcherPlaybackInstanceValues &values );

private:
    AudioDispatcherPlaybackInstanceValues Values[2];
    Eigen::Vector3f RelativePosition = Eigen::Vector3f::Zero();
    bool bReleaseRequested = false;
};
