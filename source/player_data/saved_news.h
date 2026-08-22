#pragma once

#include "core/notifier.h"

class edStore;

// One news or team invite 
struct NewsEntry {
    uint32_t ID = 0u;       
    uint8_t Kind = 0u;        // (0 = empty slot)
    uint16_t ExpiryDate = 0u; // (packed 31 * (month + 12 * year) + day)
    uint32_t Param0 = 0u;     //
    uint32_t Param1 = 0u;     //

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;
};

class SavedNewsList : public Notifiable {
public:
    static constexpr int32_t kNumNews = 100;
    static constexpr int32_t kNumTeamInvites = 0x40;

public:
    SavedNewsList();
    ~SavedNewsList();

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream );
    void removeExpiredEntries( const uint16_t currentDate );

public:
    NewsEntry News[kNumNews];               
    NewsEntry TeamInvites[kNumTeamInvites]; 
    uint32_t NumNews = 0u;                
    uint32_t NumTeamInvites = 0u;          

private:
    void clampCountersAndWipeTail();
};

uint16_t
PackSaveDate( const uint32_t year, const uint32_t month, const uint32_t day );
