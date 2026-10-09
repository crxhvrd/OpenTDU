#pragma once

class edStore;

extern bool gUnlockAllRoads;

struct SpotMap;

bool IsRoadAvailable( uint32_t roadID );
void DiscoverSpotRoad( const SpotMap *pSpot );

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

    bool isRoadLocked( const int32_t roadID, const int32_t segment ) const;
    void unlockRoad( const int32_t roadID, const int32_t segment );

public:
    uint32_t pBlocks[kNumBlocks][2]; // TEA encrypted
};
