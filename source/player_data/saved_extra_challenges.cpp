#include "shared.h"
#include "saved_extra_challenges.h"

#include "core/crypto/tea.h"
#include "core/ed_store.h"
#include "core/format.h"
#include "database/gs_database.h"
#include "flash/implementation/flash_player.h"
#include "flash/mng_flash_localize.h"
#include "gs_playerdata.h"
#include "achievements_data.h"
#include "driver.h"
#include "player_data_interface.h"

SavedExtraChallenges::SavedExtraChallenges() {
    // FUN_00889260
    clear();

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (the original also seeds the
                             // level from the achievements on its reset path)
}

SavedExtraChallenges::~SavedExtraChallenges() {}

void SavedExtraChallenges::clear() {
    memset( Counters, 0, sizeof( Counters ) );
    memset( RuntimeCounters, 0, sizeof( RuntimeCounters ) );
    for ( int32_t i = 0; i < kNumCounters + kNumRuntimeCounters; i++ ) {
        writeCounter( i, 0 );
    }

    memset( Scores, 0, sizeof( Scores ) );

    Field_0xf188 = 0.0f;
    Field_0xf18c = 0;
    Field_0xf190 = 0.0f;
    Field_0xf194 = 0;
    Field_0xf198 = 0;
    Field_0xf19c = 0;
    Field_0xf1a0 = 0.0f;
    Field_0xf1a4 = 0.0f;

    PlayerLevel = 0;
    Field_0xf02c = 0;
    Field_0xf030 = 0;

    memset( Flags, 0, sizeof( Flags ) );
    memset( BestLow, 0, sizeof( BestLow ) );
    memset( BestHigh, 0, sizeof( BestHigh ) );
    memset( Tail, 0, sizeof( Tail ) );
}

void SavedExtraChallenges::serialize( edStore &stream ) const {
    // FUN_00888270
    stream.writeInteger( 0xc103 );

    for ( const int32_t counter : Counters ) {
        stream.writeInteger( counter );
    }

    for ( int32_t i = 0; i < kNumScores - 2; i++ ) {
        stream.writeDouble( Scores[i] );
    }
    stream.writeDouble( Scores[kNumScores - 1] );
    stream.writeDouble( Scores[kNumScores - 2] );

    stream.writeFloat( Field_0xf188 );
    stream.writeInteger( Field_0xf18c );
    stream.writeFloat( Field_0xf190 );
    stream.writeInteger( Field_0xf194 );
    stream.writeInteger( Field_0xf198 );
    stream.writeInteger( Field_0xf19c );
    stream.writeFloat( Field_0xf1a0 );
    stream.writeFloat( Field_0xf1a4 );

    for ( const int32_t flag : Flags ) {
        stream.writeInteger( flag );
    }

    for ( int32_t i = 0; i < kNumBestPairs; i++ ) {
        stream.writeInteger( BestLow[i] );
        stream.writeInteger( BestHigh[i] );
    }

    for ( const int32_t value : Tail ) {
        stream.writeInteger( value );
    }

    stream.writeInteger( 0xc103 );
}

void SavedExtraChallenges::deserialize( edStore &stream ) {
    // FUN_00888670
    stream.readInteger();

    for ( int32_t &counter : Counters ) {
        counter = stream.readInteger();
    }

    for ( int32_t i = 0; i < kNumScores - 2; i++ ) {
        Scores[i] = stream.readDouble();
    }
    Scores[kNumScores - 1] = stream.readDouble();
    Scores[kNumScores - 2] = stream.readDouble();

    Field_0xf188 = stream.readFloat();
    Field_0xf18c = stream.readInteger();
    Field_0xf190 = stream.readFloat();
    Field_0xf194 = stream.readInteger();
    Field_0xf198 = stream.readInteger();
    Field_0xf19c = stream.readInteger();
    Field_0xf1a0 = stream.readFloat();
    Field_0xf1a4 = stream.readFloat();

    for ( int32_t &flag : Flags ) {
        flag = stream.readInteger();
    }

    for ( int32_t i = 0; i < kNumBestPairs; i++ ) {
        BestLow[i] = stream.readInteger();
        BestHigh[i] = stream.readInteger();
    }

    for ( int32_t &value : Tail ) {
        value = stream.readInteger();
    }

    stream.readInteger();
}

uint32_t SavedExtraChallenges::getFlagBlock( const int32_t blockIndex ) const {
    if ( blockIndex < 0 || blockIndex >= kNumFlagBlocks ) {
        return 0u;
    }

    return TestDriveTEAReadDword( (const uint32_t *)&Flags[blockIndex * 2] );
}

void SavedExtraChallenges::setFlagBlock( const int32_t blockIndex,
                                         const uint32_t value ) {
    if ( blockIndex < 0 || blockIndex >= kNumFlagBlocks ) {
        return;
    }

    TestDriveTEAWriteDword( (uint32_t *)&Flags[blockIndex * 2], value );
}

static_assert( offsetof( SavedExtraChallenges, PlayerLevel ) == 0,
               "Offset mismatch; savegame blob reads might return garbage" );
static_assert( offsetof( SavedExtraChallenges, RuntimeCounters ) ==
                   offsetof( SavedExtraChallenges, Counters ) +
                       sizeof( int32_t ) * SavedExtraChallenges::kNumCounters,
               "Offset mismatch; savegame blob reads might return garbage" );
static_assert(
    offsetof( SavedExtraChallenges, Scores ) ==
        offsetof( SavedExtraChallenges, Counters ) + 0xf130 - 0xf038,
    "Scores Offset mismatch; savegame blob reads might return garbage" );
static_assert(
    offsetof( SavedExtraChallenges, Flags ) ==
        offsetof( SavedExtraChallenges, Counters ) + 0xf1a8 - 0xf038,
    "Flags Offset mismatch; savegame blob reads might return garbage" );

static constexpr uint32_t kCommonStringDB = 0;
static constexpr uint64_t kMetricUnitHashcode = 0x5daec3ull;
static constexpr uint64_t kImperialUnitHashcode = 0x5c2823ull;
static constexpr float kKilometresToMiles = 0.6215040683746338f; // DAT_00f418c0
static constexpr float kSecondsToHours = 0.00027777778450399637f;
static constexpr float kSecondsToMinutes = 0.01666666753590107f;
static constexpr float kPercentScale = 100.0f; // DAT_00f41708

static constexpr const char *kStatsTitleVar = "/:stats_title";
static constexpr const char *kStatsLeftVar = "/:txt_field_01";
static constexpr const char *kStatsRightVar = "/:txt_field_02";

// Category titles.
static constexpr uint64_t kTitleChallenges = 0x357cd32ull; // cases 0..2
static constexpr uint64_t kTitleTimes = 0x33ccb22ull;      // cases 3, 4
static constexpr uint64_t kTitleDriving = 0x3477982ull;    // case 5
static constexpr uint64_t kTitleSpeeds = 0x358946aull;     // case 6
static constexpr uint64_t kTitleDiscovery = 0x3b6dd22ull;  // case 7
static constexpr uint64_t kTitleShopping = 0x33b968aull;   // cases 8..11

static constexpr int32_t CounterIndex( const int32_t retailOffset ) {
    return ( retailOffset - 0xf038 ) / 4;
}
static constexpr int32_t ScoreIndex( const int32_t retailOffset ) {
    return ( retailOffset - 0xf130 ) / 8;
}

int32_t SavedExtraChallenges::readCounter( const int32_t index ) const {
    if ( index < 0 || index >= kNumCounters + kNumRuntimeCounters ) {
        return 0;
    }

    uint32_t pPlainText[2] = {};
    TestDriveTEADecrypt( pPlainText, (const uint32_t *)&Counters[index & ~1] );
    return (int32_t)pPlainText[index & 1];
}

void SavedExtraChallenges::writeCounter( const int32_t index,
                                         const int32_t value ) {
    if ( index < 0 || index >= kNumCounters + kNumRuntimeCounters ) {
        return;
    }

    uint32_t *pBlock = (uint32_t *)&Counters[index & ~1];

    uint32_t pPlainText[2] = {};
    TestDriveTEADecrypt( pPlainText, pBlock );
    pPlainText[index & 1] = (uint32_t)value;
    TestDriveTEAEncrypt( pBlock, pPlainText );
}

static float DwordAsFloat( const uint32_t bits ) {
    float asFloat = 0.0f;
    memcpy( &asFloat, &bits, sizeof( float ) );
    return asFloat;
}

uint32_t SavedExtraChallenges::readTailDword( const int32_t *pBlock,
                                              const bool bHighHalf ) const {
    uint32_t pPlainText[2] = {};
    TestDriveTEADecrypt( pPlainText, (const uint32_t *)pBlock );
    return pPlainText[bHighHalf ? 1 : 0];
}

double SavedExtraChallenges::readScore( const int32_t index ) const {
    if ( index < 0 || index >= kNumScores ) {
        return 0.0;
    }

    const uint64_t bits =
        TestDriveTEAReadQword( (const uint32_t *)&Scores[index] );

    double asDouble = 0.0;
    memcpy( &asDouble, &bits, sizeof( double ) );
    return asDouble;
}

void SavedExtraChallenges::resetStatisticsColumns() {
    memset( statisticsLeftColumn, 0, sizeof( statisticsLeftColumn ) );
    memset( statisticsRightColumn, 0, sizeof( statisticsRightColumn ) );
}

void SavedExtraChallenges::publishStatisticsColumns() {
    if ( pStatisticsPlayer == nullptr ) {
        return;
    }

    pStatisticsPlayer->setVariableValue( kStatsLeftVar, statisticsLeftColumn );
    pStatisticsPlayer->setVariableValue( kStatsRightVar,
                                         statisticsRightColumn );
}

static void AppendToColumn( char *pColumn,
                            const size_t columnSize,
                            const char *pFormat,
                            const char *pText ) {
    const size_t used = strlen( pColumn );
    if ( used + 1 >= columnSize ) {
        return;
    }

    snprintf( pColumn + used, columnSize - used, pFormat,
              ( pText != nullptr ) ? pText : "" );
}

void SavedExtraChallenges::appendLabelAndValue( const char *pLabel,
                                                const char *pValue ) {
    AppendToColumn( statisticsLeftColumn, sizeof( statisticsLeftColumn ),
                    "%s\n\n", pLabel );
    AppendToColumn( statisticsRightColumn, sizeof( statisticsRightColumn ),
                    "\n%s\n", pValue );
}

static void
LocalizeLabel( char *pDest, const size_t destSize, const uint64_t hashcode ) {
    // FUN_00717280
    pDest[0] = '\0';
    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->makeSentenceFromHashcode( pDest, destSize, hashcode,
                                                      0 );
    }
}

void SavedExtraChallenges::appendStatLine( const uint64_t labelHashcode,
                                           const int32_t value ) {
    // FUN_00888d10
    char label[256];
    LocalizeLabel( label, sizeof( label ), labelHashcode );

    char formatted[32];
    snprintf( formatted, sizeof( formatted ), "%d", value );

    appendLabelAndValue( label, formatted );
}

void SavedExtraChallenges::appendDistanceLine( const uint64_t labelHashcode,
                                               float value ) {
    // FUN_00888c40
    char label[256];
    LocalizeLabel( label, sizeof( label ), labelHashcode );

    const bool bImperial =
        ( gpPlayerData != nullptr ) && gpPlayerData->isUsingImperialUnits();
    if ( bImperial ) {
        value = value * kKilometresToMiles;
    }

    const char *pUnit =
        ( gpDatabase != nullptr )
            ? gpDatabase->getStringByHashcode( kCommonStringDB,
                                               bImperial ? kImperialUnitHashcode
                                                         : kMetricUnitHashcode )
            : nullptr;

    char formatted[160];
    snprintf( formatted, sizeof( formatted ), "%.2f %s", value,
              ( pUnit != nullptr ) ? pUnit : "" );

    appendLabelAndValue( label, formatted );
}

void SavedExtraChallenges::appendDurationLine( const uint64_t labelHashcode,
                                               const double seconds ) {
    // FUN_0088a3e0
    char label[256];
    LocalizeLabel( label, sizeof( label ), labelHashcode );

    const float asFloat = (float)seconds;
    const int32_t hours = (int32_t)floorf( asFloat * kSecondsToHours );
    const float remainder = asFloat - (float)hours * 3600.0f;
    const int32_t minutes = (int32_t)floorf( remainder * kSecondsToMinutes );
    const int32_t rest = (int32_t)floorf( remainder - (float)minutes * 60.0f );

    char formatted[128];
    snprintf( formatted, sizeof( formatted ), "%d:%02d'%02d\"", hours, minutes,
              rest );

    appendLabelAndValue( label, formatted );
}

void SavedExtraChallenges::appendMoneyLine( const uint64_t labelHashcode,
                                            const int32_t money,
                                            const float ratio ) {
    // FUN_0088a770
    char label[256];
    LocalizeLabel( label, sizeof( label ), labelHashcode );

    char moneyString[128] = {};
    FormatMoneyString( moneyString, money );

    char percent[32];
    snprintf( percent, sizeof( percent ), "%.2f", ratio * kPercentScale );

    char formatted[192];
    snprintf( formatted, sizeof( formatted ), "%s (%s%%)", moneyString,
              percent );

    appendLabelAndValue( label, formatted );
}

void SavedExtraChallenges::appendSpendValue( const uint64_t labelHashcode,
                                             const int32_t money,
                                             const float total ) {
    float ratio = 0.0f;
    if ( total > 0.0f ) {
        ratio = (float)money / total;
    }

    appendMoneyLine( labelHashcode, money, ratio );
}

void SavedExtraChallenges::appendSpendLine( const uint64_t labelHashcode,
                                            const int32_t counterIndex,
                                            const float total ) {
    appendSpendValue( labelHashcode, readCounter( counterIndex ), total );
}

void SavedExtraChallenges::appendMoneyOnlyLine( const uint64_t labelHashcode,
                                                const int32_t money ) {
    char label[256];
    LocalizeLabel( label, sizeof( label ), labelHashcode );

    char moneyString[128] = {};
    FormatMoneyString( moneyString, money );

    appendLabelAndValue( label, moneyString );
}

void SavedExtraChallenges::appendDoneTotalLine( const uint64_t labelHashcode,
                                                const int32_t money,
                                                const int32_t done,
                                                const int32_t total ) {
    char label[256];
    LocalizeLabel( label, sizeof( label ), labelHashcode );

    char moneyString[128] = {};
    FormatMoneyString( moneyString, money );

    char formatted[192];
    snprintf( formatted, sizeof( formatted ), "%d/%d (%s)", done, total,
              moneyString );

    appendLabelAndValue( label, formatted );
}

void SavedExtraChallenges::appendPercentLine( const uint64_t labelHashcode,
                                              const int32_t a,
                                              const int32_t b ) {
    // FUN_0088a390
    char label[256];
    LocalizeLabel( label, sizeof( label ), labelHashcode );

    int32_t percent = 0;
    if ( a + b != 0 ) {
        percent = (int32_t)floorf( ( (float)a / ( (float)b + (float)a ) ) *
                                   kPercentScale );
    }

    char formatted[32];
    snprintf( formatted, sizeof( formatted ), "%d%%", percent );

    appendLabelAndValue( label, formatted );
}

float SavedExtraChallenges::sumCounterRun( const int32_t firstOffset,
                                           const int32_t endOffset ) const {
    float total = 0.0f;
    for ( int32_t off = firstOffset; off < endOffset; off += 4 ) {
        total += (float)readCounter( CounterIndex( off ) );
    }
    return total;
}

int32_t SavedExtraChallenges::readFlag( const int32_t index ) const {
    if ( index < 0 || index >= kNumFlags ) {
        return 0;
    }

    uint32_t pPlainText[2] = {};
    TestDriveTEADecrypt( pPlainText, (const uint32_t *)&Flags[index & ~1] );
    return (int32_t)pPlainText[index & 1];
}

void SavedExtraChallenges::appendQuadLine( const uint64_t labelHashcode,
                                           const int32_t a,
                                           const int32_t b,
                                           const int32_t c,
                                           const int32_t d ) {
    // FUN_00888c90
    char label[256];
    LocalizeLabel( label, sizeof( label ), labelHashcode );

    char formatted[128];
    snprintf( formatted, sizeof( formatted ), "%d / %d / %d / %d", a, b, c, d );

    appendLabelAndValue( label, formatted );
}

void SavedExtraChallenges::openStatisticsScreen( FlashPlayer *pPlayer ) {
    // FUN_009d2ac0 (inlined)
    pStatisticsPlayer = pPlayer;
    statisticsCategory = 0;
    buildStatisticsPage();
}

void SavedExtraChallenges::closeStatisticsScreen() {
    // FUN_009d2ac0 (inlined)
    pStatisticsPlayer = nullptr;
}

void SavedExtraChallenges::nextStatisticsCategory() {
    // FUN_0088d350
    statisticsCategory++;
    if ( statisticsCategory > kNumStatisticsCategories - 1 ) {
        statisticsCategory = 0;
    }
    buildStatisticsPage();
}

void SavedExtraChallenges::previousStatisticsCategory() {
    // FUN_0088d330
    statisticsCategory--;
    if ( statisticsCategory < 0 ) {
        statisticsCategory = kNumStatisticsCategories - 1;
    }
    buildStatisticsPage();
}

void SavedExtraChallenges::buildStatisticsPage() {
    // FUN_0088d190
    switch ( statisticsCategory ) {
    case 0:
        buildCategoryChallenges();
        return;
    case 1:
        buildCategoryRaces();
        return;
    case 2:
        buildCategoryClubs();
        return;
    case 3:
        buildCategoryTimes();
        return;
    case 4:
        buildCategorySessions();
        return;
    case 5:
        buildCategoryDriving();
        return;
    case 6:
        buildCategorySpeeds();
        return;
    case 7:
        buildCategoryDiscovery();
        return;
    case 8:
        buildCategoryShopping();
        return;
    case 9:
        buildCategoryCars();
        return;
    case 10:
        buildCategoryBikes();
        return;
    case 11:
        buildCategoryHouses();
        return;
    default:
        return;
    }
}

void SavedExtraChallenges::tickStatisticsScreen() {
    // FUN_0088d260
    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE

    if ( pStatisticsPlayer != nullptr && statisticsCategory > 2 &&
         statisticsCategory < 5 ) {
        buildStatisticsPage();
    }
}

void SavedExtraChallenges::buildCategoryChallenges() {
    // FUN_0088cf60
    if ( pStatisticsPlayer == nullptr ) {
        return;
    }

    resetStatisticsColumns();

    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->setVarLocalized( pStatisticsPlayer, kStatsTitleVar,
                                             kTitleChallenges );
    }

    appendStatLine( 0x372850aull, readCounter( CounterIndex( 0xf040 ) ) );
    appendStatLine( 0x391098aull, readCounter( CounterIndex( 0xf050 ) ) );
    appendStatLine( 0x36342caull, readCounter( CounterIndex( 0xf038 ) ) );
    appendStatLine( 0x381c74aull, readCounter( CounterIndex( 0xf048 ) ) );
    appendStatLine( 0x3a04bcaull, readCounter( CounterIndex( 0xf03c ) ) +
                                      readCounter( CounterIndex( 0xf044 ) ) );
    appendStatLine( 0x3af8e0aull, readCounter( CounterIndex( 0xf04c ) ) +
                                      readCounter( CounterIndex( 0xf054 ) ) );

    publishStatisticsColumns();
}

void SavedExtraChallenges::buildCategoryRaces() {
    // FUN_0088cd50
    if ( pStatisticsPlayer == nullptr ) {
        return;
    }

    resetStatisticsColumns();

    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->setVarLocalized( pStatisticsPlayer, kStatsTitleVar,
                                             kTitleChallenges );
    }

    appendStatLine( 0x3bed04aull, readCounter( CounterIndex( 0xf058 ) ) );
    appendStatLine( 0x33702aaull, readCounter( CounterIndex( 0xf05c ) ) );
    appendStatLine( 0x34644eaull, readCounter( CounterIndex( 0xf060 ) ) );
    appendStatLine( 0x355872aull, readCounter( CounterIndex( 0xf064 ) ) );
    appendStatLine( 0x364c96aull, readCounter( CounterIndex( 0xf068 ) ) );
    appendStatLine( 0x3740baaull, readCounter( CounterIndex( 0xf06c ) ) );
    appendStatLine( 0x36c919aull, readCounter( CounterIndex( 0xf070 ) ) );

    publishStatisticsColumns();
}

void SavedExtraChallenges::buildCategoryClubs() {
    // FUN_0088cc60
    if ( pStatisticsPlayer == nullptr ) {
        return;
    }

    resetStatisticsColumns();

    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->setVarLocalized( pStatisticsPlayer, kStatsTitleVar,
                                             kTitleChallenges );
    }

    appendStatLine( 0x3834deaull, readCounter( CounterIndex( 0xf074 ) ) );
    appendStatLine( 0x392902aull, readCounter( CounterIndex( 0xf078 ) ) );

    publishStatisticsColumns();
}

void SavedExtraChallenges::buildCategoryTimes() {
    // FUN_0088ca60
    if ( pStatisticsPlayer == nullptr ) {
        return;
    }

    resetStatisticsColumns();

    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->setVarLocalized( pStatisticsPlayer, kStatsTitleVar,
                                             kTitleTimes );
    }

    appendDurationLine( 0x3a1d26aull, readScore( ScoreIndex( 0xf130 ) ) );
    appendDurationLine( 0x3b114aaull, readScore( ScoreIndex( 0xf138 ) ) );
    appendDurationLine( 0x3c056eaull, readScore( ScoreIndex( 0xf140 ) ) );
    appendDurationLine( 0x338894aull, readScore( ScoreIndex( 0xf148 ) ) );
    appendDurationLine( 0x347cb8aull, readScore( ScoreIndex( 0xf150 ) ) );
    appendDurationLine( 0x3570dcaull, readScore( ScoreIndex( 0xf158 ) ) );
    appendDurationLine( 0x366500aull, readScore( ScoreIndex( 0xf160 ) ) );

    publishStatisticsColumns();
}

void SavedExtraChallenges::buildCategorySessions() {
    // FUN_0088c8b0
    if ( pStatisticsPlayer == nullptr ) {
        return;
    }

    resetStatisticsColumns();

    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->setVarLocalized( pStatisticsPlayer, kStatsTitleVar,
                                             kTitleTimes );
    }

    appendDurationLine( 0x375924aull, readScore( ScoreIndex( 0xf180 ) ) );
    appendDurationLine( 0x384d48aull, readScore( ScoreIndex( 0xf178 ) ) );

    const double onFoot = readScore( ScoreIndex( 0xf168 ) );
    const double driving = readScore( ScoreIndex( 0xf170 ) );
    const double total = driving + onFoot;

    int32_t percent = 100;
    if ( total != 0.0 ) {
        percent = (int32_t)( ( onFoot / total ) * 100.0 );
    }

    const char *pLabel =
        ( gpDatabase != nullptr )
            ? gpDatabase->getStringByHashcode( kCommonStringDB, 0x39416caull )
            : nullptr;

    AppendToColumn( statisticsLeftColumn, sizeof( statisticsLeftColumn ),
                    "%s\n", pLabel );

    char split[64];
    snprintf( split, sizeof( split ), "\n%d%% - %d%%", percent, 100 - percent );
    AppendToColumn( statisticsRightColumn, sizeof( statisticsRightColumn ),
                    "%s", split );

    publishStatisticsColumns();
}

void SavedExtraChallenges::buildCategoryDriving() {
    // FUN_0088c720
    if ( pStatisticsPlayer == nullptr ) {
        return;
    }

    resetStatisticsColumns();

    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->setVarLocalized( pStatisticsPlayer, kStatsTitleVar,
                                             kTitleDriving );
    }

    const int32_t *pSpeedBlock = (const int32_t *)&Field_0xf190;
    appendDistanceLine( 0x3a3590aull,
                        DwordAsFloat( readTailDword( pSpeedBlock, false ) ) );
    appendStatLine( 0x3b29b4aull, (int32_t)readTailDword( pSpeedBlock, true ) );

    IPlayerData *pSave =
        ( gpPlayerData != nullptr ) ? gpPlayerData->getLoadedSave() : nullptr;
    AchievementsData *pAchievements =
        ( pSave != nullptr ) ? pSave->getAchievements() : nullptr;
    appendStatLine(
        0x36f9edaull,
        ( pAchievements != nullptr )
            ? (int32_t)pAchievements->getNumCustomChallengesUploaded()
            : 0 );

    const int32_t *pTailBlock = &Field_0xf198;
    appendStatLine( 0x33a0feaull, (int32_t)readTailDword( pTailBlock, false ) );
    appendStatLine( 0x349522aull, (int32_t)readTailDword( pTailBlock, true ) );

    publishStatisticsColumns();
}

void SavedExtraChallenges::buildCategorySpeeds() {
    // FUN_0088c600
    if ( pStatisticsPlayer == nullptr ) {
        return;
    }

    resetStatisticsColumns();

    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->setVarLocalized( pStatisticsPlayer, kStatsTitleVar,
                                             kTitleSpeeds );
    }

    IPlayerData *pSave =
        ( gpPlayerData != nullptr ) ? gpPlayerData->getLoadedSave() : nullptr;
    Driver *pDriver = ( pSave != nullptr ) ? pSave->getDriver() : nullptr;
    appendDistanceLine( 0x367d6aaull, ( pDriver != nullptr )
                                          ? pDriver->getOdometerKms()
                                          : 0.0f );

    const int32_t *pBlock = (const int32_t *)&Field_0xf1a0;
    appendDistanceLine( 0x3865b2aull,
                        DwordAsFloat( readTailDword( pBlock, false ) ) );
    appendDistanceLine( 0x37718eaull,
                        DwordAsFloat( readTailDword( pBlock, true ) ) );

    publishStatisticsColumns();
}

void SavedExtraChallenges::buildCategoryDiscovery() {
    // FUN_0088c0c0
    if ( pStatisticsPlayer == nullptr ) {
        return;
    }

    resetStatisticsColumns();

    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->setVarLocalized( pStatisticsPlayer, kStatsTitleVar,
                                             kTitleDiscovery );
    }

    const int32_t roadsDone = readFlag( 0 );
    const int32_t roadsLeft = readFlag( 1 );
    appendPercentLine( 0x3959d6aull, roadsDone, roadsLeft );

    appendPercentLine( 0x3a4dfaaull, readFlag( 4 ) + readFlag( 6 ),
                       readFlag( 7 ) + readFlag( 5 ) );

    appendPercentLine( 0x3b421eaull, readFlag( 2 ), readFlag( 3 ) );

    const int32_t groupA = readFlag( 3 ) + readFlag( 2 );
    const int32_t groupB =
        readFlag( 7 ) + readFlag( 6 ) + readFlag( 5 ) + readFlag( 4 );
    const int32_t groupC = readFlag( 9 ) + readFlag( 8 );
    appendQuadLine( 0x3c3642aull, groupA + groupB + groupC, groupA, groupB,
                    groupC );

    publishStatisticsColumns();
}

void SavedExtraChallenges::buildCategoryShopping() {
    // FUN_0088bad0
    if ( pStatisticsPlayer == nullptr ) {
        return;
    }

    resetStatisticsColumns();

    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->setVarLocalized( pStatisticsPlayer, kStatsTitleVar,
                                             kTitleShopping );
    }

    const int32_t spentTotal = (int32_t)sumCounterRun( 0xf07c, 0xf0c0 );
    appendMoneyOnlyLine( 0x35a1b0aull, spentTotal );

    appendMoneyOnlyLine( 0x3ba637aull, readCounter( CounterIndex( 0xf08c ) ) );

    appendDoneTotalLine( 0x3695d4aull, readCounter( CounterIndex( 0xf094 ) ),
                         readFlag( 12 ), readFlag( 13 ) + readFlag( 12 ) );
    appendDoneTotalLine( 0x3789f8aull, readCounter( CounterIndex( 0xf090 ) ),
                         readFlag( 0 ), readFlag( 1 ) + readFlag( 0 ) );

    appendMoneyOnlyLine( 0x3ab213aull,
                         readCounter( CounterIndex( 0xf088 ) ) +
                             readCounter( CounterIndex( 0xf084 ) ) );
    appendMoneyOnlyLine( 0x3605c9aull, readCounter( CounterIndex( 0xf098 ) ) );

    publishStatisticsColumns();
}

void SavedExtraChallenges::buildCategoryCars() {
    // FUN_0088b510
    if ( pStatisticsPlayer == nullptr ) {
        return;
    }

    resetStatisticsColumns();

    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->setVarLocalized( pStatisticsPlayer, kStatsTitleVar,
                                             kTitleShopping );
    }

    const float total = sumCounterRun( 0xf0c0, 0xf104 );

    appendMoneyOnlyLine( 0x397240aull, (int32_t)total );

    appendSpendValue( 0x3a6664aull,
                      readCounter( CounterIndex( 0xf0c0 ) ) +
                          readCounter( CounterIndex( 0xf0c4 ) ) +
                          readCounter( CounterIndex( 0xf0c8 ) ) +
                          readCounter( CounterIndex( 0xf0cc ) ),
                      total );
    appendSpendValue( 0x3b5a88aull,
                      readCounter( CounterIndex( 0xf0e0 ) ) +
                          readCounter( CounterIndex( 0xf0e4 ) ),
                      total );

    appendStatLine( 0x34ad8caull, readCounter( CounterIndex( 0xf108 ) ) );
    appendStatLine( 0x3c4eacaull, readCounter( CounterIndex( 0xf11c ) ) );
    appendSpendLine( 0x32e01faull, CounterIndex( 0xf0ec ), total );
    appendStatLine( 0x33d443aull, readCounter( CounterIndex( 0xf120 ) ) );
    appendSpendLine( 0x34c867aull, CounterIndex( 0xf0f0 ), total );

    publishStatisticsColumns();
}

void SavedExtraChallenges::buildCategoryBikes() {
    // FUN_0088b0d0
    if ( pStatisticsPlayer == nullptr ) {
        return;
    }

    resetStatisticsColumns();

    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->setVarLocalized( pStatisticsPlayer, kStatsTitleVar,
                                             kTitleShopping );
    }

    const float total = sumCounterRun( 0xf0c0, 0xf104 );

    appendStatLine( 0x35bc8baull, readCounter( CounterIndex( 0xf114 ) ) );
    appendSpendLine( 0x36b0afaull, CounterIndex( 0xf0e0 ), total );
    appendStatLine( 0x37a4d3aull, readCounter( CounterIndex( 0xf118 ) ) );
    appendSpendLine( 0x3898f7aull, CounterIndex( 0xf0e4 ), total );
    appendStatLine( 0x398d1baull, readCounter( CounterIndex( 0xf110 ) ) );
    appendSpendLine( 0x3a813faull, CounterIndex( 0xf0e8 ), total );

    publishStatisticsColumns();
}

void SavedExtraChallenges::buildCategoryHouses() {
    // FUN_0088ad00
    if ( pStatisticsPlayer == nullptr ) {
        return;
    }

    resetStatisticsColumns();

    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->setVarLocalized( pStatisticsPlayer, kStatsTitleVar,
                                             kTitleShopping );
    }

    const float total = sumCounterRun( 0xf0c0, 0xf104 );

    appendStatLine( 0x3b7563aull, readCounter( CounterIndex( 0xf10c ) ) );
    appendSpendLine( 0x32f889aull, CounterIndex( 0xf0f4 ), total );
    appendStatLine( 0x33ecadaull, readCounter( CounterIndex( 0xf124 ) ) );
    appendStatLine( 0x34e0d1aull, readCounter( CounterIndex( 0xf128 ) ) );
    appendSpendLine( 0x37bd3daull, CounterIndex( 0xf0f8 ), total );
    appendStatLine( 0x35d4f5aull, readCounter( CounterIndex( 0xf12c ) ) );

    publishStatisticsColumns();
}
