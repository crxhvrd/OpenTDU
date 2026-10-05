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

SavedStringTable::Entry *SavedStringTable::findEntry( const char *pKey ) {
    // FUN_0088e870
    for ( Entry &entry : Entries ) {
        if ( entry.Key[0] != '\0' &&
             StringNCmpI( entry.Key, pKey, kEntryLength ) == 0 ) {
            return &entry;
        }
    }

    return nullptr;
}

const char *SavedStringTable::findValue( const char *pKey ) const {
    const Entry *pEntry = const_cast<SavedStringTable *>( this )->findEntry( pKey );
    return ( pEntry != nullptr ) ? pEntry->Value : nullptr;
}

bool SavedStringTable::setValue( const char *pKey, const char *pValue ) {
    // FUN_0088f8d0
    Entry *pEntry = findEntry( pKey );
    if ( pEntry == nullptr ) {
        for ( Entry &entry : Entries ) {
            if ( entry.Key[0] == '\0' ) {
                pEntry = &entry;
                break;
            }
        }

        if ( pEntry == nullptr ) {
            return false;
        }

        memset( pEntry->Key, 0, sizeof( pEntry->Key ) );
        strncpy( pEntry->Key, pKey, kEntryLength );
    }

    memset( pEntry->Value, 0, sizeof( pEntry->Value ) );
    strncpy( pEntry->Value, pValue, kEntryLength );
    return true;
}
