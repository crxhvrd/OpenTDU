#pragma once

#include "achievements_data.h"
#include "driver.h"
#include "player_data_interface.h"
#include "saved_avatar.h"
#include "saved_car_reserve.h"
#include "saved_extra_challenges.h"
#include "saved_garage.h"
#include "saved_item_table.h"
#include "saved_mailbox.h"
#include "saved_news.h"
#include "saved_ranking.h"
#include "saved_road_discovery.h"
#include "saved_string_table.h"

// Magic the 'playersave' stream starts with.
static constexpr int32_t kPlayerSaveMagic = 0x5d;

extern int32_t gSaveStreamRevision;
extern int32_t gSaveStreamTrailer;

class PlayerSave : public IPlayerData {
public:
    Driver *getDriver() override { return &driver; }
    AchievementsData *getAchievements() override { return &achievements; }

    Garage *getGarage() override { return &garage; }
    SavedCarReserve *getStock() override { return &carReserve; }
    SavedRanking *getRanking() override { return &ranking; }
    SavedMailbox *getMailbox() override { return &mailbox; }
    SavedNewsList *getNewsList() override { return &newsList; }
    SavedItemTable *getItemTable() override { return &itemTable; }
    SavedAvatar *getAvatar() override { return &avatar; }
    SavedRoadDiscovery *getRoadDiscovery() override { return &roadDiscovery; }
    SavedStringTable *getStringTable() override { return &stringTable; }
    SavedExtraChallenges *getExtraChallenges() override {
        return &extraChallenges;
    }
    bool isReady() const override {
        return numPendingOperations == 0 && bInitialized;
    }
    inline void resetPendingOperations() { numPendingOperations = 0; }
    inline bool hasValidGarage() const { return nextCarUniqueID != 0; }

public:
    PlayerSave();
    ~PlayerSave() override;

    void initialize() override;
    void terminate() override;
    void tick( const float deltaTime ) override;
    void unStart() override;
    void newGame( const bool bInstallCheats ) override;
    void newGameWithCheats();
    bool installCheatCars();
    uint32_t allocateCarUniqueID();

private:
    void grantStarterHouse( const uint64_t houseHashcode );

public:
    void clear() override;
    void swapCars( const int32_t indexA,
                   const uint64_t houseHashcodeA,
                   const int32_t indexB,
                   const uint64_t houseHashcodeB ) override;
    void setCurrentCar( const uint16_t index );

    bool deserialize( const void *pSaveData, const uint32_t saveDataSize );
    void serialize( edStore &stream );

private:
    Driver driver;
    SavedCarReserve carReserve;
    Garage garage;
    SavedRanking ranking;
    SavedMailbox mailbox;
    SavedNewsList newsList;
    uint32_t nextCarUniqueID;
    SavedItemTable itemTable;
    SavedAvatar avatar;
    SavedRoadDiscovery roadDiscovery;
    SavedStringTable stringTable;
    AchievementsData achievements;
    SavedExtraChallenges extraChallenges;
    SavedExtraChallengesNotifier extraChallengesNotifier;
    int32_t numPendingOperations;
    bool bInitialized;
    int32_t saveIndex;
};
