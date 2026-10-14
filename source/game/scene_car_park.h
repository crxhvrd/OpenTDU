#pragma once

#include "filesystem/bank.h"
#include "game/scenaric/scenaric_bank.h"

class CarDesc;
class CarState;
class PhysicsDrivingControl;
class Render2DB;
class VehicleBase;
struct VehicleBaseResources;

// Interior scene with one or several cars parked (e.g. GMGarage)
class SceneCarPark {
public:
    static constexpr int32_t kMaxNumCars = 0x80;
    static constexpr int32_t kNumStreamedCars = 8;

    enum ePlacementState : int32_t {
        CPPS_None = 0,
        CPPS_Pending = 1,
        CPPS_Placed = 2,
        CPPS_Replaced = 3,
    };

    enum eInnerViewState : int32_t {
        CPIV_Ready = 0,
        CPIV_Loaded = 1,
        CPIV_Unloaded = 2,
        CPIV_Unloading = 3,
        CPIV_Loading = 4,
    };

    struct Entry {
        CarDesc *pCar;
        int32_t ConfigID;
        int32_t PaintIndex;
        int32_t RimID;
        int32_t InteriorID;
        bool bStreamed;
        Matrix4x4f_t Transform;
        float field_0x60;
        ePlacementState PlacementState;
        eInnerViewState InnerViewState;
    };

public:
    inline int32_t getNumEntries() const { return numEntries; }
    inline bool isLoaded() const { return bLoaded; }
    inline void setLoaded( const bool value ) { bLoaded = value; }
    inline CarState *getCarState( const int32_t index ) const {
        return ( index >= 0 && index < kMaxNumCars ) ? pCarStates[index]
                                                     : nullptr;
    }
    inline ScenaricBank &getDealerBank() { return dealerBank; }
    inline void setEnvMapTargets( Render2DB *pFront, Render2DB *pBack ) {
        pEnvMapFront = pFront;
        pEnvMapBack = pBack;
    }
    inline Entry &getEntry( const int32_t index ) {
        return pEntries[getRingIndex( index )];
    }
    inline const Entry &getEntry( const int32_t index ) const {
        return pEntries[getRingIndex( index )];
    }

public:
    SceneCarPark();
    ~SceneCarPark();

    void setEnabled( const bool bEnabled );
    void releaseAll();
    void pushConfig( const int32_t configID );
    void pushCar( CarDesc *pCar );
    VehicleBase *getVehicle( const int32_t index ) const;
    VehicleBase *getCarVehicle( const int32_t index ) const;
    void streamAround( const int32_t index );
    void loadBank();
    void commit();
    void tick( const float deltaTime );
    void releaseCar( const int32_t index );
    void replaceCar( CarDesc *pCar, const int32_t index );
    void openInnerView( const int32_t index );
    void enterCar( const int32_t index );
    void leaveCar( const int32_t index );

    bool isEntryUsed( const int32_t index ) const;
    int32_t getNumUsedEntries() const;
    int32_t getUsedEntryIndex( int32_t usedIndex ) const;
    int32_t getLastAvailableShopSlot() const;
    void setPaint( const int32_t index, const int32_t paintIndex );
    void setRim( const int32_t index, const int32_t rimID );
    bool applyPaint( const int32_t index );

private:
    int32_t capacity;
    int32_t numEntries;
    int32_t head;
    int32_t tail;
    Entry *pEntries;

    int32_t pendingStreamIndex;
    CarDesc *pPendingCar;
    int32_t pendingCarIndex;
    eInnerViewState pendingInnerViewState;

    CarState *pCarStates[kMaxNumCars];
    VehicleBaseResources *pResources[kMaxNumCars];
    float fadeIn[kMaxNumCars];
    PhysicsDrivingControl *pSavedControls[kMaxNumCars] = {};

    bool bEnabled;
    bool bLoaded;
    bool bBankLoaded;

    Render2DB *pEnvMapFront;
    Render2DB *pEnvMapBack;

    Bank dealerBankFile;
    ScenaricBank dealerBank;

    int32_t enteredCarState;
    int32_t enteredCarIndex;
    float steeringWheelOffset;

private:
    inline int32_t getRingIndex( const int32_t index ) const {
        const int32_t ringIndex = head + 1 + index;
        return ( ringIndex >= capacity ) ? ringIndex - capacity : ringIndex;
    }

private:
    void push( const Entry &entry );
    void unloadBank();
    bool streamList( const int32_t *pList );
    void instantiateCar( const int32_t index, Entry &entry );
    bool isAnyInnerViewUnloading() const;
    void releaseResource( const int32_t index );
};

PhysicsDrivingControl *GetParkedPhysControl();
