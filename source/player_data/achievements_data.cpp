#include "shared.h"
#include "achievements_data.h"

#include "core/crypto/tea.h"
#include "core/ed_store.h"
#include "database/gs_database.h"
#include "game/gm_base.h"
#include "game/vehicle/car_desc.h"
#include "gs_playerdata.h"
#include "player_data_interface.h"
#include "saved_extra_challenges.h"
#include "saved_garage.h"
#include "tdu_instance.h"


static bool gbForceAchievementUnlock = false; // DAT_016a2d2f

static constexpr uint64_t kRewardUnknown0x5912a0 = 0x5912a0ull;
static constexpr uint64_t kRewardChrono = 0x52f820ull;
static constexpr uint64_t kRewardSnapshot = 0x560560ull;
static constexpr uint64_t kRewardGameModeEditor = 0x578c00ull;
static constexpr uint64_t kRewardHouse = 0x5c1fe0ull;
static const char *const kRewardHouseSpotName = "EPH_L4A_0340";

static constexpr uint64_t kRewardNone = 0x517180ull;

AchievementsData::AchievementsData()
    : achievements(),
      bMustLevelUp( false ),
      currentGameMode( -1 ),
      field516_0x204( false ),
      bReady( false ),
      flagAsync( 0x80000000u ),
      pNumCarsTuned{},
      pNumInstantChallengesWonOnline{},
      pNumInstantChallengesWonOffline{},
      pNumCustomChallengesUploaded{},
      pNumCustomChallengesWon{},
      pNumETradeTransactions{},
      exploredRoadLength( -1 ),
      totalRoadLength( -1 ),
      bAchievementFlags{},
      pRewardCarsBought{},
      bDisplayLevelUp( false ),
      bQuiet( false ),
      bReward0x5912a0Unlocked( false ),
      bSimulationModeUnlocked( false ),
      pointsNeeded{} {
    // FUN_0089eb80
    reset();

    pointsNeeded[0] = 40u;
    pointsNeeded[1] = 130u;
    pointsNeeded[2] = 300u;
    pointsNeeded[3] = 500u;
    pointsNeeded[4] = 1000u;
}

uint32_t AchievementsData::getNumCarsTuned() const {
    return TestDriveTEAReadDword( pNumCarsTuned );
}

uint32_t AchievementsData::getNumInstantChallengesWonOnline() const {
    return TestDriveTEAReadDword( pNumInstantChallengesWonOnline );
}

uint32_t AchievementsData::getNumInstantChallengesWonOffline() const {
    return TestDriveTEAReadDword( pNumInstantChallengesWonOffline );
}

uint32_t AchievementsData::getNumCustomChallengesUploaded() const {
    return TestDriveTEAReadDword( pNumCustomChallengesUploaded );
}

uint32_t AchievementsData::getNumCustomChallengesWon() const {
    return TestDriveTEAReadDword( pNumCustomChallengesWon );
}

uint32_t AchievementsData::getNumETradeTransactions() const {
    return TestDriveTEAReadDword( pNumETradeTransactions );
}

AchievementsData::~AchievementsData() { releaseChecks(); }

void AchievementsData::build() {
    // FUN_0089f490
    releaseChecks();

    if ( gpDatabase == nullptr ) {
        return;
    }

    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const uint32_t numEntries = gameDB.pNumItems[kAchievementDatabaseIndex];
    if ( numEntries == 0u || gameDB.DBAchivement == nullptr ) {
        return;
    }

    achievements.reserve( numEntries );
    for ( uint32_t i = 0u; i < numEntries; i++ ) {
        buildCheck( gameDB.DBAchivement[i] );
    }

    OTDU_LOG_DEBUG( "Built %d achievement check(s) out of %u database row(s); "
                    "level thresholds %u/%u/%u/%u/%u\n",
                    getNumAchievements(), numEntries, pointsNeeded[0],
                    pointsNeeded[1], pointsNeeded[2], pointsNeeded[3],
                    pointsNeeded[4] );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (FUN_0089c0e0)

    if ( bReady ) {
        return;
    }

    bReady = true;

    if ( field516_0x204 && gpTestDriveInstance != nullptr ) {
        currentGameMode = gpTestDriveInstance->getActiveGameMode();
    }

    notify( ACF_Challenge | ACF_Road | ACF_Collection, false );
}

void AchievementsData::buildCheck( const DBEntryAchievement &entry ) {
    // FUN_0089ecb0
    const DBEntryList &kindList =
        gpDatabase->getGameDB().Databases[kAchievementDatabaseIndex];

    const uint64_t kind = kindList.getHashcodeForString( entry.Kind.pAsString );
    if ( ( kind >> 32ull ) != 0ull ) {
        return;
    }

    switch ( uint32_t( kind ) ) {
    case AK_NovicePts:
        pointsNeeded[0] = uint32_t( entry.NumItemsReq );
        return;
    case AK_ProPts:
        pointsNeeded[1] = uint32_t( entry.NumItemsReq );
        return;
    case AK_ExpertPts:
        pointsNeeded[2] = uint32_t( entry.NumItemsReq );
        return;
    case AK_MasterPts:
        pointsNeeded[3] = uint32_t( entry.NumItemsReq );
        return;
    case AK_AcePts:
        pointsNeeded[4] = uint32_t( entry.NumItemsReq );
        return;
    default:
        break;
    }

    AchievementCheck *pCheck = CreateAchievementCheck( uint32_t( kind ) );
    if ( pCheck == nullptr ) {
        return;
    }

    pCheck->NumPoints = uint32_t( entry.NumPoints );
    pCheck->NumItemsReq = uint32_t( entry.NumItemsReq );
    pCheck->TileHashcode =
        kindList.getHashcodeForString( entry.Tile.pAsString );
    pCheck->TextHashcode =
        kindList.getHashcodeForString( entry.Text.pAsString );
    pCheck->RewardHashcode =
        kindList.getHashcodeForString( entry.Reward.pAsString );
    pCheck->RewardData = entry.RewardData;

    achievements.push_back( pCheck );

    pCheck->onBuilt();
}

void AchievementsData::releaseChecks() {
    for ( AchievementCheck *pCheck : achievements ) {
        delete pCheck;
    }

    achievements.clear();
}

void AchievementsData::reset() {
    // FUN_0089e980
    field516_0x204 = false;
    bMustLevelUp = false;
    currentGameMode = -1;

    TestDriveTEAWriteDword( pNumCarsTuned, 0u );
    TestDriveTEAWriteDword( pNumInstantChallengesWonOnline, 0u );
    TestDriveTEAWriteDword( pNumInstantChallengesWonOffline, 0u );
    TestDriveTEAWriteDword( pNumCustomChallengesUploaded, 0u );
    TestDriveTEAWriteDword( pNumCustomChallengesWon, 0u );
    TestDriveTEAWriteDword( pNumETradeTransactions, 0u );

    memset( bAchievementFlags, 0, sizeof( bAchievementFlags ) );

    for ( int32_t i = 0; i < kNumRewardCars; i++ ) {
        TestDriveTEAWriteDword( pRewardCarsBought[i], 0u );
    }

    for ( AchievementCheck *pAchievement : achievements ) {
        if ( pAchievement != nullptr ) {
            pAchievement->bLock = true;
        }
    }

    exploredRoadLength = -1;
    totalRoadLength = -1;
    flagAsync = kNoPendingAsyncNotification;

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE
}

void AchievementsData::tick() {
    // FUN_0089e240
    processAsyncRequest();

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (the original then drives the
                             // level-up popup off bMustLevelUp and
                             // bDisplayLevelUp; AchievementPopUp is not
                             // ported)
}

void AchievementsData::processAsyncRequest() {
    // FUN_0089e920
    if ( flagAsync == kNoPendingAsyncNotification ) {
        return;
    }

    notify( flagAsync, false );
    flagAsync = kNoPendingAsyncNotification;
}

bool AchievementsData::notify( const uint32_t eventMask, const bool bAsync ) {
    // FUN_0089e060
    if ( !field516_0x204 || !bReady || gpActiveGameMode == nullptr ) {
        return false;
    }

    const eGameMode activeGameMode = gpActiveGameMode->getCurrentGameMode();
    if ( activeGameMode == GM_Login || activeGameMode == GM_BootMenu ||
         activeGameMode == GM_VideoBumper ) {
        return false;
    }

    if ( bAsync ) {
        // Only the first request in flight is kept; the original guards this
        // with its own mutex.
        if ( flagAsync == kNoPendingAsyncNotification ) {
            flagAsync = eventMask;
        }

        return false;
    }

    bool bUnlockedAny = false;

    const int32_t levelBefore = getPlayerLevel();

    if ( ( eventMask & ACF_Road ) != 0u ) {
        // The explored road length is cached; a road event invalidates it.
        exploredRoadLength = -1;
    }

    for ( AchievementCheck *pCheck : achievements ) {
        if ( pCheck != nullptr ) {
            bUnlockedAny |= refreshCheck( pCheck, eventMask, bQuiet );
        }
    }

    const int32_t levelAfter = getPlayerLevel();
    if ( levelBefore != levelAfter ) {
        if ( levelBefore == 0 ) {
            // Leaving Rookie turns the help system off
            if ( gpPlayerData != nullptr ) {
                gpPlayerData->getLocalPlayerData().bHelpSystem = false;
            }
            OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (and pushes
                                     // '/:IsHelpSystem' to the HUD movie)
        }

        IPlayerData *pSave = ( gpPlayerData != nullptr )
                                 ? gpPlayerData->getLoadedSave()
                                 : nullptr;
        if ( pSave != nullptr ) {
            pSave->getExtraChallenges()->PlayerLevel = levelAfter;
        }

        if ( !bQuiet ) {
            bMustLevelUp = true;
        }

        for ( AchievementCheck *pCheck : achievements ) {
            if ( pCheck != nullptr ) {
                bUnlockedAny |= refreshCheck( pCheck, eventMask, bQuiet );
            }
        }
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (the original then refreshes
                             // '/:m_CurrentCption' on the HUD movie)

    return bUnlockedAny;
}

bool AchievementsData::refreshCheck( AchievementCheck *pCheck,
                                     const uint32_t eventMask,
                                     const bool bQuietRefresh ) {
    // FUN_0089dd60
    if ( !pCheck->bLock ) {
        // Already unlocked.
        return false;
    }

    const uint32_t checkFlags = uint32_t( pCheck->Flags );
    if ( ( checkFlags & eventMask ) != checkFlags ) {
        return false;
    }

    const int32_t progress = pCheck->getProgress( nullptr );
    pCheck->bLock = uint32_t( progress ) < pCheck->NumItemsReq;

    if ( pCheck->bLock && !gbForceAchievementUnlock ) {
        return false;
    }

    if ( pCheck->RewardHashcode != 0xffffffffffffffffull &&
         pCheck->RewardHashcode != kRewardNone ) {
        grantReward( pCheck );

        if ( !bQuietRefresh ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (FUN_0089bf80 queues
                                     // the 'reward unlocked' popup)
        }

        return false;
    }

    if ( !bQuietRefresh ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (FUN_0089db30 queues the
                                 // 'achievement unlocked' popup)
    }

    return !pCheck->bLock;
}

void AchievementsData::grantReward( const AchievementCheck *pCheck ) {
    // FUN_0089e330
    const uint64_t reward = pCheck->RewardHashcode;
    if ( reward == 0xffffffffffffffffull || reward == kRewardNone ) {
        return;
    }

    switch ( reward ) {
    case kRewardUnknown0x5912a0:
        // BOOL_010e7d34 - ??? (could be Ford Island/racetrack)
        bReward0x5912a0Unlocked = true;
        return;

    case kSimuModeRewardHashcode:
        // BOOL_010e7d35 - simulation / hardcore mode.
        bSimulationModeUnlocked = true;
        return;

    case kRewardChrono:
    case kRewardSnapshot: // Bookmark
    case kRewardGameModeEditor:
        // '/:Unlock_Chrono', '/:Unlock_Snapshot' and '/:Unlock_GME' on the
        // frontend movie.
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (set the matching
                                 // frontend variable)
        return;

    case kRewardHouse: {
        IPlayerData *pSave = ( gpPlayerData != nullptr )
                                 ? gpPlayerData->getLoadedSave()
                                 : nullptr;
        if ( pSave == nullptr ) {
            return;
        }

        const uint64_t houseHashcode =
            GetIdentifier64bit( kRewardHouseSpotName );
        if ( pSave->getGarage()->isHouseOwned( houseHashcode ) ) {
            return;
        }

        OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (FUN_00894d50 adds the
                                 // house to the garage, then the original
                                 // notifies again with mask 7)
        return;
    }

    default:
        // 0x547ec0, 0x519890 and 0x5da680 are hashcodes the switch knows and
        // deliberately does nothing for.
        break;
    }
}

void AchievementsData::unlockAllChecks() {
    // FUN_0089e8d0
    for ( AchievementCheck *pCheck : achievements ) {
        if ( pCheck == nullptr ) {
            continue;
        }

        pCheck->bLock = false;
        grantReward( pCheck );
    }
}

bool AchievementsData::isRewardLocked( const uint64_t rewardHashcode ) const {
    // FUN_0089bd30
    for ( size_t i = 1; i < achievements.size(); i++ ) {
        const AchievementCheck *pAchievement = achievements[i];
        if ( pAchievement != nullptr &&
             pAchievement->RewardHashcode == rewardHashcode ) {
            return achievements[i]->bLock;
        }
    }

    return true;
}

uint32_t AchievementsData::getAchievedPoints() const {
    // FUN_0089bd90
    uint32_t achievedPoints = 0u;
    for ( const AchievementCheck *pAchievement : achievements ) {
        if ( pAchievement != nullptr && !pAchievement->bLock ) {
            achievedPoints += pAchievement->NumPoints;
        }
    }

    return achievedPoints;
}

int32_t AchievementsData::getPlayerLevel() const {
    // FUN_0089c210
    const uint32_t achievedPoints = getAchievedPoints();

    if ( pointsNeeded[4] <= achievedPoints ) {
        return 5;
    }
    if ( pointsNeeded[3] <= achievedPoints ) {
        return 4;
    }
    if ( pointsNeeded[2] <= achievedPoints ) {
        return 3;
    }
    if ( pointsNeeded[1] <= achievedPoints ) {
        return 2;
    }

    return int32_t( pointsNeeded[0] <= achievedPoints );
}

void AchievementsData::deserialize( edStore &stream ) {
    // FUN_0089e640
    pNumCarsTuned[0] = stream.readULong();
    pNumCarsTuned[1] = stream.readULong();
    pNumInstantChallengesWonOnline[0] = stream.readULong();
    pNumInstantChallengesWonOnline[1] = stream.readULong();
    pNumInstantChallengesWonOffline[0] = stream.readULong();
    pNumInstantChallengesWonOffline[1] = stream.readULong();

    pNumCustomChallengesUploaded[0] = stream.readULong();
    pNumCustomChallengesUploaded[1] = stream.readULong();
    pNumCustomChallengesWon[0] = stream.readULong();
    pNumCustomChallengesWon[1] = stream.readULong();
    pNumETradeTransactions[0] = stream.readULong();
    pNumETradeTransactions[1] = stream.readULong();

    for ( bool &bFlag : bAchievementFlags ) {
        bFlag = stream.readByte() != 0u;
    }

    for ( int32_t i = 0; i < kNumRewardCars; i++ ) {
        pRewardCarsBought[i][0] = stream.readULong();
        pRewardCarsBought[i][1] = stream.readULong();
    }

    deserializeChecks( stream );
}

void AchievementsData::serialize( edStore &stream ) const {
    // FUN_0089ca40
    stream.writeInteger( int32_t( pNumCarsTuned[0] ) );
    stream.writeInteger( int32_t( pNumCarsTuned[1] ) );
    stream.writeInteger( int32_t( pNumInstantChallengesWonOnline[0] ) );
    stream.writeInteger( int32_t( pNumInstantChallengesWonOnline[1] ) );
    stream.writeInteger( int32_t( pNumInstantChallengesWonOffline[0] ) );
    stream.writeInteger( int32_t( pNumInstantChallengesWonOffline[1] ) );

    stream.writeInteger( int32_t( pNumCustomChallengesUploaded[0] ) );
    stream.writeInteger( int32_t( pNumCustomChallengesUploaded[1] ) );
    stream.writeInteger( int32_t( pNumCustomChallengesWon[0] ) );
    stream.writeInteger( int32_t( pNumCustomChallengesWon[1] ) );
    stream.writeInteger( int32_t( pNumETradeTransactions[0] ) );
    stream.writeInteger( int32_t( pNumETradeTransactions[1] ) );

    for ( const bool bFlag : bAchievementFlags ) {
        stream.writeByte( bFlag ? 1u : 0u );
    }

    for ( int32_t i = 0; i < kNumRewardCars; i++ ) {
        stream.writeInteger( int32_t( pRewardCarsBought[i][0] ) );
        stream.writeInteger( int32_t( pRewardCarsBought[i][1] ) );
    }

    serializeChecks( stream );
}

void AchievementsData::serializeChecks( edStore &stream ) const {
    // FUN_0089ca40 (tail)
    const int32_t numChecks = getNumAchievements();
    if ( numChecks == 0 ) {
        return;
    }

    stream.writeInteger( numChecks );

    for ( const AchievementCheck *pCheck : achievements ) {
        if ( pCheck == nullptr ) {
            continue;
        }

        stream.writeInteger( pCheck->bLock ? 1 : 0 );
    }
}

void AchievementsData::deserializeChecks( edStore &stream ) {
    // FUN_0089e520
    const int32_t numChecks = getNumAchievements();
    if ( numChecks == 0 ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE
                                 // (AchievementsData::build)
        OTDU_LOG_WARN( "The achievement check table is empty: the tail of "
                       "'playersave' will not line up\n" );
        return;
    }

    const int32_t numSavedChecks = stream.readInteger();
    if ( numSavedChecks != numChecks ) {
        for ( int32_t i = 0; i < numSavedChecks; i++ ) {
            stream.readInteger();
        }
    } else {
        for ( int32_t i = 0; i < numChecks; i++ ) {
            AchievementCheck *pCheck = achievements[i];
            if ( pCheck == nullptr ) {
                stream.readInteger();
                continue;
            }

            pCheck->bLock = stream.readInteger() != 0;
            if ( pCheck->bLock ) {
                continue;
            }

            if ( pCheck->RewardHashcode == 0xffffffffffffffffull ||
                 pCheck->RewardHashcode == 0x517180ull ) {
                continue;
            }

            grantReward( pCheck );
        }
    }

    if ( field516_0x204 ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (the original stamps the
                                 // current game mode here)
    }

    notify( ACF_Challenge | ACF_Road | ACF_Collection, false );
}
