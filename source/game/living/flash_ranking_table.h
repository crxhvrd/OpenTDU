#pragma once

#include "flash/entities/flash_list.h"

class Entity;
class MngFlash;
class MngRankings;
struct RankingsFilter;

// Generic leaderboard table (base of every MngRankings table)
class FlashRankingTable : public FlashList {
public:
    static constexpr int32_t kOnlinePageLength = 30;
    static constexpr int32_t kForcedListLength = 14;

public:
    inline bool isRegistered() const { return bRegistered; }
    inline const char *getID() const { return pID; }
    inline uint32_t getIDMagic() const {
        return ( uint32_t( uint8_t( pID[0] ) ) << 24 ) |
               ( uint32_t( uint8_t( pID[1] ) ) << 16 ) |
               ( uint32_t( uint8_t( pID[2] ) ) << 8 ) |
               uint32_t( uint8_t( pID[3] ) );
    }
    inline int32_t getCategory() const { return category; }
    inline void setOnline( const bool bEnabled ) { bOnline = bEnabled; }
    inline void setNumSubPages( const int32_t count ) { numSubPages = count; }
    inline void setLabel( const char *pLabel ) {
        strncpy( label, pLabel, sizeof( label ) - 1 );
        label[sizeof( label ) - 1] = '\0';
    }

public:
    FlashRankingTable();
    virtual ~FlashRankingTable() = default;

    void bind( MngRankings *pRankings,
               RankingsFilter *pRankingsFilter,
               FlashPlayer *pPlayer,
               const char *pPath,
               const char *pTableID,
               const int32_t tableCategory );
    void registerWidget( MngFlash *pMngFlash );
    void unregisterWidget( MngFlash *pMngFlash );

    int32_t getListLength() const override;
    void open() override;
    void close() override;
    void scroll( int32_t delta ) override;
    void selectedLineChanged() override;

    virtual void onFilterChanged( const bool bLevel,
                                  const bool bCountry,
                                  const bool bFriends ) = 0;
    virtual void refresh();
    virtual void requestAroundPlayer();
    virtual void nextSubPage();
    virtual void previousSubPage();

protected:
    MngRankings *pOwner;
    RankingsFilter *pFilter;
    FlashPlayer *pFilterPlayer;
    bool bResultsDisplayed;
    int32_t category;
    bool bOnline;
    int32_t field_0xb4;
    int32_t field_0xb8;
    bool bRefreshPending;
    mutable int32_t onlineLength;
    bool bRegistered;
    char pID[5];
    int32_t subPageIndex;
    int32_t numSubPages;
    char label[0x44];

protected:
    bool requestPageIfNeeded();
};

// Player best times / speeds of the solo challenges (local_ranks)
class FlashRankingTableLocal : public FlashRankingTable {
public:
    FlashRankingTableLocal();

    int32_t getListLength() const override;
    void open() override;
    void close() override;
    void update( const int32_t absoluteIndex,
                 const int32_t lineIndex ) override;
    void scroll( int32_t delta ) override;
    void selectedLineChanged() override;

    void onFilterChanged( const bool, const bool, const bool ) override {
        // FUN_007793a0
    }

private:
    int32_t savedFirstLine;

private:
    void swapOnlineTables( const bool bShowOnlineTables );
};

// Extra challenges (missions) the player completed (missions)
class FlashRankingTableMissions : public FlashRankingTable {
public:
    static constexpr int32_t kMaxMissions = 200;

public:
    inline Entity *getSelectedMission() const { return pSelectedMission; }

public:
    FlashRankingTableMissions();

    int32_t getListLength() const override;
    void open() override;
    void close() override;
    void update( const int32_t absoluteIndex,
                 const int32_t lineIndex ) override;
    void scroll( int32_t delta ) override;
    void selectedLineChanged() override;

    void onFilterChanged( const bool, const bool, const bool ) override {
        // FUN_00779360
    }

private:
    int32_t numMissions;
    int32_t missionIndexes[kMaxMissions];
    Entity *pSelectedMission;

private:
    Entity *findGoldMission( const int32_t index ) const;
};

// Online leaderboard tables (m_elo, s_chall, all_standard, club_mbrs,
// club_inter); empty while offline
class FlashRankingTableOnline : public FlashRankingTable {
public:
    void update( const int32_t absoluteIndex,
                 const int32_t lineIndex ) override;
    void onFilterChanged( const bool bLevel,
                          const bool bCountry,
                          const bool bFriends ) override;
};
