#pragma once

struct AchievementCheck {
    bool bLock = true;
    int32_t Flags = 0;
    bool bBinaryAchievement = false;
    uint32_t NumPoints = 0u;
    uint32_t NumItemsReq = 0u;
    uint64_t TileHashcode = 0ull;
    uint64_t TextHashcode = 0ull;
    uint64_t RewardHashcode = 0ull;
    int32_t RewardData = 0;
};

class AchievementsData {
public:
    static constexpr uint64_t kSimuModeRewardHashcode = 0x5a9940ull;

public:
    inline int32_t getNumAchievements() const {
        return int32_t( achievements.size() );
    }
    inline bool isReady() const { return bReady; }
    inline void setReady( const bool bIsReady ) { bReady = bIsReady; }
    inline bool getField516_0x204() const { return field516_0x204; }
    inline int32_t getCurrentGameMode() const { return currentGameMode; }
    inline void setCurrentGameMode( const int32_t gameMode ) {
        currentGameMode = gameMode;
    }

public:
    AchievementsData();
    ~AchievementsData();

    void reset();
    bool isRewardLocked( const uint64_t rewardHashcode ) const;
    uint32_t getAchievedPoints() const;
    int32_t getPlayerLevel() const;

private:
    std::vector<AchievementCheck *> achievements;
    bool bMustLevelUp;
    int32_t currentGameMode;
    bool field516_0x204;
    bool bReady;
    uint32_t flagAsync;
    int32_t numCustomUploaded;
    int32_t numCustomWon;
    int32_t numSoldBoughtEbay;
    int32_t exploredRoadsLength;
    int32_t totalRoadsLength;
    uint32_t rewardCarsBought[8];
    bool bDisplayLevelUp;
    bool bQuiet;
    uint32_t pointsNeeded[5];
};
