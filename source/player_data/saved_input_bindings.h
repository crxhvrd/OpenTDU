#pragma once

class edStore;
struct PCOptionMapping;

// One input binding saved to 'commondt.sav'
struct SavedInputBinding {
    static constexpr size_t kActionNameLength = 0x20;
    static constexpr size_t kChannelNameLength = 0x20;
    static constexpr size_t kDisplayNameLength = 0x104;

    char ActionName[kActionNameLength] = {};
    char ChannelName[kChannelNameLength] = {};
    char DisplayName[kDisplayNameLength] = {};
    uint32_t DeviceGUIDData1 = 0u; // DInput device GUID (not mapped to SDL yet)
    uint16_t DeviceGUIDData2 = 0u;
    uint16_t DeviceGUIDData3 = 0u;
    uint8_t DeviceGUIDData4[8] = {};
    uint8_t ChannelIndex = 0u;
    uint8_t ChannelKind = 0u;
    uint8_t DeviceIndex = 0u;
    bool bFlag0 = false;
    bool bFlag1 = false;

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream );
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
};
