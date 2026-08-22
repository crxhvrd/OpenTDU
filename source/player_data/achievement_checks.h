#pragma once

// Every kind the switch in FUN_0089ecb0 knows about. The values are the
// hashcodes TDU_Achievements stores in its 'Achievement_Event_' column; the
// names come from the localised database (DB_XX.bnk).
enum eAchievementKind : uint32_t {
    AK_CarsOwned = 0x03357c87u, // unused?
    AK_HousesOwned = 0x03370327u,
    AK_GoldInstantChaAllBots = 0x033889c7u,
    AK_AllGoldSoloExtraTransp = 0x033a1067u,
    AK_MinGoldCup = 0x03433827u,
    AK_CarsOwnedFerrari = 0x0344bec7u,
    AK_RoadExplored = 0x03464567u,
    AK_UploadCustom = 0x0347cc07u,
    AK_AllGoldSoloExtraConvoy = 0x034952a7u,
    AK_MinSoloGoldCup = 0x03527a67u,
    AK_CarsOwnedLambo = 0x03540107u,
    AK_AllRoadExplored = 0x035587a7u,
    AK_MinGoldCustom = 0x03570e47u,
    AK_GoldPaintDeluxeCha = 0x035894e7u,
    AK_MinMultiGoldCup = 0x0361bca7u,
    AK_CarsOwnedMerco = 0x03634347u,
    AK_MinClubGoldCup = 0x0364c9e7u,
    AK_MinETradeTransaction = 0x03665087u,
    AK_AllCarDealerUnlocked = 0x0367d727u,
    AK_MinExtraGoldCup = 0x0370fee7u,
    AK_CarsOwnedChevy = 0x03728587u,
    AK_IsClubMember = 0x03740c27u,
    AK_ClothesOwnedBenSherman = 0x037592c7u,
    AK_RoadTraveled = 0x03771967u,
    AK_AllGoldSolo = 0x03804127u,
    AK_CarsOwnedAston = 0x0381c7c7u,
    AK_CarTunedHistory = 0x03834e67u,
    AK_ClothesOwnedEcko = 0x0384d507u,
    AK_NovicePts = 0x03865ba7u, // a level threshold, not a check
    AK_AllGoldSoloTime = 0x038f8367u,
    AK_CarsOwnedFord = 0x03910a07u,
    AK_GoldGeekCha = 0x039290a7u,
    AK_MinSpecialPhotos = 0x03941747u,
    AK_ProPts = 0x03959de7u, // a level threshold, not a check
    AK_AllGoldSoloRace = 0x039ec5a7u,
    AK_CarsOwnedAll = 0x03a04c47u,
    AK_GoldInstantCha = 0x03a1d2e7u,
    AK_LevelReached = 0x03a35987u,
    AK_ExpertPts = 0x03a4e027u, // a level threshold, not a check
    AK_AllGoldSoloSpeed = 0x03ae07e7u,
    AK_BikesOwned = 0x03af8e87u,
    AK_GoldInstantChaOffline = 0x03b11527u,
    AK_AllGoldSoloExtraShopping = 0x03b29bc7u,
    AK_MasterPts = 0x03b42267u, // a level threshold, not a check
    AK_AllGoldSoloExtra = 0x03bd4a27u,
    AK_AllHousesOwned = 0x03bed0c7u,
    AK_GoldInstantChaOnline = 0x03c05767u,
    AK_AllGoldSoloExtraHitch = 0x03c1de07u,
    AK_AcePts = 0x03c364a7u, // a level threshold, not a check
    AK_CarsOwnedPC = 0x0440f917u,
    
    AK_LevelReachedHardcore = 0x2afa2687u,
};

enum eAchievementCheckFlags : int32_t {
    ACF_Challenge = 1,
    ACF_Road = 2,
    ACF_Collection = 4,
    ACF_Level = 8,
};

struct AchievementCheck {
    bool bLock = true;                               
    int32_t Flags = 1;                              
    bool bShowsProgress = false;                  
    uint32_t NumPoints = 0u;                        
    uint32_t NumItemsReq = 0u;                     
    uint64_t TileHashcode = 0ull;                   
    uint64_t TextHashcode = 0ull;                   
    uint64_t RewardHashcode = 0xffffffffffffffffull;
    int32_t RewardData = 0;                         

    AchievementCheck() = default;
    virtual ~AchievementCheck() = default;

    virtual void onBuilt() {}
    virtual int32_t getProgress( int32_t *pOutTotal ) const;
    virtual int32_t getDetailedProgress( int32_t *pOutTotal ) const {
        return getProgress( pOutTotal );
    }
};

AchievementCheck *CreateAchievementCheck( const uint32_t kind );
