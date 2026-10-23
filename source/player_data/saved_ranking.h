#pragma once

#include "core/notifier.h"

class edStore;
class SavedStringTable;

struct RankingEntry {
    uint64_t Hashcode = 0ull;

    uint32_t pBlock0[2] = {}; // TEA Encrypted
    uint32_t pBlock1[2] = {}; // TEA Encrypted

    uint64_t getHashcode() const;
    int32_t getResult() const;
    float getBestScore() const;
    uint32_t getNumSplitTimes() const;
    uint32_t getSplitTimesIndex() const;

    void setHashcode( const uint64_t hashcode );
    void setResultAndBestScore( const int32_t result, const float bestScore );
    void setSplitTimes( const uint32_t numSplitTimes,
                        const uint32_t splitTimesIndex );

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;
};

class SavedRanking : public Notifiable {
public:
    static constexpr int32_t kNumEntries = 500;
    static constexpr uint32_t kSplitTimesPoolSize = 3000u;

public:
    SavedRanking();
    ~SavedRanking();

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;

    RankingEntry *findEntry( const uint64_t hashcode );
    const RankingEntry *findEntry( const uint64_t hashcode ) const;

    int32_t getChallengeResult( const uint64_t hashcode ) const;
    float getChallengeBestScore( const uint64_t hashcode ) const;
    float getBestTime( const uint64_t hashcode ) const;
    float getBestSpeed( const uint64_t hashcode ) const;
    bool getSplitTimes( const uint64_t hashcode,
                        const uint32_t maxNumSplitTimes,
                        uint32_t *pOutSplitTimes ) const;

    void submitTimeResult( const uint64_t hashcode,
                           const int32_t result,
                           const float time );
    void submitScoreResult( const uint64_t hashcode,
                            const int32_t result,
                            const float score );
    void storeSplitTimes( const uint64_t hashcode,
                          const uint32_t numSplitTimes,
                          const uint32_t *pSplitTimes );

    void upgradeFromVersion1( SavedStringTable *pStringTable );

private:
    RankingEntry *allocateEntry();
    void setEntryResult( const uint64_t hashcode,
                         const int32_t result,
                         const float bestScore );

    uint32_t getNumUsedSplitTimes();
    void setNumUsedSplitTimes( const uint32_t numUsedSplitTimes );

public:
    RankingEntry Entries[kNumEntries];
    uint32_t NumSplitTimesSerialized = 0u;
    uint32_t NumUsedSplitTimes = 0u;
    uint32_t SplitTimesPool[kSplitTimesPoolSize];
};
