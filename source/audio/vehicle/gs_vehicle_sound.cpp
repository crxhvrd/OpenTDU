#include "shared.h"
#include "gs_vehicle_sound.h"

#include "config/gs_config.h"
#include "database/gs_database.h"
#include "audio/implementation/audio_dispatcher_playback.h"
#include "audio/implementation/ed_audio_reverb.h"
#include "audio/implementation/ed_audio_sample.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "physics/vehicle/physics_vehicle.h"

#include <cstring>
#include <string>

GSVehicleSound *gpVehicleSound = nullptr;

static constexpr float kReverbOpeningInertia = 0.9f; // DAT_00f416a0
static constexpr float kReverbOpeningRate = 0.1f;    // FLOAT_00f41668

VehicleVoiceSource::~VehicleVoiceSource() = default;

const edAudioSample *
VehicleVoiceSource::resolveSample( const void *pWaveData ) {
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

AudioDispatcherPlaybackSoundInstance *VehicleVoiceSource::acquireSoundInstance(
    const void *pWaveData, const char *pName, bool bLoop, float &duration ) {
    duration = 0.0f;
    if ( pClient == nullptr ) {
        return nullptr;
    }

    const edAudioSample *pSample = resolveSample( pWaveData );
    if ( pSample == nullptr ) {
        return nullptr;
    }

    AudioDispatcherPlaybackSoundInstance *pInstance =
        pClient->acquireSoundInstance();
    if ( pInstance == nullptr ) {
        // Pool exhausted
        return nullptr;
    }

    if ( !pInstance->setSample( pSample ) ) {
        pInstance->release();
        return nullptr;
    }
    pInstance->setLoopCount( bLoop ? -1 : 0 );
    if ( pName != nullptr ) {
        pInstance->debugSetDescription( pName );
    }

    const int32_t rate = pSample->getSampleRate();
    if ( rate > 0 ) {
        duration = static_cast<float>( pSample->getFrameCount() ) /
                   static_cast<float>( rate );
    }
    return pInstance;
}

void VehicleVoiceSource::releaseSoundInstance(
    AudioDispatcherPlaybackSoundInstance *pInstance ) {
    if ( pInstance != nullptr ) {
        pInstance->release();
    }
}

int32_t VehicleVoiceSource::forgetSamplesIn( const void *pBase,
                                             uint32_t size ) {
    if ( pBase == nullptr || size == 0 ) {
        return 0;
    }

    const uint8_t *pStart = static_cast<const uint8_t *>( pBase );
    const uint8_t *pEnd = pStart + size;

    int32_t erased = 0;
    for ( auto it = samples.begin(); it != samples.end(); ) {
        const uint8_t *pKey = static_cast<const uint8_t *>( it->first );
        if ( pKey >= pStart && pKey < pEnd ) {
            it = samples.erase( it );
            erased++;
        } else {
            ++it;
        }
    }
    return erased;
}

// TODO: Quantify
static bool IsBikeBrand( const DBEntryCarPhysicsData &entry ) {
    const char *pBikeBrand = gpDatabase->getStringByHashcode( 6, 0x3540681 );
    return entry.Brand != nullptr && pBikeBrand != nullptr &&
           entry.Brand->BrandID.pAsString == pBikeBrand;
}

GSVehicleSound::GSVehicleSound() : GameSystem() { gpVehicleSound = this; }

GSVehicleSound::~GSVehicleSound() {}

bool GSVehicleSound::initialize( TestDriveGameInstance * ) {
    // FUN_007d5cd0
    if ( gpAudio != nullptr ) {
        pClient = gpAudio->createClient( "ClientVehicle" );
        pReverbModule = gpAudio->getVehicleReverbModule();
    }
    pVoiceSource = std::make_unique<VehicleVoiceSource>( pClient );

    const char *pRootPath =
        ( gpConfig != nullptr ) ? gpConfig->getResRootPath() : "";

    if ( !gCommonVehicleSounds.load( pRootPath ) ) {
        OTDU_LOG_WARN( "Vehicle sounds that share the common bank will be "
                       "silent\n" );
    }

    return true;
}

void GSVehicleSound::tick( float totalTime, float deltaTime ) {
    // FUN_007d5c10
    sweepBankCaches();
}

void GSVehicleSound::terminate() {
    // FUN_007d5860
    for ( auto &pInstance : instances ) {
        if ( pInstance != nullptr ) {
            releaseInstance( *pInstance );
        }
        pInstance.reset();
    }

    sweepBankCaches();

    pVoiceSource.reset();
    pClient = nullptr;
    pReverbModule = nullptr;
}

VehicleSoundInstance *
GSVehicleSound::findInstance( const CarState *pCarState ) {
    if ( pCarState == nullptr ) {
        return nullptr;
    }
    for ( auto &pInstance : instances ) {
        if ( pInstance != nullptr && pInstance->pCarState == pCarState ) {
            return pInstance.get();
        }
    }
    return nullptr;
}

VehicleSoundInstance *GSVehicleSound::createInstance( CarState *pCarState ) {
    // FUN_007d56e0
    if ( pCarState == nullptr || gpDatabase == nullptr ) {
        return nullptr;
    }
    if ( VehicleSoundInstance *pExisting = findInstance( pCarState ) ) {
        return pExisting;
    }

    const DBEntryCarPhysicsData *pEntry =
        gpDatabase->getCarDBEntry( pCarState->getCarID() );
    if ( pEntry == nullptr ) {
        return nullptr;
    }

    const bool bBikeBank = IsBikeBrand( *pEntry );

    std::string bankName;
    if ( bBikeBank ) {
        auto appendIfSet = []( std::string &out, const char *pValue ) {
            if ( pValue != nullptr && strcmp( pValue, "??" ) != 0 ) {
                out += pValue;
            }
        };
        appendIfSet( bankName, pEntry->Model.pAsString );
        appendIfSet( bankName, pEntry->Version.pAsString );
    } else if ( pEntry->CarResourceFilename.pAsString != nullptr &&
                strcmp( pEntry->CarResourceFilename.pAsString, "??" ) != 0 ) {
        bankName = pEntry->CarResourceFilename.pAsString;
    }

    if ( bankName.empty() ) {
        return nullptr;
    }

    uint32_t slot = kMaxInstances;
    for ( uint32_t i = 0; i < kMaxInstances; i++ ) {
        if ( instances[i] == nullptr ) {
            slot = i;
            break;
        }
    }
    if ( slot >= kMaxInstances ) {
        return nullptr;
    }

    auto pInstance = std::make_unique<VehicleSoundInstance>();
    pInstance->pCarState = pCarState;

    VehicleBase *pVehicleBase = pCarState->getVehicleInstance();
    pInstance->bIsCar = pVehicleBase == nullptr ||
                        pVehicleBase->getType() == VehicleBase::eType::VBT_Car;

    pInstance->pBank =
        acquireBank( pCarState->getCarID(), bBikeBank, bankName.c_str() );

    if ( pInstance->pBank == nullptr ) {
        OTDU_LOG_WARN( "No vehicle sound bank for '%s'; car will be silent\n",
                       bankName.c_str() );
    }

    instances[slot] = std::move( pInstance );
    return instances[slot].get();
}

GSVehicleSound::BankCache &GSVehicleSound::getBankCache( bool bIsBike ) {
    return bIsBike ? bikeBanks : carBanks;
}

VehicleSoundBank *GSVehicleSound::acquireBank( int32_t carID,
                                               bool bIsBike,
                                               const char *pBankName ) {
    BankCache &cache = getBankCache( bIsBike );

    for ( std::unique_ptr<VehicleSoundBank> &pEntry : cache ) {
        if ( pEntry != nullptr && pEntry->getCarID() == carID ) {
            pEntry->addRef();
            return pEntry.get();
        }
    }

    if ( static_cast<int32_t>( cache.size() ) >= kMaxCachedBanks ) {
        OTDU_LOG_WARN( "Vehicle sound bank cache is full; '%s' dropped\n",
                       pBankName );
        return nullptr;
    }

    auto pEntry = std::make_unique<VehicleSoundBank>();
    const char *pRootPath =
        ( gpConfig != nullptr ) ? gpConfig->getResRootPath() : "";

    if ( !pEntry->load( pRootPath, pBankName ) ) {
        return nullptr;
    }

    pEntry->setCarID( carID );
    pEntry->addRef();

    OTDU_LOG_INFO( "Loaded vehicle sound bank '%s' (car %d)\n", pBankName,
                   carID );

    cache.push_back( std::move( pEntry ) );
    return cache.back().get();
}

void GSVehicleSound::releaseBank( VehicleSoundBank *pBank ) {
    if ( pBank != nullptr ) {
        pBank->release();
    }
}

void GSVehicleSound::sweepBankCaches() {
    // FUN_00429920 / FUN_00429a30
    for ( BankCache *pCache : { &carBanks, &bikeBanks } ) {
        for ( auto it = pCache->begin(); it != pCache->end(); ) {
            VehicleSoundBank *pEntry = it->get();
            if ( pEntry == nullptr || pEntry->getRefCount() > 0 ) {
                ++it;
                continue;
            }

            if ( pVoiceSource != nullptr ) {
                pVoiceSource->forgetSamplesIn( pEntry->getBankMemory(),
                                               pEntry->getBankMemorySize() );
            }

            OTDU_LOG_INFO( "Unloaded vehicle sound bank '%s'\n",
                           pEntry->getName() );
            it = pCache->erase( it );
        }
    }
}

void GSVehicleSound::releaseInstance( VehicleSoundInstance &instance ) {
    instance.Model.releaseVoices( pVoiceSource.get() );

    releaseBank( instance.pBank );
    instance.pBank = nullptr;
    instance.bEngineBuilt = false;
}

void GSVehicleSound::destroyInstance( CarState *pCarState ) {
    for ( auto &pInstance : instances ) {
        if ( pInstance != nullptr && pInstance->pCarState == pCarState ) {
            releaseInstance( *pInstance );
            pInstance.reset();
            return;
        }
    }
}

bool GSVehicleSound::buildEngine( VehicleSoundInstance &instance ) {
    // FUN_007d52d0
    if ( instance.bEngineBuilt ) {
        return true;
    }
    if ( instance.pBank == nullptr || !instance.pBank->isReady() ) {
        return false;
    }

    if ( !instance.Model.parse( *instance.pBank ) ) {
        return false;
    }
    instance.bEngineBuilt = true;

    OTDU_LOG_INFO( "Vehicle sound engine built from '%s' (%d valid samples)\n",
                   instance.pBank->getName(),
                   instance.Model.getValidSampleCount() );
    return true;
}

bool GSVehicleSound::isVehicleMuted( const CarState &carState ) const {
    // FUN_007d55c0
    uint32_t categories = 0;

    if ( carState.getOwner() == CarState::eOwner::CSO_NetPlayer &&
         carState.getSimulationType() ==
             CarState::eSimulationType::CSST_ReplicatedFromNetwork ) {
        categories |= VSC_LocalPlayerCar;
    }

    if ( carState.isIndoor() ) {
        categories |= VSC_Indoor;
    }

    const DBEntryCarPhysicsData *pEntry =
        gpDatabase->getCarDBEntry( carState.getCarID() );
    categories |=
        ( pEntry != nullptr && IsBikeBrand( *pEntry ) ) ? VSC_Bike : VSC_Car;

    if ( carState.isInShowcaseMode() ) {
        categories |= VSC_Showcase;
    }

    return ( categories & mutedCategoryMask ) == categories;
}

void GSVehicleSound::tickInstances( const Eigen::Vector3f &listenerPosition ) {
    for ( auto &pSlot : instances ) {
        if ( pSlot == nullptr ) {
            continue;
        }
        VehicleSoundInstance &instance = *pSlot;
        CarState *pCarState = instance.pCarState;
        if ( pCarState == nullptr ) {
            continue;
        }

        const PhysicsVehicle *pVehicle = pCarState->getPhysicsVehicle();
        if ( pVehicle == nullptr || !pVehicle->isActive() ) {
            continue;
        }

        constexpr bool bPausePhysics = false;
        constexpr int32_t postponedPause = 0;
        const bool bSimulated = pVehicle->isSimulated();

        const bool bRunnable = ( !bPausePhysics && postponedPause != 1 ) ||
                               postponedPause == 2 || bSimulated;

        if ( !bRunnable || isVehicleMuted( *pCarState ) ) {
            instance.Model.pause();
            continue;
        }

        instance.Model.resume();

        if ( !buildEngine( instance ) ) {
            continue;
        }

        instance.Wrapper.refresh( *pCarState );
        instance.Wrapper.computeEvents();
        instance.Model.update( instance.Wrapper.getSettings(),
                               instance.Wrapper.getPanelVolumeFactors(),
                               listenerPosition, pVoiceSource.get() );

        if ( instance.Wrapper.getSettings().bIsPlayerState ) {
            updateInteriorReverb( instance );
        }
    }
}

void GSVehicleSound::updateInteriorReverb( VehicleSoundInstance &instance ) {
    // FUN_007d3ec0
    if ( pReverbModule == nullptr ) {
        return;
    }

    const VSTPhysicsSettings &settings = instance.Wrapper.getSettings();

    if ( settings.bEventViewChanged && settings.bCurrentViewInternal ) {
        instance.bReverbPresetDirty = true;
    }

    if ( !pReverbModule->isAttached() ) {
        return;
    }

    const VehicleSoundBank *pBank = instance.Model.getBank();
    const int32_t presetIndex =
        ( pBank != nullptr )
            ? pBank->getDocument().readInt( "nReverbInternal", 0 )
            : 0;

    const edXMBNode preset = gCommonVehicleSounds.getDocument()
                                 .findChild( "ReverbPresets" )
                                 .getElement( presetIndex );
    if ( !preset.isValid() ) {
        return;
    }

    const int32_t channelCount = pReverbModule->getChannelCount();

    float opening = 1.0f;
    if ( settings.bCurrentViewInternal ) {
        opening = Max( settings.PhysicsValues[VSTPC_WindowAperture],
                       settings.PhysicsValues[VSTPC_DoorAperture] );
    }

    instance.ReverbOpening = instance.ReverbOpening * kReverbOpeningInertia +
                             opening * kReverbOpeningRate;
    opening = instance.ReverbOpening;

    const float presetDryVolume = preset.readFloat( "DryVolume", 0.0f );
    const float presetWetVolume = preset.readFloat( "WetVolume", 0.0f );

    if ( instance.bReverbPresetDirty ) {
        const float dryWetRatio = preset.readFloat( "DryWetRatio", 0.0f );
        const float cutoffFrequency =
            preset.readFloat( "LowPassCutoffFrequency", 0.0f );

        const edXMBNode room = preset.findChild( "Room" );
        const float roomSize = room.readFloat( "Size", 0.0f );
        const int32_t roomComplexity = room.readInt( "Complexity", 0 );
        const float roomDiffusion = room.readFloat( "Diffusion", 0.0f );

        for ( int32_t channel = 0; channel < channelCount; channel++ ) {
            edAudioReverbChannel *pChannel =
                pReverbModule->getChannel( channel );
            if ( pChannel == nullptr ) {
                continue;
            }

            pChannel->setDryWetRatio( dryWetRatio );
            pChannel->setLowPassCutoff( cutoffFrequency );
            pChannel->setRoom( roomSize, roomComplexity, roomDiffusion );
        }

        pReverbModule->reset();
        instance.bReverbPresetDirty = false;
    }

    for ( int32_t channel = 0; channel < channelCount; channel++ ) {
        edAudioReverbChannel *pChannel = pReverbModule->getChannel( channel );
        if ( pChannel == nullptr ) {
            continue;
        }

        pChannel->setDryVolume( ( 1.0f - opening ) * presetDryVolume +
                                opening );
        pChannel->setWetVolume( ( 1.0f - opening ) * presetWetVolume );
    }
}
