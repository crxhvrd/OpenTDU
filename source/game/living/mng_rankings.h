#pragma once

#include "game/mng_base.h"
#include "game/map/map_screenshot_bank.h"
#include "game/scenaric/mng_spots.h"

#include "flash_ranking_table.h"

struct FlashMessage;
struct FlashRECT;
class DrawList;
class Entity;
class FlashPlayer;
class FlashResource;
class GMLiving;
class MngFlash;
struct Material;

// Leaderboard filters (+ the movie they are read from)
struct RankingsFilter {
    MngRankings *pOwner = nullptr;
    bool bLevel = false;
    bool bCountry = false;
    bool bFriends = false;

    FlashPlayer *getPlayer() const;
};

struct SoloChallengeStats {
    int32_t NumTotal = 0;
    int32_t NumDone = 0;
    int32_t NumGold = 0;
    int32_t NumSilver = 0;
    int32_t NumBronze = 0;
};

// Rankings/leaderboard panel
class MngRankings : public Manager {
public:
    static constexpr int32_t kNumSoloRanks = 3;
    static constexpr int32_t kNumStandardRanks = 5;
    static constexpr int32_t kNumClubRanks = 2;
    static constexpr int32_t kNumMissionRanks = 4;
    static constexpr int32_t kNumOnlineChallengeRanks = 3;

    static constexpr int32_t kMaxTimeChallenges = 50;
    static constexpr int32_t kMaxSpeedChallenges = 30;
    static constexpr int32_t kMaxRaceChallenges = 50;
    static constexpr size_t kChallengeNameLength = 0x80;

public:
    const char *getName() const override {
        // FUN_0042d740
        return "Rankings";
    }
    inline void setGameMode( GMLiving *pGameMode ) { pOwner = pGameMode; }
    inline FlashPlayer *getMovie() const { return pMovie; }
    inline MngFlash *getMngFlash() const { return pMngFlash; }
    inline void setActiveTable( FlashRankingTable *pTable ) {
        pActiveTable = pTable;
    }
    inline bool isListLengthForced() const { return bForceListLength; }
    inline void setRankingsLoading( const bool bLoading ) {
        bRankingsLoading = bLoading;
    }
    inline bool isBackTransitionPending() const { return backCounter > 0; }
    inline void setMissionsOpen( const bool bOpen ) { bMissionsOpen = bOpen; }
    inline void invalidateLureMission() { pLureMission = kNoLureMission; }
    inline FlashRankingTable &getMissionsTable( const int32_t index ) {
        return missionRanks[index];
    }

public:
    MngRankings();
    ~MngRankings();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float, float ) override;
    void reset() override;
    void pause() override;
    void terminate() override;

    bool onMessage( FlashMessage *pMessage );

    void registerActiveTableEloRanks();
    void getOnlineTables( const int32_t category,
                          FlashRankingTable **ppOutEloTable,
                          FlashRankingTable **ppOutChallengeTable );
    const SoloChallengeStats &getSoloChallengeStats( const int32_t category );
    const char *getSoloChallengeName( const int32_t category,
                                      const int32_t index ) const;

private:
    enum eSoloChallengeKind : int32_t {
        SCK_Time = 0,
        SCK_Speed = 1,
        SCK_Race = 2,
        SCK_Count
    };

    static Entity *const kNoLureMission;

private:
    RankingsFilter filter;
    FlashPlayer *pMovie;
    GMLiving *pOwner;
    MngFlash *pMngFlash;
    FlashRankingTable *pActiveTable;

    FlashRankingTableLocal soloRanks[kNumSoloRanks];
    FlashRankingTableOnline standardRanks[kNumStandardRanks];
    FlashRankingTableOnline clubRanks[kNumClubRanks];
    FlashRankingTableMissions missionRanks[kNumMissionRanks];
    FlashRankingTableOnline eloRanks[kNumOnlineChallengeRanks];
    FlashRankingTableOnline challengeRanks[kNumOnlineChallengeRanks];

    bool bLeaderboardsRequested;
    bool bClubRankRequested;
    bool bRankingsLoading;
    bool bMissionsOpen;
    bool bForceListLength;
    int32_t backCounter;

    uint32_t numSpotsToScan;
    uint32_t scanSpotIndex;
    bool bScanningSpots;
    bool bWaitPopupDisplayed;
    bool bStatisticsBuilt;

    SoloChallengeStats soloStats[SCK_Count];
    char timeChallengeNames[kMaxTimeChallenges][kChallengeNameLength];
    char speedChallengeNames[kMaxSpeedChallenges][kChallengeNameLength];
    char raceChallengeNames[kMaxRaceChallenges][kChallengeNameLength];
    int32_t numSpeedChallengesMulti;
    int32_t numSpeedChallengesMultiDone;
    int32_t numRaceChallengesMulti;
    int32_t numRaceChallengesMultiDone;

    Material *pLurePicture;
    Entity *pLureMission;
    bool bLureReloadPending;
    bool bLureDrawnThisFrame;
    MapScreenshotBank lureBank;
    SpotScreensBank fallbackLureBank;

private:
    void setTablesRegistered(
        std::initializer_list<FlashRankingTable *> tablesToRemove,
        std::initializer_list<FlashRankingTable *> tablesToAdd );
    void buildStatistics();
    void displayWaitPopup();
    void hideWaitPopup();
    void scanSpots();
    void publishSoloChallengeStats();
    void resetOnlineRankFields();
    void updateLurePicture();
    void loadLurePicture();
    void onLurePictureLoaded();
    void loadFallbackLurePicture();

    static bool OnLurePictureLoaded( MapScreenshotBank *pBank,
                                     void *pUserData );
    static void LurePictureDrawCallback( FlashRECT *pViewport,
                                         float depth,
                                         DrawList *pDrawList,
                                         void *pUserData,
                                         FlashResource *pResource,
                                         const char *pClipName );
};
