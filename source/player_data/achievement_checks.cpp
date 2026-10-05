#include "shared.h"
#include "achievement_checks.h"

#include "achievements_data.h"
#include "database/gs_database.h"
#include "driver.h"
#include "game/scenaric/entity/entity_extra_challenge.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/scenaric_bank.h"
#include "game/scenaric/spots.h"
#include "gs_playerdata.h"
#include "player_data_interface.h"
#include "saved_extra_challenges.h"
#include "saved_garage.h"
#include "saved_item_table.h"
#include "saved_ranking.h"
#include "saved_road_discovery.h"

static constexpr float kKilometresToMiles = 0.6215040683746338f; // DAT_00f418c0

static constexpr int32_t kChallengeCategorySolo = 0xc0c0;
static constexpr int32_t kChallengeCategoryAny = 0xc0c3;

// Excludes reward houses from the "own every houses" achievemnet
static constexpr char kBonusRealtor[] = "ERE_BONUS"; // DAT_00f161c4

static IPlayerData *GetLoadedSave() {
    return ( gpPlayerData != nullptr ) ? gpPlayerData->getLoadedSave()
                                       : nullptr;
}

static AchievementsData *GetAchievements() {
    IPlayerData *pSave = GetLoadedSave();
    return ( pSave != nullptr ) ? pSave->getAchievements() : nullptr;
}

static Spots *GetSpotList() {
    return ( gpScenaric != nullptr ) ? &gpScenaric->getSpotList() : nullptr;
}

static int32_t GetSpotChallengeCategory( const SpotMap &spot ) {
    return int32_t( spot.SubType & 0xf ) + kChallengeCategorySolo;
}

static int32_t GetSpotChallengeDiscipline( const SpotMap &spot ) {
    const int32_t discipline = int32_t( spot.SubType >> 4 );
    return ( discipline == 0xf ) ? -1 : discipline;
}

static int32_t GetChallengeResult( const SpotMap &spot ) {
    IPlayerData *pSave = GetLoadedSave();
    if ( pSave == nullptr ) {
        return -1;
    }

    return pSave->getRanking()->getChallengeResult( spot.Hashcode );
}

static int32_t CountExtraChallenges( const eEntityType kind,
                                     int32_t *pOutNumGold ) {
    // FUN_00804d70
    int32_t numChallenges = 0;
    int32_t numGold = 0;

    IPlayerData *pSave = GetLoadedSave();
    ScenaricBank *pBank = ( gpScenaric != nullptr )
                              ? gpScenaric->getWorldExtraChallengesBank()
                              : nullptr;
    if ( pBank != nullptr && pSave != nullptr ) {
        const SavedItemTable *pChallengeStates = pSave->getItemTable();

        for ( int32_t i = 0; i < pBank->getEntityCount(); i++ ) {
            Entity *pEntity = pBank->getEntity( i );
            if ( pEntity == nullptr ) {
                continue;
            }

            const eEntityType type = pEntity->getType();
            if ( kind != eEntityType::ET_None ) {
                if ( type != kind ) {
                    continue;
                }
            } else if ( type != eEntityType::ET_SHOG &&
                        type != eEntityType::ET_HIHI &&
                        type != eEntityType::ET_TRAN &&
                        type != eEntityType::ET_CVOY ) {
                continue;
            }

            numChallenges++;

            const int32_t index =
                static_cast<EntityExtraChallenge *>( pEntity )->getIndex();
            if ( pChallengeStates->isExtraChallengeGold( index ) ) {
                numGold++;
            }
        }
    }

    if ( pOutNumGold != nullptr ) {
        *pOutNumGold = numGold;
    }

    return numChallenges;
}

int32_t AchievementCheck::getProgress( int32_t * ) const { return 0; }

class AchievementCheckMinClubGoldCup : public AchievementCheck {
public:
    AchievementCheckMinClubGoldCup() {
        // FUN_00415780
        Flags = ACF_Challenge;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089bec0
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 0;
        }

        IPlayerData *pSave = GetLoadedSave();
        if ( pSave == nullptr ) {
            return 0;
        }

        return int32_t( pSave->getExtraChallenges()->getFlagBlock(
            SavedExtraChallenges::kClubGoldCupBlock ) );
    }
};

class AchievementCheckMinMultiGoldCup : public AchievementCheck {
public:
    AchievementCheckMinMultiGoldCup() {
        // FUN_00415ba0
        Flags = ACF_Challenge;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089bf20
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 0;
        }

        IPlayerData *pSave = GetLoadedSave();
        if ( pSave == nullptr ) {
            return 0;
        }

        return int32_t( pSave->getExtraChallenges()->getFlagBlock(
            SavedExtraChallenges::kMultiGoldCupBlock ) );
    }
};

class AchievementCheckMinGoldCustom : public AchievementCheck {
public:
    AchievementCheckMinGoldCustom() {
        // FUN_004155b0
        Flags = ACF_Challenge;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089c830
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 0;
        }

        const AchievementsData *pData = GetAchievements();
        return ( pData != nullptr )
                   ? int32_t( pData->getNumCustomChallengesWon() )
                   : 0;
    }
};

class AchievementCheckUploadCustom : public AchievementCheck {
public:
    AchievementCheckUploadCustom() {
        // FUN_004155e0
        Flags = ACF_Challenge;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089c870
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 0;
        }

        const AchievementsData *pData = GetAchievements();
        return ( pData != nullptr )
                   ? int32_t( pData->getNumCustomChallengesUploaded() )
                   : 0;
    }
};

class AchievementCheckMinETradeTransaction : public AchievementCheck {
public:
    AchievementCheckMinETradeTransaction() {
        // FUN_00415580
        Flags = ACF_Collection;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089c350
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 0;
        }

        const AchievementsData *pData = GetAchievements();
        return ( pData != nullptr )
                   ? int32_t( pData->getNumETradeTransactions() )
                   : 0;
    }
};

class AchievementCheckCarTunedHistory : public AchievementCheck {
public:
    AchievementCheckCarTunedHistory() {
        // FUN_00415720
        Flags = ACF_Collection;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089c5c0
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 0;
        }

        const AchievementsData *pData = GetAchievements();
        return ( pData != nullptr ) ? int32_t( pData->getNumCarsTuned() ) : 0;
    }
};

class AchievementCheckMinSpecialPhotos : public AchievementCheck {
public:
    AchievementCheckMinSpecialPhotos() {
        // FUN_004154d0
        Flags = ACF_Collection;
    }

    int32_t getProgress( int32_t * ) const override {
        // FUN_0048d670
        return 0;
    }
};

class AchievementCheckAllRoadExplored : public AchievementCheck {
public:
    AchievementCheckAllRoadExplored() {
        // FUN_004157b0
        Flags = ACF_Road;
        bShowsProgress = true;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089d3a0
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 0;
        }

        OTDU_UNIMPLEMENTED_ONCE; // TODO: AI
        return 0;
    }

    int32_t getDetailedProgress( int32_t *pOutTotal ) const override {
        // FUN_0089d300
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 100;
        }

        OTDU_UNIMPLEMENTED_ONCE; // TODO: AI
        return 0;
    }
};

class AchievementCheckMinGoldCup : public AchievementCheck {
public:
    AchievementCheckMinGoldCup( const int32_t category,
                                const int32_t discipline )
        : category( category ), discipline( discipline ) {
        // FUN_00415bd0 (solo) and FUN_00415c00 (any category)
        Flags = ACF_Challenge;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089c930
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 0;
        }

        Spots *pSpots = GetSpotList();
        if ( pSpots == nullptr ) {
            return 0;
        }

        int32_t numGoldCups = 0;
        const int32_t numSpots = int32_t( pSpots->getNumWorldSpots() );
        for ( int32_t i = 0; i < numSpots; i++ ) {
            const SpotMap &spot = *pSpots->getWorldSpot( i );
            if ( spot.Type != ST_Challenge ) {
                continue;
            }

            if ( category != kChallengeCategoryAny &&
                 GetSpotChallengeCategory( spot ) != category ) {
                continue;
            }

            if ( discipline != -1 &&
                 GetSpotChallengeDiscipline( spot ) != discipline ) {
                continue;
            }

            if ( GetChallengeResult( spot ) == 1 ) {
                numGoldCups++;
            }

            if ( pOutTotal != nullptr ) {
                *pOutTotal = *pOutTotal + 1;
            }
        }

        return numGoldCups;
    }

private:
    int32_t category;
    int32_t discipline;
};

class AchievementCheckRoadExplored : public AchievementCheck {
public:
    AchievementCheckRoadExplored() {
        // FUN_004157e0
        Flags = ACF_Road;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089d450
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 0;
        }

        OTDU_UNIMPLEMENTED_ONCE; // TODO: AI
        return 0;
    }
};

class AchievementCheckGoldChallenge : public AchievementCheck {
public:
    explicit AchievementCheckGoldChallenge( const char *pSpotName )
        : challengeHashcode( GetIdentifier64bit( pSpotName ) ) {
        // FUN_00415390 ("RSFE0040") and FUN_004156d0 ("RSFE0029")
        Flags = ACF_Challenge;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089c390
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 1;
        }

        Spots *pSpots = GetSpotList();
        if ( pSpots == nullptr ) {
            return 0;
        }

        const int32_t spotIndex = pSpots->getSpotMapIndex( challengeHashcode );
        if ( spotIndex < 0 ||
             spotIndex >= int32_t( pSpots->getNumWorldSpots() ) ) {
            return 0;
        }

        return ( GetChallengeResult( *pSpots->getWorldSpot( spotIndex ) ) == 1 )
                   ? 1
                   : 0;
    }

private:
    uint64_t challengeHashcode; // +0x40
};

class AchievementCheckClothesOwned : public AchievementCheck {
public:
    explicit AchievementCheckClothesOwned( const char *pBrandTag ) {
        // FUN_00415500 ("ECKO") and FUN_00415540 ("SHERMAN")
        Flags = ACF_Collection;

        memset( brandTag, 0, sizeof( brandTag ) );
        strncpy( brandTag, pBrandTag, sizeof( brandTag ) - 1 );
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089d490
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 0;
        }

        OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR
        return 0;
    }

private:
    char brandTag[8];
};

class AchievementCheckLevelReached : public AchievementCheck {
public:
    AchievementCheckLevelReached() {
        // FUN_004154a0
        Flags = ACF_Level;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089c400
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 1;
        }

        const AchievementsData *pData = GetAchievements();
        return ( pData != nullptr ) ? pData->getPlayerLevel() : 0;
    }
};

class AchievementCheckAllHousesOwned : public AchievementCheck {
public:
    AchievementCheckAllHousesOwned() {
        // FUN_00415840
        Flags = ACF_Collection;
        bShowsProgress = true;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089c510
        int32_t numOwned = 0;
        const int32_t numHouses = countHouses( numOwned );

        if ( pOutTotal != nullptr ) {
            *pOutTotal = numHouses;
        }

        return ( numHouses == numOwned ) ? 1 : 0;
    }

    int32_t getDetailedProgress( int32_t *pOutTotal ) const override {
        // FUN_0089c470
        int32_t numOwned = 0;
        const int32_t numHouses = countHouses( numOwned );

        if ( pOutTotal != nullptr ) {
            *pOutTotal = numHouses;
        }

        return numOwned;
    }

private:
    static int32_t countHouses( int32_t &numOwned ) {
        numOwned = 0;

        IPlayerData *pSave = GetLoadedSave();
        if ( pSave == nullptr || gpDatabase == nullptr ) {
            return 0;
        }

        const GameDatabase &gameDB = gpDatabase->getGameDB();
        Garage *pGarage = pSave->getGarage();

        int32_t numHouses = 0;
        const uint32_t numEntries =
            gameDB.pNumItems[Garage::kHouseDatabaseIndex];
        for ( uint32_t i = 0u; i < numEntries; i++ ) {
            const DBEntryHouse &house = gameDB.DBHouse[i];
            if ( house.Level <= 0 ) {
                continue;
            }

            const char *pRealtor = house.RealtorHashcode.pAsString;
            if ( pRealtor != nullptr &&
                 memcmp( pRealtor, kBonusRealtor, sizeof( kBonusRealtor ) ) ==
                     0 ) {
                continue;
            }

            numHouses++;
            if ( pGarage->isHouseOwned(
                     GetIdentifier64bit( house.SpotName.pAsString ) ) ) {
                numOwned++;
            }
        }

        return numHouses;
    }
};

class AchievementCheckIsClubMember : public AchievementCheck {
public:
    AchievementCheckIsClubMember() {
        // FUN_00415750
        Flags = ACF_Collection;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089be40
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 1;
        }

        IPlayerData *pSave = GetLoadedSave();
        if ( pSave == nullptr ) {
            return 0;
        }

        return ( pSave->getDriver()->TeamID != 0u ) ? 1 : 0;
    }
};

class AchievementCheckHousesOwned : public AchievementCheck {
public:
    AchievementCheckHousesOwned() {
        // FUN_00415810
        Flags = ACF_Collection;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089ccb0
        if ( pOutTotal != nullptr ) {
            *pOutTotal =
                ( gpDatabase != nullptr )
                    ? int32_t( gpDatabase->getGameDB()
                                   .pNumItems[Garage::kHouseDatabaseIndex] )
                    : 0;
        }

        IPlayerData *pSave = GetLoadedSave();
        return ( pSave != nullptr ) ? pSave->getGarage()->getNumOwnedHouses()
                                    : 0;
    }
};

class AchievementCheckGoldInstantChaAllBots : public AchievementCheck {
public:
    AchievementCheckGoldInstantChaAllBots() {
        // FUN_00415610
        Flags = ACF_Challenge;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089be70
        const AchievementsData *pData = GetAchievements();
        if ( gpDatabase == nullptr || pData == nullptr ) {
            if ( pOutTotal != nullptr ) {
                *pOutTotal = 0;
            }
            return 0;
        }

        const uint32_t numBots =
            gpDatabase->getGameDB()
                .pNumItems[AchievementsData::kBotDatabaseIndex];
        if ( pOutTotal != nullptr ) {
            *pOutTotal = int32_t( numBots );
        }

        int32_t numBeaten = 0;
        for ( uint32_t i = 0u; i < numBots; i++ ) {
            if ( pData->getAchievementFlag( int32_t( i ) ) ) {
                numBeaten++;
            }
        }

        return numBeaten;
    }
};

class AchievementCheckRoadTraveled : public AchievementCheck {
public:
    AchievementCheckRoadTraveled() {
        // FUN_00415320
        Flags = ACF_Road;
    }

    int32_t getProgress( int32_t * ) const override {
        // FUN_0089c430
        IPlayerData *pSave = GetLoadedSave();
        if ( pSave == nullptr ) {
            return 0;
        }

        return int32_t( pSave->getDriver()->getOdometerKms() *
                        kKilometresToMiles );
    }
};

class AchievementCheckAllCarDealerUnlocked : public AchievementCheck {
public:
    AchievementCheckAllCarDealerUnlocked() {
        // FUN_00415360
        Flags = ACF_Road;
        bShowsProgress = true;
    }

    void onBuilt() override {
        // FUN_0089bb70
        // NOP
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089da70
        return countDealers( pOutTotal );
    }

    int32_t getDetailedProgress( int32_t *pOutTotal ) const override {
        // FUN_0089d9c0
        return countDealers( pOutTotal );
    }

private:
    // FUN_0089d9c0 / FUN_0089da70
    static int32_t countDealers( int32_t *pOutTotal ) {
        int32_t numDealers = 0;
        int32_t numUnlocked = 0;

        IPlayerData *pSave = GetLoadedSave();
        Spots *pSpots = GetSpotList();
        if ( pSave != nullptr && pSpots != nullptr && gpDatabase != nullptr ) {
            const SavedRoadDiscovery *pRoads = pSave->getRoadDiscovery();

            const int32_t numSpots = int32_t( pSpots->getNumWorldSpots() );
            for ( int32_t i = 0; i < numSpots; i++ ) {
                const SpotMap &spot = *pSpots->getWorldSpot( i );
                if ( spot.Type != ST_CarDealer ) {
                    continue;
                }

                DBEntryCarShop *pCarShop =
                    gpDatabase->getCarShopBySpotName( spot.Hashcode );
                if ( pCarShop == nullptr ||
                     gpDatabase->getNumCarsAvailableInShop( pCarShop ) <= 0 ) {
                    continue;
                }

                numDealers++;

                if ( spot.RoadID == 0u ||
                     !pRoads->isRoadLocked( spot.RoadID, spot.RoadSegment ) ) {
                    numUnlocked++;
                }
            }
        }

        if ( pOutTotal != nullptr ) {
            *pOutTotal = numDealers;
        }

        return numUnlocked;
    }
};

class AchievementCheckAllGoldSoloExtra : public AchievementCheck {
public:
    enum eExtraChallengeKind : int32_t {
        ECK_Any = -1,
        ECK_Shopping = 0x10,
        ECK_HitchHiker = 0x11,
        ECK_Transport = 0x12,
        ECK_Convoy = 0x13,
    };

    explicit AchievementCheckAllGoldSoloExtra( const int32_t kind )
        : kind( kind ) {
        // FUN_004153e0 / FUN_00415410 / FUN_00415440 / FUN_00415470 and
        // FUN_00415a80 for the untyped one.
        Flags = ACF_Challenge;
        bShowsProgress = true;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089d510
        int32_t numTotal = 0;
        const int32_t numDone = countExtraChallenges( numTotal );

        if ( pOutTotal != nullptr ) {
            *pOutTotal = numTotal;
        }

        return ( numDone >= numTotal ) ? 1 : 0;
    }

    int32_t getDetailedProgress( int32_t *pOutTotal ) const override {
        // FUN_0089d4c0
        int32_t numTotal = 0;
        const int32_t numDone = countExtraChallenges( numTotal );

        if ( pOutTotal != nullptr ) {
            *pOutTotal = numTotal;
        }

        return numDone;
    }

private:
    int32_t countExtraChallenges( int32_t &numTotal ) const {
        int32_t numGold = 0;
        numTotal = CountExtraChallenges( getEntityType(), &numGold );
        return numGold;
    }

    eEntityType getEntityType() const {
        switch ( kind ) {
        case ECK_Shopping:
            return eEntityType::ET_SHOG;
        case ECK_HitchHiker:
            return eEntityType::ET_HIHI;
        case ECK_Transport:
            return eEntityType::ET_TRAN;
        case ECK_Convoy:
            return eEntityType::ET_CVOY;
        default:
            return eEntityType::ET_None;
        }
    }

private:
    int32_t unknown0x40 = -1;
    int32_t kind;
};

class AchievementCheckCarsOwnedAll : public AchievementCheck {
public:
    AchievementCheckCarsOwnedAll() {
        // FUN_004158a0
        Flags = ACF_Collection;
        bShowsProgress = true;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089cce0
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 0;
        }

        IPlayerData *pSave = GetLoadedSave();
        if ( pSave == nullptr || gpDatabase == nullptr ) {
            return 1;
        }

        const GameDatabase &gameDB = gpDatabase->getGameDB();
        Garage *pGarage = pSave->getGarage();
        const int32_t numCars = pGarage->getNumCars();

        const uint32_t numEntries = gameDB.pNumItems[1];
        for ( uint32_t i = 0u; i < numEntries; i++ ) {
            DBEntryCarPhysicsData &car = gameDB.DBCarData[i];
            if ( gpDatabase->isBike( &car ) || gpDatabase->isTraffic( &car ) ) {
                continue;
            }

            if ( pOutTotal != nullptr ) {
                *pOutTotal = *pOutTotal + 1;
            }

            bool bOwned = false;
            for ( int32_t slotIndex = 0; slotIndex < numCars; slotIndex++ ) {
                GarageSlot *pSlot =
                    pGarage->getCarAtIndex( slotIndex, 0ull, false );
                if ( pSlot != nullptr &&
                     int32_t( pSlot->getCarConfigID() ) == car.CarID ) {
                    bOwned = true;
                    break;
                }
            }

            if ( !bOwned ) {
                return 0;
            }
        }

        return 1;
    }
};

class AchievementCheckGoldInstantCha : public AchievementCheck {
public:
    AchievementCheckGoldInstantCha( const bool bOnline, const bool bOffline )
        : bCountOnline( bOnline ), bCountOffline( bOffline ) {
        // FUN_00415640 (online), FUN_00415670 (offline) and FUN_004156a0
        // (both).
        Flags = ACF_Challenge;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089c8b0
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 0;
        }

        const AchievementsData *pData = GetAchievements();
        if ( pData == nullptr ) {
            return 0;
        }

        int32_t numWon = 0;
        if ( bCountOnline ) {
            numWon += int32_t( pData->getNumInstantChallengesWonOnline() );
        }
        if ( bCountOffline ) {
            numWon += int32_t( pData->getNumInstantChallengesWonOffline() );
        }

        return numWon;
    }

private:
    bool bCountOnline;
    bool bCountOffline;
};

class AchievementCheckMinExtraGoldCup : public AchievementCheck {
public:
    AchievementCheckMinExtraGoldCup() {
        // FUN_00415b70
        Flags = ACF_Challenge;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089d570
        int32_t numGold = 0;
        const int32_t numTotal =
            CountExtraChallenges( eEntityType::ET_None, &numGold );

        if ( pOutTotal != nullptr ) {
            *pOutTotal = numTotal;
        }

        return numGold;
    }
};

class AchievementCheckAllGoldSolo : public AchievementCheck {
public:
    explicit AchievementCheckAllGoldSolo( const int32_t discipline )
        : discipline( discipline ) {
        // FUN_00415ab0 (speed), FUN_00415ae0 (race), FUN_00415b10 (time) and
        // FUN_00415b40 (any).
        Flags = ACF_Challenge;
        bShowsProgress = true;
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089c6d0
        Spots *pSpots = GetSpotList();
        if ( pSpots == nullptr ) {
            return 1;
        }

        const int32_t numSpots = int32_t( pSpots->getNumWorldSpots() );
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 0;
            for ( int32_t i = 0; i < numSpots; i++ ) {
                if ( matches( *pSpots->getWorldSpot( i ) ) ) {
                    *pOutTotal = *pOutTotal + 1;
                }
            }
        }

        for ( int32_t i = 0; i < numSpots; i++ ) {
            const SpotMap &spot = *pSpots->getWorldSpot( i );
            if ( matches( spot ) && GetChallengeResult( spot ) != 1 ) {
                return 0;
            }
        }

        return 1;
    }

    int32_t getDetailedProgress( int32_t *pOutTotal ) const override {
        // FUN_0089c600
        if ( pOutTotal != nullptr ) {
            *pOutTotal = 0;
        }

        Spots *pSpots = GetSpotList();
        if ( pSpots == nullptr ) {
            return 0;
        }

        int32_t numGoldCups = 0;
        const int32_t numSpots = int32_t( pSpots->getNumWorldSpots() );
        for ( int32_t i = 0; i < numSpots; i++ ) {
            const SpotMap &spot = *pSpots->getWorldSpot( i );
            if ( !matches( spot ) ) {
                continue;
            }

            if ( GetChallengeResult( spot ) == 1 ) {
                numGoldCups++;
            }

            if ( pOutTotal != nullptr ) {
                *pOutTotal = *pOutTotal + 1;
            }
        }

        return numGoldCups;
    }

private:
    bool matches( const SpotMap &spot ) const {
        if ( spot.Type != ST_Challenge ||
             GetSpotChallengeCategory( spot ) != kChallengeCategorySolo ) {
            return false;
        }

        return discipline == -1 ||
               GetSpotChallengeDiscipline( spot ) == discipline;
    }

private:
    int32_t discipline;
};

class AchievementCheckCarsOwnedByBrand : public AchievementCheck {
public:
    AchievementCheckCarsOwnedByBrand( const char *pBrandTag,
                                      const bool bBikes ) {
        // FUN_00415870 (bikes), FUN_004158d0 .. FUN_00415a10 (one brand each)
        // and FUN_00415a50 (every car, whatever its brand).
        Flags = ACF_Collection;
        bBike = bBikes;

        memset( brandTag, 0, sizeof( brandTag ) );
        if ( pBrandTag != nullptr ) {
            strncpy( brandTag, pBrandTag, sizeof( brandTag ) - 1 );
        }
    }

    int32_t getProgress( int32_t *pOutTotal ) const override {
        // FUN_0089cea0
        if ( gpDatabase == nullptr ) {
            if ( pOutTotal != nullptr ) {
                *pOutTotal = 0;
            }
            return 0;
        }

        const GameDatabase &gameDB = gpDatabase->getGameDB();
        const uint32_t numEntries = gameDB.pNumItems[1];

        if ( pOutTotal != nullptr ) {
            *pOutTotal = 0;
            for ( uint32_t i = 0u; i < numEntries; i++ ) {
                DBEntryCarPhysicsData &car = gameDB.DBCarData[i];
                if ( gpDatabase->isTraffic( &car ) ) {
                    continue;
                }

                if ( gpDatabase->isBike( &car ) == bBike ) {
                    *pOutTotal = *pOutTotal + 1;
                }
            }
        }

        IPlayerData *pSave = GetLoadedSave();
        if ( pSave == nullptr ) {
            return 0;
        }

        int32_t pOwnedModels[Garage::kNumSavedCarSlots] = {};
        int32_t numOwnedModels = 0;

        Garage *pGarage = pSave->getGarage();
        const int32_t numCars = pGarage->getNumCars();
        for ( int32_t slotIndex = 0; slotIndex < numCars; slotIndex++ ) {
            GarageSlot *pSlot =
                pGarage->getCarAtIndex( slotIndex, 0ull, false );
            if ( pSlot == nullptr ) {
                continue;
            }

            const int32_t configID = int32_t( pSlot->getCarConfigID() );

            DBEntryCarPhysicsData *pCar = nullptr;
            for ( uint32_t i = 0u; i < numEntries; i++ ) {
                if ( gameDB.DBCarData[i].CarID == configID ) {
                    pCar = &gameDB.DBCarData[i];
                    break;
                }
            }

            if ( pCar == nullptr || gpDatabase->isBike( pCar ) != bBike ) {
                continue;
            }

            if ( brandTag[0] != '\0' ) {
                if ( pCar->Brand == nullptr ) {
                    continue;
                }

                if ( GetIdentifier64bit( pCar->Brand->BrandID.pAsString ) !=
                     GetIdentifier64bit( brandTag ) ) {
                    continue;
                }
            }

            bool bAlreadySeen = false;
            for ( int32_t i = 0; i < numOwnedModels; i++ ) {
                if ( pOwnedModels[i] == pCar->CarID ) {
                    bAlreadySeen = true;
                    break;
                }
            }

            if ( !bAlreadySeen && numOwnedModels < Garage::kNumSavedCarSlots ) {
                pOwnedModels[numOwnedModels++] = pCar->CarID;
            }
        }

        return numOwnedModels;
    }

private:
    char brandTag[8];
    bool bBike = false;
};

AchievementCheck *CreateAchievementCheck( const uint32_t kind ) {
    // FUN_0089ecb0
    switch ( kind ) {
    case AK_HousesOwned:
        return new AchievementCheckHousesOwned();
    case AK_GoldInstantChaAllBots:
        return new AchievementCheckGoldInstantChaAllBots();
    case AK_AllGoldSoloExtraTransp:
        return new AchievementCheckAllGoldSoloExtra(
            AchievementCheckAllGoldSoloExtra::ECK_Transport );
    case AK_MinGoldCup:
        return new AchievementCheckMinGoldCup( kChallengeCategoryAny, -1 );
    case AK_CarsOwnedFerrari:
        return new AchievementCheckCarsOwnedByBrand( "FERRARI", false );
    case AK_RoadExplored:
        return new AchievementCheckRoadExplored();
    case AK_UploadCustom:
        return new AchievementCheckUploadCustom();
    case AK_AllGoldSoloExtraConvoy:
        return new AchievementCheckAllGoldSoloExtra(
            AchievementCheckAllGoldSoloExtra::ECK_Convoy );
    case AK_MinSoloGoldCup:
        return new AchievementCheckMinGoldCup( kChallengeCategorySolo, -1 );
    case AK_CarsOwnedLambo:
        return new AchievementCheckCarsOwnedByBrand( "LAMBO", false );
    case AK_AllRoadExplored:
        return new AchievementCheckAllRoadExplored();
    case AK_MinGoldCustom:
        return new AchievementCheckMinGoldCustom();
    case AK_GoldPaintDeluxeCha:
        return new AchievementCheckGoldChallenge( "RSFE0040" );
    case AK_MinMultiGoldCup:
        return new AchievementCheckMinMultiGoldCup();
    case AK_CarsOwnedMerco:
        return new AchievementCheckCarsOwnedByBrand( "MERCO", false );
    case AK_MinClubGoldCup:
        return new AchievementCheckMinClubGoldCup();
    case AK_MinETradeTransaction:
        return new AchievementCheckMinETradeTransaction();
    case AK_AllCarDealerUnlocked:
        return new AchievementCheckAllCarDealerUnlocked();
    case AK_MinExtraGoldCup:
        return new AchievementCheckMinExtraGoldCup();
    case AK_CarsOwnedChevy:
        return new AchievementCheckCarsOwnedByBrand( "CHEVY", false );
    case AK_IsClubMember:
        return new AchievementCheckIsClubMember();
    case AK_ClothesOwnedBenSherman:
        return new AchievementCheckClothesOwned( "SHERMAN" );
    case AK_RoadTraveled:
        return new AchievementCheckRoadTraveled();
    case AK_AllGoldSolo:
        return new AchievementCheckAllGoldSolo( -1 );
    case AK_CarsOwnedAston:
        return new AchievementCheckCarsOwnedByBrand( "ASTON", false );
    case AK_CarTunedHistory:
        return new AchievementCheckCarTunedHistory();
    case AK_ClothesOwnedEcko:
        return new AchievementCheckClothesOwned( "ECKO" );
    case AK_AllGoldSoloTime:
        return new AchievementCheckAllGoldSolo( 0 );
    case AK_CarsOwnedFord:
        return new AchievementCheckCarsOwnedByBrand( "FORD", false );
    case AK_GoldGeekCha:
        return new AchievementCheckGoldChallenge( "RSFE0029" );
    case AK_MinSpecialPhotos:
        return new AchievementCheckMinSpecialPhotos();
    case AK_AllGoldSoloRace:
        return new AchievementCheckAllGoldSolo( 3 );
    case AK_CarsOwnedAll:
        return new AchievementCheckCarsOwnedAll();
    case AK_GoldInstantCha:
        return new AchievementCheckGoldInstantCha( true, true );
    case AK_LevelReached:
        return new AchievementCheckLevelReached();
    case AK_AllGoldSoloSpeed:
        return new AchievementCheckAllGoldSolo( 1 );
    case AK_BikesOwned:
        return new AchievementCheckCarsOwnedByBrand( nullptr, true );
    case AK_GoldInstantChaOffline:
        return new AchievementCheckGoldInstantCha( false, true );
    case AK_AllGoldSoloExtraShopping:
        return new AchievementCheckAllGoldSoloExtra(
            AchievementCheckAllGoldSoloExtra::ECK_Shopping );
    case AK_AllGoldSoloExtra:
        return new AchievementCheckAllGoldSoloExtra(
            AchievementCheckAllGoldSoloExtra::ECK_Any );
    case AK_AllHousesOwned:
        return new AchievementCheckAllHousesOwned();
    case AK_GoldInstantChaOnline:
        return new AchievementCheckGoldInstantCha( true, false );
    case AK_AllGoldSoloExtraHitch:
        return new AchievementCheckAllGoldSoloExtra(
            AchievementCheckAllGoldSoloExtra::ECK_HitchHiker );
    case AK_CarsOwnedPC:
        return new AchievementCheckCarsOwnedByBrand( nullptr, false );
    default:
        break;
    }

    return nullptr;
}

#ifdef OTDU_USE_IMGUI
const char *GetAchievementKindName( const uint32_t kind ) {
    switch ( kind ) {
    case AK_CarsOwned:
        return "CarsOwned";
    case AK_HousesOwned:
        return "HousesOwned";
    case AK_GoldInstantChaAllBots:
        return "GoldInstantChaAllBots";
    case AK_AllGoldSoloExtraTransp:
        return "AllGoldSoloExtraTransp";
    case AK_MinGoldCup:
        return "MinGoldCup";
    case AK_CarsOwnedFerrari:
        return "CarsOwnedFerrari";
    case AK_RoadExplored:
        return "RoadExplored";
    case AK_UploadCustom:
        return "UploadCustom";
    case AK_AllGoldSoloExtraConvoy:
        return "AllGoldSoloExtraConvoy";
    case AK_MinSoloGoldCup:
        return "MinSoloGoldCup";
    case AK_CarsOwnedLambo:
        return "CarsOwnedLambo";
    case AK_AllRoadExplored:
        return "AllRoadExplored";
    case AK_MinGoldCustom:
        return "MinGoldCustom";
    case AK_GoldPaintDeluxeCha:
        return "GoldPaintDeluxeCha";
    case AK_MinMultiGoldCup:
        return "MinMultiGoldCup";
    case AK_CarsOwnedMerco:
        return "CarsOwnedMerco";
    case AK_MinClubGoldCup:
        return "MinClubGoldCup";
    case AK_MinETradeTransaction:
        return "MinETradeTransaction";
    case AK_AllCarDealerUnlocked:
        return "AllCarDealerUnlocked";
    case AK_MinExtraGoldCup:
        return "MinExtraGoldCup";
    case AK_CarsOwnedChevy:
        return "CarsOwnedChevy";
    case AK_IsClubMember:
        return "IsClubMember";
    case AK_ClothesOwnedBenSherman:
        return "ClothesOwnedBenSherman";
    case AK_RoadTraveled:
        return "RoadTraveled";
    case AK_AllGoldSolo:
        return "AllGoldSolo";
    case AK_CarsOwnedAston:
        return "CarsOwnedAston";
    case AK_CarTunedHistory:
        return "CarTunedHistory";
    case AK_ClothesOwnedEcko:
        return "ClothesOwnedEcko";
    case AK_NovicePts:
        return "NovicePts";
    case AK_AllGoldSoloTime:
        return "AllGoldSoloTime";
    case AK_CarsOwnedFord:
        return "CarsOwnedFord";
    case AK_GoldGeekCha:
        return "GoldGeekCha";
    case AK_MinSpecialPhotos:
        return "MinSpecialPhotos";
    case AK_ProPts:
        return "ProPts";
    case AK_AllGoldSoloRace:
        return "AllGoldSoloRace";
    case AK_CarsOwnedAll:
        return "CarsOwnedAll";
    case AK_GoldInstantCha:
        return "GoldInstantCha";
    case AK_LevelReached:
        return "LevelReached";
    case AK_ExpertPts:
        return "ExpertPts";
    case AK_AllGoldSoloSpeed:
        return "AllGoldSoloSpeed";
    case AK_BikesOwned:
        return "BikesOwned";
    case AK_GoldInstantChaOffline:
        return "GoldInstantChaOffline";
    case AK_AllGoldSoloExtraShopping:
        return "AllGoldSoloExtraShopping";
    case AK_MasterPts:
        return "MasterPts";
    case AK_AllGoldSoloExtra:
        return "AllGoldSoloExtra";
    case AK_AllHousesOwned:
        return "AllHousesOwned";
    case AK_GoldInstantChaOnline:
        return "GoldInstantChaOnline";
    case AK_AllGoldSoloExtraHitch:
        return "AllGoldSoloExtraHitch";
    case AK_AcePts:
        return "AcePts";
    case AK_CarsOwnedPC:
        return "CarsOwnedPC";
    case AK_LevelReachedHardcore:
        return "LevelReachedHardcore";
    default:
        return "<unknown>";
    }
}
#endif
