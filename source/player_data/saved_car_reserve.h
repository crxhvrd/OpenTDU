#pragma once

#include "core/random/bcpl.h"

class edStore;

class SavedCarReserve {
public:
    static constexpr uint32_t kDefaultSeed = 0x3039u;
    static constexpr int32_t kMaxReservations = 1;
    static constexpr int32_t kStockSize = 0x40;

public:
    inline bool isDebugMode() const { return bDebugMode; }
    inline bool hasReservation() const { return Reservations[0] != 0u; }
    inline const uint32_t *getReservations() const { return Reservations; }

public:
    SavedCarReserve();
    ~SavedCarReserve();

    void clear();
    void reset();
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;
    void tick();
    bool restock( const bool bFromSave );

    bool isAvailable( const uint32_t carID ) const;
    bool isReserved( const uint32_t carID ) const;
    bool reserve( const uint32_t carID );
    bool cancelReservation( const uint32_t carID );
    int32_t findInStock( const uint32_t carID ) const;

    inline int32_t getPaintIndex( const int32_t slot ) const {
        return PaintIndex[slot];
    }
    inline int32_t getInteriorIndex( const int32_t slot ) const {
        return InteriorIndex[slot];
    }
    inline int32_t getRimIndex( const int32_t slot ) const {
        return RimIndex[slot];
    }

private:
    bool restockCar( const uint32_t carID, const bool bForceAdd );
    bool addCar( const uint32_t carID );
    bool removeCar( const uint32_t carID );
    int32_t findSlot( const uint32_t carID ) const;
    int32_t findReservation( const uint32_t carID ) const;
    void notifyReservedCarInStock( const uint32_t carID );

private:
    uint32_t Reservations[kMaxReservations];
    uint32_t CarIDs[kStockSize];
    bool bInStock[kStockSize];
    int32_t PaintIndex[kStockSize];
    int32_t InteriorIndex[kStockSize];
    int32_t RimIndex[kStockSize];
    uint32_t TimeSinceRestock;
    uint32_t LastDeliveredCar;
    uint32_t LastRestockTicks;
    bool bDebugMode;
    RandomGen Random;
    uint32_t SeedUpdate;
    uint32_t SeedRestock;
};
