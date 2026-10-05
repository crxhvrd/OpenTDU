#pragma once

#include "input/input_device.h"

class edStore;
struct PCOptionMapping;
struct PlayerConfiguration;

// One input binding saved to 'commondt.sav'
struct SavedInputBinding {
    static constexpr size_t kActionNameLength = 0x20;
    static constexpr size_t kChannelNameLength = 0x20;
    static constexpr size_t kDisplayNameLength = 0x104;

    char ActionName[kActionNameLength] = {};
    char ChannelName[kChannelNameLength] = {};
    char DisplayName[kDisplayNameLength] = {};
    edGUID DeviceGUID = {};
    uint8_t DeviceType = 0u;    // eDeviceType
    uint8_t DeviceSubType = 0u; // eDeviceSpecialization
    uint8_t DeviceIndex = 0u;   // among the devices of the same type/subtype
    bool bInverted = false;
    bool bCombination = false;

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream );

    InputChannel *resolveChannel() const;
    bool isBoundTo( const InputChannel &channel ) const;
    void setChannel( const InputChannel *pChannel,
                     const bool bInvertedChannel,
                     const bool bCombinationChannel );
    void updateDisplayName();
};

struct SavedInputBindings {
    static constexpr int32_t kMaxBindings = 0x100;
    static constexpr int32_t kNumCategories = 6;

    SavedInputBinding Entries[kMaxBindings] = {};
    int32_t NumEntries = 0;
    int32_t NumEntriesPerCategory[kNumCategories] = {};

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream );

    SavedInputBinding *find( const char *pActionName );
    void addFromLiveMapping( const PCOptionMapping &liveMapping );
    void updateCategoryCounts();
    void resetToDefaults();
    void applyToActions( PlayerConfiguration &playerConfig );
};
