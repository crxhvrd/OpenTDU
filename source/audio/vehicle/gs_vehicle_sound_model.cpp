#include "shared.h"
#include "gs_vehicle_sound_model.h"

#include "core/maths_helper.h"
#include "audio/implementation/audio_dispatcher_playback.h"

#include <cstring>

CommonVehicleSounds gCommonVehicleSounds;

static constexpr const char *kNoSampleFile = "No sample file";
static constexpr const char *kAudioBankSuffix = "_audio";  // DAT_00f8b434
static constexpr const char *kAudioBankExtension = ".bnk"; // DAT_00f8b438
static constexpr float kEmitterRight = 1.0f;               // FLOAT_00f4165c
static constexpr float kEmitterLeft = -1.0f;               // FLOAT_00f41664
static constexpr float kMinAudibleVolume = 0.01f;          // DAT_00f416d4
static constexpr float kMinAudiblePitch = 0.001f;          // FLOAT_00f4167c
static constexpr float kEnginePauseFadeTime = 0.1f;

enum eVSTReservedSample : int32_t {
    VSTRS_GearUp = 0x20,
    VSTRS_GearDown = 0x21,
    VSTRS_DoorOpen = 0x22,  // emitter 0 left, emitter 1 right
    VSTRS_DoorClose = 0x23, // emitter 0 left, emitter 1 right
    VSTRS_Window = 0x24,    // both emitters together
    VSTRS_Horn = 0x25,
    VSTRS_StartEngine = 0x27,
};

static inline bool IsStereoEmitterSample( int32_t sampleID ) {
    return sampleID == 0x22 || sampleID == 0x23 || sampleID == 0x24;
}

static inline int32_t ArrayCountOf( const edXMBNode &node ) {
    return node.isValid() ? node.getArrayCount() : 0;
}

static const void *GetBankEntryData( BankInstance &bankInstance,
                                     const char *pEntryName ) {
    if ( pEntryName == nullptr ) {
        return nullptr;
    }

    ParsedBankEntry entry = {};
    if ( bankInstance.getEntry( const_cast<char *>( pEntryName ), entry ) <
         0 ) {
        return nullptr;
    }
    return entry.pData;
}

float EvaluateVSTCurve( const float *pCurveSet,
                        int32_t pointCount,
                        float input ) {
    // FUN_007d2e20 (inlined)
    if ( pCurveSet == nullptr || pointCount <= 0 ) {
        return 1.0f;
    }

    for ( int32_t i = 0; i < pointCount - 1; i++ ) {
        const float x0 = pCurveSet[i * 2];
        const float x1 = pCurveSet[i * 2 + 2];
        if ( x0 <= input && input < x1 ) {
            const float y0 = pCurveSet[i * 2 + 1];
            const float y1 = pCurveSet[i * 2 + 3];
            return ( ( y1 - y0 ) / ( x1 - x0 ) ) * ( input - x0 ) + y0;
        }
    }

    if ( pCurveSet[0] <= input ) {
        if ( pCurveSet[pointCount * 2 - 2] <= input ) {
            return pCurveSet[pointCount * 2 - 1];
        }
        return 1.0f;
    }
    return pCurveSet[1];
}

bool CommonVehicleSounds::load( const char *pResRootPath ) {
    if ( isReady() ) {
        return true;
    }

    char bankPath[512];
    snprintf( bankPath, sizeof( bankPath ), "%sSound/Vehicules/Common.bnk",
              pResRootPath != nullptr ? pResRootPath : "" );

    if ( !loadBootBank( bankPath, nullptr ) ) {
        OTDU_LOG_ERROR( "Failed to load the common vehicle sound bank '%s'!\n",
                        bankPath );
        return false;
    }

    parse();
    if ( !isReady() ) {
        OTDU_LOG_ERROR( "'%s' has no usable CommonVSTConfig.xmb!\n", bankPath );
        return false;
    }

    OTDU_LOG_INFO( "Common vehicle sounds loaded ('%s', %d waves)\n", bankPath,
                   getWaveCount() );
    return true;
}

bool CommonVehicleSounds::loadFromMemory( const void *pBankMemory,
                                          uint32_t bankSize ) {
    // FUN_007d3d60
    if ( bank.load( static_cast<const uint8_t *>( pBankMemory ), bankSize,
                    true ) != 0 ) {
        return false;
    }
    parse();
    return isReady();
}

void CommonVehicleSounds::parse() {
    const void *pConfig = GetBankEntryData( bank, "CommonVSTConfig.xmb" );
    document = edXMBNode( pConfig );
    if ( !document.isValid() ) {
        return;
    }

    const edXMBNode sampleFiles = document.findChild( "SampleFiles" );
    const int32_t count = ArrayCountOf( sampleFiles );

    waveData.assign( size_t( count ), nullptr );
    for ( int32_t i = 0; i < count; i++ ) {
        const char *pFileName = sampleFiles.getElement( i ).asString();
        waveData[size_t( i )] = GetBankEntryData( bank, pFileName );
    }
}

const void *CommonVehicleSounds::getWaveData( int32_t waveIndex ) const {
    if ( waveIndex < 0 ||
         waveIndex >= static_cast<int32_t>( waveData.size() ) ) {
        return nullptr;
    }
    return waveData[size_t( waveIndex )];
}

bool VehicleSoundBank::load( const char *pResRootPath, const char *pName ) {
    // FUN_007d4850
    if ( pName == nullptr ) {
        return false;
    }
    name = pName;

    char bankPath[512];
    snprintf( bankPath, sizeof( bankPath ), "%sSound/Vehicules/%s%s%s",
              pResRootPath != nullptr ? pResRootPath : "", pName,
              kAudioBankSuffix, kAudioBankExtension );

    if ( !loadBootBank( bankPath, nullptr ) ) {
        OTDU_LOG_ERROR( "Failed to load vehicle sound bank '%s'!\n", bankPath );
        return false;
    }

    parse();
    return document.isValid();
}

bool VehicleSoundBank::loadFromMemory( const void *pBankMemory,
                                       uint32_t size ) {
    if ( bank.load( static_cast<const uint8_t *>( pBankMemory ), size, true ) !=
         0 ) {
        return false;
    }
    bankSize = size;
    parse();
    return document.isValid();
}

void VehicleSoundBank::parse() {
    // FUN_007d4310 (inlined)
    const void *pConfig = GetBankEntryData( bank, "CarVSTConfig.xmb" );
    document = edXMBNode( pConfig );
    if ( !document.isValid() ) {
        OTDU_LOG_ERROR( "'%s' has no usable CarVSTConfig.xmb!\n",
                        name.c_str() );
        return;
    }

    const edXMBNode sampleFiles = document.findChild( "SampleFiles" );
    const int32_t declaredCount = ArrayCountOf( sampleFiles );
    waveCount = declaredCount < kMaxSamples ? declaredCount : kMaxSamples;

    for ( int32_t i = 0; i < waveCount; i++ ) {
        const char *pFileName = sampleFiles.getElement( i ).asString();
        if ( pFileName == nullptr || strcmp( pFileName, kNoSampleFile ) == 0 ) {
            continue;
        }
        pWaveData[i] = GetBankEntryData( bank, pFileName );
    }
}

bool VehicleSoundModel::parse( const VehicleSoundBank &sharedBank ) {
    // FUN_007d4310
    pBank = &sharedBank;

    const edXMBNode &document = sharedBank.getDocument();
    if ( !document.isValid() ) {
        return false;
    }

    samples = {};
    bPaused = false;

    const edXMBNode samplesConfig = document.findChild( "SamplesConfig" );
    const int32_t sampleCount = ArrayCountOf( samplesConfig );

    for ( int32_t i = 0; i < sampleCount; i++ ) {
        const edXMBNode node = samplesConfig.getElement( i );

        const int32_t sampleID = node.readInt( "SampleID" );
        if ( sampleID < 0 || sampleID >= kMaxSamples ) {
            OTDU_LOG_WARN( "'%s' SamplesConfig[%d] has SampleID %d, out of the "
                           "%d slot table; skipped\n",
                           sharedBank.getName(), i, sampleID, kMaxSamples );
            continue;
        }

        parseSample( samples[sampleID], node, sharedBank );
    }

    return true;
}

void VehicleSoundModel::parseSample(
    VehicleSoundSample &sample,
    const edXMBNode &node,
    const VehicleSoundBank &sharedBank ) const {
    // FUN_007d33b0
    const int32_t carWaveCount = sharedBank.getWaveCount();
    sample.SampleID = node.readInt( "SampleID" );
    const edXMBNode bankNode = node.findChild( "Bank" );
    const bool bUseCommonBank = bankNode.isValid() && bankNode.asInt() == 1;
    const int32_t waveIndex = node.readInt( "nWaveIndex" );

    if ( bUseCommonBank ) {
        sample.pWaveData = gCommonVehicleSounds.getWaveData( waveIndex );
    } else if ( waveIndex >= 0 && waveIndex < carWaveCount ) {
        sample.pWaveData = sharedBank.getWaveData( waveIndex );
    } else {
        sample.pWaveData = nullptr;
    }

    if ( sample.pWaveData == nullptr ) {
        sample.bValid = false;
        return;
    }
    sample.bValid = true;

    sample.bMustBeLooped = node.readBool( "nMustBeLooped" );
    sample.bIsTrigger = node.readBool( "bIsTrigger" );
    sample.PanelID = node.readInt( "PanelID" );
    sample.BaseVolume = node.readFloat( "fBaseVolume" );
    sample.ClipNear = node.readFloat( "fClipNear" );
    sample.ClipFar = node.readFloat( "fClipFar" );
    sample.RefractionFactor = node.readFloat( "fRefractionFactor" );
    sample.TrackingClipNear = node.readFloat( "fTrackingClipNear" );
    sample.TrackingClipFar = node.readFloat( "fTrackingClipFar" );
    sample.TrackingRefractionFactor =
        node.readFloat( "fTrackingRefractionFactor" );

    const edXMBNode trackingFactors =
        node.findChild( "TrackingClippingFactors" );
    sample.TrackingClippingFactorCount = ArrayCountOf( trackingFactors );
    sample.pTrackingClippingFactors =
        static_cast<const float *>( trackingFactors.getValue() );

    sample.pPlaybackStateVolume = static_cast<const float *>(
        node.findChild( "PlaybackStateVolume" ).getValue() );

    sample.pSampleName = node.readString( "sSampleName" );

    const edXMBNode positions = node.findChild( "InstancePosition" );
    sample.SoundCount = ArrayCountOf( positions );

    const int32_t positionCount =
        sample.SoundCount < VehicleSoundSample::kMaxInstancePositions
            ? sample.SoundCount
            : VehicleSoundSample::kMaxInstancePositions;

    for ( int32_t i = 0; i < positionCount; i++ ) {
        const edXMBNode position = positions.getElement( i );
        sample.SoundPositions[size_t( i )][0] = position.readFloat( "fX" );
        sample.SoundPositions[size_t( i )][1] = position.readFloat( "fY" );
        sample.SoundPositions[size_t( i )][2] = position.readFloat( "fZ" );
        sample.SoundPositions[size_t( i )][3] = kEmitterRight;
    }
    sample.SoundCount = positionCount;

    if ( IsStereoEmitterSample( sample.SampleID ) ) {
        sample.SoundCount = 2;
        sample.SoundPositions[0][0] = kEmitterRight;
        sample.SoundPositions[0][1] = 0.0f;
        sample.SoundPositions[0][2] = 0.0f;
        sample.SoundPositions[0][3] = kEmitterRight;
        sample.SoundPositions[1][0] = kEmitterLeft;
        sample.SoundPositions[1][1] = 0.0f;
        sample.SoundPositions[1][2] = 0.0f;
        sample.SoundPositions[1][3] = kEmitterRight;
    }

    const edXMBNode links = node.findChild( "Links" );
    sample.LinkCount = ArrayCountOf( links );

    const int32_t linkCount = sample.LinkCount < VehicleSoundSample::kMaxLinks
                                  ? sample.LinkCount
                                  : VehicleSoundSample::kMaxLinks;

    for ( int32_t i = 0; i < linkCount; i++ ) {
        const edXMBNode link = links.getElement( i );
        VehicleSoundLink &out = sample.Links[size_t( i )];

        const edXMBNode physic = link.findChild( "Physic" );
        out.pPhysic = static_cast<const int32_t *>( physic.getValue() );
        out.PhysicCount = ArrayCountOf( physic );

        out.Audio = link.readInt( "Audio" );

        const edXMBNode curveSet = link.findChild( "CurveSet" );
        out.CurvePointCount = ArrayCountOf( curveSet ) / 2;
        out.pCurveSet = static_cast<const float *>( curveSet.getValue() );
    }
    sample.LinkCount = linkCount;
}

void VehicleSoundModel::computeSampleGain(
    VehicleSoundSample &sample,
    int32_t soundIndex,
    const VSTPhysicsSettings &settings,
    const float *pPanelVolumeFactors ) const {
    // FUN_007d2e20 (inlined)
    const uint32_t stateFlags =
        ( soundIndex >= 0 && soundIndex < VSTPhysicsSettings::kStateFlagCount )
            ? settings.StateFlags[soundIndex]
            : 0u;

    const float panelGain = ( sample.PanelID >= 0 && sample.PanelID <= 3 &&
                              pPanelVolumeFactors != nullptr )
                                ? pPanelVolumeFactors[sample.PanelID]
                                : 0.0f;

    sample.CurrentVolume = sample.BaseVolume * panelGain;
    sample.CurrentPitch = 1.0f;

    if ( sample.pPlaybackStateVolume != nullptr ) {
        for ( int32_t bit = 0; bit < 6; bit++ ) {
            if ( ( stateFlags & ( 1u << bit ) ) != 0 ) {
                sample.CurrentVolume *= sample.pPlaybackStateVolume[bit];
            }
        }
    }

    if ( sample.CurrentVolume > kMinAudibleVolume ) {
        for ( int32_t i = 0; i < sample.LinkCount; i++ ) {
            const VehicleSoundLink &link = sample.Links[size_t( i )];
            if ( link.pPhysic == nullptr || soundIndex >= link.PhysicCount ) {
                continue;
            }

            const int32_t channel = link.pPhysic[soundIndex];
            if ( channel < 0 ||
                 channel >= VSTPhysicsSettings::kPhysicsValueCount ) {
                continue;
            }

            const float gain =
                EvaluateVSTCurve( link.pCurveSet, link.CurvePointCount,
                                  settings.PhysicsValues[channel] );

            if ( link.Audio == 0 ) {
                sample.CurrentPitch *= gain;
            } else {
                sample.CurrentVolume *= gain;
            }
        }

        sample.CurrentVolume *= settings.VolumeFactor;
    }

    if ( sample.bIsTrigger ) {
        return;
    }

    int32_t &request = sample.InstanceRequest[size_t( soundIndex )];
    if ( sample.CurrentVolume < kMinAudibleVolume ||
         sample.CurrentPitch < kMinAudiblePitch ) {
        request = VSTIR_Stop;
    } else if ( sample.bValid && request != VSTIR_Held ) {
        request = VSTIR_Start;
    }
}

void VehicleSoundModel::updateSampleClipping(
    VehicleSoundSample &sample, const VSTPhysicsSettings &settings ) const {
    // FUN_007d2e20 (inlined)
    const bool bUsePlain =
        !settings.bIsPlayerState || settings.TrackingMode == 6;

    if ( bUsePlain ) {
        sample.CurrentClipNear = sample.ClipNear;
        sample.CurrentClipFar = sample.ClipFar;
        sample.CurrentRefractionFactor = sample.RefractionFactor;
    } else {
        sample.CurrentClipNear = sample.TrackingClipNear;
        sample.CurrentClipFar = sample.TrackingClipFar;
        sample.CurrentRefractionFactor = sample.TrackingRefractionFactor;
    }

    sample.CurrentDopplerFactor = bUsePlain ? 1.0f : 0.0f;
    if ( settings.bIsPlayerState && settings.TrackingMode >= 0 &&
         settings.TrackingMode < sample.TrackingClippingFactorCount &&
         sample.pTrackingClippingFactors != nullptr ) {
        const float factor =
            sample.pTrackingClippingFactors[settings.TrackingMode];
        sample.CurrentClipNear *= factor;
        sample.CurrentClipFar *= factor;
    }
}

void VehicleSoundModel::updateSampleInstance(
    VehicleSoundSample &sample,
    int32_t soundIndex,
    const Eigen::Vector3f &worldPosition,
    const Eigen::Vector3f &speed,
    IVSTSoundInstanceSource *pInstanceSource ) const {
    // FUN_007d2cd0
    const size_t emitter = size_t( soundIndex );
    if ( sample.pSoundInstances[emitter] == nullptr &&
         sample.InstanceRequest[emitter] == VSTIR_Start &&
         pInstanceSource != nullptr ) {
        float duration = 0.0f;
        sample.pSoundInstances[emitter] = pInstanceSource->acquireSoundInstance(
            sample.pWaveData, sample.pSampleName, sample.bMustBeLooped,
            duration );

        if ( sample.pSoundInstances[emitter] != nullptr ) {
            sample.SampleDuration = duration;
        }
    }

    AudioDispatcherPlaybackSoundInstance *pInstance =
        sample.pSoundInstances[emitter];
    if ( pInstance == nullptr ) {
        return;
    }

    if ( sample.InstanceRequest[emitter] != VSTIR_Stop ) {
        pInstance->setVolume( sample.CurrentVolume );
        pInstance->setPitch( sample.CurrentPitch );
        pInstance->setClipDistances( sample.CurrentClipNear,
                                     sample.CurrentClipFar );
        pInstance->setRefractionFactor( sample.CurrentRefractionFactor );
        pInstance->setPosition(
            worldPosition, AudioDispatcherPlaybackInstanceValues::ADPCS_World );
        pInstance->setSpeed( speed );
        pInstance->setDopplerFactor( sample.CurrentDopplerFactor );
    }

    switch ( sample.InstanceRequest[emitter] ) {
    case VSTIR_Start:
        if ( pInstance->isStopped() ) {
            pInstance->play( 0.0f );

            if ( sample.bMustBeLooped && sample.SoundCount > 0 ) {
                pInstance->seek(
                    ( float( soundIndex ) * sample.SampleDuration ) /
                    float( sample.SoundCount ) );
            }
        }
        break;

    case VSTIR_Stop:
        pInstance->stop( 0.0f );
        break;

    case VSTIR_Held:
        pInstance->play( 0.1f );
        break;

    default:
        break;
    }

    if ( pInstance->isStopped() ) {
        if ( pInstanceSource != nullptr ) {
            pInstanceSource->releaseSoundInstance( pInstance );
        }
        sample.pSoundInstances[emitter] = nullptr;
    }

    sample.InstanceRequest[emitter] = VSTIR_None;
}

int32_t
VehicleSoundModel::releaseVoices( IVSTSoundInstanceSource *pInstanceSource ) {
    int32_t released = 0;
    for ( VehicleSoundSample &sample : samples ) {
        for ( size_t slot = 0; slot < sample.pSoundInstances.size(); slot++ ) {
            if ( sample.pSoundInstances[slot] == nullptr ) {
                continue;
            }

            if ( pInstanceSource != nullptr ) {
                pInstanceSource->releaseSoundInstance(
                    sample.pSoundInstances[slot] );
            }
            sample.pSoundInstances[slot] = nullptr;
            sample.InstanceRequest[slot] = VSTIR_None;
            released++;
        }
    }
    return released;
}

void VehicleSoundModel::dispatchEvents( const VSTPhysicsSettings &settings ) {
    // FUN_007d29a0
    auto start = [this]( int32_t sampleID, int32_t emitter ) {
        VehicleSoundSample &sample = samples[size_t( sampleID )];
        if ( sample.bValid &&
             sample.InstanceRequest[size_t( emitter )] != VSTIR_Held ) {
            sample.InstanceRequest[size_t( emitter )] = VSTIR_Start;
        }
    };
    auto stop = [this]( int32_t sampleID, int32_t emitter ) {
        samples[size_t( sampleID )].InstanceRequest[size_t( emitter )] =
            VSTIR_Stop;
    };

    if ( settings.bEventGearUpChanged ) {
        start( VSTRS_GearUp, 0 );
    }
    if ( settings.bEventGearDownChanged ) {
        start( VSTRS_GearDown, 0 );
    }

    if ( settings.bEventStartEnginePushed ) {
        start( VSTRS_StartEngine, 0 );
    }
    if ( settings.bEventStartEngineReleased ) {
        stop( VSTRS_StartEngine, 0 );
    }

    if ( settings.bEventHornPushed ) {
        start( VSTRS_Horn, 0 );
    }
    if ( settings.bEventHornReleased ) {
        stop( VSTRS_Horn, 0 );
    }

    if ( settings.bEventDoorLeftClose ) {
        start( VSTRS_DoorClose, 0 );
    }
    if ( settings.bEventDoorLeftOpen ) {
        start( VSTRS_DoorOpen, 0 );
    }
    if ( settings.bEventDoorRightClose ) {
        start( VSTRS_DoorClose, 1 );
    }
    if ( settings.bEventDoorRightOpen ) {
        start( VSTRS_DoorOpen, 1 );
    }

    if ( settings.bEventWindowMovingStart ) {
        start( VSTRS_Window, 0 );
        start( VSTRS_Window, 1 );
    }
    if ( settings.bEventWindowMovingStop ) {
        stop( VSTRS_Window, 0 );
        stop( VSTRS_Window, 1 );
    }
}

void VehicleSoundModel::pause() {
    // FUN_007d2960
    if ( bPaused ) {
        return;
    }

    for ( VehicleSoundSample &sample : samples ) {
        // FUN_007d2860 (inlined)
        if ( !sample.bValid ) {
            continue;
        }

        for ( AudioDispatcherPlaybackSoundInstance *pInstance :
              sample.pSoundInstances ) {
            if ( pInstance != nullptr ) {
                pInstance->pause( kEnginePauseFadeTime );
            }
        }
    }

    bPaused = true;
}

void VehicleSoundModel::resume() {
    // FUN_007d2920
    if ( !bPaused ) {
        return;
    }

    for ( VehicleSoundSample &sample : samples ) {
        if ( !sample.bValid ) {
            continue;
        }

        for ( size_t emitter = 0; emitter < sample.pSoundInstances.size();
              emitter++ ) {
            AudioDispatcherPlaybackSoundInstance *pInstance =
                sample.pSoundInstances[emitter];
            if ( pInstance != nullptr && pInstance->isPaused() ) {
                sample.InstanceRequest[emitter] = VSTIR_Held;
            }
        }
    }

    bPaused = false;
}

void VehicleSoundModel::update( const VSTPhysicsSettings &settings,
                                const float *pPanelVolumeFactors,
                                const Eigen::Vector3f &listenerPosition,
                                IVSTSoundInstanceSource *pInstanceSource ) {
    // FUN_007d45e0
    dispatchEvents( settings );

    for ( int32_t id = 0; id < kMaxSamples; id++ ) {
        if ( id == 0x26 ) {
            continue;
        }

        VehicleSoundSample &sample = samples[size_t( id )];
        if ( !sample.bValid || sample.pPlaybackStateVolume == nullptr ||
             sample.SoundCount < 1 ) {
            continue;
        }

        const bool bCollapseToOrigin =
            !settings.bIsPlayerState && sample.SoundCount != 1;

        for ( int32_t emitter = 0; emitter < sample.SoundCount; emitter++ ) {
            const size_t slot = size_t( emitter );

            const Eigen::Vector3f local =
                bCollapseToOrigin
                    ? Eigen::Vector3f::Zero()
                    : Eigen::Vector3f( sample.SoundPositions[slot][0],
                                       sample.SoundPositions[slot][1],
                                       sample.SoundPositions[slot][2] );

            const Eigen::Vector3f worldPosition =
                TDUMat4x4MulVec( settings.Transform, local );

            updateSampleClipping( sample, settings );
            int32_t deferredRequest = VSTIR_None;

            const float clipFar = sample.CurrentClipFar;
            const float distanceSq =
                ( listenerPosition - worldPosition ).squaredNorm();

            if ( clipFar * clipFar <= distanceSq ) {
                deferredRequest = sample.InstanceRequest[slot];
                if ( deferredRequest != VSTIR_Stop &&
                     sample.pSoundInstances[slot] != nullptr &&
                     !sample.pSoundInstances[slot]->isStopped() ) {
                    deferredRequest = VSTIR_Start;
                }

                sample.InstanceRequest[slot] = VSTIR_Stop;
            } else {
                computeSampleGain( sample, emitter, settings,
                                   pPanelVolumeFactors );
            }

            updateSampleInstance( sample, emitter, worldPosition,
                                  settings.Speed, pInstanceSource );

            sample.InstanceRequest[slot] = deferredRequest;

            if ( bCollapseToOrigin ) {
                for ( int32_t rest = 1; rest < sample.SoundCount; rest++ ) {
                    sample.InstanceRequest[size_t( rest )] = VSTIR_Stop;
                }
                break;
            }
        }
    }
}

int32_t VehicleSoundModel::getValidSampleCount() const {
    int32_t count = 0;
    for ( const VehicleSoundSample &sample : samples ) {
        count += sample.bValid ? 1 : 0;
    }
    return count;
}
