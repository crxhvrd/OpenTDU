#include "shared.h"
#include "saved_avatar.h"

#include "core/ed_store.h"

SavedAvatar::SavedAvatar() : Config() {
    // FUN_008ff150 + FUN_008904c0
    clear();
}

SavedAvatar::~SavedAvatar() {}

void SavedAvatar::clear() {
    Config = AvatarConfiguration();

    memset( AvatarData, 0, sizeof( AvatarData ) );
    AvatarDataSize = 0u;
}

void SavedAvatar::deserialize( edStore &stream ) {
    // FUN_008925c0
    uint8_t configBuffer[0x400] = {};

    edStore configStream;
    configStream.bindExternalBuffer( configBuffer, sizeof( configBuffer ) );

    stream.popObjectNoAlloc( configStream );
    Config.deserializePacked( configStream );

    edStore avatarStream;
    avatarStream.bindExternalBuffer( AvatarData, kAvatarDataSize );

    stream.popObjectNoAlloc( avatarStream );
    AvatarDataSize = avatarStream.getWrittenSize();
}

void SavedAvatar::serialize( edStore &stream ) {
    // FUN_00892530
    {
        uint8_t configBuffer[0x80] = {};

        edStore configStream;
        configStream.bindExternalBuffer( configBuffer, sizeof( configBuffer ) );

        Config.serializePacked( configStream );
        stream.writeObject( configStream );
    }

    edStore avatarStream;
    avatarStream.bindExternalBuffer( AvatarData, kAvatarDataSize );
    avatarStream.clear();

    avatarStream.setWrittenSize( kAvatarDataSize );
    avatarStream.updateCRC( CRC );
    avatarStream.setWrittenSize( kAvatarDataSize );
    avatarStream.seekToEnd();

    stream.writeObject( avatarStream );
}
