#include "shared.h"
#include "audio_dispatcher_playback.h"

#include "core/maths_helper.h"

Eigen::Vector3f
AudioDispatcherPlaybackInstanceValues::resolveRelativePosition() const {
    // FUN_00612e60
    if ( CoordinateSystem != ADPCS_World ) {
        return Position;
    }
    return Listener.toListenerSpace( Position );
}

float AudioDispatcherPlaybackInstanceValues::computeVolumeAttenuation(
    const Eigen::Vector3f &relative ) const {
    // FUN_006132a0
    const float distance = relative.norm();

    float distanceGain;
    float coneFade;

    if ( ClipNear < distance ) {
        if ( distance < EffectiveClipFar ) {
            const float bias =
                ( 1.0f / ( 1.0f - RefractionFactor ) - 1.0f ) * ClipNear;
            const float scale = ClipNear + bias;
            const float falloff = scale / ( bias + distance ) -
                                  ( ( scale / ( EffectiveClipFar + bias ) ) /
                                    ( EffectiveClipFar - ClipNear ) ) *
                                      ( distance - ClipNear );

            distanceGain = falloff * ( 1.0f - MinVolume ) + MinVolume;
        } else {
            distanceGain = MinVolume;
        }

        coneFade = 1.0f;
    } else {
        distanceGain = 1.0f;
        coneFade = ( ClipNear != 0.0f ) ? ( distance / ClipNear ) : 0.0f;
    }

    float coneGain = 1.0f;

    const Eigen::Vector3f direction =
        ( distance > 0.0f ) ? Eigen::Vector3f( relative / distance )
                            : Eigen::Vector3f::Zero();

    const float cosInside = std::cos( InsideConeAngle * kHalf * kDegToRad );
    const float projection = -ConeOrientation.dot( direction );

    if ( projection < cosInside ) {
        const float cosOutside =
            std::cos( OutsideConeAngle * kHalf * kDegToRad );

        if ( projection <= cosOutside ) {
            coneGain = OutsideConeVolume;
        } else {
            coneGain =
                ( ( projection - cosOutside ) * ( 1.0f - OutsideConeVolume ) ) /
                    ( cosInside - cosOutside ) +
                OutsideConeVolume;
        }
    }

    return ( ( coneGain - 1.0f ) * coneFade + 1.0f ) * distanceGain;
}

float AudioDispatcherPlaybackInstanceValues::computeDopplerPitch(
    const Eigen::Vector3f &relative ) const {
    // FUN_00613080
    const float distance = relative.norm();
    const Eigen::Vector3f direction =
        ( distance > 0.0f ) ? Eigen::Vector3f( relative / distance )
                            : Eigen::Vector3f::Zero();
    const Eigen::Vector3f relativeSpeed = Speed - Listener.Speed;

    float radialSpeed =
        direction.x() * Listener.LeftToRight.dot( relativeSpeed ) +
        direction.y() * Listener.Top.dot( relativeSpeed ) +
        direction.z() * Listener.Front.dot( relativeSpeed );

    if ( std::fabs( radialSpeed ) >
         AudioDispatcherPlaybackSoundInstance::kMaxRadialSpeed ) {
        radialSpeed = std::copysign(
            AudioDispatcherPlaybackSoundInstance::kMaxRadialSpeed,
            radialSpeed );
    }

    const float pitch =
        1.0f / ( DopplerFactor * radialSpeed /
                     AudioDispatcherPlaybackSoundInstance::kSpeedOfSound +
                 1.0f );

    if ( pitch < 0.0f ) {
        return 0.0f;
    }

    return ( pitch > AudioDispatcherPlaybackSoundInstance::kMaxDopplerRatio )
               ? AudioDispatcherPlaybackSoundInstance::kMaxDopplerRatio
               : pitch;
}

AudioDispatcherPlaybackSoundInstance::AudioDispatcherPlaybackSoundInstance()
    : edAudioStreamEntry() {
    // FUN_00613020
}

void AudioDispatcherPlaybackSoundInstance::setVolume( float volume ) {
    // FUN_00613640
    Values[1].UserVolume = volume;
}

void AudioDispatcherPlaybackSoundInstance::setPitch( float pitch ) {
    // FUN_00613660
    Values[1].UserPitch = pitch;
}

void AudioDispatcherPlaybackSoundInstance::setPosition(
    const Eigen::Vector3f &position, uint8_t coordinateSystem ) {
    // FUN_00612d10
    Values[1].Position = position;
    Values[1].CoordinateSystem = coordinateSystem;
}

void AudioDispatcherPlaybackSoundInstance::setSpeed(
    const Eigen::Vector3f &speed ) {
    // FUN_00612d40
    Values[1].Speed = speed;
}

void AudioDispatcherPlaybackSoundInstance::setClipDistances( float clipNear,
                                                             float clipFar ) {
    // FUN_00612fe0
    Values[1].ClipNear = clipNear;
    Values[1].ClipFar = clipFar;
    Values[1].EffectiveClipFar = clipFar;
    Values[1].MinVolume = 0.0f;
}

void AudioDispatcherPlaybackSoundInstance::setDopplerFactor( float factor ) {
    // FUN_00612d70
    Values[1].DopplerFactor = factor;
}

void AudioDispatcherPlaybackSoundInstance::setRefractionFactor( float factor ) {
    // FUN_00612d90
    Values[1].RefractionFactor =
        ( factor >= 0.0f && factor < 1.0f ) ? factor : 0.0f;
}

void AudioDispatcherPlaybackSoundInstance::setListener(
    const edAudioListener &listener ) {
    Values[1].Listener.copyFrom( listener );
}

void AudioDispatcherPlaybackSoundInstance::release() {
    // FUN_006136b0
    setSample( nullptr );
    bReleaseRequested = true;
}

bool AudioDispatcherPlaybackSoundInstance::computeAttenuation(
    AudioDispatcherPlaybackInstanceValues &values ) {
    // FUN_006134c0
    if ( isStopped() || getPlayingState() == APES_Pause ) {
        return false;
    }

    RelativePosition = values.resolveRelativePosition();

    values.UserVolume *= values.computeVolumeAttenuation( RelativePosition );
    values.UserPitch *= values.computeDopplerPitch( RelativePosition );
    bMuted = !( values.UserVolume >= kMinAudibleVolume &&
                values.UserPitch >= kMinAudibleVolume );

    return true;
}

void AudioDispatcherPlaybackSoundInstance::update() {
    // FUN_00402ad0
    Values[0] = Values[1]; // FUN_00402b00 (inlined)

    computeAttenuation( Values[0] );

    Volume = Values[0].UserVolume;
    Pitch = Values[0].UserPitch;
}
