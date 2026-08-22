#pragma once

class edStore;

extern bool gUnlockAllRoads;

class SavedRoadDiscovery {
public:
    static constexpr int32_t kNumBlocks = 0xa28;

public:
    SavedRoadDiscovery();
    ~SavedRoadDiscovery();

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;

    void unlockAllRoads();

    uint64_t getBlock( const int32_t index ) const;
    void setBlock( const int32_t index, const uint64_t value );

public:
    uint32_t pBlocks[kNumBlocks][2]; // TEA encrypted
};
