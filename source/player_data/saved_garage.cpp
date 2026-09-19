#include "shared.h"
#include "saved_garage.h"

#include "core/crypto/tea.h"
#include "core/ed_store.h"
#include "database/gs_database.h"

int32_t GetHouseNumGarageSlots( const uint64_t houseHashcode ) {
    // FUN_0088fb00
    if ( gpDatabase == nullptr ) {
        return 0;
    }

    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const uint32_t numHouses = gameDB.pNumItems[Garage::kHouseDatabaseIndex];
    for ( uint32_t i = 0; i < numHouses; i++ ) {
        const DBEntryHouse &houseEntry = gameDB.DBHouse[i];
        if ( GetIdentifier64bit( houseEntry.SpotName.pAsString ) ==
             houseHashcode ) {
            return houseEntry.NumGarageSlot;
        }
    }

    return 0;
}

GarageSlot::GarageSlot() : CarDesc() {
    // FUN_00894fa0
    clear();
}

GarageSlot::~GarageSlot() {}

void GarageSlot::clear() {
    // FUN_00894fa0
    CarDesc::clear();

    setHouseHashcode( 0ull );
    ebayID = 0u;
}

void GarageSlot::deserialize( edStore &stream ) {
    // FUN_008947e0 (inlined)
    CarDesc::deserialize( stream );

    pHouse[0] = stream.readULong();
    pHouse[1] = stream.readULong();
    ebayID = stream.readULong();
}

void GarageSlot::serialize( edStore &stream ) const {
    // FUN_00894790 (inlined)
    CarDesc::serialize( stream );

    stream.writeInteger( int32_t( pHouse[0] ) );
    stream.writeInteger( int32_t( pHouse[1] ) );
    stream.writeInteger( int32_t( ebayID ) );
}

uint64_t GarageSlot::getHouseHashcode() const {
    return TestDriveTEAReadQword( pHouse );
}

void GarageSlot::setHouseHashcode( const uint64_t houseHashcode ) {
    TestDriveTEAWriteQword( pHouse, houseHashcode );
}

Garage::Garage() : Notifiable() {
    // FUN_00895390
    clear( true );
}

Garage::~Garage() {}

void Garage::clear( const bool bClearCars ) {
    // FUN_00894f10
    numCars = 0;

    if ( bClearCars ) {
        for ( GarageSlot &car : pCars ) {
            car.clear();
        }
    }

    // Write sentinel
    pCars[kNumCarSlots - 1].setHouseHashcode( 0xffffffffffffffffull );
    currentCar = 0u;
}

void Garage::refreshNumCars() {
    // FUN_0088eb30
    numCars = 0;
    for ( int32_t i = 0; i < kNumSavedCarSlots; i++ ) {
        if ( pCars[i].isValid() ) {
            numCars++;
        }
    }
}

void Garage::deserialize( edStore &stream ) {
    // FUN_008947e0
    for ( int32_t i = 0; i < kNumSavedCarSlots; i++ ) {
        pCars[i].deserialize( stream );
    }

    refreshNumCars();

    notify( 0u );
}

void Garage::serialize( edStore &stream ) const {
    // FUN_00894790
    for ( int32_t i = 0; i < kNumSavedCarSlots; i++ ) {
        pCars[i].serialize( stream );
    }
}

GarageSlot *Garage::getCarAtIndex( int32_t index,
                                   const uint64_t houseHashcode,
                                   const bool bIncludeInvalid ) {
    // FUN_008910a0
    if ( houseHashcode == 0ull ) {
        if ( index < 0 || index >= kNumCarSlots ) {
            index = 0;
        }
    } else if ( gpDatabase != nullptr ) {
        const GameDatabase &gameDB = gpDatabase->getGameDB();
        const uint32_t numHouses = gameDB.pNumItems[kHouseDatabaseIndex];

        for ( uint32_t i = 0; i < numHouses; i++ ) {
            const DBEntryHouse &houseEntry = gameDB.DBHouse[i];
            if ( GetIdentifier64bit( houseEntry.SpotName.pAsString ) !=
                 houseHashcode ) {
                continue;
            }

            if ( index < 0 || index >= houseEntry.NumGarageSlot ) {
                index = 0;
            }
            break;
        }
    }

    for ( int32_t slot = 0; slot < kNumCarSlots; slot++ ) {
        GarageSlot &car = pCars[slot];

        if ( !car.isValid() && !bIncludeInvalid ) {
            continue;
        }
        if ( houseHashcode != 0ull &&
             car.getHouseHashcode() != houseHashcode ) {
            continue;
        }

        if ( index == 0 ) {
            return &car;
        }
        index--;
    }

    return &pCars[0];
}

bool Garage::isHouseOwned( const uint64_t houseHashcode ) const {
    // FUN_008911a0
    for ( int32_t i = 0; i < kNumSavedCarSlots; i++ ) {
        if ( pCars[i].getHouseHashcode() == houseHashcode ) {
            return true;
        }
    }

    return false;
}

void Garage::removeHouse( const uint64_t houseHashcode ) {
    // FUN_00894cc0
    for ( int32_t i = 0; i < kNumSavedCarSlots; i++ ) {
        GarageSlot &slot = pCars[i];
        if ( slot.getHouseHashcode() != houseHashcode ) {
            continue;
        }

        slot.CarDesc::clear();
        slot.setHouseHashcode( 0ull );
        slot.ebayID = 0u;
    }
}

bool Garage::addHouse( const uint64_t houseHashcode ) {
    // FUN_00894d50
    int32_t numSlotsWanted = -1;

    if ( gpDatabase != nullptr ) {
        const GameDatabase &gameDB = gpDatabase->getGameDB();
        const uint32_t numHouses = gameDB.pNumItems[kHouseDatabaseIndex];
        for ( uint32_t i = 0; i < numHouses; i++ ) {
            const DBEntryHouse &houseEntry = gameDB.DBHouse[i];
            if ( GetIdentifier64bit( houseEntry.SpotName.pAsString ) ==
                 houseHashcode ) {
                numSlotsWanted = houseEntry.NumGarageSlot;
                break;
            }
        }
    }

    int32_t numSlotsClaimed = 0;
    for ( int32_t i = 0; i < kNumSavedCarSlots; i++ ) {
        GarageSlot &slot = pCars[i];

        // Already owned: drop every slot it holds and claim them again, so a
        // house whose slot count grew picks the extra ones up.
        if ( slot.getHouseHashcode() == houseHashcode ) {
            removeHouse( houseHashcode );
            return addHouse( houseHashcode );
        }

        if ( slot.getHouseHashcode() == 0ull &&
             numSlotsClaimed < numSlotsWanted ) {
            slot.setHouseHashcode( houseHashcode );
            numSlotsClaimed++;
        }
    }

    if ( numSlotsClaimed != numSlotsWanted ) {
        return false;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE

    return true;
}

int32_t Garage::getNumOwnedHouses() const {
    // FUN_00893460
    if ( gpDatabase == nullptr ) {
        return 0;
    }

    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const uint32_t numHouses = gameDB.pNumItems[kHouseDatabaseIndex];

    int32_t numOwnedHouses = 0;
    for ( uint32_t i = 0; i < numHouses; i++ ) {
        const uint64_t houseHashcode =
            GetIdentifier64bit( gameDB.DBHouse[i].SpotName.pAsString );
        if ( isHouseOwned( houseHashcode ) ) {
            numOwnedHouses++;
        }
    }

    return numOwnedHouses;
}
