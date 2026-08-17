#include "shared.h"
#include "gs_ambiance.h"

#include "config/gs_config.h"
#include "database/gs_database.h"
#include "filesystem/gs_boot_datas.h"
#include "filesystem/gs_file.h"
#include "game/gm_scene.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/local_player.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/vehicle_car.h"
#include "gs_audio.h"
#include "implementation/audio_sound_resource.h"
#include "render/camera/gs_camera.h"
#include "core/binary_helpers.h"

GSAmbiance *gpAmbiance = nullptr;

static constexpr uint32_t kZoneMagic = 0x5a445345; // 'ESDZ' // DAT_00fa9430
static constexpr uint32_t kZoneDefinitionMagic =
    0x454e4f5a; // 'ZONE' // DAT_00fa9434
static constexpr float kUnbeatableWeight =
    3.4028234663852886e+38f; // DAT_00f4171c
static constexpr int32_t kMaxEmitters = 128;
static constexpr int32_t kNoEnvironmentPriority = 100;
static constexpr int32_t kNoReverbPriority = 101;
static constexpr uint32_t kInteriorViewFlag = 1u << 18;
static constexpr float kDoorApertureScale = 0.5f; // FLOAT_00f41674

GSAmbiance::GSAmbiance() : GameSystem(), Mutex() {
    // FUN_009de4b0
    gpAmbiance = this;
}

GSAmbiance::~GSAmbiance() {
    // FUN_0040e540
}

bool GSAmbiance::loadCommonConfig() {
    std::string path = gpConfig->getResRootPath();
    path += "Sound/Ambiance/common.xmb";

    uint32_t configSize = 0;
    if ( gpConfig->bSearchInBootFirst ) {
        pCommonConfig = gpBootDatas->findInCache( path.c_str(), &configSize );
        if ( pCommonConfig == nullptr &&
             gpFile->loadFile( path.c_str(), &pCommonConfig, &configSize ) ) {
            bOwnsCommonConfig = true;
        }
    } else {
        if ( gpFile->loadFile( path.c_str(), &pCommonConfig, &configSize ) ) {
            bOwnsCommonConfig = true;
        } else {
            pCommonConfig =
                gpBootDatas->findInCache( path.c_str(), &configSize );
        }
    }

    if ( pCommonConfig == nullptr ) {
        OTDU_LOG_WARN( "Ambiance disabled: could not load '%s'\n",
                       path.c_str() );
        return false;
    }

    return true;
}

bool GSAmbiance::initialize( TestDriveGameInstance *pInstance ) {
    // FUN_00901f90
    pGameInstance = pInstance;
    bPaused = false;
    Mutex.initialize( "GSAmbiance" );

    if ( !loadCommonConfig() ) {
        resetReverbModule();
        return true;
    }

    const edXMBNode root( pCommonConfig );
    const edXMBNode reverbPresetArray = root.findChild( "ReverbPresets" );
    const edXMBNode environmentArray = root.findChild( "Environments" );

    if ( gpAudio != nullptr ) {
        pEnvironmentClient = gpAudio->createClient( "ClientEnvironment" );
    }

    ReverbPresets.create( reverbPresetArray.getArrayCount() );
    for ( int32_t i = 0; i < ReverbPresets.getCapacity(); i++ ) {
        AmbianceReverbPreset *pPreset = ReverbPresets.allocate();
        if ( pPreset != nullptr ) {
            pPreset->load( reverbPresetArray.getElement( i ) );
        }
    }

    Environments.create( environmentArray.getArrayCount() );
    for ( int32_t i = 0; i < Environments.getCapacity(); i++ ) {
        AmbianceEnvironment *pEnvironment = Environments.allocate();
        if ( pEnvironment != nullptr ) {
            pEnvironment->load( environmentArray.getElement( i ),
                                pEnvironmentClient, ReverbPresets );
        }
    }

    Emitters.create( kMaxEmitters );

    OTDU_LOG_INFO( "Ambiance: %i environments, %i reverb presets%s\n",
                   Environments.getCapacity(), ReverbPresets.getCapacity(),
                   pEnvironmentClient != nullptr ? "" : " (no audio client)" );

    resetReverbModule();
    return true;
}

void GSAmbiance::terminate() {
    // FUN_00901d10
    pActiveEnvironment = nullptr;
    pActiveReverbPreset = nullptr;

    Emitters.destroy();

    for ( int32_t i = 0; i < Environments.getCapacity(); i++ ) {
        AmbianceEnvironment *pEnvironment = Environments.get( i );
        if ( pEnvironment != nullptr ) {
            pEnvironment->destroy();
        }
    }
    Environments.destroy();
    ReverbPresets.destroy();

    pEnvironmentClient = nullptr;

    if ( pCommonConfig != nullptr && bOwnsCommonConfig ) {
        TestDrive::Free( pCommonConfig );
    }
    pCommonConfig = nullptr;
    bOwnsCommonConfig = false;

    InteriorVolumeMin = 0.1f; // FLOAT_00f41668
    InteriorVolumeMax = 0.7f; // DAT_00f41828

    Mutex.terminate();
}

void GSAmbiance::tick( float totalTime, float deltaTime ) {
    // FUN_009019f0
    ScopedMutexLock lock( &Mutex );

    if ( Environments.getCapacity() == 0 ) {
        return;
    }

    const edAudioListener &listener = gEdAudio.getListener();
    const Eigen::Vector4f listenerPosition( listener.Position.x(), 0.0f,
                                            listener.Position.z(), 0.0f );

    clearEmitterLists();

    OTDU_UNIMPLEMENTED; // TODO: CAMERA (missing/unknown field lookup)

    gatherEmitters( listenerPosition );

    updateEnvironments( listenerPosition, listener.LeftToRight );
}

void GSAmbiance::clearEmitterLists() {
    // FUN_00901980
    for ( int32_t i = 0; i < Environments.getCapacity(); i++ ) {
        AmbianceEnvironment *pEnvironment = Environments.get( i );
        if ( pEnvironment != nullptr ) {
            pEnvironment->clearEmitters();
        }
    }
}

void GSAmbiance::gatherEmitters( const Eigen::Vector4f &listener ) {
    // FUN_009018d0
    for ( int32_t i = 0; i < Emitters.getCapacity(); i++ ) {
        AmbianceEmitter *pEmitter = Emitters.get( i );
        if ( pEmitter == nullptr ) {
            continue;
        }

        pEmitter->updateDistance( listener );

        AmbianceEnvironment *pEnvironment = pEmitter->getEnvironment();
        if ( pEnvironment == nullptr || pEmitter->getDistance() < 0.0f ) {
            continue;
        }

        if ( pEmitter->getDistance() < pEnvironment->getFadeStop() ) {
            pEnvironment->addEmitter( pEmitter );
        }
    }
}

bool GSAmbiance::isShowcaseInteriorActive() const {
    if ( gpActiveGMScene == nullptr ) {
        return false;
    }

    VehicleBase *pVehicle = gpActiveGMScene->getShowcaseFocusedVehicle();
    if ( pVehicle == nullptr ) {
        return false;
    }

    const CarState *pCarState = pVehicle->getCarState();
    return pCarState != nullptr && pCarState->isIndoor() &&
           pCarState->isInShowcaseMode();
}

void GSAmbiance::updateEnvironments(
    const Eigen::Vector4f &listener,
    const Eigen::Vector3f &listenerLeftToRight ) {
    // FUN_009015b0
    int32_t reverbPriority = kNoReverbPriority;
    int32_t activePriorityRank = kNoEnvironmentPriority;
    float bestReverbWeight = 1.0f;
    float priorityFade = 1.0f;
    AmbianceEnvironment *pReverbEnvironment = nullptr;

    pActiveEnvironment = nullptr;

    if ( !isShowcaseInteriorActive() ) {
        for ( int32_t i = 0; i < Environments.getCapacity(); i++ ) {
            AmbianceEnvironment *pEnvironment = Environments.get( i );
            if ( pEnvironment == nullptr || !pEnvironment->isActive() ) {
                continue;
            }

            if ( pActiveEnvironment == nullptr ||
                 pEnvironment->getPriorityRank() <
                     pActiveEnvironment->getPriorityRank() ) {
                pActiveEnvironment = pEnvironment;
            }
        }
    } else {
        for ( int32_t i = 0; i < Environments.getCapacity(); i++ ) {
            AmbianceEnvironment *pEnvironment = Environments.get( i );
            if ( pEnvironment == nullptr ||
                 pEnvironment->getClassID() !=
                     AmbianceEnvironment::kInteriorClassID ) {
                continue;
            }

            pActiveEnvironment = pEnvironment;
            pReverbEnvironment = pEnvironment;
            reverbPriority = pEnvironment->getPriorityRank();
            bestReverbWeight = kUnbeatableWeight;
            break;
        }
    }

    InternalViewVolumeFactor = computeInternalViewVolumeFactor();

    if ( pActiveEnvironment != nullptr ) {
        if ( pActiveEnvironment->getClassID() ==
             AmbianceEnvironment::kInteriorClassID ) {
            pActiveEnvironment->setWeight( 1.0f );
        } else {
            pActiveEnvironment->update( listener, listenerLeftToRight,
                                        InternalViewVolumeFactor );
        }

        activePriorityRank = pActiveEnvironment->getPriorityRank();
        priorityFade =
            1.0f - ( 1.0f - pActiveEnvironment->getPriorityFactor() ) *
                       pActiveEnvironment->getWeight();
    }

    for ( int32_t i = 0; i < Environments.getCapacity(); i++ ) {
        AmbianceEnvironment *pEnvironment = Environments.get( i );
        if ( pEnvironment == nullptr ) {
            continue;
        }

        float volume = InternalViewVolumeFactor;
        if ( pEnvironment != pActiveEnvironment ) {
            if ( pEnvironment->getPriorityRank() > activePriorityRank ) {
                volume *= priorityFade;
            }
            pEnvironment->update( listener, listenerLeftToRight, volume );
        }

        if ( !pEnvironment->isActive() ) {
            continue;
        }

        const AmbianceReverbPreset *pPreset = pEnvironment->getReverbPreset();
        const int32_t presetPriority =
            ( pPreset != nullptr ) ? pPreset->Priority : 0;

        if ( presetPriority == reverbPriority ) {
            if ( pEnvironment->getWeight() * volume > bestReverbWeight ) {
                pReverbEnvironment = pEnvironment;
            }
        } else if ( presetPriority < reverbPriority ) {
            bestReverbWeight = pEnvironment->getWeight() * volume;
            reverbPriority = presetPriority;
            pReverbEnvironment = pEnvironment;
        }
    }

    if ( pReverbEnvironment == nullptr ) {
        resetReverbModule();
        return;
    }

    if ( pReverbEnvironment->getReverbPreset() != pActiveReverbPreset ) {
        applyReverbPreset( pReverbEnvironment->getReverbPreset() );
    }
    updateReverbModule();
}

float GSAmbiance::computeInternalViewVolumeFactor() {
    // FUN_009014d0
    if ( gpCamera == nullptr ) {
        return 1.0f;
    }

    CameraGame *pCamera = gpCamera->getActiveCamera();
    if ( pCamera == nullptr ||
         ( pCamera->getFlags() & kInteriorViewFlag ) == 0 ) {
        return 1.0f;
    }

    CarState *pCarState = ( gpScenaric != nullptr )
                              ? gpScenaric->getLocalPlayer().getCarState()
                              : nullptr;
    if ( pCarState == nullptr ) {
        return 1.0f;
    }

    const float range = InteriorVolumeMax - InteriorVolumeMin;

    float opening = range;

    DBEntryCarPhysicsData *pCarDB =
        ( gpDatabase != nullptr )
            ? gpDatabase->getCarDBEntry( pCarState->getCarID() )
            : nullptr;
    const bool bIsBike = pCarDB != nullptr && gpDatabase != nullptr &&
                         gpDatabase->isBike( pCarDB );

    VehicleBase *pVehicleBase = pCarState->getVehicleInstance();
    if ( !bIsBike && pVehicleBase != nullptr &&
         pVehicleBase->getType() == VehicleBase::eType::VBT_Car ) {
        const VehicleCar *pCar =
            static_cast<const VehicleCar *>( pVehicleBase );
        const float doorAperture = ( fabsf( pCar->getDoorApertureRight() ) +
                                     fabsf( pCar->getDoorApertureLeft() ) ) *
                                   kDoorApertureScale;

        opening = Max( pCar->getWindowOpenLeft(), doorAperture ) * range;
    }

    return InteriorVolumeMin + opening;
}

bool GSAmbiance::isAmbianceRegionActive( int32_t classID ) {
    // FUN_00901c10
    ScopedMutexLock lock( &Mutex );
    return isAmbianceRegionActiveUnlocked( classID );
}

bool GSAmbiance::isAmbianceRegionActiveUnlocked( int32_t classID ) const {
    int32_t activePriorityRank = kNoEnvironmentPriority;
    float priorityFade = 1.0f;

    if ( pActiveEnvironment != nullptr ) {
        activePriorityRank = pActiveEnvironment->getPriorityRank();
        priorityFade =
            1.0f - ( 1.0f - pActiveEnvironment->getPriorityFactor() ) *
                       pActiveEnvironment->getWeight();
    }

    for ( int32_t i = 0; i < Environments.getCapacity(); i++ ) {
        const AmbianceEnvironment *pEnvironment = Environments.get( i );
        if ( pEnvironment == nullptr ||
             pEnvironment->getClassID() != classID ) {
            continue;
        }

        float weight = pEnvironment->getWeight();
        if ( pEnvironment->getPriorityRank() > activePriorityRank ) {
            weight *= priorityFade;
        }

        if ( weight > 0.0f ) {
            return true;
        }
    }

    return false;
}

void GSAmbiance::registerSectorEmitters( const void *pBlob,
                                         const void *pOwner ) {
    // FUN_00901b40
    ScopedMutexLock lock( &Mutex );

    if ( pBlob == nullptr || Emitters.getCapacity() == 0 ) {
        return;
    }

    const uint8_t *pCursor = reinterpret_cast<const uint8_t *>( pBlob );

    if ( ReadUint32( pCursor ) != kZoneMagic ) {
        return;
    }
    pCursor += 0x10;

    if ( ReadUint32( pCursor ) != kZoneDefinitionMagic ) {
        return;
    }
    pCursor += 4;

    const int32_t zoneCount = ReadInt32( pCursor + 0xc );
    pCursor += 0x1c;

    for ( int32_t i = 0; i < zoneCount; i++ ) {
        AmbianceEmitter *pEmitter = Emitters.allocate();
        if ( pEmitter == nullptr ) {
            OTDU_LOG_WARN( "Ambiance emitter pool full (%i slots)\n",
                           Emitters.getCapacity() );
            break;
        }

        pEmitter->deserialize( pCursor, Environments, pOwner );
    }
}

void GSAmbiance::unregisterSectorEmitters( const void *pOwner ) {
    // FUN_00901ab0
    ScopedMutexLock lock( &Mutex );

    for ( int32_t i = 0; i < Emitters.getCapacity(); i++ ) {
        AmbianceEmitter *pEmitter = Emitters.get( i );
        if ( pEmitter != nullptr && pEmitter->getOwner() == pOwner ) {
            Emitters.free( pEmitter );
        }
    }
}

#ifdef OTDU_USE_IMGUI
void GSAmbiance::drawDebug( float ) {
    // FUN_00902860
    ScopedMutexLock lock( &Mutex );

    ImGui::Text( "Internal view volume factor : %f", InternalViewVolumeFactor );
    ImGui::Text( "Highest priority environment type : %s",
                 ( pActiveEnvironment != nullptr )
                     ? pActiveEnvironment->getTypeName()
                     : "<none>" );
    ImGui::Text( "Emitters : %i / %i", Emitters.getAllocatedCount(),
                 Emitters.getCapacity() );

    ImGui::SeparatorText( "Environments" );
    if ( ImGui::BeginTable( "ambiance", 6,
                            ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_Borders ) ) {
        ImGui::TableSetupColumn( "Type" );
        ImGui::TableSetupColumn( "Class" );
        ImGui::TableSetupColumn( "Rank" );
        ImGui::TableSetupColumn( "Emitters" );
        ImGui::TableSetupColumn( "Weight" );
        ImGui::TableSetupColumn( "Volume" );
        ImGui::TableHeadersRow();

        for ( int32_t i = 0; i < Environments.getCapacity(); i++ ) {
            const AmbianceEnvironment *pEnvironment = Environments.get( i );
            if ( pEnvironment == nullptr ) {
                continue;
            }

            const AudioSoundResource *pResource = pEnvironment->getResource();

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if ( pEnvironment->isActive() ) {
                ImGui::TextUnformatted( pEnvironment->getTypeName() );
            } else {
                ImGui::TextDisabled( "%s", pEnvironment->getTypeName() );
            }
            ImGui::TableNextColumn();
            ImGui::Text( "%i", pEnvironment->getClassID() );
            ImGui::TableNextColumn();
            ImGui::Text( "%i", pEnvironment->getPriorityRank() );
            ImGui::TableNextColumn();
            ImGui::Text( "%i", pEnvironment->getEmitterCount() );
            ImGui::TableNextColumn();
            ImGui::Text( "%.3f", pEnvironment->getWeight() );
            ImGui::TableNextColumn();
            if ( pResource != nullptr ) {
                ImGui::Text( "%.3f", pResource->getVolume() );
            } else {
                ImGui::TextDisabled( "silent" );
            }
        }

        ImGui::EndTable();
    }

    ImGui::SeparatorText( "Regions" );
    for ( int32_t classID = 0; classID <= 8; classID++ ) {
        if ( classID != 0 ) {
            ImGui::SameLine();
        }
        const bool bActive = isAmbianceRegionActiveUnlocked( classID );
        ImGui::TextColored( bActive ? ImVec4( 0.4f, 1.0f, 0.4f, 1.0f )
                                    : ImVec4( 0.5f, 0.5f, 0.5f, 1.0f ),
                            "%i", classID );
    }
}
#endif

void GSAmbiance::resetReverbModule() {
    // FUN_00901300
    pActiveReverbPreset = nullptr;
    if ( pReverbModule == nullptr ) {
        return;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: AUDIO: Reverb
}

void GSAmbiance::applyReverbPreset( AmbianceReverbPreset *pPreset ) {
    // FUN_009013a0
    pActiveReverbPreset = pPreset;
    if ( pReverbModule == nullptr ) {
        return;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: AUDIO: Reverb
}

void GSAmbiance::updateReverbModule() {
    // FUN_00901260
    if ( pReverbModule == nullptr ) {
        return;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: AUDIO: Reverb
}
