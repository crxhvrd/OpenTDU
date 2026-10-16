#include "shared.h"
#include "mng_rankings.h"

#include "database/gs_database.h"
#include "flash/flash_callbacks_register.h"
#include "flash/gs_flash.h"
#include "flash/modal_message_box.h"
#include "flash/movie_player.h"
#include "flash/entities/flash_entity.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/mng_hud.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "game/scenaric/entity/entity_extra_challenge.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_item_table.h"
#include "player_data/saved_ranking.h"
#include "render/draw_list.h"

#include "gm_living.h"

static constexpr uint64_t kStatisticsWaitPopupHashcode = 0x3b863c2ull;
static constexpr float kStatisticsWaitPopupDuration = 4.0f;
static constexpr uint32_t kMoviePlayerFlagBlocked = 0x40u;
static constexpr int32_t kBackTransitionMessages = 2;
static constexpr uint32_t kSpotScanBudget = 100u; // DAT_00fab93c
static constexpr const char *kNoChallengeName = "----";

Entity *const MngRankings::kNoLureMission =
    reinterpret_cast<Entity *>( ~uintptr_t( 0 ) );

static bool IsMissionsTableID( const uint32_t tableID ) {
    return tableID == 0x72676972     // rgir
           || tableID == 0x72686974  // rhit
           || tableID == 0x72747261  // rtra
           || tableID == 0x72636f6e; // rcon
}

static bool IsBackSoundMessage( const FlashMessage *pMessage ) {
    return pMessage[0] == FlashMessage( "sndp" ) &&
           strcmp( pMessage[1].pAsChar, "back" ) == 0;
}

FlashPlayer *RankingsFilter::getPlayer() const {
    // FUN_0042d750
    return pOwner->getMovie();
}

MngRankings::MngRankings()
    : Manager(),
      filter(),
      pMovie( nullptr ),
      pOwner( nullptr ),
      pMngFlash( nullptr ),
      pActiveTable( nullptr ),
      soloRanks(),
      standardRanks(),
      clubRanks(),
      missionRanks(),
      eloRanks(),
      challengeRanks(),
      bLeaderboardsRequested( false ),
      bClubRankRequested( false ),
      bRankingsLoading( false ),
      bMissionsOpen( false ),
      bForceListLength( false ),
      backCounter( 0 ),
      numSpotsToScan( 0u ),
      scanSpotIndex( 0u ),
      bScanningSpots( false ),
      bWaitPopupDisplayed( false ),
      bStatisticsBuilt( false ),
      soloStats(),
      timeChallengeNames(),
      speedChallengeNames(),
      raceChallengeNames(),
      numSpeedChallengesMulti( 0 ),
      numSpeedChallengesMultiDone( 0 ),
      numRaceChallengesMulti( 0 ),
      numRaceChallengesMultiDone( 0 ),
      pLurePicture( nullptr ),
      pLureMission( kNoLureMission ),
      bLureReloadPending( false ),
      bLureDrawnThisFrame( false ),
      lureBank(),
      fallbackLureBank() {
    // FUN_00764640
    filter.pOwner = this;
}

MngRankings::~MngRankings() {
    // FUN_0042d740
}

bool MngRankings::initialize( TestDriveGameInstance * ) {
    // FUN_00762d30
    lureBank.setBuffer(
        TestDrive::AllocAligned( MapScreenshotBank::kBufferSize, 0x10 ) );
    return true;
}

void MngRankings::reset() {
    // FUN_00762dc0
    pMovie = gpFlash->getFlashPlayer( "HOME" );
    if ( pMovie == nullptr || pOwner == nullptr ) {
        return;
    }

    pMngFlash = &pOwner->getFlashManager();

    static constexpr const char *kMissionIDs[kNumMissionRanks] = {
        "rgir", "rhit", "rtra", "rcon" };
    for ( int32_t i = 0; i < kNumMissionRanks; i++ ) {
        missionRanks[i].bind( this, &filter, pMovie,
                              "/ss_menu/table/tables/missions", kMissionIDs[i],
                              -1 );
    }

    static constexpr const char *kSoloIDs[kNumSoloRanks] = { "soti", "sora",
                                                             "sosp" };
    static constexpr int32_t kSoloCategories[kNumSoloRanks] = { 0, 3, 1 };
    for ( int32_t i = 0; i < kNumSoloRanks; i++ ) {
        soloRanks[i].bind( this, &filter, pMovie,
                           "/ss_menu/table/tables/local_ranks", kSoloIDs[i],
                           kSoloCategories[i] );
    }

    static constexpr const char *kEloIDs[kNumOnlineChallengeRanks] = {
        "stig", "srag", "sspg" };
    for ( int32_t i = 0; i < kNumOnlineChallengeRanks; i++ ) {
        eloRanks[i].bind( this, &filter, pMovie, "/ss_menu/table/tables/m_elo",
                          kEloIDs[i], kSoloCategories[i] );
        eloRanks[i].setOnline( true ); // FUN_007794e0
    }

    static constexpr const char *kChallengeIDs[kNumOnlineChallengeRanks] = {
        "stic", "srac", "sspc" };
    for ( int32_t i = 0; i < kNumOnlineChallengeRanks; i++ ) {
        challengeRanks[i].bind( this, &filter, pMovie,
                                "/ss_menu/table/tables/s_chall",
                                kChallengeIDs[i], kSoloCategories[i] );
        challengeRanks[i].setLabel( "/ss_menu/table/tables/s_chall" );
        challengeRanks[i].setOnline( true ); // FUN_00779460
    }

    static constexpr const char *kStandardIDs[kNumStandardRanks] = {
        "year", "mont", "mrac", "mspe", "icer" };
    for ( int32_t i = 0; i < kNumStandardRanks; i++ ) {
        standardRanks[i].bind( this, &filter, pMovie,
                               "/ss_menu/table/tables/all_standard",
                               kStandardIDs[i], -1 );
        standardRanks[i].setLabel( "/ss_menu/table/tables/all_standard" );
        standardRanks[i].setOnline( true ); // FUN_007794e0
    }

    clubRanks[0].bind( this, &filter, pMovie, "/ss_menu/table/tables/club_mbrs",
                       "cbmb", -1 );
    clubRanks[0].setLabel( "/ss_menu/table/tables/club_mbrs" );
    clubRanks[1].bind( this, &filter, pMovie,
                       "/ss_menu/table/tables/club_inter", "cbin", -1 );
    clubRanks[1].setLabel( "/ss_menu/table/tables/club_inter" );
}

void MngRankings::tick( float, float ) {
    // FUN_00765c60
    if ( bScanningSpots ) {
        scanSpots();
    }

    if ( bLeaderboardsRequested || bClubRankRequested ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (leaderboard queries)
    }

    if ( bLureReloadPending ) {
        if ( bLureDrawnThisFrame ) {
            gFlashCallbacksRegister.removeAllSynonyms(
                &MngRankings::LurePictureDrawCallback );
            pLurePicture = nullptr;
        } else {
            bLureReloadPending = false;
            if ( pLureMission != kNoLureMission ) {
                loadLurePicture();
            }
        }
    }
    bLureDrawnThisFrame = false;

    if ( bMissionsOpen ) {
        updateLurePicture();
    }
}

void MngRankings::pause() {
    // FUN_00765490
    bRankingsLoading = false;
    pActiveTable = nullptr;

    if ( pMngFlash != nullptr ) {
        setTablesRegistered(
            { &challengeRanks[2], &challengeRanks[1], &challengeRanks[0],
              &eloRanks[2],       &eloRanks[1],       &eloRanks[0],
              &missionRanks[0],   &missionRanks[1],   &missionRanks[2],
              &missionRanks[3],   &soloRanks[0],      &soloRanks[1],
              &soloRanks[2],      &standardRanks[0],  &standardRanks[1],
              &standardRanks[2],  &standardRanks[3],  &standardRanks[4],
              &clubRanks[0],      &clubRanks[1] },
            {} );
    }

    pMngFlash = nullptr;
    pMovie = nullptr;
}

void MngRankings::terminate() {
    // FUN_00765760
    if ( lureBank.getBuffer() != nullptr ) {
        TestDrive::FreeAligned( lureBank.getBuffer() );
        lureBank.setBuffer( nullptr );
    }

    gFlashCallbacksRegister.removeAllSynonyms(
        &MngRankings::LurePictureDrawCallback );
    pLurePicture = nullptr;

    fallbackLureBank.release();
    fallbackLureBank.unload();
    lureBank.release();
}

bool MngRankings::onMessage( FlashMessage *pMessage ) {
    // FUN_00764d90
    if ( backCounter > 0 ) {
        backCounter--;
    }

    if ( IsBackSoundMessage( pMessage ) ) {
        backCounter = kBackTransitionMessages;
        if ( bMissionsOpen ) {
            bMissionsOpen = false;
            pLureMission = kNoLureMission;
            return false;
        }
    }

    switch ( *pMessage ) {
    case FlashMessage( "rfii" ):
        if ( pActiveTable != nullptr ) {
            pActiveTable->jumpToLine( 0, 0 );
        }
        return true;

    case FlashMessage( "nech" ): // next challenge
        if ( pActiveTable != nullptr ) {
            pActiveTable->nextSubPage();
        }
        return true;

    case FlashMessage( "prch" ): // previous challenge
        if ( pActiveTable != nullptr ) {
            pActiveTable->previousSubPage();
        }
        return true;

    case FlashMessage( "lead" ): // leaderboard top
        if ( pActiveTable != nullptr ) {
            pActiveTable->refresh();
        }
        return true;

    case FlashMessage( "your" ): // around the player
        if ( pActiveTable != nullptr ) {
            pActiveTable->requestAroundPlayer();
        }
        return true;

    case FlashMessage( "ccha" ):
    case FlashMessage( "chch" ):
    case FlashMessage( "vgcd" ):
        return true;

    case FlashMessage( "rfio" ): { // filters changed
        const bool bLevel = pMovie->getVariableAsInt( "/:F_Level" ) == 1;
        const bool bCountry = pMovie->getVariableAsInt( "/:F_Country" ) == 1;
        const bool bFriends = pMovie->getVariableAsInt( "/:F_Friends" ) == 1;
        const bool bChanged = bLevel != filter.bLevel ||
                              bCountry != filter.bCountry ||
                              bFriends != filter.bFriends;
        if ( pActiveTable != nullptr && bChanged ) {
            filter.bCountry = bCountry;
            filter.bLevel = bLevel;
            filter.bFriends = bFriends;
            pActiveTable->onFilterChanged( bLevel, bCountry, bFriends );
        }
        return true;
    }

    case FlashMessage( "rain" ): // ranking in
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (5 leaderboard queries)
        buildStatistics();
        resetOnlineRankFields();
        bForceListLength = false;
        return true;

    case FlashMessage( "raou" ): // ranking out
        bRankingsLoading = false;
        pActiveTable = nullptr;
        setTablesRegistered(
            { &challengeRanks[2], &challengeRanks[1], &challengeRanks[0],
              &eloRanks[2],       &eloRanks[1],       &eloRanks[0],
              &missionRanks[0],   &missionRanks[1],   &missionRanks[2],
              &missionRanks[3],   &soloRanks[0],      &soloRanks[1],
              &soloRanks[2],      &standardRanks[0],  &standardRanks[1],
              &standardRanks[2],  &standardRanks[3],  &standardRanks[4],
              &clubRanks[0],      &clubRanks[1] },
            {} );
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (GSOnline+0xa1e0 reset)
        return true;

    case FlashMessage( "rkmu" ): // multiplayer rankings
        setTablesRegistered(
            { &missionRanks[0], &missionRanks[1], &missionRanks[2],
              &missionRanks[3], &soloRanks[0], &soloRanks[1], &soloRanks[2],
              &standardRanks[4], &clubRanks[0], &clubRanks[1] },
            { &standardRanks[0], &standardRanks[1], &standardRanks[2],
              &standardRanks[3], &standardRanks[4], &clubRanks[0],
              &clubRanks[1] } );
        return true;

    case FlashMessage( "rkcb" ): // club rankings
        setTablesRegistered(
            { &missionRanks[0], &missionRanks[1], &missionRanks[2],
              &missionRanks[3], &soloRanks[0], &soloRanks[1], &soloRanks[2],
              &standardRanks[0], &standardRanks[1], &standardRanks[2],
              &standardRanks[3], &standardRanks[4] },
            { &clubRanks[0], &clubRanks[1] } );
        return true;

    case FlashMessage( "rkic" ): // ice rankings
        setTablesRegistered(
            { &missionRanks[0], &missionRanks[1], &missionRanks[2],
              &missionRanks[3], &soloRanks[0], &soloRanks[1], &soloRanks[2],
              &standardRanks[0], &standardRanks[1], &standardRanks[2],
              &standardRanks[3], &clubRanks[0], &clubRanks[1] },
            { &standardRanks[4] } );
        return true;

    case FlashMessage( "rkso" ): // solo rankings
        setTablesRegistered(
            { &standardRanks[0], &standardRanks[1], &standardRanks[2],
              &standardRanks[3], &standardRanks[4], &clubRanks[0],
              &clubRanks[1] },
            { &missionRanks[0], &missionRanks[1], &missionRanks[2],
              &missionRanks[3], &soloRanks[0], &soloRanks[1], &soloRanks[2] } );
        return true;

    default:
        return false;
    }
}

void MngRankings::setTablesRegistered(
    std::initializer_list<FlashRankingTable *> tablesToRemove,
    std::initializer_list<FlashRankingTable *> tablesToAdd ) {
    // FUN_00762aa0 / FUN_00762ad0
    for ( FlashRankingTable *pTable : tablesToRemove ) {
        pTable->unregisterWidget( pMngFlash );
    }

    for ( FlashRankingTable *pTable : tablesToAdd ) {
        pTable->registerWidget( pMngFlash );
    }
}

void MngRankings::registerActiveTableEloRanks() {
    // FUN_00762b00
    switch ( pActiveTable->getIDMagic() ) {
    case 0x736f7469: // soti
        setTablesRegistered( { &eloRanks[1], &challengeRanks[1], &eloRanks[2],
                               &challengeRanks[2] },
                             { &eloRanks[0] } );
        break;
    case 0x736f7370: // sosp
        setTablesRegistered( { &eloRanks[1], &challengeRanks[1], &eloRanks[0],
                               &challengeRanks[0] },
                             { &eloRanks[2] } );
        break;
    case 0x736f7261: // sora
        setTablesRegistered( { &eloRanks[0], &challengeRanks[0], &eloRanks[2],
                               &challengeRanks[2] },
                             { &eloRanks[1] } );
        break;
    default:
        break;
    }
}

void MngRankings::getOnlineTables( const int32_t category,
                                   FlashRankingTable **ppOutEloTable,
                                   FlashRankingTable **ppOutChallengeTable ) {
    switch ( category ) {
    case 0:
        *ppOutEloTable = &eloRanks[0];
        *ppOutChallengeTable = &challengeRanks[0];
        break;
    case 1:
        *ppOutEloTable = &eloRanks[2];
        *ppOutChallengeTable = &challengeRanks[2];
        break;
    case 3:
        *ppOutEloTable = &eloRanks[1];
        *ppOutChallengeTable = &challengeRanks[1];
        break;
    default:
        *ppOutEloTable = nullptr;
        *ppOutChallengeTable = nullptr;
        break;
    }
}

const SoloChallengeStats &
MngRankings::getSoloChallengeStats( const int32_t category ) {
    switch ( category ) {
    case 1:
        return soloStats[SCK_Speed];
    case 3:
        return soloStats[SCK_Race];
    default:
        return soloStats[SCK_Time];
    }
}

const char *MngRankings::getSoloChallengeName( const int32_t category,
                                               const int32_t index ) const {
    switch ( category ) {
    case 0:
        return ( index < kMaxTimeChallenges ) ? timeChallengeNames[index]
                                              : nullptr;
    case 1:
        return ( index < kMaxSpeedChallenges ) ? speedChallengeNames[index]
                                               : nullptr;
    case 3:
        return ( index < kMaxRaceChallenges ) ? raceChallengeNames[index]
                                              : nullptr;
    default:
        return nullptr;
    }
}

void MngRankings::buildStatistics() {
    // FUN_00764b00
    if ( !bStatisticsBuilt ) {
        soloStats[SCK_Time] = SoloChallengeStats();
        soloStats[SCK_Speed] = SoloChallengeStats();
        soloStats[SCK_Race] = SoloChallengeStats();
        numSpeedChallengesMulti = 0;
        numSpeedChallengesMultiDone = 0;
        numRaceChallengesMulti = 0;
        numRaceChallengesMultiDone = 0;
        numSpotsToScan = gpScenaric->getSpotList().getNumWorldSpots();
        bScanningSpots = true;
        displayWaitPopup();
    }

    int32_t numShoppingGirls = 0;
    int32_t numShoppingGirlsDone = 0;
    int32_t numHitchHikers = 0;
    int32_t numHitchHikersDone = 0;
    int32_t numTransports = 0;
    int32_t numTransportsDone = 0;
    int32_t numConvoys = 0;
    int32_t numConvoysDone = 0;

    const SavedItemTable *pItemTable =
        gpPlayerData->getLoadedSave()->getItemTable();
    const int32_t numExtraChallenges = gpScenaric->getNumExtraChallenges();
    for ( int32_t i = 0; i < numExtraChallenges; i++ ) {
        EntityExtraChallenge *pMission = static_cast<EntityExtraChallenge *>(
            gpScenaric->findExtraChallenge( i ) );
        if ( pMission == nullptr ) {
            continue;
        }

        const bool bGold =
            pItemTable->isExtraChallengeGold( pMission->getIndex() );
        switch ( pMission->getType() ) {
        case eEntityType::ET_SHOG:
            numShoppingGirls++;
            numShoppingGirlsDone += bGold ? 1 : 0;
            break;
        case eEntityType::ET_HIHI:
            numHitchHikers++;
            numHitchHikersDone += bGold ? 1 : 0;
            break;
        case eEntityType::ET_TRAN:
            numTransports++;
            numTransportsDone += bGold ? 1 : 0;
            break;
        case eEntityType::ET_CVOY:
            numConvoys++;
            numConvoysDone += bGold ? 1 : 0;
            break;
        default:
            break;
        }
    }

    pMovie->setVariableValue( "/:sgirlTotalChall", numShoppingGirls );
    pMovie->setVariableValue( "/:sgirlDone", numShoppingGirlsDone );
    pMovie->setVariableValue( "/:hitchTotalChall", numHitchHikers );
    pMovie->setVariableValue( "/:hitchDone", numHitchHikersDone );
    pMovie->setVariableValue( "/:transpTotalChall", numTransports );
    pMovie->setVariableValue( "/:transpDone", numTransportsDone );
    pMovie->setVariableValue( "/:convoyTotalChall", numConvoys );
    pMovie->setVariableValue( "/:convoyDone", numConvoysDone );

    publishSoloChallengeStats();
}

void MngRankings::displayWaitPopup() {
    // FUN_00764a50
    if ( bWaitPopupDisplayed ) {
        return;
    }

    bWaitPopupDisplayed = true;
    gpFlashMessageBox->display( kStatisticsWaitPopupHashcode,
                                kStatisticsWaitPopupDuration, 0, nullptr,
                                nullptr, false );

    const int32_t numMovies = gpFlash->getNumMovies( 1 );
    for ( int32_t i = 0; i < numMovies; i++ ) {
        MoviePlayer *pMoviePlayer = gpFlash->getMoviePlayer( 1, i );
        if ( pMoviePlayer != nullptr ) {
            pMoviePlayer->Flags |= kMoviePlayerFlagBlocked;
        }
    }
}

void MngRankings::hideWaitPopup() {
    // FUN_00763870
    if ( !bWaitPopupDisplayed ) {
        return;
    }

    bWaitPopupDisplayed = false;

    const int32_t numMovies = gpFlash->getNumMovies( 1 );
    for ( int32_t i = 0; i < numMovies; i++ ) {
        MoviePlayer *pMoviePlayer = gpFlash->getMoviePlayer( 1, i );
        if ( pMoviePlayer != nullptr ) {
            pMoviePlayer->Flags &= ~kMoviePlayerFlagBlocked;
        }
    }

    FlashPlayer *pGeneralPlayer = gpFlash->getFlashPlayer( "GENERAL" );
    if ( pGeneralPlayer != nullptr ) {
        pGeneralPlayer->showFrame( "w_out", "/waiting", false );
    }
}

static void CopyChallengeName( char *pOutName, const SpotMap &spot ) {
    if ( spot.NameHashcode == 0ull ) {
        strcpy( pOutName, kNoChallengeName );
        return;
    }

    const char *pName = gpDatabase->getStringByHashcode( 7, spot.NameHashcode );
    strncpy( pOutName, ( pName != nullptr ) ? pName : "",
             MngRankings::kChallengeNameLength - 1 );
    pOutName[MngRankings::kChallengeNameLength - 1] = '\0';
}

static void AddChallengeMedal( SoloChallengeStats &stats,
                               const int32_t result ) {
    switch ( result ) {
    case 1:
        stats.NumDone++;
        stats.NumGold++;
        break;
    case 2:
        stats.NumDone++;
        stats.NumSilver++;
        break;
    case 3:
        stats.NumDone++;
        stats.NumBronze++;
        break;
    default:
        break;
    }
}

void MngRankings::scanSpots() {
    // FUN_00764200
    if ( numSpotsToScan <= scanSpotIndex ) {
        scanSpotIndex = 0u;
        bScanningSpots = false;
        publishSoloChallengeStats();
        hideWaitPopup();
        bStatisticsBuilt = true;
        return;
    }

    Spots &spots = gpScenaric->getSpotList();
    const SavedRanking *pRanking = gpPlayerData->getLoadedSave()->getRanking();

    const uint32_t scanEnd = scanSpotIndex + kSpotScanBudget;
    while ( scanSpotIndex < scanEnd ) {
        const SpotMap *pSpot =
            spots.getWorldSpotChecked( int32_t( scanSpotIndex ) );
        if ( pSpot == nullptr ) {
            scanSpotIndex++;
            return;
        }

        if ( pSpot->Type == ST_Challenge ) {
            const int32_t gameMode = pSpot->SubType & 0xf;
            int32_t kind = pSpot->SubType >> 4;
            if ( kind == 0xf ) {
                kind = -1;
            }

            const int32_t result =
                pRanking->getChallengeResult( pSpot->Hashcode );
            if ( gameMode == 1 ) {
                if ( kind == 1 ) {
                    numSpeedChallengesMulti++;
                    numSpeedChallengesMultiDone += ( result != -1 ) ? 1 : 0;
                } else if ( kind == 3 ) {
                    numRaceChallengesMulti++;
                    numRaceChallengesMultiDone += ( result != -1 ) ? 1 : 0;
                }
            } else if ( gameMode == 0 ) {
                SoloChallengeStats *pStats = nullptr;
                char *pName = nullptr;
                if ( kind == 0 &&
                     soloStats[SCK_Time].NumTotal < kMaxTimeChallenges ) {
                    pStats = &soloStats[SCK_Time];
                    pName = timeChallengeNames[pStats->NumTotal];
                } else if ( kind == 1 && soloStats[SCK_Speed].NumTotal <
                                             kMaxSpeedChallenges ) {
                    pStats = &soloStats[SCK_Speed];
                    pName = speedChallengeNames[pStats->NumTotal];
                } else if ( kind == 3 && soloStats[SCK_Race].NumTotal <
                                             kMaxRaceChallenges ) {
                    pStats = &soloStats[SCK_Race];
                    pName = raceChallengeNames[pStats->NumTotal];
                }

                if ( pStats != nullptr ) {
                    CopyChallengeName( pName, *pSpot );
                    pStats->NumTotal++;
                    AddChallengeMedal( *pStats, result );
                }
            }
        }

        scanSpotIndex++;
        if ( numSpotsToScan <= scanSpotIndex ) {
            return;
        }
    }
}

void MngRankings::publishSoloChallengeStats() {
    // FUN_00764020
    const SoloChallengeStats &timeStats = soloStats[SCK_Time];
    pMovie->setVariableValue( "/:TimeTotalChall", timeStats.NumTotal );
    pMovie->setVariableValue( "/:TimeDoneS", timeStats.NumDone );
    pMovie->setVariableValue( "/:TimeGoldNb", timeStats.NumGold );
    pMovie->setVariableValue( "/:TimeSilverNb", timeStats.NumSilver );
    pMovie->setVariableValue( "/:TimeBronzeNb", timeStats.NumBronze );

    const SoloChallengeStats &speedStats = soloStats[SCK_Speed];
    pMovie->setVariableValue( "/:SpeedTotalChall", speedStats.NumTotal );
    pMovie->setVariableValue( "/:SpeedDoneS", speedStats.NumDone );
    pMovie->setVariableValue( "/:SpeedGoldNb", speedStats.NumGold );
    pMovie->setVariableValue( "/:SpeedSilverNb", speedStats.NumSilver );
    pMovie->setVariableValue( "/:SpeedBronzeNb", speedStats.NumBronze );

    const SoloChallengeStats &raceStats = soloStats[SCK_Race];
    pMovie->setVariableValue( "/:RaceTotalChall", raceStats.NumTotal );
    pMovie->setVariableValue( "/:RaceDoneS", raceStats.NumDone );
    pMovie->setVariableValue( "/:RaceGoldNb", raceStats.NumGold );
    pMovie->setVariableValue( "/:RaceSilverNb", raceStats.NumSilver );
    pMovie->setVariableValue( "/:RaceBronzeNb", raceStats.NumBronze );

    challengeRanks[0].setNumSubPages( timeStats.NumTotal );
    challengeRanks[1].setNumSubPages( raceStats.NumTotal );
    challengeRanks[2].setNumSubPages( speedStats.NumTotal );

    pMovie->setVariableValue( "/:SpeedTotalChallM", numSpeedChallengesMulti );
    pMovie->setVariableValue( "/:SpeedDoneM", numSpeedChallengesMultiDone );
    pMovie->setVariableValue( "/:RaceTotalChallM", numRaceChallengesMulti );
    pMovie->setVariableValue( "/:RaceDoneM", numRaceChallengesMultiDone );
}

void MngRankings::resetOnlineRankFields() {
    // FUN_00763e30
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (ELO + club rank requests)

    static constexpr const char *kRankFields[] = {
        "/:SpeedRankM",  "/:SpeedScoreM",  "/:RaceRankM",   "/:RaceScoreM",
        "/:YearRank",    "/:YearScore",    "/:MonthRank",   "/:MonthScore",
        "/:IntraCbRank", "/:IntraCbScore", "/:InterCbRank", "/:InterCbScore",
        "/:IceRank",     "/:IceScore" };
    for ( const char *pField : kRankFields ) {
        pMovie->setVariableValueNonLinked( pField, "---" );
    }
}

void MngRankings::updateLurePicture() {
    // FUN_00765be0
    if ( pActiveTable == nullptr ) {
        return;
    }

    if ( !IsMissionsTableID( pActiveTable->getIDMagic() ) ) {
        bMissionsOpen = false;
        return;
    }

    Entity *pSelectedMission =
        static_cast<FlashRankingTableMissions *>( pActiveTable )
            ->getSelectedMission();
    if ( pSelectedMission != pLureMission ) {
        pLureMission = pSelectedMission;

        gFlashCallbacksRegister.removeAllSynonyms(
            &MngRankings::LurePictureDrawCallback );
        pLurePicture = nullptr;
        bLureReloadPending = true;
    }
}

void MngRankings::loadLurePicture() {
    // FUN_00765b70
    lureBank.release();

    gFlashCallbacksRegister.removeAllSynonyms(
        &MngRankings::LurePictureDrawCallback );
    pLurePicture = nullptr;

    if ( pLureMission != nullptr &&
         lureBank.load( pLureMission->getInstanceName().c_str(),
                        MapScreenshotBank::C_ExtraChallenges,
                        &MngRankings::OnLurePictureLoaded, this ) ) {
        return;
    }

    loadFallbackLurePicture();
    if ( pLurePicture != nullptr ) {
        gFlashCallbacksRegister.add(
            "lure", &MngRankings::LurePictureDrawCallback, this );
    }
}

void MngRankings::onLurePictureLoaded() {
    // FUN_00765af0
    pLurePicture = lureBank.getMaterial();
    if ( pLurePicture == nullptr ) {
        loadFallbackLurePicture();
    }

    if ( pLurePicture != nullptr ) {
        gFlashCallbacksRegister.add(
            "lure", &MngRankings::LurePictureDrawCallback, this );
    }
}

void MngRankings::loadFallbackLurePicture() {
    // FUN_007657d0 + FUN_0042d760
    fallbackLureBank.loadImmediate();
    pLurePicture = fallbackLureBank.getMaterial( "undf" );
}

bool MngRankings::OnLurePictureLoaded( MapScreenshotBank * /*pBank*/,
                                       void *pUserData ) {
    // LAB_00765b60
    if ( pUserData != nullptr ) {
        static_cast<MngRankings *>( pUserData )->onLurePictureLoaded();
    }
    return true;
}

void MngRankings::LurePictureDrawCallback( FlashRECT * /*pViewport*/,
                                           float /*depth*/,
                                           DrawList *pDrawList,
                                           void *pUserData,
                                           FlashResource *pResource,
                                           const char * /*pClipName*/ ) {
    // FUN_007638f0
    MngRankings *pRankings = static_cast<MngRankings *>( pUserData );
    if ( pRankings == nullptr || pRankings->pMovie == nullptr ) {
        return;
    }

    FlashClip lureClip;
    if ( !pRankings->pMovie->getClipPlayer(
             "ss_menu/table/tables/missions/datas/lure", &lureClip ) ) {
        return;
    }

    FlashRECT viewport;
    lureClip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );
    if ( pRankings->pLurePicture == nullptr ) {
        return;
    }

    pRankings->bLureDrawnThisFrame = true;
    pDrawList->setActiveMaterial( pRankings->pLurePicture );
    pDrawList->setDiffuse( 0, 0xffffffffu );
    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, viewport.X, viewport.Y,
                   viewport.X + viewport.Width, viewport.Y + viewport.Height,
                   pResource );
}
