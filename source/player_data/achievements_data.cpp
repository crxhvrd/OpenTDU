#include "shared.h"
#include "achievements_data.h"

#include "ai/gs_ai.h"
#include "core/crypto/tea.h"
#include "core/ed_store.h"
#include "database/gs_database.h"
#include "core/time.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/mng_flash_localize.h"
#include "game/gm_base.h"
#include "game/gm_scene.h"
#include "game/vehicle/car_desc.h"
#include "gs_playerdata.h"
#include "player_data_interface.h"
#include "saved_extra_challenges.h"
#include "saved_garage.h"
#include "saved_mailbox.h"
#include "saved_news.h"
#include "saved_road_discovery.h"
#include "tdu_instance.h"

static bool gbForceAchievementUnlock = false; // DAT_016a2d2f

static constexpr uint64_t kRewardUnknown0x5912a0 = 0x5912a0ull;
static constexpr uint64_t kRewardHouse = 0x5c1fe0ull;
static const char *const kRewardHouseSpotName = "EPH_L4A_0340";

static constexpr uint64_t kRewardNone = 0x517180ull;

static constexpr uint64_t kLevelUpTextHashcode = 0x3867e52ull;
static constexpr uint64_t kLevelUpMailTitleHashcode = 0x3c07a12ull;
static constexpr uint64_t kCurrentCaptionHashcode = 0x37bf702ull;
static constexpr uint64_t kCurrentLevelHashcode = 0x3a502d2ull;

// How long a level up / achievement unlock panel stays up.
static constexpr float kPopupDisplayDuration = 12.0f;            // DAT_00f41850
static constexpr float kMetresToKilometres = 0.001f;             // DAT_00f4167c
static constexpr float kKilometresToMiles = 0.6215040683746338f; // DAT_00f418c0
static constexpr int32_t kBDDMenus = 0;
static constexpr int32_t kBDDAchievement =
    int32_t( AchievementsData::kAchievementDatabaseIndex );

static constexpr int32_t kAchievementTitleLength = 0x80;
static constexpr int32_t kAchievementTextLength = 0x800;

AchievementsData::AchievementsData()
    : achievements(),
      bMustLevelUp( false ),
      pLevelUpText{},
      levelUpDisplayTime( 0.0f ),
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

int32_t AchievementsData::getTotalRoadLength() {
    // FUN_0089d240
    if ( totalRoadLength < 0 && gpAI != nullptr ) {
        const float length = GetRoadNetworkLength( &gpAI->getRoadMap(), true );
        totalRoadLength = static_cast<int32_t>( length * kMetresToKilometres *
                                                kKilometresToMiles );
    }
    return totalRoadLength;
}

int32_t AchievementsData::getExploredRoadLength() {
    // FUN_0089d2a0
    if ( exploredRoadLength < 0 && gpAI != nullptr ) {
        const float length = GetRoadNetworkLength( &gpAI->getRoadMap(), false );
        exploredRoadLength = static_cast<int32_t>(
            length * kMetresToKilometres * kKilometresToMiles );
    }
    return exploredRoadLength;
}

AchievementsData::~AchievementsData() { releaseChecks(); }

static inline int32_t GetCheckPanelID( const AchievementCheck *pCheck ) {
    if ( pCheck->RewardHashcode == 0xffffffffffffffffull ||
         pCheck->RewardHashcode == kRewardNone ) {
        return pCheck->RewardData;
    }

    return -1;
}

void AchievementsData::publishLevelSteps( FlashPlayer *pMovie ) const {
    // FUN_0089d960
    if ( pMovie == nullptr ) {
        return;
    }

    pMovie->setVariableValue( "/:stepNovice", pointsNeeded[0] );
    pMovie->setVariableValue( "/:stepPro", pointsNeeded[1] );
    pMovie->setVariableValue( "/:stepExpert", pointsNeeded[2] );
    pMovie->setVariableValue( "/:stepMaster", pointsNeeded[3] );
}

void AchievementsData::publishCheckStates( FlashPlayer *pMovie ) const {
    // FUN_0089d920
    if ( pMovie == nullptr ) {
        return;
    }

    for ( const AchievementCheck *pCheck : achievements ) {
        if ( pCheck == nullptr ) {
            continue;
        }

        // FUN_0089d5a0 (inlined)
        if ( pCheck->RewardHashcode != 0xffffffffffffffffull &&
             pCheck->RewardHashcode != kRewardNone ) {
            continue;
        }

        char varName[64];
        snprintf( varName, sizeof( varName ), "/:achv_%d", pCheck->RewardData );
        pMovie->setVariableValue( varName, pCheck->bLock ? 0 : 1 );
    }
}

void AchievementsData::publishAchievementDetails( FlashPlayer *pMovie,
                                                  const int32_t panelID ) {
    // FUN_0089d7c0
    if ( pMovie == nullptr ) {
        return;
    }

    exploredRoadLength = -1;

    int32_t numItemsTotal = 0;
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const uint32_t numEntries = gameDB.pNumItems[kAchievementDatabaseIndex];
    for ( uint32_t i = 0u; i < numEntries; i++ ) {
        const DBEntryAchievement &entry = gameDB.DBAchivement[i];
        if ( entry.RewardData != panelID ) {
            continue;
        }

        numItemsTotal = entry.NumItemsReq;
        pMovie->setVariableValue( "/:AchvTitle", entry.Tile.pAsString );
        pMovie->setVariableValue( "/:AchvDescr", entry.Description.pAsString );
        pMovie->setVariableValue( "/:achvPoints", entry.NumPoints );
        pMovie->setVariableValue( "/:achvTotal", numItemsTotal );
        break;
    }

    for ( const AchievementCheck *pCheck : achievements ) {
        if ( pCheck == nullptr || GetCheckPanelID( pCheck ) != panelID ) {
            continue;
        }

        int32_t numItemsDone = 0;
        if ( pCheck->bShowsProgress ) {
            numItemsDone = pCheck->getDetailedProgress( &numItemsTotal );
            pMovie->setVariableValue( "/:achvTotal", numItemsTotal );
        } else {
            numItemsDone = pCheck->getProgress( nullptr );
        }

        if ( !pCheck->bLock ) {
            numItemsDone = numItemsTotal;
        } else if ( numItemsDone >= numItemsTotal ) {
            numItemsDone = numItemsTotal;
        }

        pMovie->setVariableValue( "/:achvDone", numItemsDone );
        return;
    }
}

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

    if ( gpPlayerData != nullptr ) {
        gpPlayerData->setHasHardcoreMode( true );
        if ( gpPlayerData->getLoadedSave() != nullptr ) {
            gpPlayerData->getLoadedSave()
                ->getAchievements()
                ->applyHardcoreLevelTexts();
        }
    }

    markReady();
}

void AchievementsData::markReady() {
    if ( bReady ) {
        return;
    }

    bReady = true;

    if ( field516_0x204 && gpTestDriveInstance != nullptr ) {
        currentGameMode = gpTestDriveInstance->getActiveGameMode();
    }

    notify( ACF_Challenge | ACF_Road | ACF_Collection, false );
}

void AchievementsData::addCarTuned() {
    // FUN_00416000
    TestDriveTEAWriteDword( pNumCarsTuned,
                            TestDriveTEAReadDword( pNumCarsTuned ) + 1u );
}

void AchievementsData::addRewardCarBought( const uint32_t carID ) {
    // FUN_0089cb70
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const DBEntryList &kindList = gameDB.Databases[kAchievementDatabaseIndex];
    const uint32_t numEntries = gameDB.pNumItems[kAchievementDatabaseIndex];

    for ( uint32_t i = 0u; i < numEntries; i++ ) {
        const DBEntryAchievement &entry = gameDB.DBAchivement[i];
        if ( kindList.getHashcodeForString( entry.Reward.pAsString ) !=
                 kCarRewardHashcode ||
             uint32_t( entry.RewardData ) != carID ) {
            continue;
        }

        for ( uint32_t ( &rewardCar )[2] : pRewardCarsBought ) {
            if ( TestDriveTEAReadDword( rewardCar ) == 0u ) {
                TestDriveTEAWriteDword( rewardCar, carID );
                return;
            }
        }
        return;
    }
}

void AchievementsData::applyHardcoreLevelTexts() {
    // FUN_0089c0e0
    static constexpr uint64_t kPlaceholderTextHashcode = 0x88588ull;

    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const DBEntryList &kindList = gameDB.Databases[kAchievementDatabaseIndex];
    const uint32_t numEntries = gameDB.pNumItems[kAchievementDatabaseIndex];

    const DBEntryAchievement *pEntry = nullptr;
    for ( uint32_t i = 0u; i < numEntries; i++ ) {
        if ( kindList.getHashcodeForString(
                 gameDB.DBAchivement[i].Kind.pAsString ) ==
             AK_LevelReachedHardcore ) {
            pEntry = &gameDB.DBAchivement[i];
            break;
        }
    }

    if ( pEntry == nullptr ) {
        return;
    }

    for ( AchievementCheck *pCheck : achievements ) {
        if ( pCheck->TextHashcode != kPlaceholderTextHashcode ) {
            continue;
        }

        pCheck->NumItemsReq = uint32_t( pEntry->NumItemsReq );
        pCheck->NumPoints = uint32_t( pEntry->NumPoints );
        pCheck->TileHashcode =
            kindList.getHashcodeForString( pEntry->Tile.pAsString );
        pCheck->TextHashcode =
            kindList.getHashcodeForString( pEntry->Text.pAsString );
        pCheck->RewardHashcode =
            kindList.getHashcodeForString( pEntry->Reward.pAsString );
        pCheck->RewardData = pEntry->RewardData;
    }
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
#ifdef OTDU_USE_IMGUI
    debugCheckKinds.push_back( uint32_t( kind ) );
#endif

    pCheck->onBuilt();
}

void AchievementsData::releaseChecks() {
    for ( AchievementCheck *pCheck : achievements ) {
        delete pCheck;
    }

    achievements.clear();
#ifdef OTDU_USE_IMGUI
    debugCheckKinds.clear();
#endif
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

    uint32_t numBots = 0u;
    if ( gpDatabase != nullptr ) {
        numBots = gpDatabase->getGameDB().pNumItems[kBotDatabaseIndex];
        if ( numBots > uint32_t( kNumAchievementFlags ) ) {
            numBots = uint32_t( kNumAchievementFlags );
        }
    }

    memset( bAchievementFlags, 0, numBots );
    memset( bAchievementFlags + numBots, 1,
            size_t( kNumAchievementFlags ) - numBots );

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

    popUp.clear();
    bReward0x5912a0Unlocked = false;
    bSimulationModeUnlocked = false;
}

void AchievementsData::tick() {
    // FUN_0089e240
    processAsyncRequest();

    bool bCanDisplay = false;
    bool bPopupTick = false;

    if ( currentGameMode == GM_Invalid ||
         ( currentGameMode != gpTestDriveInstance->getActiveGameMode() &&
           gpActiveGameMode != nullptr &&
           !gpActiveGameMode->hasRequestedExit() &&
           ( gpActiveGameMode->getTransitionFlags() & 1u ) == 0u ) ) {
        bCanDisplay = true;
        bPopupTick = true;
        currentGameMode = GM_Invalid;
    }

    if ( gpActiveGMScene != nullptr &&
         gpActiveGMScene->getCurrentGameMode() == GM_Living &&
         gpActiveGMScene->getIntroExteriorCinematic() == nullptr ) {
        bCanDisplay = true;
        bPopupTick = true;
    }

    if ( bMustLevelUp && bCanDisplay ) {
        displayLevelUp();
        bMustLevelUp = false;
    }

    if ( bDisplayLevelUp &&
         ( levelUpDisplayTime + kPopupDisplayDuration ) < GetTimeSeconds() ) {
        FlashPlayer *pPlayer = gpFlash->getFlashPlayer( "GENERAL" );
        if ( pPlayer != nullptr ) {
            pPlayer->showFrame( "close", "/levelup", true );
        }

        bDisplayLevelUp = false;
    }

    popUp.tick( bPopupTick );
}

static void PostAchievementMail( const char *pTitle, const char *pBody ) {
    // FUN_008956c0 + FUN_00895af0 (inlined)
    const time_t nowSeconds = time( nullptr );
    const struct tm *pNow = localtime( &nowSeconds );

    MailMessage message;
    message.Language = GetMailLanguageCode( gpPlayerData->getLanguage() );
    message.Kind = 4;
    message.Category = 1;
    if ( pNow != nullptr ) {
        message.Date = PackSaveDate( uint32_t( pNow->tm_year + 1900 ),
                                     uint32_t( pNow->tm_mon + 1 ),
                                     uint32_t( pNow->tm_mday ) );
    }

    strncpy( message.Title, pTitle, MailMessage::kTitleLength );
    message.Title[MailMessage::kTitleLength - 1u] = '\0';
    strncpy( message.Body, pBody, MailMessage::kBodyLength );
    message.Body[0x76f] = '\0';

    gpPlayerData->getLoadedSave()->getMailbox()->post( message );
}

void AchievementsData::displayLevelUp() {
    // FUN_0089dde0
    uint32_t movieValidity = 0u;
    uint32_t movieIndex = 0u;
    if ( gpFlash->getFlashMovieIndex( &movieValidity, &movieIndex,
                                      "GENERAL" ) ) {
        MoviePlayer *pMoviePlayer = gpFlash->getMoviePlayer(
            int32_t( movieValidity ), int32_t( movieIndex ) );
        if ( pMoviePlayer != nullptr && pMoviePlayer->MovieBank.isLoaded() &&
             pMoviePlayer->MovieBank.getFlashPlayer() != nullptr &&
             gpMngFlashLocalize != nullptr ) {
            FlashPlayer *pPlayer = pMoviePlayer->MovieBank.getFlashPlayer();

            pPlayer->setVariableValue( "/:IsLevelUp", 1 );

            gpMngFlashLocalize->makeSentenceFromHashcode(
                pLevelUpText, sizeof( pLevelUpText ), kLevelUpTextHashcode,
                nullptr );
            pPlayer->setVariableValue( "/:LevelUpText", pLevelUpText );

            pPlayer->showFrame( "open", "/levelup", true );

            levelUpDisplayTime = GetTimeSeconds();
            bDisplayLevelUp = true;

            char pMailTitle[0x80] = {};
            char pMailBody[0x400] = {};
            gpMngFlashLocalize->makeSentenceFromHashcode(
                pMailTitle, sizeof( pMailTitle ), kLevelUpMailTitleHashcode,
                nullptr );
            gpMngFlashLocalize->makeSentenceFromHashcode(
                pMailBody, sizeof( pMailBody ), kLevelUpTextHashcode, nullptr );
            PostAchievementMail( pMailTitle, pMailBody );
        }
    }

    MoviePlayer *pHud = gpFlash->getMoviePlayer( 1, 0 );
    if ( pHud == nullptr || !pHud->MovieBank.isLoaded() ||
         pHud->MovieBank.getFlashPlayer() == nullptr ||
         gpMngFlashLocalize == nullptr ) {
        return;
    }

    FlashPlayer *pPlayer = pHud->MovieBank.getFlashPlayer();
    const int32_t playerLevel = getPlayerLevel();

    gpMngFlashLocalize->setVarLocalized( pPlayer, "/:m_CurrentLevel",
                                         kCurrentLevelHashcode );
    pPlayer->setVariableValue( "/:player_level", playerLevel + 1 );

    if ( gpActiveGameMode == nullptr ) {
        return;
    }

    const eGameMode activeGameMode = gpActiveGameMode->getCurrentGameMode();
    const char *pSpritePath =
        ( activeGameMode == GM_Map || activeGameMode == GM_Living )
            ? "player_datas/level"
            : "title/player_datas/level";
    if ( !pPlayer->spriteExists( pSpritePath ) ) {
        return;
    }

    pPlayer->goToFrameAndPlay( playerLevel, pSpritePath, false );
}

void AchievementsData::displayAchievementPopUp( AchievementCheck *pCheck ) {
    // FUN_0089db30
    if ( gpMngFlashLocalize == nullptr ) {
        return;
    }

    int32_t numItemsTotal = 0;
    const int32_t numItemsDone = pCheck->getProgress( &numItemsTotal );

    gpMngFlashLocalize->setSysVar( "NB_PTS", int32_t( pCheck->NumPoints ) );
    gpMngFlashLocalize->setSysVar( "NB_DONE", numItemsDone );
    gpMngFlashLocalize->setSysVar( "NB_TOTAL", numItemsTotal );
    gpMngFlashLocalize->setSysVar( "NB_REQ", int32_t( pCheck->NumItemsReq ) );

    char pTitle[kAchievementTitleLength] = {};
    char pText[kAchievementTextLength] = {};

    gpMngFlashLocalize->setDatabaseContext( kBDDAchievement );
    gpMngFlashLocalize->makeSentenceFromHashcode(
        pTitle, sizeof( pTitle ), pCheck->TileHashcode, nullptr );
    gpMngFlashLocalize->makeSentenceFromHashcode(
        pText, sizeof( pText ), pCheck->TextHashcode, nullptr );
    gpMngFlashLocalize->setDatabaseContext( kBDDMenus );

    PostAchievementMail( pTitle, pText );

    popUp.push( pTitle, pText );

    gpMngFlashLocalize->clearAllSysVar();
}

void AchievementsData::displayRewardPopUp( const AchievementCheck *pCheck ) {
    // FUN_0089bf80
    if ( gpMngFlashLocalize == nullptr ) {
        return;
    }

    if ( pCheck->RewardData != 0 && gpDatabase != nullptr ) {
        DBEntryCarPhysicsData *pCarEntry =
            gpDatabase->getCarDBEntry( pCheck->RewardData );
        if ( pCarEntry != nullptr ) {
            const CarConfig *pCarConfig =
                gpDatabase->getCarConfig( pCheck->RewardData );
            if ( pCarConfig != nullptr ) {
                gpMngFlashLocalize->setSysVar( "CARNAME",
                                               pCarConfig->FullName.c_str() );
            }

            DBEntryCarShop *pCarShop =
                gpDatabase->getCarShopSellingCar( pCarEntry );
            if ( pCarShop != nullptr ) {
                gpMngFlashLocalize->setSysVar( "SHOPNAME",
                                               pCarShop->Label.pAsString );
            }
        }
    }

    char pRewardText[kAchievementTextLength] = {};

    gpMngFlashLocalize->setDatabaseContext( kBDDAchievement );
    gpMngFlashLocalize->makeSentenceFromHashcode(
        pRewardText, sizeof( pRewardText ), pCheck->TextHashcode, nullptr );
    gpMngFlashLocalize->setDatabaseContext( kBDDMenus );
    gpMngFlashLocalize->clearAllSysVar();

    popUp.pushReward( pRewardText );
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
                gpPlayerData->setHelpSystem( false );
            }
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

    MoviePlayer *pHud = gpFlash->getMoviePlayer( 1, 0 );
    if ( pHud != nullptr && pHud->MovieBank.isLoaded() &&
         pHud->MovieBank.getFlashPlayer() != nullptr &&
         gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->setVarLocalized( pHud->MovieBank.getFlashPlayer(),
                                             "/:m_CurrentCption",
                                             kCurrentCaptionHashcode );
    }

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
            displayRewardPopUp( pCheck );
        }

        return false;
    }

    if ( !bQuietRefresh ) {
        displayAchievementPopUp( pCheck );
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
        // BOOL_010e7d34 - Ford Island
        bReward0x5912a0Unlocked = true;
        return;

    case kSimuModeRewardHashcode:
        // BOOL_010e7d35 - simulation / hardcore mode + Oahu Raceway
        bSimulationModeUnlocked = true;
        return;

    case AchievementsData::kChronoRewardHashcode:
    case AchievementsData::kSnapshotRewardHashcode: // Bookmark
    case AchievementsData::kGameModeEditorRewardHashcode: {
        MoviePlayer *pHud =
            ( gpFlash != nullptr ) ? gpFlash->getMoviePlayer( 1, 0 ) : nullptr;
        if ( pHud == nullptr || !pHud->MovieBank.isLoaded() ||
             pHud->MovieBank.getFlashPlayer() == nullptr ) {
            return;
        }

        const char *pVariableName = "/:Unlock_GME";
        if ( reward == AchievementsData::kChronoRewardHashcode ) {
            pVariableName = "/:Unlock_Chrono";
        } else if ( reward == AchievementsData::kSnapshotRewardHashcode ) {
            pVariableName = "/:Unlock_Snapshot";
        }

        pHud->MovieBank.getFlashPlayer()->setVariableValue( pVariableName, 1 );
        return;
    }

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

        pSave->getGarage()->addHouse( houseHashcode );
        notify( ACF_Challenge | ACF_Road | ACF_Collection, false );
        return;
    }

    default:
        // 0x547ec0, 0x519890 and 0x5da680 are hashcodes the switch knows and
        // deliberately does nothing for.
        break;
    }
}

#ifdef OTDU_USE_IMGUI
void AchievementsData::drawDebug() {
    static const char *const kLevelNames[kNumPlayerLevels + 1] = {
        "0 Rookie", "1 Novice", "2 Pro", "3 Expert", "4 Master", "5 Ace" };

    uint32_t totalPoints = 0u;
    int32_t numUnlocked = 0;
    for ( const AchievementCheck *pCheck : achievements ) {
        if ( pCheck != nullptr ) {
            totalPoints += pCheck->NumPoints;
            numUnlocked += pCheck->bLock ? 0 : 1;
        }
    }

    ImGui::Text( "%u/%u point(s), %d/%d check(s), level %s",
                 getAchievedPoints(), totalPoints, numUnlocked,
                 getNumAchievements(), kLevelNames[getPlayerLevel()] );
    ImGui::Text( "Thresholds: %u / %u / %u / %u / %u", pointsNeeded[0],
                 pointsNeeded[1], pointsNeeded[2], pointsNeeded[3],
                 pointsNeeded[4] );

    static int32_t targetLevel = 0;
    ImGui::Combo( "Level", &targetLevel, kLevelNames, kNumPlayerLevels + 1 );

    ImGui::SameLine();
    if ( ImGui::Button( "Set" ) ) {
        const int32_t levelBefore = getPlayerLevel();
        const uint32_t pointsWanted =
            ( targetLevel > 0 ) ? pointsNeeded[targetLevel - 1] : 0u;

        for ( AchievementCheck *pCheck : achievements ) {
            if ( pCheck != nullptr ) {
                pCheck->bLock = true;
            }
        }

        for ( AchievementCheck *pCheck : achievements ) {
            if ( getAchievedPoints() >= pointsWanted ) {
                break;
            }
            if ( pCheck != nullptr ) {
                pCheck->bLock = false;
            }
        }

        const int32_t levelAfter = getPlayerLevel();
        if ( levelBefore != levelAfter ) {
            IPlayerData *pSave = ( gpPlayerData != nullptr )
                                     ? gpPlayerData->getLoadedSave()
                                     : nullptr;
            if ( pSave != nullptr ) {
                pSave->getExtraChallenges()->PlayerLevel = levelAfter;
            }

            bMustLevelUp = true;
        }
    }

    if ( ImGui::Button( "Unlock all checks" ) ) {
        unlockAllChecks();
    }

    ImGui::SameLine();
    if ( ImGui::Button( "Replay level up" ) ) {
        bMustLevelUp = true;
    }

    if ( ImGui::Button( "Queue achievement popup" ) ) {
        for ( AchievementCheck *pCheck : achievements ) {
            if ( pCheck != nullptr &&
                 ( pCheck->RewardHashcode == 0xffffffffffffffffull ||
                   pCheck->RewardHashcode == kRewardNone ) ) {
                displayAchievementPopUp( pCheck );
                break;
            }
        }
    }

    ImGui::SameLine();
    if ( ImGui::Button( "Queue reward popup" ) ) {
        for ( const AchievementCheck *pCheck : achievements ) {
            if ( pCheck != nullptr &&
                 pCheck->RewardHashcode != 0xffffffffffffffffull &&
                 pCheck->RewardHashcode != kRewardNone ) {
                displayRewardPopUp( pCheck );
                break;
            }
        }
    }

    ImGui::Checkbox( "Force unlock on refresh", &gbForceAchievementUnlock );
    ImGui::SameLine();
    if ( ImGui::Button( "Notify" ) ) {
        notify( ACF_Challenge | ACF_Road | ACF_Collection, false );
    }

    ImGui::Separator();
    if ( ImGui::TreeNode( "Checks" ) ) {
        if ( ImGui::BeginTable( "achievement_checks", 5,
                                ImGuiTableFlags_Borders |
                                    ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_ScrollY,
                                ImVec2( 0.0f, 320.0f ) ) ) {
            ImGui::TableSetupColumn( "Kind" );
            ImGui::TableSetupColumn( "Progress" );
            ImGui::TableSetupColumn( "Required" );
            ImGui::TableSetupColumn( "Points" );
            ImGui::TableSetupColumn( "State" );
            ImGui::TableSetupScrollFreeze( 0, 1 );
            ImGui::TableHeadersRow();

            for ( size_t i = 0u; i < achievements.size(); i++ ) {
                const AchievementCheck *pCheck = achievements[i];
                if ( pCheck == nullptr ) {
                    continue;
                }

                int32_t progressTotal = 0;
                const int32_t progress =
                    pCheck->getDetailedProgress( &progressTotal );

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted( GetAchievementKindName(
                    i < debugCheckKinds.size() ? debugCheckKinds[i] : 0u ) );
                ImGui::TableNextColumn();
                ImGui::Text( "%d / %d", progress, progressTotal );
                ImGui::TableNextColumn();
                ImGui::Text( "%u", pCheck->NumItemsReq );
                ImGui::TableNextColumn();
                ImGui::Text( "%u", pCheck->NumPoints );
                ImGui::TableNextColumn();
                ImGui::TextUnformatted( pCheck->bLock ? "locked" : "unlocked" );
            }

            ImGui::EndTable();
        }

        ImGui::TreePop();
    }

    ImGui::Separator();
    ImGui::Checkbox( "Ford Island unlocked", &bReward0x5912a0Unlocked );
    ImGui::Checkbox( "Simu mode / Oahu Raceway unlocked",
                     &bSimulationModeUnlocked );
}
#endif

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
        markReady();
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

    markReady();
}
