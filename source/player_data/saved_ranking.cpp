#include "shared.h"
#include "saved_ranking.h"

#include "core/crypto/tea.h"
#include "core/ed_store.h"
#include "saved_string_table.h"

void RankingEntry::clear() {
    // FUN_00415ea0 
    Hashcode = 0ull;

    TestDriveTEAWriteQword( pBlock0, 0ull );
    TestDriveTEAWriteQword( pBlock1, 0ull );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (the original seeds the two
                             // blocks with (-1, -1, 0, -1) and (9, -1); what
                             // those fields mean is not mapped yet)
}

uint64_t RankingEntry::getHashcode() const {
    return TestDriveTEAReadQword( (const uint32_t *)&Hashcode );
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

SavedRanking::SavedRanking() : Notifiable(), Entries(), PlayerIDs() {
    // FUN_00415ea0
    clear();
}

SavedRanking::~SavedRanking() {}

void SavedRanking::clear() {
    // FUN_00415ea0
    for ( RankingEntry &entry : Entries ) {
        entry.clear();
    }

    NumPlayerIDs = 0u;
    memset( PlayerIDs, 0, sizeof( PlayerIDs ) );
}

void SavedRanking::serialize( edStore &stream ) const {
    // FUN_0088ff20
    for ( const RankingEntry &entry : Entries ) {
        entry.serialize( stream );
    }

    stream.writeInteger( int32_t( NumPlayerIDs ) );
    for ( uint32_t i = 0u; i < NumPlayerIDs; i++ ) {
        stream.writeInteger( int32_t( PlayerIDs[i] ) );
    }
}

void SavedRanking::deserialize( edStore &stream ) {
    // FUN_0088ffb0
    for ( RankingEntry &entry : Entries ) {
        entry.deserialize( stream );
    }

    uint32_t numPlayerIDs = stream.readULong();
    if ( numPlayerIDs > kMaxPlayerIDs - 1u ) {
        numPlayerIDs = kMaxPlayerIDs;
    }

    for ( uint32_t i = 0u; i < numPlayerIDs; i++ ) {
        PlayerIDs[i] = stream.readULong();
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

void SavedRanking::upgradeFromVersion1( SavedStringTable *pStringTable ) {
    // FUN_00890120
    static constexpr uint64_t kUnusedEntryHashcode = 0xffffffffull;

    for ( RankingEntry &entry : Entries ) {
        if ( entry.getHashcode() != kUnusedEntryHashcode ) {
            // FUN_0088eec0
            TestDriveTEAWriteQword( entry.pBlock1, 0xffffffff00000000ull );
        }
    }

    pStringTable->setValue( "RnkChkC", "0" );
    RankCheckCounter = 0u;
}
