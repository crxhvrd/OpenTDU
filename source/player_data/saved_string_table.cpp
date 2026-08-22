#include "shared.h"
#include "saved_string_table.h"

#include "core/ed_store.h"

SavedStringTable::SavedStringTable() : Entries() {
    // FUN_00415f10
    clear();
}

SavedStringTable::~SavedStringTable() {}

void SavedStringTable::clear() {
    // FUN_00415f10
    for ( Entry &entry : Entries ) {
        memset( entry.Key, 0, sizeof( entry.Key ) );
        memset( entry.Value, 0, sizeof( entry.Value ) );
    }
}

void SavedStringTable::deserialize( edStore &stream ) {
    // FUN_0088f970
    for ( Entry &entry : Entries ) {
        memset( entry.Key, 0, sizeof( entry.Key ) );
        memset( entry.Value, 0, sizeof( entry.Value ) );

        stream.readStringNoAlloc( entry.Key, kEntryLength );
        stream.readStringNoAlloc( entry.Value, kEntryLength );
    }
}

void SavedStringTable::serialize( edStore &stream ) const {
    // FUN_0088e900
    for ( const Entry &entry : Entries ) {
        stream.writeString( entry.Key );
        stream.writeString( entry.Value );
    }
}

const char *SavedStringTable::findValue( const char *pKey ) const {
    for ( const Entry &entry : Entries ) {
        if ( entry.Key[0] != '\0' && strcmp( entry.Key, pKey ) == 0 ) {
            return entry.Value;
        }
    }

    return nullptr;
}
