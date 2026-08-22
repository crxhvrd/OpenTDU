#include "shared.h"
#include "saved_item_table.h"

#include "core/ed_store.h"

SavedItemTable::SavedItemTable() : Notifiable(), Kinds(), Values() {
    // FUN_0088e600
    clear();
}

SavedItemTable::~SavedItemTable() {}

void SavedItemTable::clear() {
    // FUN_0088e600
    for ( int32_t i = 0; i < kNumEntries; i++ ) {
        Kinds[i] = kDefaultKind;
        Values[i] = 0u;
    }

    Count = 0u;
}

void SavedItemTable::serialize( edStore &stream ) const {
    // FUN_0088f140
    for ( int32_t i = 0; i < kNumEntries; i++ ) {
        stream.writeInteger( int32_t( Kinds[i] ) );
        stream.writeInteger( int32_t( Values[i] ) );
    }

    stream.writeInteger( int32_t( Count ) );
}

void SavedItemTable::deserialize( edStore &stream ) {
    // FUN_0088f190
    for ( int32_t i = 0; i < kNumEntries; i++ ) {
        Kinds[i] = uint16_t( stream.readULong() );
        Values[i] = stream.readULong();
    }

    Count = stream.readUInteger();

    notify( 0u );
}
