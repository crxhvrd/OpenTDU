#pragma once

#include <vector>

#include "game/gs_base.h"
#include "game/gm_base.h"
#include "game_intro_tutorial.h"

#include "game/scenaric/scenaric_bank.h"
#include "game/scenaric/local_player.h"
#include "game/scenaric/avatar_manager.h"
#include "game/scenaric/spots.h"
#include "game/scenaric/ik_offset_db.h"
#include "game/scenaric/extra_challenge_cine_bank.h"

class MngActor;
#include "game/scenaric/event_parameters.h"

#include "core/random/bcpl.h"

class MngHud;
class CarState;
class EntityDriver;
class EntityAvatarPassenger;
class EntityPassengerGoodies;
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

struct NextIntroInfo {
    bool bIsActive = false;
    const char *pPath = nullptr;
    const char *pFilename = nullptr;
    const char *pCinematicName = nullptr;
    eGameMode NextGameMode = GM_Invalid;
    struct SpotMap *pSpot = nullptr;
};

class CinematicPlayer {
public:
    virtual bool
    executeAction( int32_t param_2, void *param_3, void *pData = nullptr );
};

static constexpr int32_t kNumICEChallengers = 16;

// Instant challenge route data shared with the opponent (S_IC_DATAS).
struct ICRoute {
    Eigen::Vector4f StartPosition = Eigen::Vector4f::Zero();
    int32_t field_0x10 = 0;
    int32_t OwnerPlayerIndex = 0;
    int32_t Stake = 0;
};

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

// Drive-in challenge selected from a drive-in spot.
struct DriveInData {
    bool bIsValid = false;
    bool bIsValidToo = false;
    SpotMap *pSpot = nullptr;
    int32_t ID = -1;
    int32_t Fee = 0;
    char pName[65] = {};
    int32_t Pop = 0;
    uint32_t Duration = 0u;
    uint32_t Score = 0u;
    uint32_t CurrentScore = 0u;
    uint32_t Car = 0u;
    uint32_t Driver = 0u;
    uint32_t Theme = 0u;
    uint32_t pCheckpoints[100] = {};
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
    inline void setTimeLeftForRental( const float time ) { rentalTimer = time; }
    inline SpotMap *getRentalSpot() const { return pRentalSpot; }
    inline void setRentalSpot( SpotMap *pSpot ) { pRentalSpot = pSpot; }
    inline void setMailboxUpdated( const bool bUpdated ) {
        bMailboxUpdated = bUpdated;
    }
    inline Eigen::Vector4f getDefaultStartPosition() const {
        // HACK: REMOVE ME: VEHICLE PHYSICS TEST
        if ( startPosition.head<3>().squaredNorm() < 1.0f ) {
            return kDefaultPosition;
        }
        return startPosition;
    }
    inline bool hasDefaultStartPosition() const {
        return startPosition.x() != 0.0f;
    }
    inline LocalPlayer &getLocalPlayer() { return localPlayer; }
    inline void setObjectiveSpotHashcode( const uint64_t hashcode ) {
        objectiveSpotHashcode = hashcode;
    }
    inline ObjectiveMarker &getObjective( const int32_t index ) {
        return objectives[index];
    }
    inline Spots &getSpotList() { return spotList; }
    inline int32_t getCurrentSpotIndex() const { return currentSpotIndex; }
    inline void setCurrentSpotIndex( const int32_t index ) {
        currentSpotIndex = index;
    }
    // FUN_008031f0
    void setNextIntro( const char *pPath,
                       const char *pFilename,
                       const char *pCinematicName,
                       eGameMode nextGameMode,
                       SpotMap *pSpot );
    inline NextIntroInfo &getNextIntro() { return nextIntro; }
    inline ICEChallenger *getICEChallengers() { return iceChallengers; }
    inline void removeRefusedICEChallengers() {
        // FUN_00803050
        for ( ICEChallenger &challenger : iceChallengers ) {
            if ( challenger.bRefusedICE ) {
                challenger.ID = ICEChallenger::kNoChallenger;
            }
        }
    }
    inline AvatarManager &getAvatarManager() { return avatarManager; }
    inline SCIKOffsetDB &getIKOffsetDB() { return ikOffsetDB; }
    inline ScenaricBank &getPedestrianBank() { return pedestrianBank; }
    inline ScenaricBank &getSpotMeshBank() { return spotMeshBank; }
    inline ScenaricBank &getWorldLifeSetBank() { return worldLifeSetBank; }
    inline ScenaricBank &getWorldActorsBank() { return worldActorsBank; }

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
    bool loadPackBank( const char *pBankFolder,
                       const char *pBankName,
                       ScenaricBank *pBank );
    inline EventParameters &getSelectedEventParameters() {
        return selectedEventParameters;
    }

    void prepareWorldEntityTemplates();
    void installLevelWorldSpots();

    bool placeDriver( CarState *pCarState, EntityDriver *pDriver );
    bool placePassenger( CarState *pCarState,
                         EntityAvatarPassenger *pPassenger );
    bool unloadPassenger( CarState *pCarState,
                          EntityAvatarPassenger *pPassenger );
    bool placeCargo( CarState *pCarState, EntityPassengerGoodies *pCargo );
    bool unloadCargo( CarState *pCarState, EntityPassengerGoodies *pCargo );

    void loadPacks();
    void unloadPacks();

    Entity *findEntityByHashcode( uint64_t hashcode ) const;
    SoundBinary *getSCTDSound( uint64_t hashcode ) const;
    void *findWavData( uint64_t hashcode, int32_t *pOutSize = nullptr ) const;
    Entity *getSCVolume( uint64_t hashcode ) const;
    Path *getSCPath( uint64_t hashcode ) const;
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
    inline int32_t getNumExtraChallenges() const { return numExtraChallenges; }
    inline Entity *getActiveExtraChallenge() const {
        return pActiveExtraChallenge;
    }
    inline void setActiveExtraChallenge( Entity *pChallenge ) {
        pActiveExtraChallenge = pChallenge;
    }
    Entity *findExtraChallenge( int32_t index );
    int32_t countExtraChallenges( const eEntityType kind ) const;
    int32_t countGoldExtraChallenges( const eEntityType kind ) const;
    inline ScenaricBank *getWorldExtraChallengesBank() {
        return &worldExtraChallengesBank;
    }
    inline ScenaricBank *getWorldScenaricBank() { return &worldScenaricBank; }
    inline ScenaricBank &getCinematicPackBank() { return cinematicPackBank; }
    inline ScenaricBank *getWorldScenaricBank() const {
        return const_cast<ScenaricBank *>( &worldScenaricBank );
    }

    Render3DD *findRender3DD( uint64_t hashcode );
    Render3DG *findRender3DG( uint64_t hashcode );
    Render2DM *findRender2DM( uint64_t hashcode );
    Render2DB *findRender2DB( uint64_t hashcode );
    SkeletonInstance *findSkeleton( uint64_t hashcode );
    ClipInstance *findClip( uint64_t hashcode );
    AnimStateInstance *findAnimState( uint64_t hashcode );

    SkeletonInstance *resolveSkeleton( ScenaricBank *pBank,
                                       void *pRefSkeleton );
    ClipInstance *resolveClip( ScenaricBank *pBank, void *pRefAnm );
    AnimStateInstance *resolveAnimState( ScenaricBank *pBank, void *pRefBas );

    void initWoosh();
    void terminateWoosh();
    bool addWoosh( const Eigen::Vector3f &position, const void *pOwner );
    bool removeWoosh( const void *pOwner );
    void updateWooshVoices( float listenerSpeed );

    void setCinematicViewerDefault( const std::string &param_1,
                                    const std::string &param_2 );
    inline const CinematicViewerConfig &getCinematicViewerDefault() const {
        return cineViewConfig;
    }
    void setDemoTimer( float param_1 );
    inline float getDemoTimer() const { return demoTimer; }
    inline bool isDemoModeActive() const { return bDemoMode; }
    inline void setDemoModeActive( const bool value ) { bDemoMode = value; }
    inline bool isFromDriveIn() const { return bIsFromDriveIn; }
    inline void setFromDriveIn( const bool value ) { bIsFromDriveIn = value; }
    inline DriveInData &getDriveInData() { return driveInData; }
    inline void setTrainingLevel( const bool value ) { bTrainingLevel = value; }
    inline bool isTrainingLevel() const { return bTrainingLevel; }
    inline bool isInstantChallenge() const { return bInstantChallenge; }
    inline void setInstantChallenge( const bool value ) {
        bInstantChallenge = value;
    }
    inline const Eigen::Vector4f &getGMEStartPosition() const {
        return gmeStartPosition;
    }
    inline void setGMEStartPosition( const Eigen::Vector4f &position ) {
        gmeStartPosition = position;
    }
    inline ICRoute &getICDatas() { return icDatas; }
    inline bool isTestedLevel() const { return bTestedLevel; }
    inline void setTestedLevel( const bool value ) { bTestedLevel = value; }
    inline bool isModifiedIGE() const { return bModifiedIGE; }
    inline void setModifiedIGE( const bool value ) { bModifiedIGE = value; }
    inline bool isFromGameInvite() const { return bFromGameInvite; }
    inline void setFromGameInvite( const bool value ) {
        bFromGameInvite = value;
    }
    inline void setLobbyMapOpened( const bool value ) {
        bLobbyMapOpened = value;
    }
    inline bool isLobbyMapOpened() const { return bLobbyMapOpened; }
    inline void **getIGEBufferPointer() { return &pIGEBuffer; }
    inline const uint8_t *getIGEBuffer() const {
        return static_cast<const uint8_t *>( pIGEBuffer );
    }
    inline const uint8_t *getICIGEBuffer() const { return pICIGEBuffer; }
    inline uint32_t getICIGESize() const { return icIGESize; }
    inline int32_t getICELocalPlayerIndex() const {
        return iceLocalPlayerIndex;
    }
    inline const uint8_t *getTestIGEBuffer() const { return pTestIGEBuffer; }
    inline uint32_t getTestIGESize() const { return testIGESize; }
    inline int32_t getTestedScore() const { return testedScore; }
    inline void setTestedScore( const int32_t score ) { testedScore = score; }
    inline int32_t getClubChallengeKind() const { return clubChallengeKind; }
    inline void setClubChallengeKind( const int32_t value ) {
        clubChallengeKind = value;
    }
    inline ScenaricBank *getCheckpointBank() const { return pCheckpointBank; }
    inline void setCheckpointBank( ScenaricBank *pBank ) {
        pCheckpointBank = pBank;
    }
    inline bool wasMapExited() const { return bWasMapExited; }
    inline void setWasMapExited( const bool value ) { bWasMapExited = value; }
    void startDemoMode();
    void delockChallengeRoads( const bool bDelocked );
    inline bool areChallengeRoadsDelocked() const {
        return bDelockChallengesRoad;
    }
    void setTraceSize( uint32_t param_1 );
    void setDefaultIGE( const char *param_1 );
    void releaseIGEBuffer();
    void releaseTestIGEBuffer();
    void requestTestIGE( MngActor *pMngActor );
    void requestICIGE( MngActor *pMngActor );
    void releaseICIGEBuffer();
    void pushObjectivesToCompass( MngHud *pHud );

private:
    static constexpr int32_t kMaxNumWoosh = 64;

private:
    GameIntroTutorial introTutorial;
    EventParameters selectedEventParameters; // DAT_011f30f8
    LocalPlayer localPlayer;
    AvatarManager avatarManager;
    SCIKOffsetDB ikOffsetDB;
    Spots spotList;
    CinematicViewerConfig cineViewConfig;
    std::string defaultIGE;

    Eigen::Vector4f startPosition;
    bool bDemoMode;
    float demoTimer;
    uint32_t demoTimeEnd;
    uint32_t traceSize;
    uint32_t currentDefVersion;
    bool bWasMapExited;                                         // DAT_01214740
    int32_t currentSpotIndex;                                   // DAT_01214744
    bool bIsFromDriveIn;                                        // DAT_012162c4
    DriveInData driveInData;                                    // 0x12162c8
    bool bMailboxUpdated = false;                               // DAT_012164de
    bool bTrainingLevel = false;                                // DAT_01214748
    bool bInstantChallenge = false;                             // DAT_01214750
    bool bTestedLevel = false;                                  // DAT_01214749
    Eigen::Vector4f gmeStartPosition = Eigen::Vector4f::Zero(); // 0x12146b0
    ICRoute icDatas;                                            // 0x1214760
    bool bModifiedIGE = false;                                  // DAT_0120bdd8
    bool bFromGameInvite = false;                               // DAT_012164dc
    bool bLobbyMapOpened = false;                               // DAT_012164dd
    int32_t clubChallengeKind = 0; // DAT_012164e4 (0: none, 1/2: club)
    ScenaricBank *pCheckpointBank = nullptr; // DAT_012147b8
    void *pIGEBuffer = nullptr;              // DAT_0120bdf0
    uint32_t testIGESize = 0u;               // DAT_0120bddc
    uint8_t *pTestIGEBuffer = nullptr;       // DAT_0120bde0
    int32_t testedScore = 0;                 // DAT_0121474c
    uint32_t icIGESize = 0u;                 // DAT_0120bde4
    uint8_t *pICIGEBuffer = nullptr;         // DAT_0120bde8
    int32_t iceLocalPlayerIndex = -1;        // DAT_01214920
    NextIntroInfo nextIntro;
    Entity *pActiveExtraChallenge; // DAT_01214948
    int32_t numExtraChallenges;    // DAT_0121494c

    float rentalTimer;    // DAT_01214928
    SpotMap *pRentalSpot; // DAT_0121492c
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
