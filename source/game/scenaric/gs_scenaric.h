#pragma once

#include <vector>

#include "game/gs_base.h"
#include "game_intro_tutorial.h"

#include "game/scenaric/scenaric_bank.h"
#include "game/scenaric/local_player.h"
#include "game/scenaric/avatar_manager.h"
#include "game/scenaric/spots.h"
#include "game/scenaric/ik_offset_db.h"
#include "game/scenaric/extra_challenge_cine_bank.h"

#include "core/random/bcpl.h"

class MngHud;
class CarState;
class EntityDriver;
class AudioClient;
class AudioDispatcherPlaybackSoundInstance;
class edAudioSample;

// This is used as a safe fallback to initialize player start pos (points to
// south lighthouse)
static const Eigen::Vector4f kDefaultPosition = { 48783.555f, 32.11759f,
                                                  52610.99f, 1.0f };

struct CinematicViewerConfig {
    std::string BankName = "";
    std::string CinematicName = "";
};

// HUD compass target
struct ObjectiveMarker {
    Eigen::Vector4f Position = Eigen::Vector4f::Zero();
    uint64_t NetPlayerID = 0;
    const char *Type = nullptr;
};

class CinematicPlayer {
public:
    virtual bool executeAction( int32_t param_2, void *param_3 );
};

static constexpr int32_t kNumICEChallengers = 16;

struct ICEChallenger {
    static constexpr uint32_t kNoChallenger = 0xffffffffu;

    bool bBot;
    uint32_t ID =
        kNoChallenger; // P2P player index, or the bot id when bBot is set.
    bool bRefusedICE;
    bool bAcceptedICE;

    inline void reset() {
        ID = kNoChallenger;
        bRefusedICE = false;
        bAcceptedICE = false;
        bBot = false;
    }
};

static constexpr float kWooshNoListener = -1.0f; // FLOAT_00f41664
struct WooshVoice {
    AudioDispatcherPlaybackSoundInstance *pSound = nullptr;
    const void *pOwner = nullptr;
};

class GSScenaric : public GameSystem {
public:
    const char *getName() const override { return "Service : Scenaric"; }

    inline bool isICEnabled() const { return bInstantChallengeEnabled; }
    inline void setICEnabled( bool param_1 ) {
        bInstantChallengeEnabled = param_1;
    }
    inline uint32_t getVersion() const { return currentDefVersion; }
    inline void setAssetsVersion( uint32_t version ) {
        currentDefVersion = version;
    }
    inline RandomGen &getRandomGenerator() { return randomGenerator; }
    inline float getTimeLeftForRental() const { return rentalTimer; }
    inline Eigen::Vector4f getDefaultStartPosition() const {
        // HACK: REMOVE ME: VEHICLE PHYSICS TEST
        if ( startPosition.head<3>().squaredNorm() < 1.0f ) {
            return kDefaultPosition;
        }
        return startPosition;
    }
    inline LocalPlayer &getLocalPlayer() { return localPlayer; }
    inline ObjectiveMarker &getObjective( const int32_t index ) {
        return objectives[index];
    }
    inline Spots &getSpotList() { return spotList; }
    inline ICEChallenger *getICEChallengers() { return iceChallengers; }
    inline AvatarManager &getAvatarManager() { return avatarManager; }
    inline SCIKOffsetDB &getIKOffsetDB() { return ikOffsetDB; }
    inline ScenaricBank &getPedestrianBank() { return pedestrianBank; }
    inline ScenaricBank &getSpotMeshBank() { return spotMeshBank; }
    inline ScenaricBank &getWorldLifeSetBank() { return worldLifeSetBank; }

public:
    GSScenaric();
    ~GSScenaric();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void terminate() override;
    void draw( float deltaTime ) override;
    void pause() override;
    void reset() override;

    GameIntroTutorial &getIntroTutorial();
    void setDefaultStartPosition( const Eigen::Vector4f &position );
    void clearObjectives();

    static void LoadScenaricBank( const char *pBankFolder,
                                  const char *pBankName,
                                  ScenaricBank *pOutBank,
                                  bool bNoCopy = false );

    void prepareWorldEntityTemplates();

    bool placeDriver( CarState *pCarState, EntityDriver *pDriver );

    void loadPacks();
    void unloadPacks();

    Entity *findEntityByHashcode( uint64_t hashcode ) const;
    SoundBinary *getSCTDSound( uint64_t hashcode ) const;
    void *findWavData( uint64_t hashcode, int32_t *pOutSize = nullptr ) const;
    Entity *getSCVolume( uint64_t hashcode ) const;
    void registerBank( ScenaricBank *pBank );
    void unregisterBank( ScenaricBank *pBank );
    void unloadBank( ScenaricBank *pBank );
    ScenaricBank *getNewSpotBank();

    inline ScenaricBank *getBoatBank() { return &boatBank; }
    inline ScenaricBank *getBoatsPathsBank() { return &boatsPathsBank; }

    inline ScenaricBank *getAircraftBank() { return &aircraftsBank; }
    inline ScenaricBank *getAircraftsPathsBank() { return &aircraftsPathsBank; }

    // All registered scenaric banks (used by the debug entity-zoo to enumerate
    // every loaded entity template).
    inline const std::vector<ScenaricBank *> &getRegisteredBanks() const {
        return registeredBanks;
    }
    inline ExtraChallengeCineBank &getExtraChallengesCineBank() {
        return extraChallengesCineBank;
    }
    inline ScenaricBank *getWorldScenaricBank() { return &worldScenaricBank; }
    inline ScenaricBank *getWorldScenaricBank() const {
        return const_cast<ScenaricBank *>( &worldScenaricBank );
    }

    Render3DD *findRender3DD( uint64_t hashcode );
    Render3DG *findRender3DG( uint64_t hashcode );
    Render2DM *findRender2DM( uint64_t hashcode );
    Render2DB *findRender2DB( uint64_t hashcode );
    SkeletonInstance *findSkeleton( uint64_t hashcode );
    ClipInstance *findClip( uint64_t hashcode );

    SkeletonInstance *resolveSkeleton( ScenaricBank *pBank,
                                       void *pRefSkeleton );
    ClipInstance *resolveClip( ScenaricBank *pBank, void *pRefAnm );

    void initWoosh();
    void terminateWoosh();
    bool addWoosh( const Eigen::Vector3f &position, const void *pOwner );
    bool removeWoosh( const void *pOwner );
    void updateWooshVoices( float listenerSpeed );

    void setCinematicViewerDefault( const std::string &param_1,
                                    const std::string &param_2 );
    void setDemoTimer( float param_1 );
    void delockChallengeRoads();
    void setTraceSize( uint32_t param_1 );
    void setDefaultIGE( const char *param_1 );

private:
    static constexpr int32_t kMaxNumWoosh = 64;

private:
    void pushObjectivesToCompass( MngHud *pHud );

    GameIntroTutorial introTutorial;
    LocalPlayer localPlayer;
    AvatarManager avatarManager;
    SCIKOffsetDB ikOffsetDB;
    Spots spotList;
    CinematicViewerConfig cineViewConfig;
    std::string defaultIGE;

    Eigen::Vector4f startPosition;
    float demoTimer;
    uint32_t traceSize;
    uint32_t currentDefVersion;
    int32_t extraRewardsCineCount;

    float rentalTimer;
    uint64_t objectiveSpotHashcode;
    ObjectiveMarker objectives[6];

    ICEChallenger iceChallengers[kNumICEChallengers];

    uint8_t bDelockChallengesRoad : 1;
    uint8_t bInstantChallengeEnabled : 1;

    // NOTE: The original uses derived bank types for a few of these
    // (aircrafts/boats/spot-mesh); they are plain
    // ScenaricBank here until those subclasses are implemented
    ExtraChallengeCineBank extraChallengesCineBank;

    ScenaricBank worldScenaricBank;        // scworld
    ScenaricBank cinematicPackBank;        // pack_cinematiks
    ScenaricBank worldExtraChallengesBank; // extrachallenges
    ScenaricBank pedestrianBank;           // scenaric_pedestrian
    ScenaricBank aircraftsBank;            // aircrafts
    ScenaricBank boatBank;                 // boats
    ScenaricBank aircraftsPathsBank;       // world_aircrafts
    ScenaricBank boatsPathsBank;           // world_boats
    ScenaricBank spotMeshBank;             // pack_spots
    ScenaricBank worldActorsBank;          // worldactors
    ScenaricBank worldLifeSetBank;         // worldlifeset

    // Streamed spot exterior banks — one per SpotStream cell (pSpotsBanks[3]).
    ScenaricBank spotBanks[3];

    std::vector<ScenaricBank *> registeredBanks;

    RandomGen randomGenerator;
    float wooshMinSpeed = 0.0f;
    float wooshMaxSpeed = 0.0f;
    float wooshClipNear = 0.0f;
    float wooshClipFar = 0.0f;
    float wooshRefraction = 0.0f;
    float wooshDoppler = 0.0f;
    void *pWooshWavData = nullptr;
    bool bOwnsWooshWavData = false;
    AudioClient *pWooshAudioClient = nullptr;
    std::array<WooshVoice, kMaxNumWoosh> wooshVoices;
    std::unique_ptr<edAudioSample> wooshSample;
};

extern GSScenaric *gpScenaric;

bool IsWooshImpostor( uint64_t instanceHashcode, int32_t gridIndex );

extern std::vector<uint64_t> gRemovedSpots;
extern bool gbWooshEnabled;
extern bool gbDisplayWooshDebug;
