#pragma once

#include "core/notifier.h"
#include "game/vehicle/car_desc.h"

class edStore;

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

    bool isHouseOwned( const uint64_t houseHashcode ) const;
    int32_t getNumOwnedHouses() const;

public:
    int16_t numCars = 0;
    GarageSlot pCars[kNumCarSlots];
    uint32_t currentCar = 0u;
};
