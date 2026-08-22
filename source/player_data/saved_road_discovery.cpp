#include "shared.h"
#include "saved_road_discovery.h"

#include "core/crypto/tea.h"
#include "core/ed_store.h"

bool gUnlockAllRoads = false; // DAT_010e7539

SavedRoadDiscovery::SavedRoadDiscovery() : pBlocks() {
    // FUN_0088d560
    clear();
}

SavedRoadDiscovery::~SavedRoadDiscovery() {}

void SavedRoadDiscovery::clear() {
    // FUN_0088d560
    for ( int32_t i = 0; i < kNumBlocks; i++ ) {
        TestDriveTEAWriteQword( pBlocks[i], 0ull );
    }
}

void SavedRoadDiscovery::deserialize( edStore &stream ) {
    // FUN_0088d400
    for ( int32_t i = 0; i < kNumBlocks; i++ ) {
        pBlocks[i][0] = stream.readULong();
        pBlocks[i][1] = stream.readULong();
    }
}

void SavedRoadDiscovery::serialize( edStore &stream ) const {
    // FUN_0088d3d0
    for ( int32_t i = 0; i < kNumBlocks; i++ ) {
        stream.writeInteger( int32_t( pBlocks[i][0] ) );
        stream.writeInteger( int32_t( pBlocks[i][1] ) );
    }
}

void SavedRoadDiscovery::unlockAllRoads() {
    // FUN_0088d520
    for ( int32_t i = 0; i < kNumBlocks; i++ ) {
        TestDriveTEAWriteQword( pBlocks[i], 0xffffffffffffffffull );
    }
}

uint64_t SavedRoadDiscovery::getBlock( const int32_t index ) const {
    return TestDriveTEAReadQword( pBlocks[index] );
}

void SavedRoadDiscovery::setBlock( const int32_t index, const uint64_t value ) {
    TestDriveTEAWriteQword( pBlocks[index], value );
}
