#include "shared.h"
#include "gs_audio.h"

#include "implementation/audio_dispatcher_playback.h"
#include "implementation/ed_audio.h"
#include "implementation/ed_audio_backend.h"
#include "implementation/ed_audio_memory.h"
#include "implementation/ed_audio_object.h"
#include "implementation/ed_audio_reverb.h"

#include "audio/radio/gs_radio.h"
#include "core/ed_store.h"
#include "game/gm_base.h"
#include "game/cinematic/gs_cinematics.h"
#include "game/cinematic/mng_cinematics.h"

#include "physics/replay.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"

GSAudio *gpAudio = nullptr;

#ifdef OTDU_USE_IMGUI
// A reverb tap delay is stored as a fraction of the delay line.
static constexpr float kDelayLineToMilliseconds =
    float( edAudioReverbChannel::kDelayLineSize ) * 1000.0f /
    float( kAudioOutputSampleRate );
#endif

static constexpr float kMinAudibleClientVolume = 0.01f; // DAT_00f416d4
static constexpr float kClientVolumeSmoothing = 0.5f;   // DAT_00f41674

static void UnlinkFromParent( edAudioObject *pModule ) {
    if ( pModule != nullptr && pModule->getParent() != nullptr ) {
        pModule->getParent()->unlinkEntry( pModule );
    }
}

static void CopyAudioName( char *pDestination,
                           size_t destinationSize,
                           const std::string &source ) {
    snprintf( pDestination, destinationSize, "%s", source.c_str() );
}

class ClientRootModule final : public edAudioObject {
public:
    ClientRootModule( int32_t inputChannels,
                      int32_t outputChannels,
                      AudioClient *pInClient )
        : edAudioObject( inputChannels, outputChannels ),
          pClient( pInClient ) {}

    bool prepare( edAudioDSPNode &node ) override {
        // FUN_0040df80
        if ( pClient != nullptr ) {
            const float volume = pClient->resolveVolume();
            const ClientConfig *pConfig = pClient->getConfig();

            Volume = volume;
            if ( pConfig != nullptr ) {
                Pitch = pConfig->Pitch;
            }

            const bool bPaused = ( pConfig != nullptr ) && pConfig->bPaused;
            if ( !bPaused ) {
                bMuted = false;
            } else if ( volume < kMinAudibleClientVolume ) {
                bMuted = true;
            }

            if ( pClient->isPositional() ) {
                pClient->rebuildSpatialGraph();
            }
        }

        return edAudioObject::prepare( node );
    }

private:
    AudioClient *pClient;
};

AudioClient::AudioClient( const ClientConfig *pInConfig,
                          Mixer *pInMixer,
                          edAudioObject *pParent,
                          bool bInPositional )
    // FUN_0040cba0
    : pConfig( pInConfig ),
      pMixer( pInMixer ),
      DuckGain( 1.0f ),
      bPositional( bInPositional ),
      pParentBuffer( pParent ),
      pRoot( nullptr ),
      pStereoPipeline( nullptr ),
      Clustering(),
      pClusters{} {
    const int32_t rootChannelCount = ( bPositional || pParent == nullptr )
                                         ? kSpatialChannelCount
                                         : pParent->getInputChannelCount();

    pRoot = new ClientRootModule( rootChannelCount, rootChannelCount, this );
    pRoot->debugSetDescription( ( pConfig != nullptr ) ? pConfig->pName
                                                       : "Pipeline Spatial" );

    if ( bPositional ) {
        for ( int32_t i = 0; i < SpatialClustering::kMaxClusters; i++ ) {
            pClusters[i] = std::make_unique<edAudioCluster>();
        }
    } else {
        pStereoPipeline = std::make_unique<PipelineStereo>();
        pStereoPipeline->create( pRoot );
    }

    if ( pParentBuffer != nullptr ) {
        pParentBuffer->addNewEntry( pRoot );
    }
}

float AudioClient::resolveVolume() {
    // FUN_009cfaa0
    float mixerGain = 1.0f;
    for ( const Mixer *pWalk = pMixer; pWalk != nullptr;
          pWalk = pWalk->pOutputMixer ) {
        if ( pWalk->bEnabled ) {
            mixerGain *= pWalk->Volume;
        }
    }

    float target = mixerGain;
    if ( pConfig != nullptr ) {
        target *= pConfig->Volume * DuckGain;
    }

    bool bAttenuated =
        ( pConfig != nullptr ) && ( pConfig->bPaused || pConfig->bMuted );
    if ( !bAttenuated ) {
        for ( const Mixer *pWalk = pMixer; pWalk != nullptr;
              pWalk = pWalk->pOutputMixer ) {
            if ( pWalk->bEnabled && pWalk->bMuted ) {
                bAttenuated = true;
                break;
            }
        }
    }

    if ( bAttenuated ) {
        target *= kMinAudibleClientVolume;
    }

    SmoothedVolume = ( SmoothedVolume + target ) * kClientVolumeSmoothing;
    return SmoothedVolume;
}

AudioClient::~AudioClient() {
    // FUN_0040df20
    UnlinkFromParent( pRoot );

    if ( pStereoPipeline != nullptr ) {
        pStereoPipeline->destroy();
        pStereoPipeline.reset();
    }

    for ( int32_t c = 0; c < Clustering.ClusterCount; c++ ) {
        if ( pClusters[c] != nullptr ) {
            pClusters[c]->getBuffer()->unlinkAllEntries();
        }
    }

    if ( pRoot != nullptr ) {
        pRoot->unlinkAllEntries();
    }

    for ( std::unique_ptr<edAudioCluster> &cluster : pClusters ) {
        cluster.reset();
    }

    {
        // FUN_0040dde0
        ScopedMutexLock lock( &GetAudioGraphMutex() );

        while ( pInstances != nullptr ) {
            AudioDispatcherPlaybackSoundInstance *pNext =
                pInstances->pNextInstance;
            DestroySoundInstance( pInstances );
            pInstances = pNext;
        }
    }

    delete pRoot;
    pRoot = nullptr;
}

edAudioCluster *
AudioClient::findNearestFreeCluster( const Eigen::Vector3f &probe,
                                     int32_t clusterCount ) {
    // FUN_0040c970
    edAudioCluster *pBest = nullptr;
    float bestDistance = FLT_MAX;

    const int32_t count = Min( clusterCount, SpatialClustering::kMaxClusters );
    for ( int32_t i = 0; i < count; i++ ) {
        std::unique_ptr<edAudioCluster> &cluster = pClusters[i];

        // A cluster already linked this round is taken.
        if ( cluster == nullptr || cluster->getParent() != nullptr ) {
            continue;
        }

        const float distance = ( probe - cluster->getPosition() ).squaredNorm();
        if ( distance < bestDistance ) {
            pBest = cluster.get();
            bestDistance = distance;
        }
    }

    return pBest;
}

AudioDispatcherPlaybackSoundInstance *AudioClient::acquireSoundInstance() {
    // FUN_004029d0
    void *pMemory = edAudioHeap::Allocate(
        int32_t( sizeof( AudioDispatcherPlaybackSoundInstance ) ) );
    if ( pMemory == nullptr ) {
        return nullptr;
    }

    AudioDispatcherPlaybackSoundInstance *pInstance =
        new ( pMemory ) AudioDispatcherPlaybackSoundInstance();

    {
        // FUN_0040d020
        ScopedMutexLock lock( &GetAudioGraphMutex() );

        pInstance->pNextInstance = pInstances;
        pInstances = pInstance;
    }

    return pInstance;
}

void AudioClient::DestroySoundInstance(
    AudioDispatcherPlaybackSoundInstance *pInstance ) {
    pInstance->~AudioDispatcherPlaybackSoundInstance();
    edAudioHeap::Free( pInstance );
}

bool AudioClient::sweepInstances() {
    // FUN_0040cf10
    ScopedMutexLock lock( &GetAudioGraphMutex() );

    AudibleSourceCount = 0;
    MutedSourceCount = 0;

    AudioDispatcherPlaybackSoundInstance *pInstance = pInstances;
    AudioDispatcherPlaybackSoundInstance *pPrevious = nullptr;

    while ( pInstance != nullptr ) {
        AudioDispatcherPlaybackSoundInstance *pNext = pInstance->pNextInstance;

        if ( pInstance->isReleaseRequested() ) {
            if ( pPrevious != nullptr ) {
                pPrevious->pNextInstance = pNext;
            } else {
                pInstances = pNext;
            }

            DestroySoundInstance( pInstance );
        } else {
            pInstance->setListener( CachedListener );
            pInstance->update();

            const bool bAccepted =
                Clustering.addSource( pInstance->getRelativePosition(),
                                      kDefaultSourceWeight, pInstance );

            if ( bAccepted && pInstance->getPlayingState() ==
                                  edAudioStreamEntry::APES_Play ) {
                if ( pInstance->bMuted ) {
                    MutedSourceCount++;
                } else {
                    AudibleSourceCount++;
                }
            }

            pPrevious = pInstance;
        }

        pInstance = pNext;
    }

    return true;
}

void AudioClient::rebuildSpatialGraph() {
    // FUN_0040cda0
    if ( pRoot == nullptr ) {
        return;
    }

    CachedListener.copyFrom( gEdAudio.getListener() );
    for ( int32_t c = 0; c < Clustering.ClusterCount; c++ ) {
        if ( pClusters[c] != nullptr ) {
            pClusters[c]->getBuffer()->unlinkAllEntries();
        }
    }

    pRoot->unlinkAllEntries();
    Clustering.reset();

    if ( !sweepInstances() ) {
        return;
    }

    if ( Clustering.SourceCount == 0 ) {
        return;
    }

    Clustering.build( kDefaultMinSeparation );
    Clustering.flattenMembers();

    for ( int32_t c = 0; c < Clustering.ClusterCount; c++ ) {
        ClusterInfo &info = Clustering.Clusters[c];

        const Eigen::Vector3f centre = info.Placement.Position.head<3>();

        edAudioCluster *pCluster =
            findNearestFreeCluster( centre, Clustering.ClusterCount );
        if ( pCluster == nullptr ) {
            break;
        }

        pCluster->fillMatrix( 0.0f );
        pCluster->setPosition( centre );

        const SpatialSource *pMember = info.pFirstMember;
        for ( int32_t n = 0; n < info.MemberCount && pMember != nullptr; n++ ) {
            const int32_t index = pMember->SourceIndex;
            edAudioObject *pSourceModule =
                ( index >= 0 && index < SpatialClustering::kMaxSources )
                    ? Clustering.pSourceObjects[index]
                    : nullptr;

            if ( pSourceModule != nullptr ) {
                pCluster->pan( pMember->Placement.Direction.head<3>(), 1.0f,
                               0 );
                pCluster->getBuffer()->addNewEntry( pSourceModule );
            }

            pMember = pMember->pNextMember;
        }

        pRoot->addNewEntry( pCluster );
    }
}

GSAudio::GSAudio()
    : pMemory( nullptr ),
      MemorySize( 0 ),
      NbSpeakers( 5 ),
      OutputSpatialFactor( 0.03f ),  // DAT_00ef1b40
      OutputSpatialFactor2( 0.03f ), // DAT_00ef1b40
      OutputDopplerFactor( 1.0f ),   // DAT_00ef1b3c
      OutputDopplerFactor2( 1.0f ),  // DAT_00ef1b3c
      pSpatialBuffer( nullptr ),
      pSpatialMixer( nullptr ),
      pSpatialOutput( nullptr ) {
    // FUN_009ddfd0
    gpAudio = this;
}

GSAudio::~GSAudio() {
    // FUN_00401e60
    gpAudio = nullptr;
}

bool GSAudio::initialize( TestDriveGameInstance * ) {
    // FUN_009b1df0
    MemorySize = 0x300000;
    pMemory = new uint8_t[MemorySize];
    edAudioHeap::InitMemory( pMemory, MemorySize );

    if ( gEdAudio.initialize() != 0 ) {
        gEdAudio.destroy();
        return false;
    }

    MixerPool.reserve( kMaxMixers );
    ClientConfigPool.reserve( kMaxClientConfigs );
    ClientHandlerPool.reserve( kMaxClientHandlers );

    const int32_t channelCount = gEdAudio.getChannelCount();

    pSpatialOutput = new edAudioBuffer( kSpatialChannelCount );
    pSpatialOutput->debugSetDescription( "Spatial Pipeline Output" );

    pSpatialMixer = new edAudioMixer( kSpatialChannelCount );
    pSpatialMixer->debugSetDescription( "Spatial Pipelines Mixer" );

    pSpatialBuffer = new edAudioBuffer( kSpatialChannelCount );
    pSpatialBuffer->debugSetDescription( "Spatial Pipelines Buffer" );

    pSpatialAdapter =
        new edAudioChannelAdapter( kSpatialChannelCount, channelCount );
    pSpatialAdapter->debugSetDescription( "Spatial Pipelines Adapter" );

    pSpatialMixer->addNewEntry( pSpatialBuffer );
    pSpatialOutput->addNewEntry( pSpatialMixer );
    pSpatialAdapter->addNewEntry( pSpatialOutput );
    gEdAudio.getOutputModule()->addNewEntry( pSpatialAdapter );

    for ( edAudioReverb *&pReverb : pReverbPool ) {
        pReverb = nullptr;
    }
    pAmbianceReverbModule = nullptr;
    pVehicleReverbModule = nullptr;

    return true;
}

ClientHandler *GSAudio::findClientHandler( const char *pName ) {
    // FUN_00401da0
    for ( ClientHandler &handler : ClientHandlerPool ) {
        if ( handler.pClient == nullptr ) {
            continue;
        }

        const ClientConfig *pConfig = handler.pClient->getConfig();
        if ( pConfig != nullptr && strcmp( pConfig->pName, pName ) == 0 ) {
            return &handler;
        }
    }

    return nullptr;
}

ClientHandler *GSAudio::findClientHandler( const AudioClient *pClient ) {
    // FUN_00401d40
    for ( ClientHandler &handler : ClientHandlerPool ) {
        if ( handler.pClient == pClient ) {
            return &handler;
        }
    }

    return nullptr;
}

void GSAudio::registerClient( AudioClient *pClient ) {
    // FUN_009b23e0
    if ( pClient == nullptr ) {
        return;
    }

    ClientHandler *pHandler = findClientHandler( pClient );
    if ( pHandler != nullptr ) {
        pHandler->RefCount++;
        return;
    }

    if ( int32_t( ClientHandlerPool.size() ) >= kMaxClientHandlers ) {
        OTDU_LOG_ERROR( "Out of audio client handlers (%d)!\n",
                        kMaxClientHandlers );
        return;
    }

    ClientHandler handler = {};
    handler.pClient = pClient;
    handler.RefCount = 1;

    ClientHandlerPool.push_back( handler );
}

void GSAudio::releaseClient( AudioClient *pClient ) {
    // FUN_009b2a60
    ClientHandler *pHandler = findClientHandler( pClient );
    if ( pHandler == nullptr || --pHandler->RefCount != 0 ) {
        return;
    }

    ClientHandlerPool.erase( ClientHandlerPool.begin() +
                             ( pHandler - ClientHandlerPool.data() ) );
    Clients.erase( std::remove_if( Clients.begin(), Clients.end(),
                                   [pClient]( const auto &client ) {
                                       return client.get() == pClient;
                                   } ),
                   Clients.end() );
}

AudioClient *GSAudio::createClient( const char *pName ) {
    // FUN_0040cab0
    if ( pName == nullptr ) {
        return nullptr;
    }

    // Already built: take a reference on it rather than
    // making a second one.
    if ( ClientHandler *pHandler = findClientHandler( pName ) ) {
        registerClient( pHandler->pClient );
        return pHandler->pClient;
    }

    ClientConfig *pConfig = getClientConfig( pName );
    if ( pConfig == nullptr ) {
        OTDU_LOG_ERROR( "No audio client config named '%s'!\n", pName );
        return nullptr;
    }

    Mixer *pMixer = findMixer( pConfig->pMixerName );

    const bool bPositional =
        strcmp( pConfig->pOutputName, kOutputMixerName ) != 0;

    edAudioObject *pParent =
        bPositional ? static_cast<edAudioObject *>( pSpatialBuffer )
                    : gEdAudio.getOutputModule();

    if ( pParent == nullptr ) {
        OTDU_LOG_ERROR( "Audio graph is not built, can't create '%s'\n",
                        pName );
        return nullptr;
    }

    auto client =
        std::make_unique<AudioClient>( pConfig, pMixer, pParent, bPositional );

    AudioClient *pClient = client.get();
    Clients.push_back( std::move( client ) );

    registerClient( pClient );

    return pClient;
}

PipelineMono::PipelineMono()
    : pRoot( nullptr ), pAdapter( nullptr ), pBuffer( nullptr ) {}

PipelineMono::~PipelineMono() { destroy(); }

bool PipelineMono::create( edAudioObject *pParent ) {
    // FUN_00403cb0
    if ( pParent == nullptr || pRoot != nullptr ) {
        return false;
    }

    const int32_t channelCount = pParent->getInputChannelCount();

    pRoot = new edAudioObject( channelCount, channelCount );
    pRoot->debugSetDescription( "PipelineMono" );

    pAdapter = new edAudioChannelAdapter( 1, channelCount );
    pAdapter->debugSetDescription( "PipelineMono Adapter" );

    pBuffer = new edAudioBuffer( 1 );
    pBuffer->debugSetDescription( "PipelineMono Buffer" );

    pAdapter->addNewEntry( pBuffer );
    pRoot->addNewEntry( pAdapter );
    pParent->addNewEntry( pRoot );

    return true;
}

void PipelineMono::destroy() {
    UnlinkFromParent( pRoot );

    if ( pRoot != nullptr ) {
        pRoot->unlinkAllEntries();
    }
    if ( pAdapter != nullptr ) {
        pAdapter->unlinkAllEntries();
    }

    delete pBuffer;
    delete pAdapter;
    delete pRoot;

    pBuffer = nullptr;
    pAdapter = nullptr;
    pRoot = nullptr;
}

AudioStereoSource::AudioStereoSource() : bStarted( false ) {
    // FUN_004312e0
    ChannelLeft.debugSetDescription( "StereoSource Left" );
    ChannelRight.debugSetDescription( "StereoSource Right" );
}

AudioStereoSource::~AudioStereoSource() {
    ChannelLeft.setSample( nullptr );
    ChannelRight.setSample( nullptr );
}

bool AudioStereoSource::setSamples( const edAudioSample *pLeft,
                                    const edAudioSample *pRight ) {
    const bool bLeft = ChannelLeft.setSample( pLeft );
    const bool bRight = ChannelRight.setSample( pRight );
    return bLeft && bRight;
}

void AudioStereoSource::setVolume( float volume ) {
    ChannelLeft.Volume = volume;
    ChannelRight.Volume = volume;
}

void AudioStereoSource::setPitch( float pitch ) {
    ChannelLeft.Pitch = pitch;
    ChannelRight.Pitch = pitch;
}

void AudioStereoSource::setLoopState( bool bLoop ) {
    // FUN_006137f0
    ChannelLeft.setLoopCount( bLoop ? -1 : 0 );
    ChannelRight.setLoopCount( bLoop ? -1 : 0 );
}

void AudioStereoSource::play() {
    ChannelLeft.play();
    ChannelRight.play();
    bStarted = true;
}

void AudioStereoSource::stop( float fadeTime ) {
    // FUN_009b0260
    bStarted = false;
    ChannelLeft.stop( fadeTime );
    ChannelRight.stop( fadeTime );
}

PipelineStereo::PipelineStereo()
    : pLeftBuffer( nullptr ),
      pRightBuffer( nullptr ),
      pLeftRouter( nullptr ),
      pRightRouter( nullptr ),
      pBuffer( nullptr ),
      pAdapter( nullptr ) {}

PipelineStereo::~PipelineStereo() { destroy(); }

bool PipelineStereo::create( edAudioObject *pParent ) {
    // FUN_004316a0
    if ( pParent == nullptr || pAdapter != nullptr ) {
        return false;
    }

    const int32_t channelCount = pParent->getInputChannelCount();

    pLeftBuffer = new edAudioBuffer( 1 );
    pLeftBuffer->debugSetDescription( "PipelineStereo Left Buffer" );

    pRightBuffer = new edAudioBuffer( 1 );
    pRightBuffer->debugSetDescription( "PipelineStereo Right Buffer" );

    pLeftRouter = new edAudioSpatializer( 1, 2 );
    pLeftRouter->debugSetDescription( "PipelineStereo Left Router" );
    pLeftRouter->setGain( 1.0f, 0, 0 );
    pLeftRouter->addNewEntry( pLeftBuffer );

    pRightRouter = new edAudioSpatializer( 1, 2 );
    pRightRouter->debugSetDescription( "PipelineStereo Right Router" );
    pRightRouter->setGain( 1.0f, 0, 1 );
    pRightRouter->addNewEntry( pRightBuffer );

    pBuffer = new edAudioBuffer( 2 );
    pBuffer->debugSetDescription( "PipelineStereo Buffer" );
    pBuffer->addNewEntry( pLeftRouter );
    pBuffer->addNewEntry( pRightRouter );

    pAdapter = new edAudioChannelAdapter( 2, channelCount );
    pAdapter->debugSetDescription( "PipelineStereo Adapter" );
    pAdapter->addNewEntry( pBuffer );

    pParent->addNewEntry( pAdapter );

    return true;
}

void PipelineStereo::destroy() {
    UnlinkFromParent( pAdapter );

    for ( edAudioObject *pModule :
          { static_cast<edAudioObject *>( pAdapter ),
            static_cast<edAudioObject *>( pBuffer ),
            static_cast<edAudioObject *>( pLeftRouter ),
            static_cast<edAudioObject *>( pRightRouter ),
            static_cast<edAudioObject *>( pLeftBuffer ),
            static_cast<edAudioObject *>( pRightBuffer ) } ) {
        if ( pModule != nullptr ) {
            pModule->unlinkAllEntries();
        }
    }

    delete pAdapter;
    delete pBuffer;
    delete pLeftRouter;
    delete pRightRouter;
    delete pLeftBuffer;
    delete pRightBuffer;

    pAdapter = nullptr;
    pBuffer = nullptr;
    pLeftRouter = nullptr;
    pRightRouter = nullptr;
    pLeftBuffer = nullptr;
    pRightBuffer = nullptr;
}

void PipelineStereo::addSource( AudioStereoSource *pSource ) {
    // FUN_00431430
    if ( pSource == nullptr || pLeftBuffer == nullptr ) {
        return;
    }

    pLeftBuffer->addNewEntry( &pSource->getLeftChannel() );
    pRightBuffer->addNewEntry( &pSource->getRightChannel() );
}

void PipelineStereo::removeSource( AudioStereoSource *pSource ) {
    if ( pSource == nullptr || pLeftBuffer == nullptr ) {
        return;
    }

    pLeftBuffer->unlinkEntry( &pSource->getLeftChannel() );
    pRightBuffer->unlinkEntry( &pSource->getRightChannel() );
}

void PipelineStereo::addStereoEntry( edAudioObject *pEntry ) {
    // FUN_00403f20
    if ( pEntry == nullptr || pBuffer == nullptr ) {
        return;
    }

    pBuffer->addNewEntry( pEntry );
}

void PipelineStereo::removeStereoEntry( edAudioObject *pEntry ) {
    // FUN_00403f70
    if ( pEntry == nullptr || pBuffer == nullptr ) {
        return;
    }

    pBuffer->unlinkEntry( pEntry );
}

Mixer *GSAudio::findMixer( const char *pName ) {
    // FUN_009b1860
    if ( strcmp( pName, kOutputMixerName ) == 0 ) {
        return nullptr;
    }

    for ( Mixer &mixer : MixerPool ) {
        if ( strcmp( mixer.pName, pName ) == 0 ) {
            return &mixer;
        }
    }

    return nullptr;
}

ClientConfig *GSAudio::getClientConfig( const char *pName ) {
    // FUN_009b2440
    for ( ClientConfig &config : ClientConfigPool ) {
        if ( strcmp( config.pName, pName ) == 0 ) {
            return &config;
        }
    }

    return nullptr;
}

void GSAudio::syncListenerToActiveCamera() {
    if ( !bListenerSyncToCamera || gpCamera == nullptr ) {
        return;
    }

    CameraGame *pCamera = gpCamera->getActiveCamera();
    if ( pCamera == nullptr ) {
        return;
    }

    const Matrix4x4f_t &camToWorld = pCamera->getCamToWorld();

    edAudioListener &listener = gEdAudio.getListener();
    ScopedMutexLock lock( &GetAudioGraphMutex() );

    listener.Position = pCamera->getWorldPosition();
    listener.LeftToRight = camToWorld.row( 0 ).head<3>();
    listener.Top = camToWorld.row( 1 ).head<3>();
    listener.Front = camToWorld.row( 2 ).head<3>();
    listener.Speed = pCamera->getVelocity();
}

void GSAudio::setListener( const Eigen::Vector4f &position,
                           const Eigen::Vector4f &speed,
                           const Eigen::Vector4f &front,
                           const Eigen::Vector4f &top ) {
    // FUN_009b1d80
    edAudioListener &listener = gEdAudio.getListener();
    ScopedMutexLock lock( &GetAudioGraphMutex() );

    listener.Position = position.head<3>();
    listener.Front = front.head<3>();
    listener.Top = top.head<3>();
    listener.LeftToRight = listener.Front.cross( listener.Top );
    listener.Speed = speed.head<3>();
}

void GSAudio::tick( float totalTime, float deltaTime ) {
    // FUN_009b25a0
    if ( gpActiveGameMode != nullptr &&
         GameMode !=
             static_cast<int32_t>( gpActiveGameMode->getCurrentGameMode() ) ) {
        for ( ClientConfig &config : ClientConfigPool ) {
            config.bPaused = true;
        }
        GameMode =
            static_cast<int32_t>( gpActiveGameMode->getCurrentGameMode() );
        bMixStateTransitionSync = true;
    }

    const uint32_t transitionFlags =
        ( gpActiveGameMode != nullptr ) ? gpActiveGameMode->getTransitionFlags()
                                        : 0u;
    const bool bTransitioning = ( transitionFlags & ( 1u << 1 ) ) != 0 ||
                                ( transitionFlags & ( 1u << 3 ) ) != 0;

    if ( bMixStateTransitionSync && !bTransitioning && GameMode >= 0 &&
         GameMode < kMaxGameModes &&
         ( gpCinematics == nullptr ||
           !gpCinematics->isCinematicPlaying( CT_Foreground, true, false ) ) ) {
        bMixStateTransitionSync = false;
        changeMixState( GameModeMixState[GameMode] );

        for ( int32_t i = kMaxReverbs - 1; i >= 0; i-- ) {
            if ( pReverbPool[i] != nullptr ) {
                pReverbPool[i]->reset();
            }
        }

        OTDU_LOG_DEBUG( "Audio mix state for game mode %d is 0x%08x\n",
                        GameMode, GameModeMixState[GameMode] );
    }

    updateReplayPitch();
    updateClientPriorities();

    const bool bRadioIsOnAir = gpRadio != nullptr && gpRadio->isOnAir();
    if ( bRadioIsOnAir != bRadioOnAir ) {
        bRadioOnAir = bRadioIsOnAir;
        for ( ClientConfig &config : ClientConfigPool ) {
            if ( config.bAffectedByMusic ) {
                config.bMuted = bRadioOnAir;
            }
        }
    }
}

void GSAudio::updateReplayPitch() {
    // FUN_009b1a60
    float pitch = 1.0f;
    if ( gReplay.isPlayingBack() ) {
        const float speedRatio = gReplay.getTimeScale();
        if ( speedRatio < 1.0f ) {
            pitch = ( speedRatio + 1.0f ) * 0.5f * 0.1f +
                    ReplayPitch * 0.9f; // FLOAT_00f41674, FLOAT_00f41668,
                                        // DAT_00f416a0
        }
    }

    pSpatialMixer->Volume = pitch;
    ReplayPitch = pitch;
    pSpatialMixer->Pitch = pitch;
}

void GSAudio::updateClientPriorities() {
    // FUN_009b2070
    const ClientConfig *pDucker = nullptr;

    for ( const ClientHandler &handler : ClientHandlerPool ) {
        if ( handler.pClient == nullptr ) {
            continue;
        }
        const ClientConfig *pConfig = handler.pClient->getConfig();
        if ( pConfig == nullptr || pConfig->bPaused || pConfig->bMuted ) {
            continue;
        }
        if ( pConfig->PriorityFactor >= 1.0f ) {
            continue;
        }

        if ( handler.pClient->getAudibleSourceCount() <= 0 ) {
            continue;
        }

        if ( pDucker == nullptr ||
             pConfig->PriorityRank < pDucker->PriorityRank ) {
            pDucker = pConfig;
        }
    }

    const int32_t duckRank =
        ( pDucker != nullptr ) ? pDucker->PriorityRank : kNoDuckPriorityRank;
    const float duckGain =
        ( pDucker != nullptr ) ? pDucker->PriorityFactor : 1.0f;

    for ( const ClientHandler &handler : ClientHandlerPool ) {
        if ( handler.pClient == nullptr ) {
            continue;
        }
        const ClientConfig *pConfig = handler.pClient->getConfig();

        const bool bUnducked = pConfig == nullptr || pConfig == pDucker ||
                               pConfig->PriorityRank <= duckRank;
        handler.pClient->setDuckGain( bUnducked ? 1.0f : duckGain );
    }
}

#ifdef OTDU_USE_IMGUI
void GSAudio::drawDebug( float ) {
    ImGui::SeparatorText( "Mix state" );
    ImGui::Text( "Game mode %d   mix state 0x%08x", GameMode, MixState );
    if ( bMixStateTransitionSync ) {
        ImGui::SameLine();
        ImGui::TextUnformatted( "(transition pending)" );
    }

    ImGui::Text( "bRadioActive: %i", bRadioOnAir );

    ImGui::SeparatorText( "Reverbs" );
    for ( edAudioReverb *pReverb : pReverbPool ) {
        if ( pReverb == nullptr ) {
            continue;
        }

        ImGui::PushID( pReverb );

        edAudioReverbChannel *pChannel = pReverb->getChannel( 0 );
        const char *pRole =
            ( pReverb == pAmbianceReverbModule )
                ? " (ambiance)"
                : ( pReverb == pVehicleReverbModule ? " (vehicle)" : "" );

        if ( pChannel == nullptr ) {
            ImGui::Text( "%s%s: detached", pReverb->getDescription(), pRole );
        } else {
            ImGui::Text( "%s%s -> %s", pReverb->getDescription(), pRole,
                         ( pReverb->getParent() != nullptr )
                             ? pReverb->getParent()->getDescription()
                             : "<none>" );
            // Both are stored normalized, so scale them back for display.
            const float cutoffHz =
                pChannel->LowPassCutoff * float( kAudioOutputSampleRate );
            const float firstTapMs =
                ( pChannel->TapCount > 0 )
                    ? pChannel->Taps[0].Delay * kDelayLineToMilliseconds
                    : 0.0f;

            ImGui::Text( "  dry %.3f  wet %.3f  ratio %.3f  cutoff %.0f Hz",
                         pChannel->DryVolume, pChannel->WetVolume,
                         pChannel->DryWetRatio, cutoffHz );
            ImGui::Text( "  %i taps, first %.1f ms", pChannel->TapCount,
                         firstTapMs );
        }

        ImGui::PopID();
    }

    ImGui::SeparatorText( "Mixers" );
    for ( Mixer &mixer : MixerPool ) {
        ImGui::PushID( &mixer );

        ImGui::SetNextItemWidth( 140.0f );
        ImGui::SliderFloat( mixer.pName, &mixer.Volume, 0.0f, 1.0f, "%.2f" );

        ImGui::SameLine();
        ImGui::Checkbox( "on", &mixer.bEnabled );
        ImGui::SameLine();
        ImGui::Checkbox( "mute", &mixer.bMuted );

        ImGui::SameLine();
        ImGui::TextDisabled( "-> %s", ( mixer.pOutputMixer != nullptr )
                                          ? mixer.pOutputMixer->pName
                                          : kOutputMixerName );

        ImGui::PopID();
    }

    if ( ImGui::BeginTable( "clients", 6,
                            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                ImGuiTableFlags_SizingStretchProp ) ) {
        ImGui::TableSetupColumn( "client" );
        ImGui::TableSetupColumn( "bit" );
        ImGui::TableSetupColumn( "state" );
        ImGui::TableSetupColumn( "cfg vol" );
        ImGui::TableSetupColumn( "mixer" );
        ImGui::TableSetupColumn( "resolved" );
        ImGui::TableHeadersRow();

        for ( size_t i = 0; i < ClientConfigPool.size(); i++ ) {
            const ClientConfig &config = ClientConfigPool[i];

            const AudioClient *pClient = nullptr;
            for ( const auto &pCandidate : Clients ) {
                if ( pCandidate != nullptr &&
                     pCandidate->getConfig() == &config ) {
                    pClient = pCandidate.get();
                    break;
                }
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( config.pName );
            ImGui::TableNextColumn();
            ImGui::Text( "%zu", i );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( config.bPaused  ? "paused"
                                    : config.bMuted ? "muted"
                                                    : "live" );
            ImGui::TableNextColumn();
            ImGui::Text( "%.2f", config.Volume );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( config.pMixerName );
            ImGui::TableNextColumn();
            if ( pClient != nullptr ) {
                ImGui::Text( "%.3f", pClient->getSmoothedVolume() );
            } else {
                ImGui::TextUnformatted( "-" );
            }
        }
        ImGui::EndTable();
    }
}
#endif

void GSAudio::terminate() {
    // FUN_009b2750
    Clients.clear();
    ClientHandlerPool.clear();

    ClientConfigPool.clear();
    MixerPool.clear();

    pAmbianceReverbModule = nullptr;
    pVehicleReverbModule = nullptr;
    for ( int32_t i = kMaxReverbs - 1; i >= 0; i-- ) {
        UnlinkFromParent( pReverbPool[i] );
        delete pReverbPool[i];
        pReverbPool[i] = nullptr;
    }

    UnlinkFromParent( pSpatialBuffer );
    delete pSpatialBuffer;
    pSpatialBuffer = nullptr;

    UnlinkFromParent( pSpatialMixer );
    delete pSpatialMixer;
    pSpatialMixer = nullptr;

    UnlinkFromParent( pSpatialOutput );
    delete pSpatialOutput;
    pSpatialOutput = nullptr;

    UnlinkFromParent( pSpatialAdapter );
    delete pSpatialAdapter;
    pSpatialAdapter = nullptr;

    gEdAudio.destroy();

    delete[] static_cast<uint8_t *>( pMemory );
    pMemory = nullptr;
    MemorySize = 0;
}

void GSAudio::addMixer( ParsedAudioMixer &mixer ) {
    // FUN_009b2360
    if ( static_cast<int32_t>( MixerPool.size() ) >= kMaxMixers ) {
        OTDU_LOG_WARN( "Mixer pool is full; '%s' dropped\n",
                       mixer.Name.c_str() );
        return;
    }

    // Resolved before the new entry is added, so a mixer cannot route to
    // itself.
    Mixer *pOutputMixer = findMixer( mixer.Output.c_str() );

    Mixer &newMixer = MixerPool.emplace_back();
    CopyAudioName( newMixer.pName, sizeof( newMixer.pName ), mixer.Name );
    newMixer.pOutputMixer = pOutputMixer;
    newMixer.Volume = mixer.Volume;
    newMixer.bEnabled = mixer.bEnabled;
    newMixer.bMuted = mixer.bMuted;
}

void GSAudio::restoreMixerSettings( edStore &store ) {
    // FUN_009b21d0
    NbSpeakers = int32_t( store.readUInteger() );

    const int32_t numMixers = store.readInteger();
    for ( int32_t i = 0; i < numMixers; i++ ) {
        if ( i >= int32_t( MixerPool.size() ) ) {
            continue;
        }

        Mixer &mixer = MixerPool[i];
        mixer.Volume = store.readFloat();
        mixer.bMuted = store.readInteger() != 0;
    }
}

void GSAudio::saveMixerSettings( edStore &store ) {
    // FUN_009b2260
    store.writeInteger( NbSpeakers );
    store.writeInteger( kMaxMixers );

    for ( const Mixer &mixer : MixerPool ) {
        store.writeFloat( mixer.Volume );
        store.writeInteger( int32_t( mixer.bMuted ) );
    }
}

void GSAudio::addReverb( ParsedAudioReverb &reverb ) {
    // FUN_009b1ba0
    const edAudioObject *pOutputModule = gEdAudio.getOutputModule();

    edAudioObject *pOutput = nullptr;
    if ( pOutputModule != nullptr &&
         strcmp( reverb.Output.c_str(), pOutputModule->getDescription() ) ==
             0 ) {
        pOutput = pSpatialOutput;
    } else {
        edAudioReverb *pParentReverb =
            findReverbModule( reverb.Output.c_str() );
        if ( pParentReverb == nullptr ) {
            OTDU_LOG_WARN( "Reverb '%s' routes to unknown output '%s'\n",
                           reverb.Name.c_str(), reverb.Output.c_str() );
            return;
        }
        pOutput = pParentReverb->getBuffer();
    }

    if ( pOutput == nullptr || pSpatialMixer == nullptr ) {
        return;
    }

    edAudioObject *pInput = pSpatialMixer;
    for ( edAudioObject *pNode = pSpatialMixer->getParent(); pNode != pOutput;
          pNode = pNode->getParent() ) {
        if ( pNode == nullptr ) {
            OTDU_LOG_WARN( "Reverb '%s': '%s' is not above the spatial mixer\n",
                           reverb.Name.c_str(), reverb.Output.c_str() );
            return;
        }
        pInput = pNode;
    }

    int32_t slot = 0;
    while ( pReverbPool[slot] != nullptr ) {
        slot++;
        if ( slot >= kMaxReverbs ) {
            OTDU_LOG_WARN( "Reverb pool is full; '%s' dropped\n",
                           reverb.Name.c_str() );
            return;
        }
    }

    edAudioReverb *pReverb =
        new edAudioReverb( kSpatialChannelCount, reverb.Name.c_str() );

    UnlinkFromParent( pInput );
    pOutput->addNewEntry( pReverb );
    pReverb->getBuffer()->addNewEntry( pInput );

    pReverbPool[slot] = pReverb;

    OTDU_LOG_DEBUG( "Reverb '%s' inserted between '%s' and '%s'\n",
                    reverb.Name.c_str(), pOutput->getDescription(),
                    pInput->getDescription() );
}

edAudioReverb *GSAudio::findReverbModule( const char *pName ) {
    // FUN_009b1970
    if ( pName == nullptr ) {
        return nullptr;
    }

    for ( edAudioReverb *pReverb : pReverbPool ) {
        if ( pReverb != nullptr &&
             strcmp( pReverb->getDescription(), pName ) == 0 ) {
            return pReverb;
        }
    }

    return nullptr;
}

void GSAudio::setAmbianceReverbModule( const char *pName ) {
    // FUN_00987de0
    pAmbianceReverbModule = findReverbModule( pName );
    if ( pAmbianceReverbModule == nullptr ) {
        OTDU_LOG_WARN( "Unknown ambiance reverb module '%s'\n", pName );
    }
}

void GSAudio::setVehicleReverbModule( const char *pName ) {
    // FUN_00987e00
    pVehicleReverbModule = findReverbModule( pName );
    if ( pVehicleReverbModule == nullptr ) {
        OTDU_LOG_WARN( "Unknown vehicle reverb module '%s'\n", pName );
    }
}

void GSAudio::addClient( ParsedAudioClient &client ) {
    // FUN_004023c0
    if ( static_cast<int32_t>( ClientConfigPool.size() ) >=
         kMaxClientConfigs ) {
        OTDU_LOG_WARN( "Client config pool is full; '%s' dropped\n",
                       client.Name.c_str() );
        return;
    }

    ClientConfig &config = ClientConfigPool.emplace_back();
    CopyAudioName( config.pName, sizeof( config.pName ), client.Name );
    CopyAudioName( config.pMixerName, sizeof( config.pMixerName ),
                   client.Mixer );
    CopyAudioName( config.pOutputName, sizeof( config.pOutputName ),
                   client.Output );
    config.Volume = client.Volume;
    config.Pitch = client.Pitch;
    config.PriorityRank = client.Priority;
    config.PriorityFactor = client.PriorityFactor;
    config.bAffectedByMusic = client.bAffectedByMusic;
}

uint32_t GSAudio::getMixFlags( const char *pName ) const {
    // FUN_009b2ab0
    if ( pName == nullptr ) {
        return 0;
    }

    for ( const MixPreset &preset : MixPresets ) {
        if ( strcmp( preset.pName, pName ) == 0 ) {
            return preset.MixState;
        }
    }

    for ( size_t i = 0; i < ClientConfigPool.size(); i++ ) {
        if ( strcmp( ClientConfigPool[i].pName, pName ) == 0 ) {
            return 1u << i;
        }
    }

    return 0;
}

void GSAudio::changeMixState( uint32_t mixState ) {
    // FUN_009b2500
    for ( size_t i = 0; i < ClientConfigPool.size(); i++ ) {
        ClientConfigPool[i].bPaused = ( mixState & ( 1u << i ) ) == 0;
    }
    MixState = mixState;
}

void GSAudio::setGameModeMix( const char *pGameModeName,
                              const char *pPresetName ) {
    // FUN_009b1b10
    static_assert( eGameMode::GM_Count == kMaxGameModes,
                   "pGameModeMixState is a fixed 31 entry array" );

    if ( pGameModeName == nullptr || pPresetName == nullptr ) {
        return;
    }

    int32_t gameModeIndex = -1;
    for ( int32_t i = 0; i < eGameMode::GM_Count; i++ ) {
        if ( strcmp( kGameModeIniNames[i], pGameModeName ) == 0 ) {
            gameModeIndex = i;
            break;
        }
    }

    if ( gameModeIndex < 0 ) {
        OTDU_LOG_WARN( "Unknown game mode '%s' in SetGameModeMix\n",
                       pGameModeName );
        return;
    }

    for ( const MixPreset &preset : MixPresets ) {
        if ( strcmp( preset.pName, pPresetName ) == 0 ) {
            GameModeMixState[gameModeIndex] = preset.MixState;
            return;
        }
    }
}

void GSAudio::addMixPreset( const char *pName,
                            const std::vector<std::string> &mixList ) {
    // FUN_009b2bc0
    if ( pName == nullptr ) {
        return;
    }
    if ( static_cast<int32_t>( MixPresets.size() ) >= kMaxMixPresets ) {
        OTDU_LOG_WARN( "Mix preset pool is full; '%s' dropped\n", pName );
        return;
    }

    uint32_t mixState = 0;
    for ( const std::string &entry : mixList ) {
        mixState |= getMixFlags( entry.c_str() );
    }

    MixPreset &preset = MixPresets.emplace_back();
    CopyAudioName( preset.pName, sizeof( preset.pName ), pName );
    preset.MixState = mixState;
}
