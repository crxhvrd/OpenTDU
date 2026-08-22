#include "shared.h"
#include "saved_car_reserve.h"

#include "core/ed_store.h"
#include "core/time.h"

SavedCarReserve::SavedCarReserve() {
    // FUN_00895120
    clear();
}

SavedCarReserve::~SavedCarReserve() {}

void SavedCarReserve::clear() {
    // FUN_00895120
    Seed = kDefaultSeed;

    memset( Stock, 0, sizeof( Stock ) );

    NumRestocks = 0u;
    LastRestockDate = 0u;
    LastRestockTicks = uint32_t( MonotonicToMs( GetTimeMonotonic() ) );
    RestockCounter = 0u;
    bStockLocked = false;
    RestockTimer = 0u;

    restock( false );
}

void SavedCarReserve::deserialize( edStore &stream ) {
    // FUN_008953d0
    RestockTimer = stream.readUInteger();
    LastRestockDate = stream.readULong();
    RestockCounter = stream.readULong();
    NumRestocks = stream.readUInteger();

    restock( true );

    LastRestockTicks = uint32_t( MonotonicToMs( GetTimeMonotonic() ) );
}

void SavedCarReserve::serialize( edStore &stream ) const {
    // FUN_00899420 (inlined)
    stream.writeInteger( int32_t( RestockTimer ) );

    const uint32_t nowMs = uint32_t( MonotonicToMs( GetTimeMonotonic() ) );
    stream.writeInteger(
        int32_t( LastRestockDate + ( nowMs - LastRestockTicks ) ) );

    stream.writeInteger( int32_t( RestockCounter ) );
    stream.writeInteger( int32_t( NumRestocks ) );
}

bool SavedCarReserve::restock( const bool bFromSave ) {
    // FUN_00895070
    if ( bStockLocked ) {
        return false;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (the original bails out when
                             // the game mode is 4, then reseeds the stock and
                             // restarts the restock timer)
    (void)bFromSave;
    return false;
}
