#include "shared.h"
#include "saved_input_bindings.h"

#include "config/gs_config.h"
#include "core/ed_store.h"
#include "database/gs_database.h"
#include "input/ed_dev.h"
#include "input/input_templates.h"

static constexpr uint32_t kInputBindingsMagic = 0x4b4d4150; // 'KMAP'

struct StickChannelLabel {
    const char *pChannelName;
    uint64_t LabelHashcode;
};

static constexpr StickChannelLabel kStickChannelLabels[] = {
    // DAT_00f81708
    {"STICK_0_LEFT",     0x36cdfbaull},
    {"STICK_0_RIGHT",    0x37c21faull},
    {"STICK_0_UP",       0x38b643aull},
    {"STICK_0_DOWN",     0x39aa67aull},
    {"STICK_0_FORWARD",  0x3a9e8baull},
    {"STICK_0_BACKWARD", 0x3b92afaull},
    {"STICK_1_LEFT",     0x3315d5aull},
    {"STICK_1_RIGHT",    0x3409f9aull},
    {"STICK_1_UP",       0x34fe1daull},
    {"STICK_1_DOWN",     0x35f241aull},
    {"STICK_1_FORWARD",  0x36e665aull},
    {"STICK_1_BACKWARD", 0x37da89aull},
    {"POV_0_LEFT",       0x38ceadaull},
    {"POV_0_RIGHT",      0x39c2d1aull},
    {"POV_0_UP",         0x3ab6f5aull},
    {"POV_0_DOWN",       0x3bab19aull},
};

static void ReadDeviceGUID( edStore &stream, edGUID &outGUID ) {
    // FUN_00896780
    outGUID.Data1 = stream.readULong();
    outGUID.Data2 = stream.readUShort();
    outGUID.Data3 = stream.readUShort();

    for ( uint8_t &guidByte : outGUID.Data4 ) {
        guidByte = stream.readByte();
    }
}

static void WriteDeviceGUID( edStore &stream, const edGUID &guid ) {
    // FUN_008967c0
    stream.writeInteger( int32_t( guid.Data1 ) );
    stream.writeUShort( guid.Data2 );
    stream.writeUShort( guid.Data3 );

    for ( const uint8_t guidByte : guid.Data4 ) {
        stream.writeByte( guidByte );
    }
}

static int32_t GetDeviceIndexInKind( const Device *pDevice ) {
    // FUN_00896380
    int32_t kindIndex = 0;
    for ( uint32_t i = 0; i < gEdDev.NumDevices; i++ ) {
        Device *pOther = gEdDev.ppDevices[i];
        if ( pOther == nullptr ) {
            continue;
        }

        if ( pOther->getType() == pDevice->getType() &&
             pOther->getSpecialization() == pDevice->getSpecialization() ) {
            if ( pOther == pDevice ) {
                return kindIndex;
            }
            kindIndex++;
        }
    }

    return 0;
}

static void ApplyBindingToAction( DeviceAction *pAction,
                                  SavedInputBindings &bindings ) {
    // FUN_00896b60
    if ( pAction == nullptr ) {
        return;
    }

    for ( int32_t i = 0; i < pAction->NumChannels; i++ ) {
        pAction->pChannel[i] = InputChanneMapping();
    }
    pAction->NumAddedChannels = 0;

    const SavedInputBinding *pBinding = bindings.find( pAction->pName );
    if ( pBinding == nullptr ) {
        return;
    }

    InputChannel *pChannel = pBinding->resolveChannel();
    if ( pChannel != nullptr ) {
        pAction->addChannel( pBinding->bInverted, pBinding->bCombination,
                             pChannel );
    }
}

void SavedInputBinding::clear() {
    // FUN_008962f0
    memset( ActionName, 0, sizeof( ActionName ) );
    memset( ChannelName, 0, sizeof( ChannelName ) );
    memset( DisplayName, 0, sizeof( DisplayName ) );

    DeviceGUID = edGUID();

    DeviceType = 0u;
    DeviceSubType = 0u;
    DeviceIndex = 0u;
    bInverted = false;
    bCombination = false;
}

void SavedInputBinding::deserialize( edStore &stream ) {
    // FUN_00896cc0
    stream.readStringNoAlloc( ActionName, kActionNameLength - 1 );
    stream.readStringNoAlloc( ChannelName, kChannelNameLength - 1 );
    stream.readStringNoAlloc( DisplayName, kDisplayNameLength - 1 );

    DeviceType = stream.readByte();
    DeviceSubType = stream.readByte();
    DeviceIndex = stream.readByte();

    ReadDeviceGUID( stream, DeviceGUID );

    bInverted = stream.readByte() == 1u;
    bCombination = stream.readByte() == 1u;

    updateDisplayName();
}

void SavedInputBinding::serialize( edStore &stream ) {
    // FUN_00896820
    stream.writeString( ActionName );
    stream.writeString( ChannelName );
    stream.writeString( DisplayName );

    stream.writeByte( DeviceType );
    stream.writeByte( DeviceSubType );
    stream.writeByte( DeviceIndex );

    WriteDeviceGUID( stream, DeviceGUID );

    stream.writeByte( bInverted ? 1u : 0u );
    stream.writeByte( bCombination ? 1u : 0u );
}

InputChannel *SavedInputBinding::resolveChannel() const {
    // FUN_008968b0
    Device *pDevice = gEdDev.getDeviceByGUID( DeviceGUID );
    if ( pDevice == nullptr ) {
        return nullptr;
    }

    const int32_t channelIndex = pDevice->getChannelIndex( ChannelName );
    if ( channelIndex < 0 ) {
        return nullptr;
    }

    return &pDevice->getChannels()[channelIndex];
}

bool SavedInputBinding::isBoundTo( const InputChannel &channel ) const {
    // FUN_00896900
    if ( StringNCmpI( channel.pName, ChannelName, kChannelNameLength ) != 0 ) {
        return false;
    }

    const Device *pDevice = channel.pParent;
    if ( pDevice == nullptr || DeviceType != uint8_t( pDevice->getType() ) ||
         DeviceSubType != uint8_t( pDevice->getSpecialization() ) ) {
        return false;
    }

    // FUN_008961e0
    if ( !( pDevice->getInstanceGUID() == DeviceGUID ) ) {
        return false;
    }

    return GetDeviceIndexInKind( pDevice ) == DeviceIndex;
}

void SavedInputBinding::setChannel( const InputChannel *pChannel,
                                    const bool bInvertedChannel,
                                    const bool bCombinationChannel ) {
    // FUN_00896d60
    if ( pChannel == nullptr ) {
        memset( ChannelName, 0, sizeof( ChannelName ) );
        memset( DisplayName, 0, sizeof( DisplayName ) );
        DeviceType = 0u;
        DeviceSubType = 0u;
        DeviceIndex = 0u;
        DeviceGUID = edGUID();
        return;
    }

    strncpy( ChannelName, pChannel->pName, kChannelNameLength );

    const Device *pDevice = pChannel->pParent;
    if ( pDevice == nullptr ) {
        return;
    }

    DeviceType = uint8_t( pDevice->getType() );
    DeviceSubType = uint8_t( pDevice->getSpecialization() );
    DeviceIndex = uint8_t( GetDeviceIndexInKind( pDevice ) );
    DeviceGUID = pDevice->getInstanceGUID();
    bInverted = bInvertedChannel;
    bCombination = bCombinationChannel;

    updateDisplayName();
}

void SavedInputBinding::updateDisplayName() {
    // FUN_00896990
    const InputChannel *pChannel = resolveChannel();
    if ( pChannel == nullptr || pChannel->pParent == nullptr ) {
        return;
    }

    for ( const StickChannelLabel &label : kStickChannelLabels ) {
        if ( strcmp( pChannel->pName, label.pChannelName ) != 0 ) {
            continue;
        }

        const char *pLabel =
            ( gpDatabase != nullptr )
                ? gpDatabase->getStringByHashcode( 0, label.LabelHashcode )
                : nullptr;
        if ( pLabel != nullptr ) {
            strncpy( DisplayName, pLabel, kDisplayNameLength );
            DisplayName[kDisplayNameLength - 1] = '\0';
            return;
        }
        break;
    }

    pChannel->pParent->getChannelDisplayName( *pChannel, DisplayName,
                                              kDisplayNameLength );
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

    const int32_t channelIndex =
        pDevice->getChannelIndex( liveMapping.pCurrentChannel );
    InputChannel *pChannel =
        ( channelIndex >= 0 ) ? &pDevice->getChannels()[channelIndex] : nullptr;

    if ( pDevice->getType() == eDeviceType::DT_Keyboard &&
         strlen( liveMapping.pCurrentChannel ) == 1 ) {
        const int32_t asciiChannelIndex = pDevice->getChannelIndexFromAscii(
            uint8_t( liveMapping.pCurrentChannel[0] ) );
        if ( asciiChannelIndex >= 0 ) {
            pChannel = &pDevice->getChannels()[asciiChannelIndex];
        }
    }

    if ( pChannel != nullptr ) {
        binding.setChannel( pChannel, liveMapping.Unknown1,
                            liveMapping.Unknown2 );
    }
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

void SavedInputBindings::resetToDefaults() {
    // FUN_00897000
    clear();

    const int32_t numLiveMappings = GetNumPCOptionMapping();
    for ( int32_t mappingIdx = 0; mappingIdx < numLiveMappings; mappingIdx++ ) {
        addFromLiveMapping( *GetPCOptionMapping( mappingIdx ) );
    }

    updateCategoryCounts();
}

void SavedInputBindings::applyToActions( PlayerConfiguration &playerConfig ) {
    // FUN_00896bd0
    for ( uint32_t actionIdx = 0; actionIdx < playerConfig.NumActions;
          actionIdx++ ) {
        DeviceAction &action = playerConfig.pActions[actionIdx];
        if ( action.pName[0] == '\0' ) {
            break;
        }

        ApplyBindingToAction( &action, *this );
    }

    // OTDU_ADDITION: per-device input templates
    InputTemplates::ApplyToActions( playerConfig );
}
