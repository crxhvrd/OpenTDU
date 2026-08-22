#include "shared.h"
#include "saved_mailbox.h"

#include "core/ed_store.h"

void MailMessage::clear() {
    // FUN_0088e780
    Kind = 0;
    Category = 0;
    Date = 0u;
    Title[0] = '\0';

    memset( Body, 0, sizeof( Body ) );
    memset( Sender, 0, sizeof( Sender ) );

    ReceivedDate = 0u;
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

    ReceivedDate = stream.readULong();

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

    stream.writeInteger( int32_t( ReceivedDate ) );

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
