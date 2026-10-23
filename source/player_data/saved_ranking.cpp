#include "shared.h"
#include "saved_ranking.h"

#include "core/crypto/tea.h"
#include "core/ed_store.h"
#include "gs_playerdata.h"
#include "player_data_interface.h"
#include "saved_string_table.h"

static constexpr const char *kNumUsedSplitTimesKey = "RnkChkC"; // DAT_00f16714
static constexpr uint64_t kUnusedEntryHashcode = 0xffffffffull;

static int32_t GetBestChallengeResult( const int32_t resultA,
                                       const int32_t resultB ) {
    // FUN_0088e4c0
    for ( int32_t cup = 1; cup <= 3; cup++ ) {
        if ( resultA == cup || resultB == cup ) {
            return cup;
        }
    }

    if ( resultA == 0 || resultB == 0 ) {
        return 0;
    }

    return -1;
}

void RankingEntry::clear() {
    // FUN_0088ef40 + FUN_0088eec0
    TestDriveTEAWriteQword( (uint32_t *)&Hashcode, 0xffffffffull );
    TestDriveTEAWriteQword( pBlock0, 0xffffffffffffffffull );
    TestDriveTEAWriteQword( pBlock1, 0xffffffff00000009ull );
}

uint64_t RankingEntry::getHashcode() const {
    return TestDriveTEAReadQword( (const uint32_t *)&Hashcode );
}

int32_t RankingEntry::getResult() const {
    // FUN_00416410
    return int32_t( TestDriveTEAReadDword( pBlock0 ) );
}

float RankingEntry::getBestScore() const {
    // FUN_004163d0
    uint32_t pPlainText[2] = {};
    TestDriveTEADecrypt( pPlainText, pBlock0 );

    float bestScore = 0.0f;
    memcpy( &bestScore, &pPlainText[1], sizeof( float ) );
    return bestScore;
}

uint32_t RankingEntry::getNumSplitTimes() const {
    // FUN_00416350
    return TestDriveTEAReadDword( pBlock1 );
}

uint32_t RankingEntry::getSplitTimesIndex() const {
    // FUN_00416390
    return uint32_t( TestDriveTEAReadQword( pBlock1 ) >> 32ull );
}

void RankingEntry::setHashcode( const uint64_t hashcode ) {
    // FUN_00430cf0
    TestDriveTEAWriteQword( (uint32_t *)&Hashcode, hashcode );
}

void RankingEntry::setResultAndBestScore( const int32_t result,
                                          const float bestScore ) {
    // FUN_0088efd0 (without the hashcode write)
    uint32_t bestScoreAsDword = 0u;
    memcpy( &bestScoreAsDword, &bestScore, sizeof( uint32_t ) );

    TestDriveTEAWriteQword( pBlock0,
                            uint64_t( uint32_t( result ) ) |
                                ( uint64_t( bestScoreAsDword ) << 32ull ) );
}

void RankingEntry::setSplitTimes( const uint32_t numSplitTimes,
                                  const uint32_t splitTimesIndex ) {
    // FUN_0088eec0
    TestDriveTEAWriteQword( pBlock1,
                            uint64_t( numSplitTimes ) |
                                ( uint64_t( splitTimesIndex ) << 32ull ) );
}

void RankingEntry::deserialize( edStore &stream ) {
    // FUN_0088ffb0 (inlined)
    Hashcode = stream.readULongLong();

    pBlock0[0] = uint32_t( stream.readInteger() );

    const float asFloat = stream.readFloat();
    memcpy( &pBlock0[1], &asFloat, sizeof( uint32_t ) );

    pBlock1[0] = stream.readULong();
    pBlock1[1] = stream.readULong();
}

void RankingEntry::serialize( edStore &stream ) const {
    // FUN_0088ff20 (inlined)
    stream.writeULongLong( Hashcode );

    stream.writeInteger( int32_t( pBlock0[0] ) );

    float asFloat = 0.0f;
    memcpy( &asFloat, &pBlock0[1], sizeof( float ) );
    stream.writeFloat( asFloat );

    stream.writeInteger( int32_t( pBlock1[0] ) );
    stream.writeInteger( int32_t( pBlock1[1] ) );
}

SavedRanking::SavedRanking() : Notifiable(), Entries(), SplitTimesPool() {
    // FUN_00415ea0
    clear();
}

SavedRanking::~SavedRanking() {}

void SavedRanking::clear() {
    // FUN_00890460
    for ( RankingEntry &entry : Entries ) {
        entry.clear();
    }

    NumUsedSplitTimes = 0xffffffffu;
    NumSplitTimesSerialized = kSplitTimesPoolSize;
    memset( SplitTimesPool, 0, sizeof( SplitTimesPool ) );
}

void SavedRanking::serialize( edStore &stream ) const {
    // FUN_0088ff20
    for ( const RankingEntry &entry : Entries ) {
        entry.serialize( stream );
    }

    stream.writeInteger( int32_t( NumSplitTimesSerialized ) );
    for ( uint32_t i = 0u; i < NumSplitTimesSerialized; i++ ) {
        stream.writeInteger( int32_t( SplitTimesPool[i] ) );
    }
}

void SavedRanking::deserialize( edStore &stream ) {
    // FUN_0088ffb0
    for ( RankingEntry &entry : Entries ) {
        entry.deserialize( stream );
    }

    uint32_t numSplitTimes = stream.readULong();
    if ( numSplitTimes > kSplitTimesPoolSize - 1u ) {
        numSplitTimes = kSplitTimesPoolSize;
    }

    for ( uint32_t i = 0u; i < numSplitTimes; i++ ) {
        SplitTimesPool[i] = stream.readULong();
    }
}

RankingEntry *SavedRanking::findEntry( const uint64_t hashcode ) {
    // FUN_0088edb0
    for ( RankingEntry &entry : Entries ) {
        if ( entry.getHashcode() == hashcode ) {
            return &entry;
        }
    }

    return nullptr;
}

const RankingEntry *SavedRanking::findEntry( const uint64_t hashcode ) const {
    // FUN_0088edb0
    return const_cast<SavedRanking *>( this )->findEntry( hashcode );
}

int32_t SavedRanking::getChallengeResult( const uint64_t hashcode ) const {
    // FUN_00890380
    const RankingEntry *pEntry = findEntry( hashcode );
    if ( pEntry == nullptr ) {
        return -1;
    }

    return int32_t( TestDriveTEAReadDword( pEntry->pBlock0 ) );
}

float SavedRanking::getChallengeBestScore( const uint64_t hashcode ) const {
    // FUN_008902d0
    const RankingEntry *pEntry = findEntry( hashcode );
    if ( pEntry == nullptr ) {
        return 4294967296.0f; // FLOAT_00f41670
    }

    uint32_t pPlainText[2] = {};
    TestDriveTEADecrypt( pPlainText, pEntry->pBlock0 );

    float bestScore = 0.0f;
    memcpy( &bestScore, &pPlainText[1], sizeof( float ) );
    return bestScore;
}

float SavedRanking::getBestTime( const uint64_t hashcode ) const {
    // FUN_00890230
    const RankingEntry *pEntry = findEntry( hashcode );
    if ( pEntry == nullptr ) {
        return 0.0f;
    }

    return pEntry->getBestScore();
}

float SavedRanking::getBestSpeed( const uint64_t hashcode ) const {
    // FUN_008901e0
    const RankingEntry *pEntry = findEntry( hashcode );
    if ( pEntry == nullptr ) {
        return 0.0f;
    }

    return pEntry->getBestScore();
}

bool SavedRanking::getSplitTimes( const uint64_t hashcode,
                                  const uint32_t maxNumSplitTimes,
                                  uint32_t *pOutSplitTimes ) const {
    // FUN_00890040
    const RankingEntry *pEntry = findEntry( hashcode );
    if ( pEntry == nullptr || pEntry->getSplitTimesIndex() == 0xffffffffu ||
         pEntry->getNumSplitTimes() == 0u ) {
        return false;
    }

    const uint32_t *pSplitTimes = &SplitTimesPool[pEntry->getSplitTimesIndex()];
    const uint32_t numSplitTimes =
        std::min( pEntry->getNumSplitTimes(), maxNumSplitTimes );
    for ( uint32_t i = 0u; i < numSplitTimes; i++ ) {
        pOutSplitTimes[i] = pSplitTimes[i];
    }

    return true;
}

void SavedRanking::submitTimeResult( const uint64_t hashcode,
                                     const int32_t result,
                                     const float time ) {
    // FUN_00892330
    const RankingEntry *pEntry = findEntry( hashcode );
    if ( pEntry == nullptr ) {
        setEntryResult( hashcode, result, time );
        return;
    }

    const float previousTime = pEntry->getBestScore();
    const float bestTime = ( previousTime > time ) ? time : previousTime;
    const int32_t bestResult =
        GetBestChallengeResult( result, pEntry->getResult() );
    setEntryResult( hashcode, bestResult, bestTime );
}

void SavedRanking::submitScoreResult( const uint64_t hashcode,
                                      const int32_t result,
                                      const float score ) {
    // FUN_00892230
    const RankingEntry *pEntry = findEntry( hashcode );
    if ( pEntry == nullptr ) {
        setEntryResult( hashcode, result, score );
        return;
    }

    float bestScore = score;
    if ( pEntry->getBestScore() == pEntry->getBestScore() ) {
        const float previousScore = pEntry->getBestScore();
        bestScore = ( score > previousScore ) ? score : previousScore;
    }

    const int32_t bestResult =
        GetBestChallengeResult( result, pEntry->getResult() );
    setEntryResult( hashcode, bestResult, bestScore );
}

void SavedRanking::storeSplitTimes( const uint64_t hashcode,
                                    const uint32_t numSplitTimes,
                                    const uint32_t *pSplitTimes ) {
    // FUN_00892110
    RankingEntry *pEntry = findEntry( hashcode );
    if ( pEntry == nullptr ) {
        pEntry = allocateEntry();
    }

    // NOTE: Retail dereferences the entry without checking if the allocation
    // succeeded.
    if ( pEntry == nullptr ) {
        return;
    }

    pEntry->setHashcode( hashcode );

    if ( pEntry->getSplitTimesIndex() != 0xffffffffu &&
         numSplitTimes <= pEntry->getNumSplitTimes() ) {
        memcpy( &SplitTimesPool[pEntry->getSplitTimesIndex()], pSplitTimes,
                numSplitTimes * sizeof( uint32_t ) );
        pEntry->setSplitTimes( numSplitTimes, pEntry->getSplitTimesIndex() );
        return;
    }

    if ( getNumUsedSplitTimes() + numSplitTimes >= kSplitTimesPoolSize ) {
        return;
    }

    const uint32_t splitTimesIndex = getNumUsedSplitTimes();
    memcpy( &SplitTimesPool[splitTimesIndex], pSplitTimes,
            numSplitTimes * sizeof( uint32_t ) );
    pEntry->setSplitTimes( numSplitTimes, splitTimesIndex );
    setNumUsedSplitTimes( getNumUsedSplitTimes() + numSplitTimes );
}

RankingEntry *SavedRanking::allocateEntry() {
    // FUN_008903d0
    for ( RankingEntry &entry : Entries ) {
        if ( entry.getHashcode() == kUnusedEntryHashcode ) {
            entry.clear();
            return &entry;
        }
    }

    return nullptr;
}

void SavedRanking::setEntryResult( const uint64_t hashcode,
                                   const int32_t result,
                                   const float bestScore ) {
    // FUN_008920d0
    RankingEntry *pEntry = findEntry( hashcode );
    if ( pEntry == nullptr ) {
        pEntry = allocateEntry();
        if ( pEntry == nullptr ) {
            return;
        }
    }

    // FUN_0088efd0
    pEntry->setHashcode( hashcode );
    pEntry->setResultAndBestScore( result, bestScore );
}

uint32_t SavedRanking::getNumUsedSplitTimes() {
    // FUN_0088ed60
    if ( NumUsedSplitTimes == 0xffffffffu ) {
        NumUsedSplitTimes = 0u;

        const char *pValue =
            gpPlayerData->getLoadedSave()->getStringTable()->findValue(
                kNumUsedSplitTimesKey );
        if ( pValue != nullptr ) {
            NumUsedSplitTimes = uint32_t( atoi( pValue ) );
        }
    }

    return NumUsedSplitTimes;
}

void SavedRanking::setNumUsedSplitTimes( const uint32_t numUsedSplitTimes ) {
    // FUN_008900d0
    char pValue[8];
    snprintf( pValue, sizeof( pValue ), "%d", int32_t( numUsedSplitTimes ) );
    gpPlayerData->getLoadedSave()->getStringTable()->setValue(
        kNumUsedSplitTimesKey, pValue );

    NumUsedSplitTimes = numUsedSplitTimes;
}

void SavedRanking::upgradeFromVersion1( SavedStringTable *pStringTable ) {
    // FUN_00890120
    for ( RankingEntry &entry : Entries ) {
        if ( entry.getHashcode() != kUnusedEntryHashcode ) {
            // FUN_0088eec0
            TestDriveTEAWriteQword( entry.pBlock1, 0xffffffff00000000ull );
        }
    }

    pStringTable->setValue( kNumUsedSplitTimesKey, "0" );
    NumUsedSplitTimes = 0u;
}
