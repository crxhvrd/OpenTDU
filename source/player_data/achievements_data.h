#pragma once

#include "achievement_checks.h"

class edStore;
struct DBEntryAchievement;

class AchievementsData {
public:
    static constexpr uint64_t kSimuModeRewardHashcode = 0x5a9940ull;
    static constexpr int32_t kNumAchievementFlags = 0x96;
    static constexpr int32_t kNumRewardCars = 8;
    static constexpr int32_t kNumPlayerLevels = 5;
    static constexpr uint32_t kAchievementDatabaseIndex = 15u;
    static constexpr uint32_t kBotDatabaseIndex = 14u;
    static constexpr uint32_t kNoPendingAsyncNotification = 0x80000000u;

public:
    inline int32_t getNumAchievements() const {
        return int32_t( achievements.size() );
    }
    inline bool isReady() const { return bReady; }
    inline void setReady( const bool bIsReady ) { bReady = bIsReady; }
    inline bool getField516_0x204() const { return field516_0x204; }
    inline void setField516_0x204( const bool bValue ) {
        field516_0x204 = bValue;
    }
    inline void setQuiet( const bool bIsQuiet ) { bQuiet = bIsQuiet; }

    inline bool isSimulationModeUnlocked() const {
        return bSimulationModeUnlocked;
    }
    inline bool isReward0x5912a0Unlocked() const {
        return bReward0x5912a0Unlocked;
    }
    inline int32_t getCurrentGameMode() const { return currentGameMode; }
    inline void setCurrentGameMode( const int32_t gameMode ) {
        currentGameMode = gameMode;
    }

    inline bool getAchievementFlag( const int32_t index ) const {
        return ( index >= 0 && index < kNumAchievementFlags )
                   ? bAchievementFlags[index]
                   : false;
    }

    uint32_t getNumCarsTuned() const;                   
    uint32_t getNumInstantChallengesWonOnline() const;  
    uint32_t getNumInstantChallengesWonOffline() const; 
    uint32_t getNumCustomChallengesUploaded() const;    
    uint32_t getNumCustomChallengesWon() const;        
    uint32_t getNumETradeTransactions() const;          

public:
    AchievementsData();
    ~AchievementsData();

    void reset();
    void build();
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;
    void tick();
    bool notify( const uint32_t eventMask, const bool bAsync );

    void unlockAllChecks();

    bool isRewardLocked( const uint64_t rewardHashcode ) const;
    uint32_t getAchievedPoints() const;
    int32_t getPlayerLevel() const;

private:
    void buildCheck( const DBEntryAchievement &entry );
    void releaseChecks();
    bool refreshCheck( AchievementCheck *pCheck,
                       const uint32_t eventMask,
                       const bool bQuiet );
    void grantReward( const AchievementCheck *pCheck );

    void processAsyncRequest();
    void deserializeChecks( edStore &stream );
    void serializeChecks( edStore &stream ) const;

private:
    std::vector<AchievementCheck *> achievements;
    bool bMustLevelUp;
    int32_t currentGameMode;
    bool field516_0x204;
    bool bReady;
    uint32_t flagAsync;
    // TEA encrypted, in retail layout order.
    uint32_t pNumCarsTuned[2];                   
    uint32_t pNumInstantChallengesWonOnline[2];  
    uint32_t pNumInstantChallengesWonOffline[2]; 
    uint32_t pNumCustomChallengesUploaded[2];    
    uint32_t pNumCustomChallengesWon[2];         
    uint32_t pNumETradeTransactions[2];          
    int32_t exploredRoadLength;
    int32_t totalRoadLength;
    bool bAchievementFlags[kNumAchievementFlags];  
    uint32_t pRewardCarsBought[kNumRewardCars][2]; // TEA encrypted
    bool bDisplayLevelUp;
    bool bQuiet;
    bool bReward0x5912a0Unlocked; 
    bool bSimulationModeUnlocked; 
    uint32_t pointsNeeded[kNumPlayerLevels];
};
