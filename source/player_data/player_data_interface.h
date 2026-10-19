#pragma once

struct DBEntryCarPack;
class AchievementsData;
class Driver;
class EbayList;
class Garage;
class SavedAvatar;
class SavedCarReserve;
class SavedExtraChallenges;
class SavedItemTable;
class SavedMailbox;
class SavedNewsList;
class SavedRanking;
class SavedRoadDiscovery;
class SavedStringTable;

class IPlayerData {
public:
    virtual ~IPlayerData() {}

    virtual void initialize() = 0;
    virtual void terminate() = 0;
    virtual void tick( const float deltaTime ) = 0;
    virtual void unStart() = 0;
    virtual void newGame( const bool bInstallCheats ) = 0;

    virtual Driver *getDriver() = 0;
    virtual SavedCarReserve *getStock() = 0;
    virtual Garage *getGarage() = 0;
    virtual EbayList *getEbayList() = 0;
    virtual SavedRanking *getRanking() = 0;
    virtual SavedMailbox *getMailbox() = 0;
    virtual SavedNewsList *getNewsList() = 0;
    virtual SavedItemTable *getItemTable() = 0;
    virtual SavedAvatar *getAvatar() = 0;
    virtual SavedRoadDiscovery *getRoadDiscovery() = 0;
    virtual SavedStringTable *getStringTable() = 0;
    virtual AchievementsData *getAchievements() = 0;
    virtual SavedExtraChallenges *getExtraChallenges() = 0;

    virtual void buyCar( const uint32_t carConfigID,
                         const int32_t slot,
                         const int32_t paintIndex,
                         const int32_t rimID,
                         const int32_t interiorID ) = 0;
    virtual void sellCar( const uint32_t carID ) = 0;
    virtual void swapCars( const int32_t indexA,
                           const uint64_t houseHashcodeA,
                           const int32_t indexB,
                           const uint64_t houseHashcodeB ) = 0;

    virtual void upgradeCar( DBEntryCarPack *pCarPack,
                             const int32_t level ) = 0;

    virtual void clear() = 0;
    virtual bool isReady() const = 0;
};
