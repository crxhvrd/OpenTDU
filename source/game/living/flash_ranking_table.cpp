#include "shared.h"
#include "flash_ranking_table.h"

#include "database/gs_database.h"
#include "flash/mng_flash.h"
#include "flash/implementation/flash_player.h"
#include "game/game_mechanism_player.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "game/scenaric/entity/entity_extra_challenge.h"
#include "online/gs_online.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_item_table.h"
#include "player_data/saved_ranking.h"

#include "mng_rankings.h"

static constexpr float kMetersPerSecondToMph = 2.2374146f; // DAT_00f419f4
static constexpr float kMetersPerSecondToKmh = 3.6f;       // DAT_00f41768
static constexpr float kSecondsToCentiseconds = 100.0f;    // DAT_00f41708
static constexpr int32_t kSoloChallengeGameModeID = 0xc0c0;

static void FormatChallengeTime( const uint32_t centiseconds,
                                 char *pOutBuffer,
                                 const size_t bufferSize ) {
    // FUN_00779620
    const uint32_t cents = centiseconds % 100u;
    const uint32_t seconds = centiseconds / 100u;
    const uint32_t hours = seconds / 3600u;
    const uint32_t minutes = ( seconds % 3600u ) / 60u;
    const uint32_t remainingSeconds = seconds % 60u;
    snprintf( pOutBuffer, bufferSize, "%u:%s%u'%s%u\"%s%u", hours,
              ( minutes < 10u ) ? "0" : "", minutes,
              ( remainingSeconds < 10u ) ? "0" : "", remainingSeconds,
              ( cents < 10u ) ? "0" : "", cents );
}

static GSOnline::eChallengeLeaderboard
GetLeaderboardKind( const int32_t category ) {
    switch ( category ) {
    case 0:
        return GSOnline::CL_Time;
    case 1:
        return GSOnline::CL_Speed;
    case 3:
        return GSOnline::CL_Race;
    default:
        return GSOnline::CL_Count;
    }
}

static eEntityType GetMissionEntityType( const uint32_t tableID ) {
    switch ( tableID ) {
    case 0x72676972: // rgir
        return eEntityType::ET_SHOG;
    case 0x72686974: // rhit
        return eEntityType::ET_HIHI;
    case 0x72747261: // rtra
        return eEntityType::ET_TRAN;
    case 0x72636f6e: // rcon
        return eEntityType::ET_CVOY;
    default:
        return eEntityType::ET_None;
    }
}

FlashRankingTable::FlashRankingTable()
    : FlashList(),
      pOwner( nullptr ),
      pFilter( nullptr ),
      pFilterPlayer( nullptr ),
      bResultsDisplayed( true ),
      category( 0 ),
      bOnline( false ),
      field_0xb4( 0 ),
      field_0xb8( 0 ),
      bRefreshPending( false ),
      onlineLength( 0 ),
      bRegistered( false ),
      pID(),
      subPageIndex( 0 ),
      numSubPages( 1 ),
      label() {
    // FUN_00779ae0
    levelIndex = 1;
    field16_0x67 = true;
}

void FlashRankingTable::bind( MngRankings *pRankings,
                              RankingsFilter *pRankingsFilter,
                              FlashPlayer *pPlayer,
                              const char *pPath,
                              const char *pTableID,
                              const int32_t tableCategory ) {
    // FUN_00762dc0 (inlined)
    pFilter = pRankingsFilter;
    pOwner = pRankings;
    pFilterPlayer = pRankingsFilter->getPlayer();
    setPlayer( pPlayer, pPath );
    bOnline = false;
    category = tableCategory;
    strncpy( pID, pTableID, sizeof( pID ) - 1 );
    pID[sizeof( pID ) - 1] = '\0';
}

void FlashRankingTable::registerWidget( MngFlash *pMngFlash ) {
    // FUN_00762ad0
    if ( bRegistered ) {
        return;
    }

    pMngFlash->addWidget( pID, this );
    bRegistered = true;
}

void FlashRankingTable::unregisterWidget( MngFlash *pMngFlash ) {
    // FUN_00762aa0
    if ( !bRegistered ) {
        return;
    }

    pMngFlash->removeWidget( this );
    bRegistered = false;
}

int32_t FlashRankingTable::getListLength() const {
    // FUN_00779aa0
    if ( pOwner != nullptr && pOwner->isListLengthForced() ) {
        return kForcedListLength;
    }

    if ( !gpPlayerData->isOnline() ) {
        onlineLength = 0;
    } else {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (DAT_011e4e28 session check)
    }

    return onlineLength;
}

void FlashRankingTable::open() {
    // FUN_0077d210
    FlashList::open();

    pFilter->bLevel = false;
    pFilter->bCountry = false;
    pFilter->bFriends = false;

    FlashPlayer *pPlayer = pFilter->getPlayer();
    if ( pPlayer != nullptr ) {
        pPlayer->setVariableValue( "/:F_Level", 0u );
        pPlayer->setVariableValue( "/:F_Country", 0u );
        pPlayer->setVariableValue( "/:F_Friends", 0u );
    }

    if ( pOwner != nullptr ) {
        pOwner->setActiveTable( this );
        pOwner->registerActiveTableEloRanks();
    }

    field_0xb4 = 0;
    field_0xb8 = 0;
    bRefreshPending = false;
    onlineLength = kOnlinePageLength;
}

void FlashRankingTable::close() {
    // FUN_0077e110
    if ( pFilterPlayer != nullptr ) {
        pFilterPlayer->setVariableValue( "/:RankingLoading", 0 );
    }

    bResultsDisplayed = true;
    if ( pOwner != nullptr ) {
        pOwner->setRankingsLoading( false );
    }

    bOpened = false;
}

void FlashRankingTable::scroll( int32_t delta ) {
    // FUN_0077e0b0
    firstLine += delta;

    const int32_t lastFirstLine = getListLength() - numLines;
    if ( lastFirstLine <= firstLine ) {
        firstLine = lastFirstLine;
    }
    if ( firstLine < 0 ) {
        firstLine = 0;
    }

    requestPageIfNeeded();
    reset();
}

void FlashRankingTable::selectedLineChanged() {
    // FUN_0077e100
    requestPageIfNeeded();
}

void FlashRankingTable::refresh() {
    // FUN_0077cf80
    bRefreshPending = false;
    if ( !bOnline || pFilter->bFriends ) {
        return;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (leaderboard request)
}

void FlashRankingTable::requestAroundPlayer() {
    // FUN_0077ce10
    if ( !bOnline || pFilter->bFriends ) {
        return;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (leaderboard request)
}

void FlashRankingTable::nextSubPage() {
    // FUN_00779400
    subPageIndex = ( subPageIndex < numSubPages - 1 ) ? subPageIndex + 1 : 0;
}

void FlashRankingTable::previousSubPage() {
    // FUN_007793e0
    subPageIndex = ( ( subPageIndex != 0 ) ? subPageIndex : numSubPages ) - 1;
}

bool FlashRankingTable::requestPageIfNeeded() {
    // FUN_0077d0f0
    if ( !bOnline || pFilter->bFriends ) {
        return false;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (leaderboard page request)
    return false;
}

FlashRankingTableLocal::FlashRankingTableLocal()
    : FlashRankingTable(), savedFirstLine( 0 ) {
    // FUN_00764640 (inlined)
    field17_0x68 = true;
}

int32_t FlashRankingTableLocal::getListLength() const {
    // FUN_007793b0
    const GSOnline::eChallengeLeaderboard kind = GetLeaderboardKind( category );
    if ( kind == GSOnline::CL_Count ) {
        return 0;
    }

    return gpOnline->getNumChallengeLeaderboards( kind );
}

void FlashRankingTableLocal::swapOnlineTables( const bool bShowOnlineTables ) {
    FlashRankingTable *pEloTable = nullptr;
    FlashRankingTable *pChallengeTable = nullptr;
    pOwner->getOnlineTables( category, &pEloTable, &pChallengeTable );

    MngFlash *pMngFlash = pOwner->getMngFlash();
    FlashRankingTable &missionsTable = pOwner->getMissionsTable( 0 );
    if ( bShowOnlineTables ) {
        missionsTable.unregisterWidget( pMngFlash );
        if ( pEloTable != nullptr ) {
            pEloTable->registerWidget( pMngFlash );
            pChallengeTable->registerWidget( pMngFlash );
        }
    } else {
        if ( pEloTable != nullptr ) {
            pEloTable->unregisterWidget( pMngFlash );
            pChallengeTable->unregisterWidget( pMngFlash );
        }
        missionsTable.registerWidget( pMngFlash );
    }
}

void FlashRankingTableLocal::open() {
    // FUN_0077dd90
    FlashRankingTable::open();

    if ( pOwner == nullptr ) {
        return;
    }

    swapOnlineTables( false );

    const SoloChallengeStats &stats = pOwner->getSoloChallengeStats( category );

    char doneString[32];
    snprintf( doneString, sizeof( doneString ), "%u/%u",
              uint32_t( stats.NumDone ), uint32_t( stats.NumTotal ) );
    pFilterPlayer->setVariableValueNonLinked(
        "/ss_menu/table/tables/local_ranks/line0/text:done", doneString );
    pFilterPlayer->setVariableValue(
        "/ss_menu/table/tables/local_ranks/line0/text:bronze",
        stats.NumBronze );
    pFilterPlayer->setVariableValue(
        "/ss_menu/table/tables/local_ranks/line0/text:silver",
        stats.NumSilver );
    pFilterPlayer->setVariableValue(
        "/ss_menu/table/tables/local_ranks/line0/text:gold", stats.NumGold );

    if ( savedFirstLine > 0 ) {
        jumpToLine( savedFirstLine, -1 );
    }
}

void FlashRankingTableLocal::close() {
    // FUN_007797d0
    savedFirstLine = firstLine;
    bOpened = false;

    if ( pOwner != nullptr && !pOwner->isBackTransitionPending() ) {
        swapOnlineTables( true );
    }
}

void FlashRankingTableLocal::update( int32_t absoluteIndex,
                                     const int32_t lineIndex ) {
    // FUN_0077a8a0
    if ( savedFirstLine > 0 ) {
        jumpToLine( savedFirstLine, -1 );
        absoluteIndex += savedFirstLine;
        if ( numLines - 1 == lineIndex ) {
            savedFirstLine = 0;
        }
    }

    if ( getListLength() <= absoluteIndex ) {
        setLineState( eLineState::FLLS_Empty, lineIndex );
        return;
    }

    char clipPath[128];
    char validityPath[128];
    snprintf( clipPath, sizeof( clipPath ), "%s/line%d", movieName.c_str(),
              lineIndex + 1 );
    snprintf( validityPath, sizeof( validityPath ), "%s/line%d/:IsValid",
              movieName.c_str(), lineIndex + 1 );
    pMovie->setVariableValue( validityPath, 1 );
    pMovie->showFrame( "normal", clipPath, false );

    const GSOnline::eChallengeLeaderboard kind = GetLeaderboardKind( category );
    if ( kind == GSOnline::CL_Count ) {
        return;
    }

    const uint64_t spotHashcode =
        gpOnline->getChallengeLeaderboard( kind, absoluteIndex ).SpotHashcode;
    SavedRanking *pRanking = gpPlayerData->getLoadedSave()->getRanking();
    const float bestScore = pRanking->getChallengeBestScore( spotHashcode );

    char scoreString[128];
    if ( kind == GSOnline::CL_Speed ) {
        const CommonDB &commonDB = gpDatabase->getCommonDB();
        if ( gpPlayerData->isUsingImperialUnits() ) {
            snprintf( scoreString, sizeof( scoreString ), "%.2f %s",
                      double( bestScore * kMetersPerSecondToMph ),
                      commonDB.CommonStrings[0] );
        } else {
            snprintf( scoreString, sizeof( scoreString ), "%.2f %s",
                      double( bestScore * kMetersPerSecondToKmh ),
                      commonDB.CommonStrings[1] );
        }
    } else {
        const uint32_t centiseconds = uint32_t(
            int64_t( double( bestScore ) * double( kSecondsToCentiseconds ) ) );
        FormatChallengeTime( centiseconds, scoreString, sizeof( scoreString ) );
    }

    const char *pChallengeName =
        ( pOwner != nullptr )
            ? pOwner->getSoloChallengeName( category, absoluteIndex )
            : nullptr;

    Spots &spots = gpScenaric->getSpotList();
    const SpotMap *pSpot =
        spots.getWorldSpotChecked( spots.getSpotMapIndex( spotHashcode ) );
    if ( pSpot == nullptr || pSpot->Type != ST_Challenge ||
         GetSpotGameModeID( pSpot ) != kSoloChallengeGameModeID ) {
        setLineFieldNonLinked( lineIndex, "col_1", "" );
        setLineFieldNonLinked( lineIndex, "col_2", "" );
        return;
    }

    if ( pChallengeName != nullptr ) {
        setLineField( lineIndex, "col_1", pChallengeName );
    } else {
        setLineFieldNonLinked( lineIndex, "col_1", "-" );
    }

    const int32_t result = pRanking->getChallengeResult( spotHashcode );
    if ( result == -1 ) {
        strcpy( scoreString, "----" );
    }
    setLineFieldNonLinked( lineIndex, "col_2", scoreString );

    const char *pCupFrame = "no_cup";
    switch ( result ) {
    case 1:
        pCupFrame = "gold";
        break;
    case 2:
        pCupFrame = "silver";
        break;
    case 3:
        pCupFrame = "bronze";
        break;
    default:
        break;
    }
    showLineSubClipFrame( lineIndex, "cup", pCupFrame );
}

void FlashRankingTableLocal::scroll( int32_t delta ) {
    // FUN_0077ccd0
    FlashList::scroll( delta );
}

void FlashRankingTableLocal::selectedLineChanged() {
    // FUN_007797c0
}

FlashRankingTableMissions::FlashRankingTableMissions()
    : FlashRankingTable(),
      numMissions( 0 ),
      missionIndexes(),
      pSelectedMission( nullptr ) {}

int32_t FlashRankingTableMissions::getListLength() const {
    // FUN_007797a0
    if ( pOwner != nullptr && pOwner->isListLengthForced() ) {
        return kForcedListLength;
    }

    return numMissions;
}

Entity *
FlashRankingTableMissions::findGoldMission( const int32_t index ) const {
    EntityExtraChallenge *pMission = static_cast<EntityExtraChallenge *>(
        gpScenaric->findExtraChallenge( index ) );
    if ( pMission == nullptr ) {
        return nullptr;
    }

    const SavedItemTable *pItemTable =
        gpPlayerData->getLoadedSave()->getItemTable();
    return pItemTable->isExtraChallengeGold( pMission->getIndex() ) ? pMission
                                                                    : nullptr;
}

void FlashRankingTableMissions::open() {
    // FUN_0077d560
    FlashRankingTable::open();

    numMissions = 0;
    memset( missionIndexes, 0, sizeof( missionIndexes ) );

    const eEntityType missionType = GetMissionEntityType( getIDMagic() );
    const SavedItemTable *pItemTable =
        gpPlayerData->getLoadedSave()->getItemTable();
    const int32_t numExtraChallenges = gpScenaric->getNumExtraChallenges();

    for ( const bool bGoldPass : { true, false } ) {
        for ( int32_t i = 0; i < numExtraChallenges; i++ ) {
            EntityExtraChallenge *pMission =
                static_cast<EntityExtraChallenge *>(
                    gpScenaric->findExtraChallenge( i ) );
            if ( pMission == nullptr || pMission->getType() != missionType ||
                 numMissions >= kMaxMissions ) {
                continue;
            }

            if ( pItemTable->isExtraChallengeGold( pMission->getIndex() ) ==
                 bGoldPass ) {
                missionIndexes[numMissions++] = i;
            }
        }
    }

    pOwner->setMissionsOpen( true );
    selectedLineChanged();
}

void FlashRankingTableMissions::close() {
    // FUN_00779770
    pSelectedMission = nullptr;
    pOwner->setMissionsOpen( false );
    pOwner->invalidateLureMission();
    bOpened = false;
}

void FlashRankingTableMissions::update( const int32_t absoluteIndex,
                                        const int32_t lineIndex ) {
    // FUN_0077a6e0
    if ( getListLength() <= absoluteIndex ) {
        setLineState( eLineState::FLLS_Empty, lineIndex );
        return;
    }

    char clipPath[128];
    char validityPath[128];
    snprintf( clipPath, sizeof( clipPath ), "%s/line%d", movieName.c_str(),
              lineIndex + 1 );
    snprintf( validityPath, sizeof( validityPath ), "%s/line%d/:IsValid",
              movieName.c_str(), lineIndex + 1 );
    pMovie->setVariableValue( validityPath, 1 );
    pMovie->showFrame( "normal", clipPath, false );

    EntityExtraChallenge *pMission = static_cast<EntityExtraChallenge *>(
        findGoldMission( missionIndexes[absoluteIndex] ) );
    if ( pMission == nullptr ) {
        setLineFieldNonLinked( lineIndex, "col_1", "?" );
        setLineFieldNonLinked( lineIndex, "col_2", "" );
        return;
    }

    setLineFieldNonLinked( lineIndex, "col_1", pMission->getChallengeName() );

    const SavedItemTable *pItemTable =
        gpPlayerData->getLoadedSave()->getItemTable();
    const CarConfig *pCarConfig = gpDatabase->getCarConfig(
        int32_t( pItemTable->Values[pMission->getIndex()] ) );
    setLineField( lineIndex, "col_2",
                  ( pCarConfig != nullptr ) ? pCarConfig->FullName.c_str()
                                            : "----" );
}

void FlashRankingTableMissions::scroll( int32_t delta ) {
    // FUN_0077b1b0
    FlashList::scroll( delta );
    selectedLineChanged();
}

void FlashRankingTableMissions::selectedLineChanged() {
    // FUN_0077a680
    pSelectedMission =
        findGoldMission( missionIndexes[firstLine + selectedLineIndex] );
}

void FlashRankingTableOnline::update( const int32_t absoluteIndex,
                                      const int32_t lineIndex ) {
    if ( getListLength() <= absoluteIndex ) {
        setLineState( eLineState::FLLS_Empty, lineIndex );
        return;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (leaderboard rows)
}

void FlashRankingTableOnline::onFilterChanged( const bool,
                                               const bool,
                                               const bool ) {
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (leaderboard filters)
}
