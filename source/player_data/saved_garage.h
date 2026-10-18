#pragma once

#include "core/notifier.h"
#include "game/vehicle/car_desc.h"

class edStore;
struct DBEntryHouse;

int32_t GetHouseNumGarageSlots( const uint64_t houseHashcode );

// One slot of the player garage (a car + where it is parked)
class GarageSlot : public CarDesc {
public:
    GarageSlot();
    ~GarageSlot();

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;

    uint64_t getHouseHashcode() const;
    void setHouseHashcode( const uint64_t houseHashcode );

public:
    uint32_t pHouse[2] = {}; // TEA Encrypted
    uint32_t ebayID = 0u;
};

class Garage : public Notifiable {
public:
    static constexpr int32_t kNumCarSlots = 0x281;
    static constexpr int32_t kNumSavedCarSlots = 0x280;
    static constexpr uint32_t kHouseDatabaseIndex = 7u;

public:
    inline int16_t getNumCars() const { return numCars; }
    inline uint32_t getCurrentCar() const { return currentCar; }
    bool isValid() override { /*FUN_0042df00*/ return true; }

public:
    Garage();
    ~Garage();

    void clear( const bool bClearCars );
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;
    void refreshNumCars();
    GarageSlot *getCarAtIndex( int32_t index,
                               const uint64_t houseHashcode,
                               const bool bIncludeInvalid );

    int32_t getNumCarsInHouse( const uint64_t houseHashcode );
    GarageSlot *getHouseCarAtIndex( const int32_t index );
    int32_t getNumHouseCars();
    int32_t getHouseCarIndex( const CarDesc *pCar );
    int32_t getSlotIndex( const CarDesc *pCar ) const;
    int32_t getCarHouseIndex( const CarDesc *pCar, uint64_t &houseHashcode );

    bool isHouseOwned( const uint64_t houseHashcode ) const;
    int32_t getNumOwnedHouses() const;
    const DBEntryHouse *getOwnedHouse( int32_t index ) const;
    int32_t getNumGarageSlots() const;

    bool addHouse( const uint64_t houseHashcode );
    void removeHouse( const uint64_t houseHashcode );

    void beginTestDrive( const uint32_t carID,
                         const int32_t paintIndex,
                         const int32_t rimID,
                         const int32_t interiorID );
    void beginRental( const uint32_t carID,
                      const int32_t rentalTime,
                      const int32_t paintIndex,
                      const int32_t rimID,
                      const int32_t interiorID );
    void endRental();
    bool validateCars();
    int32_t findFreeSlot( const uint64_t houseHashcode,
                          const int32_t previousSlot = -1 ) const;
    int32_t getNumSellableCars( const uint64_t houseHashcode ) const;

public:
    int16_t numCars = 0;
    GarageSlot pCars[kNumCarSlots];
    uint32_t currentCar = 0u;
};
