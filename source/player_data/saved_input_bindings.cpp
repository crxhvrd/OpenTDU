#include "shared.h"
#include "saved_input_bindings.h"

#include "core/ed_store.h"
#include "input/ed_dev.h"
#include "input/input_device.h"

static constexpr uint32_t kInputBindingsMagic = 0x4b4d4150; // 'KMAP'

static void ReadDeviceGUID( edStore &stream, SavedInputBinding &outBinding ) {
    // FUN_00896780
    outBinding.DeviceGUIDData1 = stream.readULong();
    outBinding.DeviceGUIDData2 = stream.readUShort();
    outBinding.DeviceGUIDData3 = stream.readUShort();

    for ( uint8_t &guidByte : outBinding.DeviceGUIDData4 ) {
        guidByte = stream.readByte();
    }
}

static void WriteDeviceGUID( edStore &stream,
                             const SavedInputBinding &binding ) {
    // FUN_008967c0
    stream.writeInteger( int32_t( binding.DeviceGUIDData1 ) );
    stream.writeUShort( binding.DeviceGUIDData2 );
    stream.writeUShort( binding.DeviceGUIDData3 );

    for ( const uint8_t guidByte : binding.DeviceGUIDData4 ) {
        stream.writeByte( guidByte );
    }
}

void SavedInputBinding::clear() {
    // FUN_008962f0
    memset( ActionName, 0, sizeof( ActionName ) );
    memset( ChannelName, 0, sizeof( ChannelName ) );
    memset( DisplayName, 0, sizeof( DisplayName ) );

    DeviceGUIDData1 = 0u;
    DeviceGUIDData2 = 0u;
    DeviceGUIDData3 = 0u;
    memset( DeviceGUIDData4, 0, sizeof( DeviceGUIDData4 ) );

    ChannelIndex = 0u;
    ChannelKind = 0u;
    DeviceIndex = 0u;
    bFlag0 = false;
    bFlag1 = false;
}

void SavedInputBinding::deserialize( edStore &stream ) {
    // FUN_00896cc0
    stream.readStringNoAlloc( ActionName, kActionNameLength - 1 );
    stream.readStringNoAlloc( ChannelName, kChannelNameLength - 1 );
    stream.readStringNoAlloc( DisplayName, kDisplayNameLength - 1 );

    ChannelIndex = stream.readByte();
    ChannelKind = stream.readByte();
    DeviceIndex = stream.readByte();

    ReadDeviceGUID( stream, *this );

    bFlag0 = stream.readByte() == 1u;
    bFlag1 = stream.readByte() == 1u;

    OTDU_UNIMPLEMENTED_ONCE; // TODO: INPUT (DirectInput GUID -> SDL device)
}

void SavedInputBinding::serialize( edStore &stream ) {
    // FUN_00896820
    stream.writeString( ActionName );
    stream.writeString( ChannelName );
    stream.writeString( DisplayName );

    stream.writeByte( ChannelIndex );
    stream.writeByte( ChannelKind );
    stream.writeByte( DeviceIndex );

    WriteDeviceGUID( stream, *this );

    stream.writeByte( bFlag0 ? 1u : 0u );
    stream.writeByte( bFlag1 ? 1u : 0u );
}

void SavedInputBindings::clear() {
    // FUN_008966f0
    NumEntries = 0;
    memset( NumEntriesPerCategory, 0, sizeof( NumEntriesPerCategory ) );

    for ( SavedInputBinding &binding : Entries ) {
        binding.clear();
    }
}

void SavedInputBindings::deserialize( edStore &stream ) {
    // FUN_008970a0
    if ( stream.readUInteger() != kInputBindingsMagic ) {
        return;
    }

    clear();

    const int32_t numBindings = stream.readInteger();
    NumEntries = 0;

    for ( int32_t bindingIdx = 0; bindingIdx < numBindings; bindingIdx++ ) {
        if ( bindingIdx >= kMaxBindings ) {
            // The table is full
            SavedInputBinding discardedBinding;
            discardedBinding.deserialize( stream );
            continue;
        }

        SavedInputBinding &binding = Entries[NumEntries];
        binding.deserialize( stream );

        // A binding naming an action the build does not expose is dropped.
        if ( IsRemappableAction( binding.ActionName ) ) {
            NumEntries++;
        } else {
            binding.clear();
        }
    }

    // Every action the save did not carry keeps its DevicesPC.ini default.
    const int32_t numLiveMappings = GetNumPCOptionMapping();
    for ( int32_t mappingIdx = 0; mappingIdx < numLiveMappings; mappingIdx++ ) {
        const PCOptionMapping *pLiveMapping = GetPCOptionMapping( mappingIdx );
        if ( find( pLiveMapping->pActionName ) == nullptr ) {
            addFromLiveMapping( *pLiveMapping );
        }
    }

    updateCategoryCounts();
}

void SavedInputBindings::serialize( edStore &stream ) {
    // FUN_00896c70
    stream.writeInteger( int32_t( kInputBindingsMagic ) );
    stream.writeInteger( NumEntries );

    for ( int32_t i = 0; i < NumEntries; i++ ) {
        Entries[i].serialize( stream );
    }
}

SavedInputBinding *SavedInputBindings::find( const char *pActionName ) {
    // FUN_00895e80
    if ( pActionName == nullptr || pActionName[0] == '\0' ) {
        return nullptr;
    }

    for ( int32_t i = 0; i < NumEntries; i++ ) {
        if ( StringNCmpI( pActionName, Entries[i].ActionName,
                          SavedInputBinding::kActionNameLength ) == 0 ) {
            return &Entries[i];
        }
    }

    return nullptr;
}

void SavedInputBindings::addFromLiveMapping(
    const PCOptionMapping &liveMapping ) {
    // FUN_00896e70
    if ( NumEntries >= kMaxBindings || liveMapping.pActionName[0] == '\0' ) {
        return;
    }

    SavedInputBinding &binding = Entries[NumEntries];
    NumEntries++;

    strncpy( binding.ActionName, liveMapping.pActionName,
             SavedInputBinding::kActionNameLength );
    binding.ActionName[SavedInputBinding::kActionNameLength - 1] = '\0';

    const int32_t deviceID = gEdDev.getDeviceID( liveMapping.pCurrentDevice,
                                                 liveMapping.DeviceIndex );
    if ( deviceID < 0 ) {
        return;
    }

    Device *pDevice = gEdDev.ppDevices[deviceID];
    if ( pDevice == nullptr ) {
        return;
    }

    strncpy( binding.ChannelName, liveMapping.pCurrentChannel,
             SavedInputBinding::kChannelNameLength );
    binding.ChannelName[SavedInputBinding::kChannelNameLength - 1] = '\0';

    binding.DeviceIndex = liveMapping.DeviceIndex;
    binding.bFlag0 = liveMapping.Unknown1;
    binding.bFlag1 = liveMapping.Unknown2;

    OTDU_UNIMPLEMENTED_ONCE; // TODO: INPUT (DirectInput GUID -> SDL device)
}

void SavedInputBindings::updateCategoryCounts() {
    // FUN_00895fe0
    memset( NumEntriesPerCategory, 0, sizeof( NumEntriesPerCategory ) );

    for ( int32_t i = 0; i < NumEntries; i++ ) {
        const PCOptionMapping *pLiveMapping =
            FindPCOptionMapping( Entries[i].ActionName );
        if ( pLiveMapping == nullptr ) {
            continue;
        }

        const uint8_t category = uint8_t( pLiveMapping->Category );
        if ( category != 0u && category <= uint8_t( kNumCategories ) ) {
            NumEntriesPerCategory[category - 1]++;
        }
    }
}
