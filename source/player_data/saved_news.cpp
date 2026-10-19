#include "shared.h"
#include "saved_news.h"

#include "core/ed_store.h"
#include "flash/mng_flash_localize.h"

uint16_t
PackSaveDate( const uint32_t year, const uint32_t month, const uint32_t day ) {
    // FUN_0089a730 (inlined)
    return uint16_t( 31u * ( month + 12u * ( year % 100u ) ) + day );
}

void UnpackSaveDate( const uint16_t packedDate,
                     int32_t &outYear,
                     int32_t &outMonth,
                     int32_t &outDay ) {
    // FUN_007190a0 (inlined)
    outYear = 0;
    outMonth = 0;
    outDay = 0;
    if ( packedDate == 0u ) {
        return;
    }

    const int32_t date = int32_t( packedDate );
    outDay = ( date % 31 != 0 ) ? date % 31 : 31;
    const int32_t month = ( ( date - outDay ) / 31 ) % 12;
    outMonth = ( month != 0 ) ? month : 12;
    outYear = ( date - outMonth * 31 - outDay ) / 372;
}

void FormatSaveDate( char *pDest,
                     const size_t destLength,
                     const int32_t year,
                     const int32_t month,
                     const int32_t day ) {
    // FUN_00718fd0
    if ( gpMngFlashLocalize == nullptr ) {
        strncpy( pDest, "(ERROR-DATE)", destLength );
        pDest[destLength - 1] = '\0';
        return;
    }

    char yearString[8];
    char monthString[8];
    char dayString[8];
    snprintf( yearString, sizeof( yearString ), "%02d", year );
    snprintf( monthString, sizeof( monthString ), "%02d", month );
    snprintf( dayString, sizeof( dayString ), "%02d", day );

    gpMngFlashLocalize->setSysVar( "YEAR", yearString );
    gpMngFlashLocalize->setSysVar( "MONTH", monthString );
    gpMngFlashLocalize->setSysVar( "DAY", dayString );
    gpMngFlashLocalize->makeSentenceFromHashcode( pDest, destLength,
                                                  0x39439f2ull );
    gpMngFlashLocalize->clearAllSysVar();
}

void NewsEntry::clear() {
    // FUN_00895bc0 (inlined)
    ID = 0u;
    Kind = 0u;
    ExpiryDate = 0u;
    Param0 = 0u;
    Param1 = 0u;
}

void NewsEntry::deserialize( edStore &stream ) {
    // FUN_0088f5b0
    ID = stream.readULong();
    Kind = stream.readByte();
    ExpiryDate = stream.readUShort();
    Param0 = stream.readULong();
    Param1 = stream.readULong();
}

void NewsEntry::serialize( edStore &stream ) const {
    // FUN_00890560 (inlined)
    stream.writeInteger( int32_t( ID ) );
    stream.writeByte( Kind );
    stream.writeUShort( ExpiryDate );
    stream.writeInteger( int32_t( Param0 ) );
    stream.writeInteger( int32_t( Param1 ) );
}

SavedNewsList::SavedNewsList() : Notifiable() {
    // FUN_00416050
    clear();
}

SavedNewsList::~SavedNewsList() {}

void SavedNewsList::clear() {
    // FUN_00895bc0
    for ( NewsEntry &entry : News ) {
        entry.clear();
    }

    for ( NewsEntry &entry : TeamInvites ) {
        entry.clear();
    }

    NumNews = 0u;
    NumTeamInvites = 0u;
}

void SavedNewsList::clampCountersAndWipeTail() {
    // FUN_00890940 (inlined) / FUN_00890560 (inlined)
    if ( NumNews > uint32_t( kNumNews ) ) {
        NumNews = 0u;
    }

    if ( NumTeamInvites > uint32_t( kNumTeamInvites ) ) {
        NumTeamInvites = 0u;
    }

    uint32_t numUsed = 0u;
    for ( NewsEntry &entry : News ) {
        if ( entry.Kind == 0u ) {
            continue;
        }

        numUsed++;
        if ( numUsed > NumNews ) {
            entry.clear();
        }
    }

    numUsed = 0u;
    for ( NewsEntry &entry : TeamInvites ) {
        if ( entry.Kind == 0u ) {
            continue;
        }

        numUsed++;
        if ( numUsed > NumTeamInvites ) {
            entry.clear();
        }
    }
}

void SavedNewsList::deserialize( edStore &stream ) {
    // FUN_00890940
    NumNews = stream.readULong();
    NumTeamInvites = stream.readULong();

    for ( NewsEntry &entry : News ) {
        entry.deserialize( stream );
    }

    for ( NewsEntry &entry : TeamInvites ) {
        entry.deserialize( stream );
    }

    clampCountersAndWipeTail();

    notify( 0u );
}

void SavedNewsList::serialize( edStore &stream ) {
    // FUN_00890560
    clampCountersAndWipeTail();

    stream.writeInteger( int32_t( NumNews ) );
    stream.writeInteger( int32_t( NumTeamInvites ) );

    for ( const NewsEntry &entry : News ) {
        entry.serialize( stream );
    }

    for ( const NewsEntry &entry : TeamInvites ) {
        entry.serialize( stream );
    }
}

void SavedNewsList::removeExpiredEntries( const uint16_t currentDate ) {
    // FUN_0088f4e0
    for ( int32_t i = 0; i < kNumNews; ) {
        if ( News[i].Kind != 0u && News[i].ExpiryDate <= currentDate ) {
            // FUN_0088f3d0
            for ( int32_t j = i; j < kNumNews - 1; j++ ) {
                News[j] = News[j + 1];
            }

            if ( NumNews > 0u ) {
                NumNews--;
            }

            News[( NumNews < uint32_t( kNumNews ) ) ? NumNews : 0u].clear();
            notify( 0u );
            continue;
        }

        i++;
    }

    for ( int32_t i = 0; i < kNumTeamInvites; ) {
        if ( TeamInvites[i].Kind != 0u &&
             TeamInvites[i].ExpiryDate <= currentDate ) {
            // FUN_0088f220
            for ( int32_t j = i; j < kNumTeamInvites - 1; j++ ) {
                TeamInvites[j] = TeamInvites[j + 1];
            }

            if ( NumTeamInvites > 0u ) {
                NumTeamInvites--;
            }

            TeamInvites[( NumTeamInvites < uint32_t( kNumTeamInvites ) )
                            ? NumTeamInvites
                            : 0u]
                .clear();
            notify( 0u );
            continue;
        }

        i++;
    }
}
