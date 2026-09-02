#include "shared.h"
#include "entity_cinematic.h"

#include <cctype>

#include "core/maths_helper.h"
#include "filesystem/bank.h"
#include "filesystem/binary.h"
#include "audio/gs_audio.h"
#include "audio/vehicle/gs_vehicle_sound.h"
#include "gs_timer.h"
#include "game/game_mechanism.h"
#include "game/gm_scene.h"
#include "game/cinematic/gs_cinematics.h"
#include "game/cinematic/mng_cinematics.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/entity/entity_pedestrian.h"
#include "game/scenaric/mng_actor.h"
#include "game/scenaric/scenaric_bank.h"
#include "game/scenaric/spots.h"
#include "render/animation/clip.h"
#include "render/animation/skeleton.h"
#include "world/gs_world_streaming.h"
#include "render/camera/gs_camera.h"
#include "render/camera/camera_cinematic.h"
#include "render/gs_render.h"
#include "tdu_instance.h"

static constexpr float kMillisecondsToSeconds = 0.001f; // FLOAT_00f4167c
static constexpr float kCinematicPlaneUnset = -1.0f; // FLOAT_00f41664

namespace {
    void ResolveCinematicEntry( BankInstance &bank,
                                const std::string &name,
                                ParsedBankEntry &outEntry ) {
        bank.getEntry( const_cast<char *>( name.c_str() ), outEntry );
        if ( outEntry.pData != nullptr ) {
            return;
        }

        std::string suffix = name;
        if ( suffix.size() > 2 && suffix[0] == 'C' && suffix[1] == '_' ) {
            suffix = suffix.substr( 2 );
        }
        for ( char &c : suffix ) {
            c = static_cast<char>(
                ::toupper( static_cast<unsigned char>( c ) ) );
        }

        const int32_t count = bank.getNumElements();
        for ( int32_t i = 0; i < count; i++ ) {
            ParsedBankEntry entry;
            bank.getEntry( i, entry );
            if ( entry.pData == nullptr || entry.Size < 4 ||
                 *reinterpret_cast<const uint32_t *>( entry.pData ) !=
                     0x214e4943u /* "CIN!" */ ) {
                continue;
            }
            std::string base = entry.Filename;
            const size_t slash = base.find_last_of( "\\/" );
            if ( slash != std::string::npos ) {
                base = base.substr( slash + 1 );
            }
            const size_t dot = base.find_last_of( '.' );
            if ( dot != std::string::npos ) {
                base = base.substr( 0, dot );
            }
            for ( char &c : base ) {
                c = static_cast<char>(
                    ::toupper( static_cast<unsigned char>( c ) ) );
            }
            if ( base.size() >= suffix.size() &&
                 base.compare( base.size() - suffix.size(), suffix.size(),
                               suffix ) == 0 ) {
                outEntry = entry; // matched; pData points into the bank buffer
                return;
            }
        }
    }

    void PlayBestAmbientClip( Entity *pEntity, ScenaricBank *pBank ) {
        if ( pEntity == nullptr || pBank == nullptr ||
             pEntity->getType() != eEntityType::ET_PTDR ) {
            return;
        }
        EntityPedestrian *pPed = static_cast<EntityPedestrian *>( pEntity );
        void *pSkeletonBlob = pPed->debugGetCharSkeleton();
        if ( pSkeletonBlob == nullptr ) {
            return;
        }
        SkeletonInstance skeleton;
        skeleton.wrapBlob( pSkeletonBlob );
        ClipInstance *pClip = pBank->debugPickCalmestClip( &skeleton, 0.3f );
        if ( pClip != nullptr ) {
            pPed->playAnimationClip( pClip );
        }
    }
} // namespace

EntityCinematic::EntityCinematic()
    : Entity(),
      cinematicFlags( 0 ),
      cinematicPlaybackFlags( 0 ),
      cinematicPlaybackFlags2( 3 ),
      StartTime( 0.0f ),
      Time( 0.0f ),
      CineTotalTime( 0.0f ),
      FadeTime( 0.0f ),
      pCameraRef( nullptr ),
      pZoneRef( nullptr ),
      cinematicZNear( 0.0f ),
      cinematicZFar( 0.0f ),
      pMusic1Ref( nullptr ),
      pSoundEmitter1Ref( nullptr ),
      pMusic2Ref( nullptr ),
      pSoundEmitter2Ref( nullptr ),
      pMicroRef( nullptr ),
      pStreamCenterRef( nullptr ),
      numRefs( 0 ),
      pCinRef( nullptr ),
      pSceneRefs( nullptr ),
      pSCBank( nullptr ),
      pMusicEmitter( nullptr ),
      pGMInterface( nullptr ),
      pSpotWorld( nullptr ),
      pCinData( nullptr ),
      cinDataSize( 0 ),
      bPaused( false ),
      playbackSpeed( 1.0f ),
      bLoop( false ),
      bHideFrontEnd( false ),
      bOnFinalFrame( false ),
      bSkipped( false ),
      bStopWhenSkipped( false ),
      bDontSwitchCamera( false ),
      bInitialized( false ),
      bForceStop( false ),
      bResourcesPrepared( false ),
      bForegroundCineApplied( false ),
      bApplyCineMix( true ),
      bRestoreCineMix( true ),
      savedMutedVehicleCategories( 0 ),
      bStopGuard( false ),
      bStackActive( false ),
      bCameraHandedOver( false ),
      bCameraDrivenByObject( false ),
      activeGameMode( GM_FreeRide ) {
    // FUN_007e80d0
    cinematicPlayer.setOwner( this );
}

EntityCinematic::~EntityCinematic() {
    // FUN_00425ae0
}

void EntityCinematic::initialize() {
    // FUN_007e9530
    pCinData = nullptr;
    cinDataSize = 0;
    cinematicBuffer.clear();
    bInitialized = true;

    bLoop = ( cinematicPlaybackFlags & 0x2u ) != 0;

    if ( pSCBank == nullptr || cutsceneName.empty() ) {
        return;
    }

    if ( pCinRef != nullptr ) {
        const uint64_t cinHashcode =
            *reinterpret_cast<const uint64_t *>( pCinRef );
        int32_t cinSize = 0;
        pCinData = pSCBank->getCinData( cinHashcode, &cinSize );
        cinDataSize = static_cast<uint32_t>( cinSize );
    }

    if ( pCinData == nullptr ) {
        ParsedBankEntry entry;
        ResolveCinematicEntry( pSCBank->getBank(), cutsceneName, entry );
        if ( entry.pData == nullptr ) {
            return;
        }
        pCinData = entry.pData;
        cinDataSize = static_cast<uint32_t>( entry.Size );
        entry.Clear();
    }

    const uint32_t bufferSize = ExtractBufferSize( pCinData, cinDataSize );
    if ( bufferSize != 0 ) {
        cinematicBuffer.assign( bufferSize, 0 );
    }

    bResourcesPrepared = false;
    bApplyCineMix = true;
    bRestoreCineMix = true;
    bForegroundCineApplied = false;
}

void EntityCinematic::applyForegroundCine() {
    // FUN_007e89d0
    if ( bForegroundCineApplied ) {
        return;
    }
    bForegroundCineApplied = true;

    if ( gpAudio != nullptr ) {
        if ( bApplyCineMix ) {
            uint32_t mix = gpAudio->getMixFlags( "MixCinematicDefault" );
            if ( static_cast<int8_t>( cinematicPlaybackFlags & 0xffu ) >= 0 ) {
                mix |= gpAudio->getMixFlags( "MixAmbiance" );
            }
            if ( pMusicEmitter == nullptr ) {
                mix |= gpAudio->getMixFlags( "ClientMenuMusic" );
            }
            gpAudio->changeMixState( mix );
        }

        Mixer *pMixer = gpAudio->findMixer( "MixerCinematic" );
        if ( pMixer != nullptr ) {
            pMixer->bEnabled = true;
        }
    }

    if ( ( cinematicPlaybackFlags & 0x100u ) != 0 && gpVehicleSound != nullptr ) {
        savedMutedVehicleCategories = gpVehicleSound->getMutedCategoryMask();
        gpVehicleSound->setMutedCategoryMask( 0x1eu );
    }

    float shotNearPlane = gpRender->getDefaultNearPlane();
    if ( cinematicZNear != kCinematicPlaneUnset ) {
        shotNearPlane = cinematicZNear;
    }

    float shotFarPlane = gpRender->getDefaultFarPlane();
    if ( cinematicZFar != kCinematicPlaneUnset ) {
        shotFarPlane = cinematicZFar;
    }

    CameraCinematic *pCinematicCamera = cinematicPlayer.getCinematicCamera();
    if ( pCinematicCamera != nullptr && gpCamera != nullptr ) {
        pCinematicCamera->setNearPlane( shotNearPlane );
        pCinematicCamera->setFarPlane( shotFarPlane );

        gpCamera->setFarPlane( pCinematicCamera->getFarPlane() );
        gpCamera->setNearPlane( pCinematicCamera->getNearPlane() );
        gpCamera->setEnableDepthAware( true );
        gpCamera->setInteriorFarPlane(
            pCinematicCamera->getInteriorFarPlane() );
        gpCamera->setInteriorNearPlane(
            pCinematicCamera->getInteriorNearPlane() );
        gpCamera->setEnableDepthAwareInterior( true );
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: the rest of FUN_007e89d0
}

void EntityCinematic::restoreForegroundCine() {
    // FUN_007e8650
    if ( !bForegroundCineApplied ) {
        return;
    }
    bForegroundCineApplied = false;

    if ( bLoop || gpAudio == nullptr ) {
        return;
    }

    Mixer *pMixer = gpAudio->findMixer( "MixerCinematic" );
    if ( pMixer != nullptr ) {
        pMixer->bEnabled = false;
    }

    if ( ( cinematicPlaybackFlags & 0x100u ) != 0 && gpVehicleSound != nullptr ) {
        gpVehicleSound->setMutedCategoryMask( savedMutedVehicleCategories );
    }

    if ( bRestoreCineMix ) {
        gpAudio->requestMixStateResync();
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: the rest of FUN_007e8650
}

bool EntityCinematic::prepareResources() {
    // FUN_007e7ed0
    if ( bResourcesPrepared ) {
        return true;
    }

    strncpy( cinematicPlayer.debugName, name.c_str(),
             sizeof( cinematicPlayer.debugName ) - 1 );

    if ( !createTDUInterface() ) {
        return false;
    }

    if ( pMusicEmitter != nullptr ) {
        pMusicEmitter->installSoundBanks();
    }

    bResourcesPrepared = true;
    return true;
}

void EntityCinematic::loadResources( ScenaricBank *param_2 ) {
    // FUN_007e8e80
    if ( pMechanism != nullptr ) {
        pMechanism->loadResources( param_2 );
    }
    resolveSoundReference();
    pSCBank = param_2;

    uint8_t *pLeftRef = const_cast<uint8_t *>(
        static_cast<const uint8_t *>( pSoundEmitter1Ref ) );
    uint8_t *pRightRef = const_cast<uint8_t *>(
        static_cast<const uint8_t *>( pSoundEmitter2Ref ) );
    if ( pLeftRef != nullptr &&
         *reinterpret_cast<uint32_t *>( pLeftRef ) != 0 &&
         pRightRef != nullptr &&
         *reinterpret_cast<uint32_t *>( pRightRef ) != 0 ) {
        pMusicEmitter =
            std::make_unique<CinematicMusicEmitter>( pLeftRef, pRightRef );
        pMusicEmitter->resolveEmitters( param_2 );
        cinematicPlayer.setMusicEmitter( pMusicEmitter.get() );

        OTDU_LOG_DEBUG( "Cinematic '%s' owns a music emitter (%s)\n",
                        name.c_str(),
                        pMusicEmitter->hasBothEmitters()
                            ? "both sound emitters resolved"
                            : "unresolved sound emitters" );
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC (camera/micro/stream refs)

    bStopWhenSkipped = true;
}

void EntityCinematic::installMusicBanks() {
    // FUN_007e7c90
    if ( gpCinematics != nullptr && gpCinematics->isInitialized() &&
         pMusicEmitter != nullptr ) {
        pMusicEmitter->installSoundBanks();
    }
}

void EntityCinematic::setPaused( bool bEnabled ) {
    // FUN_007e7b50
    if ( bPaused == bEnabled ) {
        return;
    }

    if ( pMusicEmitter != nullptr ) {
        pMusicEmitter->setPaused( bEnabled, 0.0f );
    }

    bPaused = bEnabled;
}

void EntityCinematic::releaseMusicEmitter() {
    if ( pMusicEmitter == nullptr ) {
        return;
    }

    cinematicPlayer.setMusicEmitter( nullptr );
    pMusicEmitter->releaseChannels();
    pMusicEmitter.reset();
}

void EntityCinematic::initializeInstance( void *param_2, void *param_3 ) {
    // FUN_007e79c0
    // NOP
}

void EntityCinematic::unloadResources() {
    // FUN_007e8dd0
    releaseMusicEmitter();
    cinematicPlayer.reset();
    cinematicBuffer.clear();
    pCinData = nullptr;
    cinDataSize = 0;
    bInitialized = false;
    if ( pMechanism != nullptr ) {
        pMechanism->terminate();
        delete pMechanism;
        pMechanism = nullptr;
    }
}

void EntityCinematic::tick() {
    // FUN_007e9320
    Entity::tick();

    const float dt =
        ( gpTimer != nullptr ) ? gpTimer->GameDeltaTime : ( 1.0f / 60.0f );

    if ( isPlaying() ) {
        const float totalTime = cinematicPlayer.getTotalTime();

        if ( pMusicEmitter != nullptr ) {
            pMusicEmitter->setPaused( bPaused, 0.0f );
        }

        const float advance = playbackSpeed * dt;
        if ( Time + advance > 0.0f && !bPaused ) {
            Time += advance;
        }

        const bool reachedEnd = totalTime < Time;
        if ( !reachedEnd ) {
            cinematicPlayer.tick( Time, dt );
            publishCameraView();
        }
        if ( pGMInterface != nullptr &&
             Time >= totalTime - FadeTime * kMillisecondsToSeconds ) {
            pGMInterface->executeAction( CE_StartEndFade, this );
        }

        if ( reachedEnd ) {
            if ( !bLoop ) {
                if ( pGMInterface != nullptr ) {
                    pGMInterface->executeAction( CE_Finished, this );
                }
                stop( true );
                return;
            }
            Time = 0.0f;
        }
    }

    if ( ( cinematicFlags & 0x4u ) == 0 ) {
        if ( ( cinematicFlags & 0x1u ) != 0 ) {
            begin();
        }
    } else if ( ( cinematicFlags & 0x2u ) != 0 ) {
        end();
    }
}

void EntityCinematic::seek( float time ) {
    Time = time;
    if ( Time < 0.0f ) {
        Time = 0.0f;
    }
    const float totalTime = cinematicPlayer.getTotalTime();
    if ( totalTime > 0.0f && Time > totalTime ) {
        Time = totalTime;
    }
    cinematicPlayer.tick( Time, 0.0f );
}

void EntityCinematic::setPlaybackAnchor( const Eigen::Vector3f &pos ) {
    cinematicPlayer.setBaseTranslation( pos );
}

void EntityCinematic::deserialize( BinaryReader *pReader ) {
    // FUN_007e8400
    Entity::deserialize( pReader );

    auto readRef = [pReader]() -> const void * {
        return ReadStreamRef( pReader );
    };

    const uint32_t version = gpScenaric->getVersion();

    cinematicPlaybackFlags = ReadStreamU32( pReader );
    if ( version > 0xc ) {
        cinematicPlaybackFlags2 = ReadStreamU32( pReader );
    }

    pCameraRef = readRef();
    pZoneRef = readRef();

    if ( version >= 9 ) {
        cinematicZNear = *reinterpret_cast<const float *>( pReader->pCursor );
        pReader->pCursor += sizeof( float );
        cinematicZFar = *reinterpret_cast<const float *>( pReader->pCursor );
        pReader->pCursor += sizeof( float );

        if ( version >= 0x13 ) {
            pSoundEmitter1Ref = readRef();
            pSoundEmitter2Ref = readRef();
        } else {
            if ( ReadStreamU32( pReader ) != 0 ) {
                pMusic1Ref = readRef();
            }
            if ( ReadStreamU32( pReader ) != 0 ) {
                pMusic2Ref = readRef();
            }
        }

        if ( version >= 0xa ) {
            if ( version >= 0xc ) {
                pMicroRef = readRef();
            }
            pStreamCenterRef = readRef();
        }
    }

    numRefs = ReadStreamU32( pReader );
    if ( numRefs != 0 ) {
        pCinRef = readRef();
        pSceneRefs = pReader->pCursor;
        pReader->pCursor += ( numRefs - 1 ) * 8;
    }

    if ( version >= 0x20 ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC (the trailing per-actor
                                 // override list)
    }

    cutsceneName = name;
}

void EntityCinematic::reset() {
    // FUN_007e9720
    bPaused = false;
    if ( ( cinematicFlags & 0x4u ) != 0 ) {
        Time = 0.0f;
    }
    start( true );
}

void EntityCinematic::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_007e8200
    Entity::instanciate( pMngActor, param_1 );

    EntityCinematic *pInstance = static_cast<EntityCinematic *>( param_1 );
    pInstance->cinematicFlags = cinematicFlags;
    pInstance->cinematicPlaybackFlags = cinematicPlaybackFlags;
    pInstance->cinematicPlaybackFlags2 = cinematicPlaybackFlags2;
    pInstance->StartTime = StartTime;
    pInstance->CineTotalTime = CineTotalTime;
    pInstance->cinematicZNear = cinematicZNear;
    pInstance->cinematicZFar = cinematicZFar;
    pInstance->pSCBank = pSCBank;
    pInstance->cutsceneName = cutsceneName;
    pInstance->pSpotWorld = pSpotWorld;
    pInstance->bLoop = bLoop;
    pInstance->bHideFrontEnd = bHideFrontEnd;
    pInstance->bSkipped = bSkipped;
    pInstance->bStopWhenSkipped = bStopWhenSkipped;
    pInstance->playbackSpeed = playbackSpeed;

    OTDU_UNIMPLEMENTED;
}

void EntityCinematic::start( bool bBeginNow ) {
    // FUN_007e91f0
    if ( gpMngCinematics == nullptr || !gpMngCinematics->isValid() ||
         pSCBank == nullptr || !pSCBank->isLoaded() ) {
        stop( true );
        return;
    }

    // Already ending (bit 2) or already playing (bit 0): nothing to do.
    if ( ( cinematicFlags & 0x4u ) != 0 || ( cinematicFlags & 0x1u ) != 0 ) {
        return;
    }

    OTDU_UNIMPLEMENTED; // TODO: INPUT

    if ( gpActiveGMScene != nullptr ) {
        FadeTime = gpActiveGMScene->getTransitionTime();
    }

    activeGameMode =
        ( gpTestDriveInstance != nullptr )
            ? gpTestDriveInstance->getActiveGameMode()
            : GM_FreeRide;
    bSkipped = false;
    cinematicFlags |= 0x1u;

    if ( pGMInterface != nullptr ) {
        pGMInterface->executeAction( CE_Requested, this );
    }

    if ( !bBeginNow || begin() ) {
        if ( gpMngCinematics == nullptr ) {
            stop( true );
            return;
        }
        gpMngCinematics->addPlayingCinematic( this );
    }
}

void EntityCinematic::stop( bool bEndNow ) {
    // FUN_007e8d10
    const uint32_t flags = cinematicFlags;

    if ( ( ( flags & 0x4u ) != 0 || bForceStop ) && !bStopGuard ) {
        cinematicFlags = flags | 0x2u;
        if ( pGMInterface != nullptr ) {
            pGMInterface->executeAction( CE_Stopping, this );
        }
        if ( bEndNow ) {
            end();
        }
        return;
    }

    if ( ( flags & 0x1u ) != 0 ) {
        cinematicFlags = flags & ~0x1u;
        if ( pGMInterface != nullptr ) {
            pGMInterface->executeAction( CE_Stopping, this );
            pGMInterface->executeAction( CE_Ended, this );
        }
    }

    if ( cinematicPlayer.isCreated() ) {
        cinematicPlayer.reset();
        bStackActive = false;
    }
}

bool EntityCinematic::calculateSpotMatrix( SpotMap *pSpot,
                                           Matrix4x4f_t &outMatrix ) {
    // FUN_007e7dd0
    outMatrix = Matrix4x4f_t::Identity();

    SpotSector *pSector =
        gpScenaric->getSpotList().getSpotSector( pSpot->Hashcode );
    if ( pSector == nullptr ) {
        return false;
    }
    outMatrix = BuildSpotSectorRotation( pSector );

    Eigen::Vector4f shift = gpWorldStreaming->isEnabled()
                                ? gpWorldStreaming->getPosition()
                                : Eigen::Vector4f::Zero();
    outMatrix( 3, 0 ) = pSpot->X - shift.x();
    outMatrix( 3, 1 ) = pSpot->Y - shift.y();
    outMatrix( 3, 2 ) = pSpot->Z - shift.z();
    outMatrix( 3, 3 ) = 1.0f;
    return true;
}

bool EntityCinematic::createTDUInterface() {
    // FUN_007e7c10
    if ( pCinData == nullptr || cinematicBuffer.empty() ||
         pSCBank == nullptr ) {
        return false;
    }

    return cinematicPlayer.createInterface( pCinData, cinDataSize,
                                            cinematicBuffer.data(),
                                            &pSCBank->getBank() );
}

bool EntityCinematic::launchSpotCine( SpotMap *pSpot, float startTime ) {
    // FUN_007e96c0
    Matrix4x4f_t mat;
    calculateSpotMatrix( pSpot, mat );
    pSpotWorld = pSpot;
    mat( 3, 1 ) += startTime;

    cinematicPlayer.setBaseMatrix( mat );

    start( true );
    return true;
}

bool EntityCinematic::begin() {
    // FUN_007e8f90
    if ( pCinData == nullptr || cinematicBuffer.empty() ||
         pSCBank == nullptr ) {
        OTDU_LOG_WARN( "Cinematic '%s': no playable data (cin=%p size=%u "
                       "buffer=%zu bank=%p); playback aborted\n",
                       name.c_str(), pCinData, cinDataSize,
                       cinematicBuffer.size(), (void *)pSCBank );
        stop( true );
        return false;
    }

    if ( !prepareResources() ) {
        stop( true );
        return false;
    }

    if ( pMusicEmitter != nullptr && pMusicEmitter->areSoundBanksInstalling() ) {
        return true;
    }

    if ( !cinematicPlayer.createInterface( pCinData, cinDataSize,
                                           cinematicBuffer.data(),
                                           &pSCBank->getBank() ) ) {
        return false;
    }

    CinResCollection::bQuiet = true;

    if ( gpActiveGMScene != nullptr ) {
        const int32_t numActors = cinematicPlayer.getActorCount();
        for ( int32_t i = 0; i < numActors; i++ ) {
            Entity *pTemplate =
                static_cast<Entity *>( cinematicPlayer.getActorEntity( i ) );
            if ( pTemplate == nullptr ) {
                continue;
            }
            if ( Entity::IsAvatarEntityType( pTemplate ) ) {
                gpActiveGMScene->getActorManager().registerActor( pTemplate );
                continue;
            }
            Entity *pInstance =
                gpActiveGMScene->getActorManager().instanciateActor(
                    pTemplate );
            if ( pInstance != nullptr ) {
                cinematicPlayer.setActorEntity( i, pInstance );
                PlayBestAmbientClip( pInstance, pSCBank );
            } else {
                cinematicPlayer.setActorEntity( i, nullptr );
            }
        }
    }

    if ( pGMInterface != nullptr ) {
        pGMInterface->executeAction( CE_Begun, this );
    }

    if ( hasFocus() ) {
        CameraCinematic *pCinematicCamera =
            cinematicPlayer.getCinematicCamera();
        if ( pCinematicCamera != nullptr && !bDontSwitchCamera &&
             gpCamera != nullptr ) {
            gpCamera->setNextCamera( pCinematicCamera, nullptr, 0.0f );
        }

        applyForegroundCine();
    }

    CineTotalTime = cinematicPlayer.getTotalTime();
    Time = StartTime;
    cinematicFlags = ( cinematicFlags & 0xfffffffcu ) | 4u;

    if ( pMusicEmitter != nullptr ) {
        pMusicEmitter->setPaused( bPaused, 0.0f );
    }

    if ( pGMInterface != nullptr ) {
        pGMInterface->executeAction( CE_Started, this );
    }

    return true;
}

void EntityCinematic::publishCameraView() {
    if ( !hasFocus() || gpCamera == nullptr ) {
        return;
    }

    const CinResCollection::CameraView &view = cinematicPlayer.getCameraView();
    if ( !view.bValid ) {
        return;
    }

    CameraCinematic *pCinematicCamera = cinematicPlayer.getCinematicCamera();
    const bool bDrivesCamera = pCinematicCamera != nullptr &&
                               gpCamera->getActiveCamera() == pCinematicCamera;

    if ( !bCameraHandedOver || bCameraDrivenByObject != bDrivesCamera ) {
        bCameraHandedOver = true;
        bCameraDrivenByObject = bDrivesCamera;
        OTDU_LOG_DEBUG( "Cinematic '%s' took the camera: pos=(%.1f %.1f %.1f) "
                       "target=(%.1f %.1f %.1f) fov=%.1f deg, %.2fs%s "
                       "(LED flags 0x%08x, via %s, active cam flags 0x%x)\n",
                       name.c_str(), view.Position.x(), view.Position.y(),
                       view.Position.z(), view.Target.x(), view.Target.y(),
                       view.Target.z(), view.Fov, CineTotalTime,
                       bLoop ? ", looping" : "", cinematicPlaybackFlags,
                       bDrivesCamera ? "the CINEMATIC camera"
                                     : "the view override",
                       gpCamera->getActiveCamera()->getFlags() );
    }

    if ( bDrivesCamera ) {
        gpCamera->clearCinematicCameraOverride();
        return;
    }

    gpCamera->setCinematicCameraOverride( view.Position, view.Up, view.Target,
                                          view.Fov * kDegToRad );
}

void EntityCinematic::end() {
    // FUN_007e8840
    const bool bWasRunning = ( cinematicFlags & 0x4u ) != 0 || bForceStop;

    restoreForegroundCine();

    if ( pMusicEmitter != nullptr ) {
        pMusicEmitter->uninstallSoundBanks();
        releaseMusicEmitter();
    }

    cinematicPlayer.releasePlayerCarActors();

    if ( gpActiveGMScene != nullptr ) {
        MngActor &sceneActors = gpActiveGMScene->getActorManager();
        const int32_t count = cinematicPlayer.getActorCount();
        for ( int32_t i = 0; i < count; i++ ) {
            Entity *pActor =
                static_cast<Entity *>( cinematicPlayer.getActorEntity( i ) );
            if ( pActor != nullptr ) {
                if ( Entity::IsAvatarEntityType( pActor ) ) {
                    sceneActors.removeActorFromList( pActor );
                    if ( gpScenaric != nullptr ) {
                        gpScenaric->getAvatarManager().releaseAvatar(
                            static_cast<EntityAvatar *>( pActor ) );
                    }
                } else {
                    MngActor::Uninstanciate( &sceneActors, pActor );
                }
                cinematicPlayer.setActorEntity( i, nullptr );
            }
        }
    }
    cinematicFlags = ( cinematicFlags & 0xfffffff8u ) | 0x10u;
    Time = 0.0f;
    bStopGuard = false;
    if ( bWasRunning ) {
        bForceStop = false;
    }
    if ( hasFocus() && gpCamera != nullptr ) {
        gpCamera->clearCinematicCameraOverride();
        bCameraHandedOver = false;
        bCameraDrivenByObject = false;
    }
    if ( bWasRunning && pGMInterface != nullptr ) {
        pGMInterface->executeAction( CE_Ended, this );
    }

    if ( gpMngCinematics != nullptr ) {
        gpMngCinematics->removeCinematic( this );
    }
}
