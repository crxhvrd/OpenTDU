#include "shared.h"
#include "saved_garage.h"

#include "core/crypto/tea.h"
#include "core/ed_store.h"
#include "database/gs_database.h"
#include "driver.h"
#include "game/scenaric/gs_scenaric.h"
#include "gs_playerdata.h"
#include "player_data_interface.h"
#include "saved_extra_challenges.h"

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

int32_t Garage::getNumCarsInHouse( const uint64_t houseHashcode ) {
    // FUN_00891020
    if ( houseHashcode == 0ull ) {
        return numCars;
    }

    int32_t numHouseCars = 0;
    for ( int32_t i = 0; i < kNumSavedCarSlots; i++ ) {
        if ( pCars[i].isValid() &&
             pCars[i].getHouseHashcode() == houseHashcode ) {
            numHouseCars++;
        }
    }

    return numHouseCars;
}

GarageSlot *Garage::getHouseCarAtIndex( const int32_t index ) {
    // FUN_00892d70
    Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();
    const GarageSlot &currentCar = pCars[pDriver->CurrentCar];
    if ( index == 0 ) {
        return &pCars[pDriver->CurrentCar];
    }

    const uint64_t houseHashcode = pDriver->getCurrentHouse();
    if ( currentCar.getHouseHashcode() != houseHashcode ) {
        return getCarAtIndex( index - 1, houseHashcode, false );
    }

    const int32_t numHouseCars = getNumCarsInHouse( houseHashcode );
    for ( int32_t i = 0; i < numHouseCars; i++ ) {
        const GarageSlot *pCar = getCarAtIndex( i, houseHashcode, false );
        if ( pCar->getCarID() == currentCar.getCarID() ) {
            break;
        }

        if ( i == index - 1 ) {
            return getCarAtIndex( index - 1, houseHashcode, false );
        }
    }

    return getCarAtIndex( index, houseHashcode, false );
}

int32_t Garage::getNumHouseCars() {
    // FUN_00892f50
    Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();
    const uint64_t houseHashcode = pDriver->getCurrentHouse();
    const int32_t numHouseCars = getNumCarsInHouse( houseHashcode );

    if ( pCars[pDriver->CurrentCar].getHouseHashcode() == houseHashcode ) {
        return numHouseCars;
    }

    return numHouseCars + 1;
}

int32_t Garage::getHouseCarIndex( const CarDesc *pCar ) {
    // FUN_00894c80
    const int32_t numHouseCars = getNumHouseCars();
    for ( int32_t i = 0; i < numHouseCars; i++ ) {
        if ( getHouseCarAtIndex( i )->getCarID() == pCar->getCarID() ) {
            return i;
        }
    }

    return -1;
}

int32_t Garage::getSlotIndex( const CarDesc *pCar ) const {
    // FUN_0088fa00
    for ( int32_t i = 0; i <= kNumSavedCarSlots; i++ ) {
        if ( pCars[i].getCarID() == pCar->getCarID() ) {
            return i;
        }
    }

    return 0;
}

int32_t Garage::getCarHouseIndex( const CarDesc *pCar,
                                  uint64_t &houseHashcode ) {
    // FUN_00890e20
    for ( int32_t i = 0; i < kNumSavedCarSlots; i++ ) {
        if ( pCars[i].getCarID() == pCar->getCarID() ) {
            houseHashcode = pCars[i].getHouseHashcode();
        }
    }

    int32_t index = 0;
    for ( int32_t i = 0; i < kNumSavedCarSlots; i++ ) {
        GarageSlot &car = pCars[i];
        if ( car.getHouseHashcode() != houseHashcode ) {
            continue;
        }

        if ( car.getCarID() == pCar->getCarID() ) {
            return index;
        }
        if ( car.isValid() ) {
            index++;
        }
    }

    return 0;
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

void Garage::beginTestDrive( const uint32_t carID,
                             const int32_t paintIndex,
                             const int32_t rimID,
                             const int32_t interiorID ) {
    // FUN_00894af0
    GarageSlot &testSlot = pCars[kNumSavedCarSlots];
    if ( testSlot.state != CarDesc::eState::CDS_Invalid ) {
        endRental();
    }

    testSlot.setup( carID, paintIndex, rimID, interiorID, 0u );
    if ( testSlot.state != CarDesc::eState::CDS_TestDrive ) {
        testSlot.state = CarDesc::eState::CDS_TestDrive;
        testSlot.notify( 0u );
    }

    Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();
    if ( pDriver->CurrentCar != kNumSavedCarSlots ) {
        currentCar = pDriver->CurrentCar;
    }
    pDriver->CurrentCar = kNumSavedCarSlots;
    pDriver->notify( 0u );
    notify( 0u );
}

void Garage::beginRental( const uint32_t carID,
                          const int32_t rentalTime,
                          const int32_t paintIndex,
                          const int32_t rimID,
                          const int32_t interiorID ) {
    // FUN_00894b90
    GarageSlot &rentalSlot = pCars[kNumSavedCarSlots];
    if ( rentalSlot.state != CarDesc::eState::CDS_Invalid ) {
        endRental();
    }

    rentalSlot.setup( carID, paintIndex, rimID, interiorID, 0u );
    if ( rentalSlot.state != CarDesc::eState::CDS_Rental ) {
        rentalSlot.state = CarDesc::eState::CDS_Rental;
        rentalSlot.notify( 0u );
    }

    Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();
    if ( pDriver->CurrentCar != kNumSavedCarSlots ) {
        currentCar = pDriver->CurrentCar;
    }
    pDriver->CurrentCar = kNumSavedCarSlots;
    pDriver->notify( 0u );

    gpScenaric->setRentalSpot( gpScenaric->getSpotList().getWorldSpotChecked(
        gpScenaric->getCurrentSpotIndex() ) );
    gpScenaric->setTimeLeftForRental( float( rentalTime ) );
    notify( 0u );
}

void Garage::endRental() {
    // FUN_00894890
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE

    GarageSlot &rentalSlot = pCars[kNumSavedCarSlots];
    rentalSlot.CarDesc::clear();
    rentalSlot.setHouseHashcode( 0ull );
    rentalSlot.ebayID = 0u;

    Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();
    pDriver->CurrentCar = uint16_t( currentCar );
    pDriver->notify( 0u );

    GarageSlot &car = pCars[pDriver->CurrentCar];
    if ( !car.isValid() || car.state == CarDesc::eState::CDS_PutOnEbay ) {
        int32_t index = 0;
        for ( int32_t i = 0; i < numCars; i++ ) {
            GarageSlot *pCandidate = getCarAtIndex( i, 0ull, false );
            if ( pCandidate->isValid() &&
                 pCandidate->state != CarDesc::eState::CDS_PutOnEbay ) {
                index = i;
                break;
            }
        }

        GarageSlot *pNewCurrentCar = getCarAtIndex( index, 0ull, false );
        if ( pNewCurrentCar->isValid() ) {
            pDriver->CurrentCar = uint16_t( getSlotIndex( pNewCurrentCar ) );
            pDriver->notify( 0u );
        }
    }

    gpScenaric->setTimeLeftForRental( -1.0f );
    notify( 0u );
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

    gpPlayerData->getLoadedSave()->getExtraChallenges()->addCounter(
        CounterIndex( 0xf124 ), 1 );

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

bool Garage::validateCars() {
    // FUN_00895510
    for ( int32_t i = 0; i < kNumSavedCarSlots; i++ ) {
        GarageSlot &slot = pCars[i];
        if ( !slot.isValid() ) {
            continue;
        }

        if ( slot.getCarID() == 0xffffffffu ) {
            slot.clear();
            continue;
        }

        DBEntryCarPhysicsData *pCarEntry =
            gpDatabase->getCarDBEntry( int32_t( slot.getCarConfigID() ) );
        if ( pCarEntry == nullptr || !gpDatabase->carBankExists( pCarEntry ) ) {
            return false;
        }
    }

    return true;
}

int32_t Garage::findFreeSlot( const uint64_t houseHashcode,
                              const int32_t previousSlot ) const {
    // FUN_00890ef0
    static constexpr uint64_t kNoHouse = 0xffffffffffffffffull;

    for ( int32_t i = previousSlot + 1; i < kNumSavedCarSlots; i++ ) {
        GarageSlot &slot = const_cast<GarageSlot &>( pCars[i] );
        if ( slot.isValid() ) {
            continue;
        }

        const uint64_t slotHouse = slot.getHouseHashcode();
        if ( slotHouse == 0ull || slotHouse == kNoHouse ) {
            continue;
        }

        if ( houseHashcode == 0ull || slotHouse == houseHashcode ) {
            return i;
        }
    }
    return -1;
}

int32_t Garage::getNumSellableCars( const uint64_t houseHashcode ) const {
    // FUN_00893020
    int32_t numCars = 0;
    for ( int32_t i = 0; i < kNumSavedCarSlots; i++ ) {
        GarageSlot &slot = const_cast<GarageSlot &>( pCars[i] );
        if ( !slot.isValid() || slot.state == CarDesc::eState::CDS_PutOnEbay ) {
            continue;
        }

        if ( houseHashcode != 0ull &&
             slot.getHouseHashcode() != houseHashcode ) {
            continue;
        }

        if ( gpDatabase->getCarPrice( int32_t( slot.getCarConfigID() ), -1, -1,
                                      -1 ) > 0.0f ) {
            numCars++;
        }
    }
    return numCars;
}
