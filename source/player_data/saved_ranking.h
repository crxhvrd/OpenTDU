#pragma once

#include "core/notifier.h"

class edStore;
class SavedStringTable;

struct RankingEntry {
    uint64_t Hashcode = 0ull;

    uint32_t pBlock0[2] = {}; // TEA Encrypted
    uint32_t pBlock1[2] = {}; // TEA Encrypted

    uint64_t getHashcode() const;

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;
};

class SavedRanking : public Notifiable {
public:
    static constexpr int32_t kNumEntries = 500;
    static constexpr uint32_t kMaxPlayerIDs = 3000u;

public:
    SavedRanking();
    ~SavedRanking();

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;

    RankingEntry *findEntry( const uint64_t hashcode );
    const RankingEntry *findEntry( const uint64_t hashcode ) const;

    int32_t getChallengeResult( const uint64_t hashcode ) const;
    void upgradeFromVersion1( SavedStringTable *pStringTable );

public:
    RankingEntry Entries[kNumEntries];
    uint32_t NumPlayerIDs = 0u;
    uint32_t RankCheckCounter = 0u;
    uint32_t PlayerIDs[kMaxPlayerIDs]; 
};
