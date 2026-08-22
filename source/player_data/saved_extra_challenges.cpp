#include "shared.h"
#include "saved_extra_challenges.h"

#include "core/crypto/tea.h"
#include "core/ed_store.h"

SavedExtraChallenges::SavedExtraChallenges() {
    // FUN_00889260
    clear();

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (the original also builds the
                             // TEA workspace this block is addressed through,
                             // and seeds the level from the achievements)
}

SavedExtraChallenges::~SavedExtraChallenges() {}

void SavedExtraChallenges::clear() {
    memset( Counters, 0, sizeof( Counters ) );
    memset( Scores, 0, sizeof( Scores ) );

    Field_0xf188 = 0.0f;
    Field_0xf18c = 0;
    Field_0xf190 = 0.0f;
    Field_0xf194 = 0;
    Field_0xf198 = 0;
    Field_0xf19c = 0;
    Field_0xf1a0 = 0.0f;
    Field_0xf1a4 = 0.0f;

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
