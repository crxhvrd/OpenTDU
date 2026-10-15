#include "shared.h"
#include "saved_car_reserve.h"

#include "core/ed_store.h"
#include "core/time.h"
#include "database/gs_database.h"
#include "game/gm_base.h"
#include "gs_timer.h"
#include "tdu_instance.h"

static constexpr uint32_t kRestockInterval = 1800000u; // 30 mins
static constexpr float kRestockSeedScale = 100.0f;     // DAT_00f41708
static constexpr float kRandomToUnit = 2.3283064e-10f; // DAT_00f418e4

static uint32_t GetTickCountMs() {
    return uint32_t( MonotonicToMs( GetTimeMonotonic() ) );
}

SavedCarReserve::SavedCarReserve()
    : Reservations{},
      CarIDs{},
      bInStock{},
      PaintIndex{},
      InteriorIndex{},
      RimIndex{},
      TimeSinceRestock( 0u ),
      LastDeliveredCar( 0u ),
      LastRestockTicks( 0u ),
      bDebugMode( false ),
      Random(),
      SeedUpdate( 0u ),
      SeedRestock( 0u ) {
    // FUN_00895120
    clear();
}

SavedCarReserve::~SavedCarReserve() {}

void SavedCarReserve::clear() {
    // FUN_00895120
    Random.setSeed( kDefaultSeed );
    memset( bInStock, 0, sizeof( bInStock ) );
    Reservations[0] = 0u;
    TimeSinceRestock = 0u;
    LastRestockTicks = GetTickCountMs();
    LastDeliveredCar = 0u;
    bDebugMode = false;

    restock( false );
}

void SavedCarReserve::reset() {
    // FUN_0089ab80 (inlined)
    memset( bInStock, 0, sizeof( bInStock ) );
    Reservations[0] = 0u;
    TimeSinceRestock = 0u;
    LastRestockTicks = GetTickCountMs();
    LastDeliveredCar = 0u;
    bDebugMode = false;
}

void SavedCarReserve::deserialize( edStore &stream ) {
    // FUN_008953d0
    SeedRestock = stream.readUInteger();
    TimeSinceRestock = stream.readULong();
    LastDeliveredCar = stream.readULong();
    Reservations[0] = stream.readUInteger();

    restock( true );

    LastRestockTicks = GetTickCountMs();
}

void SavedCarReserve::serialize( edStore &stream ) const {
    // FUN_00899420 (inlined)
    stream.writeInteger( int32_t( SeedRestock ) );
    stream.writeInteger(
        int32_t( TimeSinceRestock + ( GetTickCountMs() - LastRestockTicks ) ) );
    stream.writeInteger( int32_t( LastDeliveredCar ) );
    stream.writeInteger( int32_t( Reservations[0] ) );
}

void SavedCarReserve::tick() {
    // FUN_00895420
    const uint32_t elapsed =
        GetTickCountMs() + ( TimeSinceRestock - LastRestockTicks );
    if ( elapsed <= kRestockInterval ) {
        return;
    }

    TimeSinceRestock = 0u;
    LastRestockTicks = GetTickCountMs();
    restock( false );
}

bool SavedCarReserve::restock( const bool bFromSave ) {
    // FUN_00895070
    if ( bDebugMode ||
         ( gpTestDriveInstance != nullptr &&
           gpTestDriveInstance->getActiveGameMode() == GM_Shop ) ) {
        return false;
    }

    if ( !bFromSave ) {
        const float totalTime =
            ( gpTimer != nullptr ) ? gpTimer->SystemTotalTime : 0.0f;
        SeedRestock = uint32_t( uint64_t( totalTime * kRestockSeedScale ) );
    }
    Random.setSeed( SeedRestock );

    if ( gpDatabase == nullptr ) {
        return true;
    }

    const GameDatabase &gameDB = gpDatabase->getGameDB();
    for ( uint32_t i = 0; i < gameDB.pNumItems[1]; i++ ) {
        const DBEntryCarPhysicsData &car = gameDB.DBCarData[i];
        if ( car.Rarety < 1.0f ) {
            restockCar( uint32_t( car.CarID ), false );
        }
    }
    return true;
}

bool SavedCarReserve::restockCar( const uint32_t carID, const bool bForceAdd ) {
    // FUN_00893570
    const DBEntryCarPhysicsData *pCar =
        gpDatabase->getCarDBEntry( int32_t( carID ) );
    if ( pCar == nullptr ) {
        return false;
    }

    const float roll = float( Random.getRandomUint32() ) * kRandomToUnit;
    if ( pCar->Rarety <= roll && !bForceAdd && carID != LastDeliveredCar ) {
        removeCar( carID );
        return true;
    }

    if ( addCar( carID ) && isReserved( carID ) ) {
        LastDeliveredCar = carID;
        const int32_t reservation = findReservation( carID );
        if ( reservation >= 0 ) {
            Reservations[reservation] = 0u;
        }
        notifyReservedCarInStock( carID );
    }
    return true;
}

bool SavedCarReserve::addCar( const uint32_t carID ) {
    // FUN_00891210
    const int32_t slot = findSlot( carID );
    if ( slot < 0 ) {
        return false;
    }

    CarIDs[slot] = carID;
    bInStock[slot] = true;

    PaintIndex[slot] = 0;
    const uint32_t numColors =
        uint32_t( gpDatabase->getNumColors( int32_t( carID ) ) );
    if ( int32_t( numColors ) > 1 ) {
        PaintIndex[slot] = int32_t( Random.getRandomUint32() % numColors );
    }

    InteriorIndex[slot] = 0;
    const DBEntryCarColor *pColor =
        gpDatabase->getColorByIndex( int32_t( carID ), PaintIndex[slot] );
    if ( pColor != nullptr ) {
        const uint32_t numInteriors =
            uint32_t( gpDatabase->getNumInteriors( pColor ) );
        if ( int32_t( numInteriors ) > 1 ) {
            InteriorIndex[slot] =
                int32_t( Random.getRandomUint32() % numInteriors );
        }
    }

    RimIndex[slot] = 0;
    const uint32_t numRims =
        uint32_t( gpDatabase->getNumRims( int32_t( carID ) ) );
    if ( int32_t( numRims ) > 1 ) {
        RimIndex[slot] = int32_t( Random.getRandomUint32() % numRims );
    }
    return true;
}

bool SavedCarReserve::removeCar( const uint32_t carID ) {
    // FUN_0088ec20
    const int32_t slot = findSlot( carID );
    if ( slot < 0 || !bInStock[slot] ) {
        return false;
    }

    bInStock[slot] = false;
    return true;
}

int32_t SavedCarReserve::findSlot( const uint32_t carID ) const {
    // FUN_0088eb90
    int32_t freeSlot = -1;
    for ( int32_t i = 0; i < kStockSize; i++ ) {
        if ( !bInStock[i] ) {
            freeSlot = i;
        } else if ( CarIDs[i] == carID ) {
            return i;
        }
    }
    return freeSlot;
}

int32_t SavedCarReserve::findInStock( const uint32_t carID ) const {
    // FUN_0088ebd0
    for ( int32_t i = 0; i < kStockSize; i++ ) {
        if ( bInStock[i] && CarIDs[i] == carID ) {
            return i;
        }
    }
    return -1;
}

int32_t SavedCarReserve::findReservation( const uint32_t carID ) const {
    // FUN_0088eb60
    int32_t freeSlot = 0;
    for ( int32_t i = 0; i < kMaxReservations; i++ ) {
        if ( Reservations[i] == 0u ) {
            freeSlot = i;
        } else if ( Reservations[i] == carID ) {
            return i;
        }
    }
    return freeSlot;
}

bool SavedCarReserve::isReserved( const uint32_t carID ) const {
    // FUN_0088ec90
    for ( int32_t i = 0; i < kMaxReservations; i++ ) {
        if ( Reservations[i] == carID ) {
            return true;
        }
    }
    return false;
}

bool SavedCarReserve::reserve( const uint32_t carID ) {
    // FUN_0088ec50
    const int32_t reservation = findReservation( carID );
    if ( reservation < 0 ) {
        return false;
    }

    Reservations[reservation] = carID;
    LastDeliveredCar = 0u;
    return true;
}

bool SavedCarReserve::cancelReservation( const uint32_t carID ) {
    // FUN_0088ec00
    const int32_t reservation = findReservation( carID );
    if ( reservation < 0 ) {
        return false;
    }

    Reservations[reservation] = 0u;
    return true;
}

bool SavedCarReserve::isAvailable( const uint32_t carID ) const {
    // FUN_0088fd60
    if ( findInStock( carID ) >= 0 ) {
        return true;
    }

    const DBEntryCarPhysicsData *pCar =
        gpDatabase->getCarDBEntry( int32_t( carID ) );
    return pCar != nullptr && pCar->Rarety == 1.0f;
}

void SavedCarReserve::notifyReservedCarInStock( const uint32_t carID ) {
    // FUN_0088fc40
    (void)carID;
    OTDU_UNIMPLEMENTED; // TODO: ONLINE (posts a localized "reserved car in
                        // stock" message through gpCP2PGameService)
}
