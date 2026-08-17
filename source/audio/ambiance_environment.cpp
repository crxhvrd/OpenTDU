#include "shared.h"
#include "ambiance_environment.h"

#include "audio/implementation/audio_sound_resource.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/local_player.h"
#include "game/vehicle/car_state.h"
#include "core/binary_helpers.h"

static constexpr float kNegate = -1.0f; // FLOAT_00f41664

void AmbianceReverbPreset::load( const edXMBNode &node ) {
    // FUN_0040e470 + FUN_0040e2c0
    const edXMBNode room = node.findChild( "Room" );

    Name = node.readString( "Name", "" );
    DryVolume = node.readFloat( "DryVolume", 0.0f );
    WetVolume = node.readFloat( "WetVolume", 0.0f );
    DryWetRatio = node.readFloat( "DryWetRatio", 0.0f );
    LowPassCutoffFrequency =
        float( node.readInt( "LowPassCutoffFrequency", 0 ) );

    RoomSize = room.readFloat( "Size", 0.0f );
    RoomComplexity = room.readInt( "Complexity", 0 );
    RoomDiffusion = room.readFloat( "Diffusion", 0.0f );

    Priority = node.readInt( "Priority", 0 );
}

void AmbianceEmitter::deserialize(
    const uint8_t *&pCursor,
    AmbiancePool<AmbianceEnvironment> &environments,
    const void *pInOwner ) {
    // FUN_009011a0
    pOwner = pInOwner;
    Distance = kInitialDistance;
    BoundaryPoint = Eigen::Vector4f::Zero();
    Attenuation = 0.0f;
    ChannelAttenuation[0] = 0.0f;
    ChannelAttenuation[1] = 0.0f;

    const int32_t environmentIndex = ReadInt32( pCursor );
    pCursor += 4;
    pEnvironment = environments.get( environmentIndex );

    const float x = ReadFloat( pCursor );
    const float y = ReadFloat( pCursor + 4 );
    const float w = ReadFloat( pCursor + 12 );
    Position = Eigen::Vector4f( x, 0.0f, -y, w );
    pCursor += 0x10;

    PointCount = ReadInt32( pCursor );
    pCursor += 0xc;

    pPoints = reinterpret_cast<const float *>( pCursor );
    pCursor += size_t( PointCount ) * 0x10;
}

Eigen::Vector3f AmbianceEmitter::getPoint( int32_t index ) const {
    const uint8_t *pRaw =
        reinterpret_cast<const uint8_t *>( pPoints ) + size_t( index ) * 0x10;
    return Eigen::Vector3f( ReadFloat( pRaw ), ReadFloat( pRaw + 4 ),
                            ReadFloat( pRaw + 8 ) );
}

bool AmbianceEmitter::containsPoint( const Eigen::Vector3f &local ) const {
    // FUN_00900b00
    if ( PointCount == 0 || pPoints == nullptr ) {
        return true;
    }

    Eigen::Vector3f previous = getPoint( PointCount - 1 );
    for ( int32_t i = 0; i < PointCount; i++ ) {
        const Eigen::Vector3f current = getPoint( i );

        const float side =
            ( previous.x() - current.x() ) * ( local.z() - previous.z() ) +
            ( current.z() - previous.z() ) * ( local.x() - previous.x() );
        if ( side > 0.0f ) {
            return false;
        }

        previous = current;
    }

    return true;
}

bool AmbianceEmitter::findBoundaryCrossing( const Eigen::Vector3f &local,
                                            Eigen::Vector4f &out ) const {
    // FUN_00900f40
    if ( PointCount == 0 || pPoints == nullptr ) {
        return false;
    }

    Eigen::Vector3f previous = getPoint( PointCount - 1 );
    for ( int32_t i = 0; i < PointCount; i++ ) {
        const Eigen::Vector3f current = getPoint( i );

        const float edgeX = current.x() - previous.x();
        const float edgeZ = current.z() - previous.z();

        const float denominator = edgeX * -local.z() - edgeZ * -local.x();
        if ( fabsf( denominator ) < kCrossProductEpsilon ) {
            previous = current;
            continue;
        }

        const float deltaZ = previous.z() - local.z();
        const float deltaX = previous.x() - local.x();
        const float inverse = 1.0f / denominator;

        const float edgeParam =
            ( deltaZ * -local.x() - -local.z() * deltaX ) * inverse;
        const float segmentParam =
            ( edgeX * deltaZ - edgeZ * deltaX ) * inverse;

        if ( edgeParam >= kParameterEpsilon && edgeParam <= 1.0f &&
             segmentParam >= kParameterEpsilon && segmentParam <= 1.0f ) {
            out = Eigen::Vector4f( previous.x() + edgeX * edgeParam, 0.0f,
                                   previous.z() + edgeZ * edgeParam, 0.0f );
            return true;
        }

        previous = current;
    }

    return false;
}

void AmbianceEmitter::updateDistance( const Eigen::Vector4f &listener ) {
    // FUN_009010b0
    Distance = 0.0f;

    if ( pEnvironment == nullptr ) {
        return;
    }

    if ( pEnvironment->isTunnel() ) {
        CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
        if ( pCarState == nullptr || !pCarState->getBody()->bIsInTunnel ) {
            Distance = kInfiniteDistance;
            return;
        }
    }

    const Eigen::Vector3f local = listener.head<3>() - Position.head<3>();

    if ( containsPoint( local ) ) {
        return;
    }

    if ( findBoundaryCrossing( local, BoundaryPoint ) ) {
        Distance = ( local - BoundaryPoint.head<3>() ).norm();
    }
}

void AmbianceEmitter::computeAttenuations(
    const Eigen::Vector4f &listener,
    const Eigen::Vector3f &listenerLeftToRight ) {
    // FUN_00900ca0
    if ( pEnvironment == nullptr ) {
        return;
    }

    const float fadeStart = pEnvironment->getFadeStart();
    const float fadeStop = pEnvironment->getFadeStop();
    const float bias =
        ( 1.0f / ( 1.0f - pEnvironment->getEnvironmentAttenuation() ) - 1.0f ) *
        fadeStart;
    const float numerator = fadeStart + bias;

    const auto attenuate = [=]( float distance ) {
        return numerator / ( bias + distance ) -
               ( ( numerator / ( fadeStop + bias ) ) /
                 ( fadeStop - fadeStart ) ) *
                   ( distance - fadeStart );
    };

    Attenuation = attenuate( Distance );

    AudioSoundResource *pResource = pEnvironment->getResource();
    if ( pResource == nullptr ) {
        ChannelAttenuation[0] = Attenuation;
        ChannelAttenuation[1] = Attenuation;
        return;
    }

    const Eigen::Vector3f fromBoundary =
        ( listener.head<3>() - Position.head<3>() ) - BoundaryPoint.head<3>();
    const Eigen::Vector3f spread =
        listenerLeftToRight * pResource->getEmittersDistance();

    ChannelAttenuation[0] =
        attenuate( ( spread * kNegate + fromBoundary ).norm() );
    ChannelAttenuation[1] = attenuate( ( spread + fromBoundary ).norm() );
}

void AmbianceEnvironment::load(
    const edXMBNode &node,
    AudioClient *pClient,
    AmbiancePool<AmbianceReverbPreset> &reverbPresets ) {
    // FUN_00900830
    EmitterCount = 0;
    Weight = 0.0f;
    bActive = false;

    TypeName = node.readString( "TypeName", "Unknown" );
    TypeID = node.readInt( "TypeID", 0 );
    ClassID = node.readInt( "ClassID", 0 );
    PriorityRank = node.readInt( "PriorityRank", 0 );
    PriorityFactor = node.readFloat( "PriorityFactor", 1.0f );
    bIsTunnel = node.readBool( "IsTunnel", false );
    SampleName = node.readString( "SampleName", "" );
    FadeStart = node.readFloat( "FadeStart", 0.0f );
    FadeStop = node.readFloat( "FadeStop", 100.0f );
    Attenuation = node.readFloat( "Attenuation", 0.0f );

    pReverbPreset = reverbPresets.get( node.readInt( "ReverbPresetIndex", 0 ) );

    pResource =
        AudioSoundResource::create( node.findChild( "Resource" ), pClient );
    if ( pResource == nullptr ) {
        ResourceVolume = 0.0f;
        return;
    }

    ResourceVolume = pResource->getVolume();
    pResource->setDopplerFactor( 0.0f );
    pResource->setPosition(
        Eigen::Vector4f::Zero(),
        AudioDispatcherPlaybackInstanceValues::ADPCS_ListenerRelative );
}

void AmbianceEnvironment::destroy() {
    pResource.reset();
    pReverbPreset = nullptr;
    EmitterCount = 0;
    Weight = 0.0f;
    bActive = false;
}

AudioSoundResource *AmbianceEnvironment::getResource() const {
    return pResource.get();
}

bool AmbianceEnvironment::addEmitter( AmbianceEmitter *pEmitter ) {
    // FUN_009018d0
    if ( EmitterCount >= kMaxEmitters ) {
        return false;
    }

    pEmitters[EmitterCount] = pEmitter;
    EmitterCount++;
    return true;
}

void AmbianceEnvironment::update( const Eigen::Vector4f &listener,
                                  const Eigen::Vector3f &listenerLeftToRight,
                                  float volume ) {
    // FUN_00900690
    bActive = EmitterCount != 0;
    Weight = 0.0f;

    if ( !bActive ) {
        if ( pResource != nullptr ) {
            pResource->stop( 0.0f );
        }
        return;
    }

    float leftGain = 0.0f;
    float rightGain = 0.0f;

    for ( int32_t i = 0; i < EmitterCount; i++ ) {
        AmbianceEmitter *pEmitter = pEmitters[i];

        if ( pEmitter->getDistance() == 0.0f ) {
            Weight = 1.0f;
            leftGain = 1.0f;
            rightGain = 1.0f;
            break;
        }

        pEmitter->computeAttenuations( listener, listenerLeftToRight );

        Weight = Max( Weight, pEmitter->getAttenuation() );
        leftGain = Max( leftGain, pEmitter->getChannelAttenuation( 0 ) );
        rightGain = Max( rightGain, pEmitter->getChannelAttenuation( 1 ) );
    }

    if ( pResource == nullptr ) {
        return;
    }

    pResource->setVolume( ResourceVolume * volume );

    const float gains[2] = { leftGain, rightGain };
    for ( int32_t i = 0; i < pResource->getChannelCount() && i < 2; i++ ) {
        AudioSoundResource::Channel &channel = pResource->getChannel( i );
        channel.Gain = gains[i];
        channel.pInstance->setVolume( pResource->getVolume() * channel.Gain );
    }

    if ( !pResource->isPlaying() ) {
        pResource->play( 0.0f );
    }
}
