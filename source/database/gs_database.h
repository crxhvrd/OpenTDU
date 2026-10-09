#pragma once

#include "core/locale.h"
#include "game/gs_base.h"
#include "game/vehicle/car_desc.h"

struct DBReference {
    union {
        uint64_t Hashcode;
        char *pAsString;
        void *pAsRawPointer;
    };
};
OTDU_SIZE_MUST_MATCH( DBReference, 0x08 );

#include "db_achievement.h"
#include "db_aftermarket_pack.h"
#include "db_bot.h"
#include "db_brand.h"
#include "db_car_color.h"
#include "db_car_pack.h"
#include "db_car_physics_data.h"
#include "db_car_rim.h"
#include "db_car_shop.h"
#include "db_clothe.h"
#include "db_hair.h"
#include "db_house.h"
#include "db_interior.h"
#include "db_menu.h"
#include "db_pnj.h"
#include "db_rim.h"
#include "db_subtitle.h"
#include "db_tutorial.h"

static constexpr int32_t kNumDatabases = 18;
static constexpr int32_t kNumCommonStrings = 6;

class FlashPlayer;
struct SpotMap;

struct DBEntry {
    uint64_t Hashcode;
    uint32_t Offset;
    uint32_t __PADDING__;
};
OTDU_SIZE_MUST_MATCH( DBEntry, 0x10 );

struct DBEntryList {
    std::string Name = "";
    DBEntry *List = nullptr;
    uint32_t NumItems = 0;
    char *pData = nullptr;

    uint64_t getHashcodeForPointer( void *param_1 ) const;
    uint64_t getHashcodeForString( const char *pString ) const;
};

struct GameDatabase {
    DBEntryMenu *DBMenu = nullptr;
    DBEntryCarPhysicsData *DBCarData = nullptr;
    DBEntryRim *DBRim = nullptr;
    DBEntryCarRim *DBCarRims = nullptr;
    DBEntryInterior *DBInterior = nullptr;
    DBEntryCarColor *DBColors = nullptr;
    DBEntryBrand *DBBrand = nullptr;
    DBEntryCarShop *DBCarShop = nullptr;
    DBEntryHouse *DBHouse = nullptr;
    DBEntryHair *DBHair = nullptr;
    DBEntryClothe *DBClothe = nullptr;
    DBEntryTutorial *DBTutorial = nullptr;
    DBEntryAftermarketPack *DBPack = nullptr;
    DBEntryCarPack *DBCarPack = nullptr;
    DBEntryBot *DBBot = nullptr;
    DBEntryAchievement *DBAchivement = nullptr;
    DBEntryPNJ *DBPNJ = nullptr;
    DBEntrySubtitle *DBSubtitle = nullptr;

    uint32_t pNumItems[kNumDatabases];
    std::array<DBEntryList, kNumDatabases> Databases;
    std::string Filepath = "";
};

struct CommonDBContent {
    char *pDB = nullptr;
    int32_t NumData = 0;
    uint32_t NumFields = 0;
    char *pList = nullptr;
    uint32_t NumEntries = 0;
    uint32_t NumItems = 0;
    size_t DBSize = 0ull;
    uint64_t Hashcode = 0ull;
    int32_t Version = 0;
    int32_t Release = 0;
    char *pDB2 = nullptr;
    int32_t Stride = 0;
    uint32_t Attributes = 0;
};

struct CommonDBEntry {
    uint64_t References[24];
    std::string Name;
    std::string Filepath;
    char ParsedContent[256];
    char Language[4] = { '\0', '\0', '\0', '\0' };
    CommonDBContent Content;
};

struct CommonDB {
    std::array<CommonDBEntry, kNumDatabases> DBList;
    std::array<char *, kNumCommonStrings> CommonStrings;
    uint8_t bInitialized : 1;
    uint8_t bUnknownFlag : 1;
};

template <typename TCharType> struct DBInstallRequest {
    TCharType *pBuffer = nullptr;
    size_t BufferSize = 0ull;
};

struct ModDBInjection;

class GSDatabase : public GameSystem {
public:
    int32_t NbChallengeNames;
    int32_t NbChallengeDesc;

public:
    const char *getName() const override { return "DataBase"; }
    inline size_t getNumCars() const { return cars.size(); }
    inline const std::vector<CarConfig> &getCarList() const { return cars; }
    inline const GameDatabase &getGameDB() const { return gameDatabase; }
    inline const CommonDB &getCommonDB() const { return engineDatabase; }

public:
    GSDatabase();
    ~GSDatabase();

    bool initialize( TestDriveGameInstance * ) override;
    void terminate() override;
    void reset() override;
    void pause() override;

    bool reloadDatabases( const char *pLanguage,
                          const bool bForceReload = false );
    char *getStringByHashcode( const uint32_t dbIndex,
                               const uint64_t hashcode );
    void releaseDatabases( const bool bRetainBuffers = false );
    bool switchLocale( const eLocale locale );
    inline eLocale getActiveLocale() const { return activeLocale; }
    void republishFrontEndStrings();

    int32_t setFlashLocalization( int32_t param_2,
                                  uint32_t param_3,
                                  FlashPlayer *param_4 );
    bool setLanguage( FlashPlayer *param_1 );

    bool isInitialized() const;
    int32_t getVehicleId( const char *pBrand,
                          const char *pModel,
                          const char *pVersion,
                          int32_t *pOutIndex = nullptr ) const;

    DBEntryCarPhysicsData *getCarDBEntry( int32_t carID );
    bool carBankExists( const DBEntryCarPhysicsData *pCarEntry ) const;
    int32_t getRimID( int32_t carID, DBEntryRim *rimRef );
    int32_t getRimID( DBEntryCarPhysicsData *param_3, DBEntryRim *rimRef );
    DBEntryRim *getRim( int32_t carID, int32_t rimID );
    DBEntryRim *getRim( DBEntryCarPhysicsData *param_2, int32_t rimID );
    DBEntryAftermarketPack *getAftermarketPack( uint64_t param_1 );
    bool isCarPackAvailable( DBEntryCarPack *pCarPack );
    int32_t getCarPackMaxLevel( DBEntryCarPack *pCarPack );
    int32_t getPackLevel( DBEntryCarPhysicsData *pCarEntry,
                          const uint64_t packHashcode,
                          int32_t *pOutMaxLevel = nullptr );
    bool isBike( DBEntryCarPhysicsData *param_1 );
    bool isTraffic( DBEntryCarPhysicsData *param_1 );

    const CarConfig *getCarConfig( int32_t carID ) const;

    DBEntryCarShop *getCarShopBySpotName( uint64_t spotHashcode );
    int32_t getNumShopCarPacks( const DBEntryCarShop *pCarShop );
    DBEntryCarPack *getShopCarPack( const DBEntryCarShop *pCarShop,
                                    int32_t index );
    bool isCarTunable( int32_t carID );
    bool isCarTunableInShop( const SpotMap *pSpot, int32_t carID );
    DBEntryCarShop *getCarShopSellingCar( DBEntryCarPhysicsData *pCarEntry );
    bool isCarShopSlotAvailable( DBEntryCarShop *pCarShop, int32_t slotIndex );
    int32_t getNumCarsAvailableInShop( DBEntryCarShop *pCarShop );

    int32_t getNumRims( int32_t carID );
    int32_t getNumColors( int32_t carID );
    DBEntryCarColor *getColorByIndex( int32_t carID, int32_t param_3 );
    DBEntryCarColor *getColorByIndex( DBEntryCarPhysicsData *param_2,
                                      int32_t param_3 );
    DBEntryInterior *getInteriorByIndex( DBEntryCarColor *param_2,
                                         int32_t param_3 );

private:
    struct CarShopStockEntry {
        DBEntryCarShop *pCarShop;
        int32_t NumCarsAvailable;
    };
    static constexpr int32_t kNumCarShopStockEntries = 0x78;

private:
    CommonDB engineDatabase;
    GameDatabase gameDatabase;

    std::array<DBInstallRequest<char>, kNumDatabases> dbInstallRequests;
    std::array<DBInstallRequest<char>, kNumDatabases>
        localizedDbInstallRequests;

    std::array<DBInstallRequest<char>, kNumDatabases> baseDbInstallRequests;
    std::array<DBInstallRequest<char>, kNumDatabases>
        baseLocalizedDbInstallRequests;
    std::vector<char> injectedDbText;
    std::vector<char> injectedDbList;
    std::vector<void *> retiredAllocations;
    CarShopStockEntry carShopStockCache[kNumCarShopStockEntries];

    std::vector<CarConfig> cars;
    char pActiveLanguage[3];
    eLocale activeLocale;

    uint8_t bInitialized : 1;
    uint8_t bModDatabaseInjectionDisabled : 1;

private:
    bool loadDatabases( const char *pLanguage );
    bool initializeDatabase( const char *pName,
                             const uint32_t index,
                             const uint32_t maxNumElements,
                             char **pDatabaseItems,
                             const uint32_t numInjectedRows = 0u );
    bool loadDatabaseWithInjections( const char *pName,
                                     const uint32_t index,
                                     const uint32_t stride,
                                     char **pDatabaseItems );
    bool
    tryLoadDatabase( const char *pName,
                     const uint32_t index,
                     const uint32_t stride,
                     char **pDatabaseItems,
                     const std::vector<const ModDBInjection *> &injections );
    uint32_t buildInjectedBuffers(
        const uint32_t index,
        const std::vector<const ModDBInjection *> &injections );
    void releaseDatabase( const uint32_t index, char **pDatabaseItems );
    bool openDatabase( const char *pName, const uint32_t index );
    bool finalizeDatabaseLoad( const int32_t databaseIndex,
                               const uint32_t stride,
                               char **pDatabaseItems );
    bool dbFillExternals( int32_t param_3, int32_t param_4 );
    int32_t findGlobalReference( int32_t param_2_00, uint64_t param_3 );
    void fillCarList();
    void initCommonStrings();
    void parseMenusForVars();
};

extern GSDatabase *gpDatabase;

std::string GetFullCarName( DBEntryCarPhysicsData *param_1 );

static inline bool IsCarEntryValid( const DBEntryCarPhysicsData *pEntry ) {
    // FUN_009a8a60
    return ( pEntry->BitField & 1 ) != 0 && pEntry->Brand != nullptr &&
           ( pEntry->Brand->Flags & 1 ) != 0;
}
