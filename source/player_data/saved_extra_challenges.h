#pragma once

#include "core/notifier.h"

class edStore;
class FlashPlayer;

class SavedExtraChallenges;

static constexpr int32_t CounterIndex( const int32_t retailOffset ) {
    return ( retailOffset - 0xf038 ) / 4;
}

class SavedExtraChallengesNotifier : public Notifier {
public:
    SavedExtraChallengesNotifier( SavedExtraChallenges *pOwner );

    void onUpdate( uint32_t param ) override;

private:
    SavedExtraChallenges *pExtraChallenges;
};

class SavedExtraChallenges {
public:
    static constexpr int32_t kNumCounters = 17;
    static constexpr int32_t kNumScores = 11;
    static constexpr int32_t kNumFlags = 16;
    static constexpr int32_t kNumBestPairs = 0x11;
    static constexpr int32_t kNumTail = 11;

    static constexpr int32_t kNumFlagBlocks = kNumFlags / 2;
    static constexpr int32_t kMultiGoldCupBlock = 3;
    static constexpr int32_t kClubGoldCupBlock = 5;

    static constexpr int32_t kNumRuntimeCounters = 45;
    static constexpr int32_t kFirstCounterOffset = 0xf038;

    static constexpr int32_t kNumStatisticsCategories = 12;
    static constexpr int32_t kStatisticsColumnSize = 0x7800;

    enum eVehicleType : int32_t {
        VT_Unknown = 0,
        VT_Bike = 1,
        VT_Car = 2,
    };

public:
    // /!\ DO NOT REORDER (order must match the serialized blob on disk)
    int32_t PlayerLevel = 0;
    float freerideDistanceKms = 0.0f;
    eVehicleType currentVehicleType = VT_Unknown;
    int32_t Padding_0xf034 = 0;

    int32_t Counters[kNumCounters] = {};
    int32_t RuntimeCounters[kNumRuntimeCounters] = {};
    double Scores[kNumScores] = {};
    float Field_0xf188 = 0.0f;
    int32_t Field_0xf18c = 0;
    float Field_0xf190 = 0.0f;
    int32_t Field_0xf194 = 0;
    int32_t Field_0xf198 = 0;
    int32_t Field_0xf19c = 0;
    float Field_0xf1a0 = 0.0f;
    float Field_0xf1a4 = 0.0f;
    int32_t Flags[kNumFlags] = {};
    int32_t BestLow[kNumBestPairs] = {};
    int32_t BestHigh[kNumBestPairs] = {};
    int32_t Tail[kNumTail] = {};
    // /!\ DO NOT REORDER (order must match the serialized blob on disk)

    FlashPlayer *pStatisticsPlayer = nullptr;
    int32_t statisticsCategory = 0;
    char statisticsLeftColumn[kStatisticsColumnSize] = {};
    char statisticsRightColumn[kStatisticsColumnSize] = {};

public:
    inline bool isStatisticsScreenOpen() const {
        return pStatisticsPlayer != nullptr;
    }

public:
    SavedExtraChallenges();
    ~SavedExtraChallenges();

    void clear( const bool bSeedPlayerLevel );
    void resetVehicleType();
    void addDistanceDriven( const float kms );
    void addPlayTime( const float seconds );
    void addCounter( const int32_t index, const int32_t value );
    void countCarTuned( const bool bBike );
    void countCarPainted( const bool bBike );
    void countCarRented();
    void countChallengeResult( const uint32_t spotType,
                               const bool bWon,
                               const bool bOnlineRanked );
    void countClotheBought();
    void addCouponsSpent( const uint32_t numCoupons );
    void countCarTransaction( const uint32_t carID,
                              const bool bBought,
                              const bool bETrade );
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;

    uint32_t getFlagBlock( const int32_t blockIndex ) const;
    void setFlagBlock( const int32_t blockIndex, const uint32_t value );

    void openStatisticsScreen( FlashPlayer *pPlayer );
    void closeStatisticsScreen();
    void nextStatisticsCategory();
    void previousStatisticsCategory();
    void buildStatisticsPage();
    void tickStatisticsScreen();

private:
    int32_t readCounter( const int32_t index ) const;
    void writeCounter( const int32_t index, const int32_t value );
    uint32_t readTailDword( const int32_t *pBlock, const bool bHighHalf ) const;
    void writeTailDword( int32_t *pBlock,
                         const bool bHighHalf,
                         const uint32_t value );
    int32_t readFlag( const int32_t index ) const;
    float sumCounterRun( const int32_t firstOffset,
                         const int32_t endOffset ) const;
    double readScore( const int32_t index ) const;
    void addScore( const int32_t index, const double value );
    void resolveVehicleType();

    void resetStatisticsColumns();
    void publishStatisticsColumns();
    void appendStatLine( uint64_t labelHashcode, int32_t value );
    void appendDistanceLine( uint64_t labelHashcode, float value );
    void appendDurationLine( uint64_t labelHashcode, double seconds );
    void appendMoneyLine( uint64_t labelHashcode, int32_t money, float ratio );
    void appendSpendLine( uint64_t labelHashcode,
                          int32_t counterIndex,
                          float total );
    void appendSpendValue( uint64_t labelHashcode, int32_t money, float total );
    void appendMoneyOnlyLine( uint64_t labelHashcode, int32_t money );
    void appendDoneTotalLine( uint64_t labelHashcode,
                              int32_t money,
                              int32_t done,
                              int32_t total );
    void appendPercentLine( uint64_t labelHashcode, int32_t a, int32_t b );
    void appendQuadLine(
        uint64_t labelHashcode, int32_t a, int32_t b, int32_t c, int32_t d );
    void appendLabelAndValue( const char *pLabel, const char *pValue );

    void buildCategoryChallenges();
    void buildCategoryRaces();
    void buildCategoryClubs();
    void buildCategoryTimes();
    void buildCategorySessions();
    void buildCategoryDriving();
    void buildCategorySpeeds();
    void buildCategoryDiscovery();
    void buildCategoryShopping();
    void buildCategoryCars();
    void buildCategoryBikes();
    void buildCategoryHouses();
};
