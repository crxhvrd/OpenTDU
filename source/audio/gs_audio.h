#pragma once

#include "game/gs_base.h"
#include "implementation/ed_audio.h"
#include "implementation/pipeline_spatial.h"

class AudioDispatcherPlaybackSoundInstance;
class edAudioBuffer;
class edAudioMixer;
class edAudioChannelAdapter;
class edAudioCluster;
class edAudioObject;
class AudioClient;

static constexpr int32_t kMaxMixers = 9;
static constexpr int32_t kMaxClientConfigs = 20;
static constexpr int32_t kMaxClientHandlers = 19;
static constexpr int32_t kMaxMixPresets = 16; // pMixPresets[16]
static constexpr int32_t kMaxGameModes = 31;
static constexpr const char *kOutputMixerName = "Output";

struct ParsedAudioMixer {
    std::string Name;
    std::string Output;
    float Volume;
    uint8_t bEnabled : 1;
    uint8_t bMuted : 1;

    ParsedAudioMixer()
        : Name( "" ),
          Output( "" ),
          Volume( 1.0f ),
          bEnabled( false ),
          bMuted( false ) {}
};

struct ParsedAudioReverb {
    std::string Name;
    std::string Output;

    ParsedAudioReverb() : Name( "" ), Output( "" ) {}
};

struct ParsedAudioClient {
    std::string Name;
    std::string Mixer;
    std::string Output;
    float Volume;
    float Pitch;
    int32_t Priority;
    float PriorityFactor;
    uint8_t bAffectedByMusic : 1;

    ParsedAudioClient()
        : Name( "" ),
          Mixer( "" ),
          Output( "" ),
          Volume( 1.0f ),
          Pitch( 1.0f ),
          Priority( 0 ),
          PriorityFactor( 1.0f ),
          bAffectedByMusic( false ) {
        /* FUN_004051d0 */
    }
};

struct Mixer {
    char pName[32];
    Mixer *pOutputMixer; // if null goes straight to the master output
    float Volume;
    bool bEnabled;
    bool bMuted;
};

struct ClientHandler {
    AudioClient *pClient = nullptr;
    int32_t RefCount = 0;
};

struct ClientConfig {
    char pName[32] = {};
    char pMixerName[32] = {};
    char pOutputName[32] = {};
    float Volume = 1.0f;
    float Pitch = 1.0f;
    int32_t PriorityRank = 0;
    float PriorityFactor = 1.0f;

    bool bAffectedByMusic = false;
    bool bPaused = true;
    bool bMuted = false;
};

struct MixPreset {
    char pName[32] = {};
    uint32_t MixState = 0;
};

class AudioClient {
public:
    static constexpr float kDefaultMinSeparation = 0.05f; // DAT_00f41778
    static constexpr float kDefaultSourceWeight = 1.0f;

public:
    inline const ClientConfig *getConfig() const { return pConfig; }
    inline Mixer *getMixer() const { return pMixer; }
    inline edAudioObject *getRoot() const { return pRoot; }
    inline bool isPositional() const { return bPositional; }
    inline SpatialClustering &getClustering() { return Clustering; }
    inline edAudioCluster *getCluster( int32_t index ) {
        return pClusters[index].get();
    }
    inline int32_t getAudibleSourceCount() const { return AudibleSourceCount; }
    inline int32_t getMutedSourceCount() const { return MutedSourceCount; }
    inline void setDuckGain( float gain ) { DuckGain = gain; }
    inline float getDuckGain() const { return DuckGain; }
    inline float getSmoothedVolume() const { return SmoothedVolume; }

public:
    AudioClient( const ClientConfig *pInConfig,
                 Mixer *pInMixer,
                 edAudioObject *pParent,
                 bool bInPositional );
    ~AudioClient();

    void rebuildSpatialGraph();
    AudioDispatcherPlaybackSoundInstance *acquireSoundInstance();
    bool sweepInstances();
    float resolveVolume();

private:
    edAudioCluster *findNearestFreeCluster( const Eigen::Vector3f &probe,
                                            int32_t clusterCount );

    static void
    DestroySoundInstance( AudioDispatcherPlaybackSoundInstance *pInstance );

private:
    const ClientConfig *pConfig;
    Mixer *pMixer;

    float DuckGain;
    float SmoothedVolume = 0.0f;
    bool bPositional;

    edAudioObject *pParentBuffer;
    edAudioObject *pRoot;
    SpatialClustering Clustering;
    std::unique_ptr<edAudioCluster> pClusters[SpatialClustering::kMaxClusters];
    AudioDispatcherPlaybackSoundInstance *pInstances = nullptr;
    edAudioListener CachedListener;

    int32_t AudibleSourceCount = 0;
    int32_t MutedSourceCount = 0;
};

class PipelineMono {
public:
    inline edAudioBuffer *getBuffer() { return pBuffer; }

public:
    PipelineMono();
    ~PipelineMono();

    bool create( edAudioObject *pParent );
    void destroy();

private:
    edAudioObject *pRoot;
    edAudioChannelAdapter *pAdapter;
    edAudioBuffer *pBuffer;
};

class GSAudio : public GameSystem {
public:
    void *pMemory;
    int32_t MemorySize;

    int32_t NbSpeakers;
    float OutputSpatialFactor;
    float OutputSpatialFactor2; // TODO: Why did they duplicate this?
    float OutputDopplerFactor;
    float OutputDopplerFactor2; // TODO: Why did they duplicate this?

    std::vector<Mixer> MixerPool;
    std::vector<ClientConfig> ClientConfigPool;
    std::vector<ClientHandler> ClientHandlerPool;
    std::vector<std::unique_ptr<AudioClient>> Clients;

    edAudioBuffer *pSpatialBuffer;
    edAudioMixer *pSpatialMixer;
    edAudioBuffer *pSpatialOutput;

    edAudioChannelAdapter *pSpatialAdapter = nullptr;
    bool bListenerSyncToCamera = true;

public:
    const char *getName() const override { return "[GSAudio]"; }
    inline bool isListenerSyncedToCamera() const {
        return bListenerSyncToCamera;
    }
    inline uint32_t getMixState() const { return MixState; }
    inline void setMusicMuted( bool bMuted ) { bMusicMuted = bMuted; }

public:
    GSAudio();
    ~GSAudio();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void syncListenerToActiveCamera();
    void terminate() override;

#ifdef OTDU_USE_IMGUI
    void drawDebug( float deltaTime ) override;
#endif

    void addMixer( ParsedAudioMixer &mixer );
    void addReverb( ParsedAudioReverb &reverb );
    void addClient( ParsedAudioClient &client );
    void setGameModeMix( const char *pGameModeName, const char *pPresetName );
    void addMixPreset( const char *pName,
                       const std::vector<std::string> &mixList );
    uint32_t getMixFlags( const char *pName ) const;
    void changeMixState( uint32_t mixState );
    Mixer *findMixer( const char *pName );
    ClientConfig *getClientConfig( const char *pName );
    AudioClient *createClient( const char *pName );
    void registerClient( AudioClient *pClient );
    ClientHandler *findClientHandler( const char *pName );
    ClientHandler *findClientHandler( const AudioClient *pClient );

private:
    static constexpr int32_t kNoDuckPriorityRank = 100;

private:
    std::vector<MixPreset> MixPresets;
    uint32_t GameModeMixState[kMaxGameModes] = {};

    int32_t GameMode = -1;
    uint32_t MixState = 0;
    bool bMixStateTransitionSync = false;
    bool bMusicMuted = false;
    bool bMusicMuteApplied = false;

private:
    void updateClientPriorities();
};

extern GSAudio *gpAudio;
