#include "shared.h"
#include "saved_mailbox.h"

#include "core/ed_store.h"
#include "core/mutex.h"
#include "game/scenaric/gs_scenaric.h"
#include "gs_playerdata.h"

uint32_t GetMailLanguageCode( const char *pLanguage ) {
    return uint32_t( int32_t( pLanguage[0] ) << 8 | int32_t( pLanguage[1] ) );
}

void MailMessage::clear() {
    // FUN_0088e780
    Kind = 0;
    Category = 0;
    Date = 0u;
    Title[0] = '\0';

    memset( Body, 0, sizeof( Body ) );
    memset( Sender, 0, sizeof( Sender ) );

    Language = 0u;
    Index = -1;
    Hashcode = 0ull;

    Param0 = 0u;
    Param1 = 0u;
    Param2 = 0u;

    memset( Blob0, 0, sizeof( Blob0 ) );
    memset( Blob1, 0, sizeof( Blob1 ) );

    bRead = false;
    bFlagged = false;
}

void MailMessage::copyFrom( const MailMessage &other ) {
    // FUN_00895800 / FUN_008956c0
    clear();

    Language = other.Language;
    if ( Language == 0xffffffffu ) {
        Language = GetMailLanguageCode( gpPlayerData->getLanguage() );
    }

    Kind = other.Kind;
    Category = other.Category;
    Date = other.Date;

    strncpy( Title, other.Title, kTitleLength );
    Title[kTitleLength - 1u] = '\0';

    strncpy( Body, other.Body, kBodyLength );
    Body[0x76f] = '\0';

    memcpy( Sender, other.Sender, kSenderLength );
    Index = other.Index;

    Hashcode = other.Hashcode;

    strcpy( (char *)Blob1, (const char *)other.Blob1 );
    strcpy( (char *)Blob0, (const char *)other.Blob0 );

    Param0 = other.Param0;
    Param1 = other.Param1;
    Param2 = other.Param2;

    bRead = other.bRead;
    bFlagged = other.bFlagged;
}

bool MailMessage::isVisible() const {
    // FUN_00895880
    const uint32_t currentLanguage =
        GetMailLanguageCode( gpPlayerData->getLanguage() );

    switch ( Language ) {
    case 0x6765: // ge
    case 0x6672: // fr
    case 0x6974: // it
    case 0x7370: // sp
    case 0x7573: // us
        return true;
    case 0x6368: // ch
    case 0x6a61: // ja
    case 0x6b6f: // ko
        return Language == currentLanguage;
    default:
        return false;
    }
}

void MailMessage::deserialize( edStore &stream ) {
    // FUN_0088f730
    Kind = stream.readByte();
    Category = stream.readByte();

    stream.readBufferNoAlloc( Title, kTitleLength );
    Title[kTitleLength - 1u] = '\0';

    stream.readBufferNoAlloc( Body, kBodyLength );
    Body[kBodyLength - 1u] = '\0';

    stream.readBufferNoAlloc( Sender, kSenderLength );
    Sender[kSenderLength - 1u] = '\0';

    Index = stream.readInteger();
    Hashcode = stream.readULongLong();
    Date = stream.readUShort();

    Param0 = stream.readULong();
    Param1 = stream.readULong();
    Param2 = stream.readULong();

    stream.readBufferNoAlloc( Blob0, sizeof( Blob0 ) );
    stream.readBufferNoAlloc( Blob1, sizeof( Blob1 ) );

    bRead = stream.readByte() == 1u;
    bFlagged = stream.readByte() == 1u;

    Language = stream.readULong();

    stream.readInteger();
}

void MailMessage::serialize( edStore &stream ) const {
    // FUN_0088f620
    stream.writeByte( uint8_t( Kind ) );
    stream.writeByte( uint8_t( Category ) );

    stream.writeBuffer( Title, kTitleLength );
    stream.writeBuffer( Body, kBodyLength );
    stream.writeBuffer( Sender, kSenderLength );

    stream.writeInteger( Index );
    stream.writeULongLong( Hashcode );
    stream.writeUShort( Date );

    stream.writeInteger( int32_t( Param0 ) );
    stream.writeInteger( int32_t( Param1 ) );
    stream.writeInteger( int32_t( Param2 ) );

    stream.writeBuffer( Blob0, sizeof( Blob0 ) );
    stream.writeBuffer( Blob1, sizeof( Blob1 ) );

    stream.writeByte( bRead ? 1u : 0u );
    stream.writeByte( bFlagged ? 1u : 0u );

    stream.writeInteger( int32_t( Language ) );

    // A constant (EOF?)
    stream.writeInteger( 0xc103 );
}

SavedMailbox::SavedMailbox() : Notifiable(), Messages() {
    // FUN_004160c0
    clear();
}

SavedMailbox::~SavedMailbox() {}

void SavedMailbox::clear() {
    // FUN_004160c0
    for ( MailMessage &message : Messages ) {
        message.clear();
    }
}

void SavedMailbox::serialize( edStore &stream ) const {
    // FUN_00890d50
    for ( const MailMessage &message : Messages ) {
        message.serialize( stream );
    }
}

void SavedMailbox::deserialize( edStore &stream ) {
    // FUN_00890d90
    for ( MailMessage &message : Messages ) {
        message.deserialize( stream );
    }

    notify( 0u );
}

int32_t SavedMailbox::getNumVisibleMessages() const {
    // FUN_00895920
    int32_t numMessages = 0;
    for ( const MailMessage &message : Messages ) {
        if ( message.Category == 0 || !message.isVisible() ) {
            break;
        }
        numMessages++;
    }

    return numMessages;
}

int32_t SavedMailbox::getFreeSlotIndex( int32_t numToSkip ) const {
    // FUN_00895a00
    for ( int32_t i = 0; i < kNumMessages; i++ ) {
        const MailMessage &message = Messages[i];
        if ( message.Category == 0 || message.isVisible() ) {
            if ( numToSkip == 0 ) {
                return i;
            }
            numToSkip--;
        }
    }

    return 0;
}

void SavedMailbox::post( const MailMessage &message ) {
    // FUN_00895af0
    {
        ScopedMutexLock lock( gpPlayerData->getMutex() );

        MailMessage *pSlot = nullptr;
        const int32_t numMessages = getNumVisibleMessages();
        if ( numMessages < kNumMessages ) {
            int32_t slotIndex = getFreeSlotIndex( numMessages );
            if ( slotIndex < 0 || slotIndex >= kNumMessages ) {
                slotIndex = 0;
            }
            pSlot = &Messages[slotIndex];
        } else {
            for ( int32_t i = 0; i < kNumMessages - 1; i++ ) {
                Messages[i] = Messages[i + 1];
            }
            pSlot = &Messages[kNumMessages - 1];
        }

        pSlot->copyFrom( message );
    }

    gpScenaric->setMailboxUpdated( true );

    notify( 0u );
}
