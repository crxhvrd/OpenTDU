#include "shared.h"
#include "gs_physicsfx.h"
#include <algorithm>
#include <string>
#include <vector>

#include "audio/gs_audio.h"
#include "audio/implementation/audio_dispatcher_playback.h"
#include "audio/implementation/ed_audio_sample.h"
#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "render/particles/gs_particles.h"

GSPhysicsFX *gpPhysicsFX = nullptr; // DAT_010e7c5c

// eMaterialType -> FX material LUT
static constexpr int32_t kMaterialRemap[48] = {
    // DAT_00f8b5f0
    // 0..13 map to themselves (Asphalt, Dirt, Grass, Rock, Water, Wood,
    // Metal, Plastic, Concrete, Pavement, Rubber, Vegetation1..3)
    0,  1,  2,  3,  4,  5,  6, 7, 8, 9, 10, 11, 12, 13,
    16,                           // 14 Tires
    6,                            // 15 Hood     -> Metal
    6,                            // 16 Roof     -> Metal
    14,                           // 17 Chassis
    6,                            // 18 Body     -> Metal
    6,  6,  6,  6,  6,  6,  6, 6, // 19..26 BodyF..BodyMR -> Metal
    15, 15, 15, 15, 15, 15,       // 27..32 BumperF..BumperRR
    6,  6,  6,  6,  6,  6,        // 33..38 DoorF..DoorFR  -> Metal
    6,                            // 39 SP1      -> Metal
    6,                            // 40 SP2      -> Metal
    7,                            // 41 SP3      -> Plastic
    17,                           // 42 SP4
    18,                           // 43 Foliage1
    19,                           // 44 Foliage2
    20,                           // 45 Foliage3
    21,                           // 46 Foliage4
    22,                           // 47 Default  -> rejected
};

// How long a looping slot may live once no contact refreshes it anymore.
static constexpr float kSlotLingerTime = 0.4f; // DAT_00f4175c

inline const SFXChunkHeader *ChunkAt( const void *pBase, int32_t offset ) {
    return reinterpret_cast<const SFXChunkHeader *>(
        static_cast<const uint8_t *>( pBase ) + offset );
}

inline const uint8_t *ChunkPayload( const SFXChunkHeader *pChunk ) {
    return reinterpret_cast<const uint8_t *>( pChunk ) + 0x10;
}

inline const SFXChunkHeader *FirstChild( const SFXChunkHeader *pChunk ) {
    if ( pChunk == nullptr || pChunk->NumChildren < 1 ) {
        return nullptr;
    }
    return ChunkAt( pChunk, pChunk->HeaderSize );
}

inline const SFXChunkHeader *NextSibling( const SFXChunkHeader *pChunk ) {
    return ChunkAt( pChunk, pChunk->Size );
}
Matrix4x4f_t MakeContactBasis( const Eigen::Vector3f &normal,
                               const Eigen::Vector3f &position ) {
    // FUN_0042a3d0
    Eigen::Vector3f up = normal;
    const float length = up.norm();
    up = ( length > 0.0001f ) ? Eigen::Vector3f( up / length )
                              : Eigen::Vector3f::UnitY();

    const Eigen::Vector3f seed = ( std::fabs( up.y() ) > 0.99f )
                                     ? Eigen::Vector3f::UnitX()
                                     : Eigen::Vector3f::UnitY();
    Eigen::Vector3f right = seed.cross( up );
    const float rightLength = right.norm();
    right = ( rightLength > 0.0001f ) ? Eigen::Vector3f( right / rightLength )
                                      : Eigen::Vector3f::UnitX();
    const Eigen::Vector3f forward = up.cross( right );

    Matrix4x4f_t basis = Matrix4x4f_t::Identity();
    basis.row( 0 ) << right.x(), right.y(), right.z(), 0.0f;
    basis.row( 1 ) << up.x(), up.y(), up.z(), 0.0f;
    basis.row( 2 ) << forward.x(), forward.y(), forward.z(), 0.0f;
    basis.row( 3 ) << position.x(), position.y(), position.z(), 1.0f;
    return basis;
}

inline float LerpOverBand( float minValue,
                           float maxValue,
                           const SFXSampleRecord &record,
                           float energy ) {
    const float span = record.EnergyMax - record.EnergyMin;
    if ( span == 0.0f ) {
        return minValue;
    }
    return ( maxValue - minValue ) * ( energy - record.EnergyMin ) / span +
           minValue;
}

void PhysicsFXEventQueue::push( const PhysicsFXEvent &event ) {
    ScopedMutexLock lock( &mutex );

    int32_t &count = counts[writeIndex];
    if ( count >= kMaxEventsPerFrame ) {
        return;
    }

    auto &buffer = events[writeIndex];
    for ( int32_t i = 0; i < count; i++ ) {
        const PhysicsFXEvent &queued = buffer[i];
        if ( ( queued.pBodyA == event.pBodyA && queued.pBodyB == event.pBodyB &&
               queued.FXMaterialA == event.FXMaterialA &&
               queued.FXMaterialB == event.FXMaterialB ) ||
             ( queued.pBodyA == event.pBodyB && queued.pBodyB == event.pBodyA &&
               queued.FXMaterialA == event.FXMaterialB &&
               queued.FXMaterialB == event.FXMaterialA ) ) {
            return;
        }
    }

    buffer[count] = event;
    count++;
}

void PhysicsFXEventQueue::swapBuffers() {
    // FUN_0097f260 (inlined)
    ScopedMutexLock lock( &mutex );
    std::swap( writeIndex, readIndex );
    counts[writeIndex] = 0;
}

void PhysicsFXEventQueue::reset() {
    ScopedMutexLock lock( &mutex );
    counts[0] = 0;
    counts[1] = 0;
    writeIndex = 0;
    readIndex = 1;
}

GSPhysicsFX::GSPhysicsFX() : GameSystem() {
    // FUN_009de650
    gpPhysicsFX = this;
}

GSPhysicsFX::~GSPhysicsFX() {
    // FUN_0042a390
    releaseConfig();
    gpPhysicsFX = nullptr;
}

int32_t GSPhysicsFX::RemapMaterial( int32_t materialType ) {
    if ( materialType < 0 || materialType >= kNumMaterialTypes ) {
        return kInvalidFXMaterial;
    }
    return kMaterialRemap[materialType];
}

bool GSPhysicsFX::initialize( TestDriveGameInstance * ) {
    // FUN_007cb620
    bMuted = false;
    loadConfig();
    return true;
}

void GSPhysicsFX::loadConfig() {
    // FUN_007cb290
    if ( gpConfig == nullptr ) {
        return;
    }

    std::string path = gpConfig->getResRootPath();
    path += "Sound/Physics/SFXConfig.bin";

    void *pContent = nullptr;
    uint32_t contentSize = 0;
    if ( gpFile == nullptr ||
         !gpFile->loadFile( path.c_str(), &pContent, &contentSize ) ||
         pContent == nullptr || contentSize < sizeof( SFXChunkHeader ) ) {
        OTDU_LOG_WARN( "Could not load '%s'; collision FX are disabled\n",
                       path.c_str() );
        return;
    }
    configBlob.assign( static_cast<const uint8_t *>( pContent ),
                       static_cast<const uint8_t *>( pContent ) + contentSize );

    std::string bankPath = gpConfig->getResRootPath();
    bankPath += "Sound/Physics/Samples.bnk";
    loadBootBank( bankPath.c_str(), nullptr );
    if ( !isLoaded() ) {
        OTDU_LOG_WARN( "Could not load '%s'; collision FX are disabled\n",
                       bankPath.c_str() );
        configBlob.clear();
        return;
    }

    const SFXChunkHeader *pRoot =
        reinterpret_cast<const SFXChunkHeader *>( configBlob.data() );

    // Section 0 (FILELIST) names one Samples.bnk entry per sample index.
    const SFXChunkHeader *pFileList = FirstChild( pRoot );
    if ( pFileList == nullptr || !loadSampleTable( pFileList ) ) {
        configBlob.clear();
        return;
    }

    // Section 1 (BINOLIST) — one BINOMIAL per (material, material, kind).
    if ( pRoot->NumChildren < 2 ) {
        configBlob.clear();
        return;
    }
    pBinoList = NextSibling( pFileList );

    // Section 2 (SETTINGS) — { unused, clipNear, clipFar, refraction }.
    if ( pRoot->NumChildren >= 3 ) {
        pSettings = reinterpret_cast<const float *>(
            ChunkPayload( NextSibling( pBinoList ) ) );
    }

    materialLUT.fill( nullptr );

    const SFXChunkHeader *pBinomial = FirstChild( pBinoList );
    for ( int32_t i = 0; i < pBinoList->NumChildren && pBinomial != nullptr;
          i++ ) {
        const int32_t *pPayload =
            reinterpret_cast<const int32_t *>( ChunkPayload( pBinomial ) );
        const int32_t materialA = pPayload[0];
        const int32_t materialB = pPayload[1];
        const int32_t kind = pPayload[2];

        if ( materialA >= 0 && materialA < kNumFXMaterials && materialB >= 0 &&
             materialB < kNumFXMaterials && kind >= 0 && kind < PFXEK_Count ) {
            materialLUT[( materialA * kNumFXMaterials + materialB ) *
                            PFXEK_Count +
                        kind] = pBinomial;
            materialLUT[( materialB * kNumFXMaterials + materialA ) *
                            PFXEK_Count +
                        kind] = pBinomial;
        }

        pBinomial = NextSibling( pBinomial );
    }

    if ( gpAudio != nullptr ) {
        pAudioClient = gpAudio->createClient( "ClientPhysicSFX" );
    }

    int32_t numResolvedSamples = 0;
    for ( const void *pData : sampleData ) {
        numResolvedSamples += ( pData != nullptr ) ? 1 : 0;
    }
    OTDU_LOG_DEBUG( "Collision FX loaded: %d/%d samples, %d material pairs, "
                    "audio client %s\n",
                    numResolvedSamples, int32_t( sampleData.size() ),
                    pBinoList->NumChildren,
                    pAudioClient != nullptr ? "OK" : "MISSING" );

    bLoaded = true;
}

bool GSPhysicsFX::loadSampleTable( const SFXChunkHeader *pFileList ) {
    std::unordered_map<std::string, const void *> byName;
    const int32_t numEntries = getNumElements();
    for ( int32_t i = 0; i < numEntries; i++ ) {
        ParsedBankEntry entry;
        getEntry( i, entry );
        if ( entry.pData == nullptr ) {
            continue;
        }
        std::string name = entry.Filename;
        const size_t slash = name.find_last_of( "/\\" );
        if ( slash != std::string::npos ) {
            name = name.substr( slash + 1 );
        }
        for ( char &c : name ) {
            c = static_cast<char>( tolower( static_cast<unsigned char>( c ) ) );
        }
        byName.emplace( std::move( name ), entry.pData );
    }

    sampleData.reserve( static_cast<size_t>( pFileList->NumChildren ) );
    const SFXChunkHeader *pFileName = FirstChild( pFileList );
    for ( int32_t i = 0; i < pFileList->NumChildren && pFileName != nullptr;
          i++ ) {
        std::string name =
            reinterpret_cast<const char *>( ChunkPayload( pFileName ) );
        for ( char &c : name ) {
            c = static_cast<char>( tolower( static_cast<unsigned char>( c ) ) );
        }

        auto it = byName.find( name );
        sampleData.push_back( it != byName.end() ? it->second : nullptr );
        pFileName = NextSibling( pFileName );
    }

    return !sampleData.empty();
}

void GSPhysicsFX::releaseConfig() {
    // FUN_007cb1e0
    if ( !bLoaded ) {
        return;
    }

    stopAllEffects();

    samples.clear();
    sampleData.clear();
    configBlob.clear();
    materialLUT.fill( nullptr );
    pBinoList = nullptr;
    pSettings = nullptr;

    if ( isLoaded() ) {
        unload();
    }

    // GSAudio owns the client objects; dropping the reference is enough.
    pAudioClient = nullptr;
    bLoaded = false;
}

void GSPhysicsFX::stopAllEffects() {
    // FUN_007cae50
    for ( FXSlot &slot : slots ) {
        for ( FXLayer &layer : slot.Layers ) {
            if ( layer.pSound != nullptr ) {
                layer.pSound->stop();
                layer.pSound->release();
                layer.pSound = nullptr;
            }
            for ( ParticleInstance *&pInstance : layer.pParticles ) {
                if ( pInstance != nullptr && gpParticles != nullptr ) {
                    pInstance->State = eParticleState::PS_Killed;
                    pInstance->stop();
                }
                pInstance = nullptr;
            }
            layer.bActive = false;
            layer.pRecord = nullptr;
        }
        slot.State = FXSS_Free;
        slot.bRefreshedThisFrame = false;
        slot.Timer = 0.0f;
    }

    eventQueue.reset();
}

void GSPhysicsFX::terminate() {
    // FUN_007cb5e0
    releaseConfig();
}

void GSPhysicsFX::draw( float ) {
    // FUN_007cb570
    // NOP
}

const SFXChunkHeader *GSPhysicsFX::getBinomial( int32_t fxMaterialA,
                                                int32_t fxMaterialB,
                                                int32_t kind ) const {
    if ( fxMaterialA < 0 || fxMaterialA >= kNumFXMaterials || fxMaterialB < 0 ||
         fxMaterialB >= kNumFXMaterials || kind < 0 || kind >= PFXEK_Count ) {
        return nullptr;
    }
    return materialLUT[( fxMaterialA * kNumFXMaterials + fxMaterialB ) *
                           PFXEK_Count +
                       kind];
}

const SFXChunkHeader *GSPhysicsFX::selectMassBucket(
    const SFXChunkHeader *pBinomial, float massA, float massB ) {
    // FUN_007ca840
    const SFXChunkHeader *pJoined = FirstChild( pBinomial );
    for ( int32_t i = 0; i < pBinomial->NumChildren && pJoined != nullptr;
          i++ ) {
        const float *pRange =
            reinterpret_cast<const float *>( ChunkPayload( pJoined ) );
        if ( pRange[0] <= massA && massA <= pRange[1] && pRange[2] <= massB &&
             massB <= pRange[3] ) {
            return pJoined;
        }
        pJoined = NextSibling( pJoined );
    }
    return nullptr;
}

const SFXSampleRecord *
GSPhysicsFX::selectEnergyBand( const SFXChunkHeader *pSample, float energy ) {
    // FUN_007ca6e0
    const uint8_t *pPayload = ChunkPayload( pSample );
    const int32_t numRecords = *reinterpret_cast<const int32_t *>( pPayload );
    const SFXSampleRecord *pRecords =
        reinterpret_cast<const SFXSampleRecord *>( pPayload + 4 );

    for ( int32_t i = 0; i < numRecords; i++ ) {
        if ( pRecords[i].EnergyMin <= energy &&
             energy < pRecords[i].EnergyMax ) {
            return &pRecords[i];
        }
    }
    return nullptr;
}

GSPhysicsFX::FXSlot *
GSPhysicsFX::findActiveSlot( const PhysicsFXEvent &event ) {
    // FUN_007ca660
    for ( FXSlot &slot : slots ) {
        if ( slot.State == FXSS_Free || slot.Event.Kind != event.Kind ) {
            continue;
        }
        const PhysicsFXEvent &queued = slot.Event;
        if ( ( queued.pBodyA == event.pBodyA && queued.pBodyB == event.pBodyB &&
               queued.FXMaterialA == event.FXMaterialA &&
               queued.FXMaterialB == event.FXMaterialB ) ||
             ( queued.pBodyA == event.pBodyB && queued.pBodyB == event.pBodyA &&
               queued.FXMaterialA == event.FXMaterialB &&
               queued.FXMaterialB == event.FXMaterialA ) ) {
            return &slot;
        }
    }
    return nullptr;
}

GSPhysicsFX::FXSlot *GSPhysicsFX::acquireSlot() {
    // FUN_007ca720
    for ( FXSlot &slot : slots ) {
        if ( slot.State == FXSS_Free ) {
            return &slot;
        }
    }
    return nullptr;
}

const edAudioSample *GSPhysicsFX::resolveSample( const void *pWaveData ) {
    if ( pWaveData == nullptr ) {
        return nullptr;
    }

    auto it = samples.find( pWaveData );
    if ( it != samples.end() ) {
        return it->second.get();
    }

    const uint8_t *pBytes = static_cast<const uint8_t *>( pWaveData );
    if ( memcmp( pBytes, "RIFF", 4 ) != 0 ||
         memcmp( pBytes + 8, "WAVE", 4 ) != 0 ) {
        samples.emplace( pWaveData, nullptr );
        return nullptr;
    }

    uint32_t riffSize = 0;
    memcpy( &riffSize, pBytes + 4, sizeof( riffSize ) );
    const int64_t waveSize = int64_t( riffSize ) + 8;
    if ( waveSize <= 44 || waveSize > ( 64 << 20 ) ) {
        samples.emplace( pWaveData, nullptr );
        return nullptr;
    }

    auto pSample = std::make_unique<edAudioSample>();
    if ( !pSample->loadFromMemory( pWaveData,
                                   static_cast<int32_t>( waveSize ) ) ) {
        samples.emplace( pWaveData, nullptr );
        return nullptr;
    }

    const edAudioSample *pResult = pSample.get();
    samples.emplace( pWaveData, std::move( pSample ) );
    return pResult;
}

void GSPhysicsFX::startEffect( const PhysicsFXEvent &event ) {
    // FUN_007cabc0
    if ( bMuted ) {
        return;
    }

    const SFXChunkHeader *pBinomial =
        getBinomial( event.FXMaterialA, event.FXMaterialB, event.Kind );
    if ( pBinomial == nullptr ) {
        return;
    }

    const SFXChunkHeader *pJoined =
        selectMassBucket( pBinomial, event.MassA, event.MassB );
    if ( pJoined == nullptr ) {
        return;
    }

    FXSlot *pSlot = acquireSlot();
    if ( pSlot == nullptr ) {
        OTDU_LOG_ERROR( "Slot pool exhausted!\n" );
        return;
    }

    const int32_t numLayers = ( pJoined->NumChildren > kNumLayersPerSlot )
                                  ? kNumLayersPerSlot
                                  : pJoined->NumChildren;
    const SFXChunkHeader *pSample = FirstChild( pJoined );

    const bool bLooping =
        ( event.Kind == PFXEK_ImpactLooped || event.Kind == PFXEK_Slide );

    for ( int32_t i = 0; i < numLayers && pSample != nullptr; i++ ) {
        FXLayer &layer = pSlot->Layers[i];

        const SFXSampleRecord *pRecord =
            selectEnergyBand( pSample, event.Energy );
        if ( pRecord == nullptr || pRecord->SampleIndex < 0 ||
             pRecord->SampleIndex >=
                 static_cast<int32_t>( sampleData.size() ) ) {
            pSample = NextSibling( pSample );
            continue;
        }

        const edAudioSample *pWave =
            resolveSample( sampleData[pRecord->SampleIndex] );
        if ( pWave == nullptr ) {
            pSample = NextSibling( pSample );
            continue;
        }

        if ( pAudioClient != nullptr ) {
            layer.pSound = pAudioClient->acquireSoundInstance();
        }
        if ( layer.pSound == nullptr ) {
            pSample = NextSibling( pSample );
            continue;
        }

        layer.bActive = true;
        layer.pRecord = pRecord;
        layer.pSound->setSample( pWave );

        layer.pSound->setVolume( LerpOverBand( pRecord->VolumeMin,
                                               pRecord->VolumeMax, *pRecord,
                                               event.Energy ) *
                                 masterVolumeScale );
        layer.pSound->setPitch( LerpOverBand(
            pRecord->PitchMin, pRecord->PitchMax, *pRecord, event.Energy ) );
        layer.pSound->setLoopCount( bLooping ? -1 : 0 );
        if ( pSettings != nullptr ) {
            layer.pSound->setClipDistances( pSettings[1], pSettings[2] );
            layer.pSound->setRefractionFactor( pSettings[3] );
        }
        layer.pSound->setPosition( event.ContactPosition.head<3>(), 0 );
        layer.pSound->play( 0.0f );

        // Each layer carries up to two particle systems.
        if ( gpParticles != nullptr ) {
            for ( int32_t p = 0; p < kNumParticlesPerLayer; p++ ) {
                if ( pRecord->ParticleName[p][0] == '\0' ) {
                    continue;
                }
                // OTDU_ADDITION: by name, case-insensitively (the records
                // and particles.bnk spell some names differently)
                ParticleInstance *pInstance =
                    gpParticles->activateParticlesByName(
                        pRecord->ParticleName[p], true );
                layer.pParticles[p] = pInstance;
                if ( pInstance == nullptr ) {
                    // Some records name particles the game data never had
                    // (prt_metal): report each one once
                    static std::vector<std::string> sReported;
                    const std::string name = pRecord->ParticleName[p];
                    if ( std::find( sReported.begin(), sReported.end(),
                                    name ) == sReported.end() ) {
                        sReported.push_back( name );
                        OTDU_LOG_WARN( "Particle '%s' is not registered\n",
                                       name.c_str() );
                    }
                }
                if ( pInstance != nullptr ) {
                    pInstance->Density = pRecord->ParticleDensity[p];
                    pInstance->Scale = pRecord->ParticleSize[p];
                    pInstance->Speed = pRecord->ParticleSpeed[p];
                }
            }
        }

        pSample = NextSibling( pSample );
    }

    pSlot->State = FXSS_Playing;
    pSlot->Event = event;
    pSlot->bRefreshedThisFrame = true;
    pSlot->Timer = 0.0f;
}

void GSPhysicsFX::reapLayer( FXLayer &layer ) {
    // FUN_007ca7e0
    if ( !layer.bActive || layer.pSound == nullptr ) {
        return;
    }
    if ( !layer.pSound->isStopped() ) {
        return;
    }

    layer.pSound->stop();
    layer.pSound->release();
    layer.pSound = nullptr;

    for ( ParticleInstance *&pInstance : layer.pParticles ) {
        if ( pInstance != nullptr ) {
            pInstance->State = eParticleState::PS_Killed;
            pInstance->stop();
            pInstance = nullptr;
        }
    }

    layer.bActive = false;
    layer.pRecord = nullptr;
}

void GSPhysicsFX::ReleaseLayer( FXLayer &layer ) {
    // FUN_007ca790
    for ( ParticleInstance *&pInstance : layer.pParticles ) {
        if ( pInstance != nullptr ) {
            pInstance->State = eParticleState::PS_Killed;
            pInstance->stop();
            pInstance = nullptr;
        }
    }

    if ( layer.pSound != nullptr ) {
        layer.pSound->setLoopCount( 0 );
        layer.pSound->stop();
    }
}

void GSPhysicsFX::ReleaseSlot( FXSlot &slot ) {
    // FUN_0042a830
    for ( FXLayer &layer : slot.Layers ) {
        ReleaseLayer( layer );
    }
    slot.State = FXSS_Releasing;
    slot.bRefreshedThisFrame = false;
}

void GSPhysicsFX::updateSlot( FXSlot &slot, float deltaTime ) {
    // FUN_007caf20
    if ( slot.State == FXSS_Free ) {
        return;
    }

    if ( slot.bRefreshedThisFrame ) {
        const Matrix4x4f_t contactBasis = MakeContactBasis(
            slot.Event.Normal.head<3>(), slot.Event.ContactPosition.head<3>() );

        for ( int32_t i = 0; i < kNumLayersPerSlot; i++ ) {
            FXLayer &layer = slot.Layers[i];

            if ( slot.Event.Kind == PFXEK_Slide && layer.pRecord != nullptr &&
                 layer.pSound != nullptr ) {
                layer.pSound->setVolume( LerpOverBand( layer.pRecord->VolumeMin,
                                                       layer.pRecord->VolumeMax,
                                                       *layer.pRecord,
                                                       slot.Event.Energy ) *
                                         masterVolumeScale );
                layer.pSound->setPitch( LerpOverBand(
                    layer.pRecord->PitchMin, layer.pRecord->PitchMax,
                    *layer.pRecord, slot.Event.Energy ) );
            }

            if ( layer.pSound != nullptr ) {
                layer.pSound->setPosition( slot.Event.ContactPosition.head<3>(),
                                           0 );
            }
            const Eigen::Vector4f &sourceVelocity =
                ( i == 0 ) ? slot.Event.VelocityA : slot.Event.VelocityB;
            for ( ParticleInstance *pInstance : layer.pParticles ) {
                if ( pInstance == nullptr ) {
                    continue;
                }
                pInstance->Matrix = contactBasis;
                if ( pInstance->pParticleInstanciated != nullptr &&
                     pInstance->pParticleInstanciated->Type ==
                         eParticleType::PT_Sparkle ) {
                    pInstance->SparkleVelocity = sourceVelocity;
                }
            }
        }
    }

    for ( FXLayer &layer : slot.Layers ) {
        reapLayer( layer );
    }

    if ( !slot.Layers[0].bActive && !slot.Layers[1].bActive ) {
        slot.State = FXSS_Free;
        slot.bRefreshedThisFrame = false;
        return;
    }

    if ( slot.State == FXSS_Playing ) {
        slot.Timer += deltaTime;
        if ( slot.Timer >= kSlotLingerTime ) {
            const bool bAllLooping = ( slot.Layers[0].pSound == nullptr ||
                                       slot.Layers[0].pSound->isLooping() ) &&
                                     ( slot.Layers[1].pSound == nullptr ||
                                       slot.Layers[1].pSound->isLooping() );
            if ( bAllLooping ) {
                ReleaseSlot( slot );
                return;
            }
        }
    }

    slot.bRefreshedThisFrame = false;
}

#ifdef OTDU_USE_IMGUI
void GSPhysicsFX::drawDebug( float ) {
    ImGui::Text( "Config loaded  : %s", bLoaded ? "yes" : "no" );
    ImGui::Text( "Samples        : %d", int32_t( sampleData.size() ) );
    ImGui::Text( "Audio client   : %s", pAudioClient ? "ok" : "none" );
    ImGui::Text( "Events (frame) : %d", eventQueue.getReadCount() );

    int32_t activeSlots = 0;
    for ( const FXSlot &slot : slots ) {
        activeSlots += ( slot.State != FXSS_Free ) ? 1 : 0;
    }
    ImGui::Text( "Active slots   : %d / %d", activeSlots, kNumSlots );

    ImGui::Checkbox( "Mute collision FX", &bMuted );
    ImGui::SliderFloat( "Master volume", &masterVolumeScale, 0.0f, 2.0f );

    if ( ImGui::TreeNode( "Slots" ) ) {
        for ( int32_t i = 0; i < kNumSlots; i++ ) {
            const FXSlot &slot = slots[i];
            if ( slot.State == FXSS_Free ) {
                continue;
            }
            ImGui::Text( "%2d  mat %2d/%-2d kind %d  E=%6.2f  t=%.2f  %s", i,
                         slot.Event.FXMaterialA, slot.Event.FXMaterialB,
                         slot.Event.Kind, slot.Event.Energy, slot.Timer,
                         ( slot.State == FXSS_Releasing ) ? "releasing"
                                                          : "playing" );
        }
        ImGui::TreePop();
    }
}
#endif

void GSPhysicsFX::tick( float, float deltaTime ) {
    // FUN_007cb5c0 -> FUN_007cb0f0
    if ( !bLoaded ) {
        return;
    }

    eventQueue.swapBuffers();

    const int32_t numEvents = eventQueue.getReadCount();
    for ( int32_t i = 0; i < numEvents; i++ ) {
        const PhysicsFXEvent &event = eventQueue.getReadEvent( i );

        // Contact-removed events never spawn anything.
        if ( event.Kind == PFXEK_Removed ) {
            continue;
        }

        FXSlot *pSlot = findActiveSlot( event );
        if ( pSlot == nullptr ) {
            startEffect( event );
            continue;
        }

        // A slot that is being torn down is left alone.
        if ( pSlot->State != FXSS_Playing ) {
            continue;
        }

        pSlot->Event = event;
        pSlot->bRefreshedThisFrame = true;
        pSlot->Timer = 0.0f;
    }

    for ( FXSlot &slot : slots ) {
        updateSlot( slot, deltaTime );
    }
}
