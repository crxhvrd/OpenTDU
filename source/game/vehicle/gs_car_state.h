#pragma once

#include "game/gs_base.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/vehicle_resource.h"
#include "game/vehicle/vehicle_base.h"

struct PointerSet {
    std::array<void *, kMaxNumVehicles> Content;

    PointerSet();
};

template <typename T> struct ResourcePool {
    std::array<T *, kMaxNumVehicles>
        pUsed; // in-use item pointers (indices 0..NumUsed-1)
    std::array<T *, kMaxNumVehicles>
        pFree; // free stack, top at [kMaxNumVehicles-1-NumUsed]
    std::array<bool, kMaxNumVehicles>
        pbUsed; // per-element used flag indexed by element position
    uint32_t NumUsed;
    std::array<T, kMaxNumVehicles> Elements;

    ResourcePool() : NumUsed( 0 ) {
        pUsed.fill( nullptr );
        pFree.fill( nullptr );
        pbUsed.fill( false );
        Elements.fill( T() );
        for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
            pFree[i] = &Elements[i];
        }
    }
};

class PhysFactory;
class PhysicsVehicle;
struct DBEntryAftermarketPack;
struct DBEntryCarPhysicsData;

enum eVehicleInstallType : int32_t {
    VIT_AI = 0,
    VIT_Player = 1,
    VIT_Network = 2,
    VIT_Opponent = 3,
    VIT_Traffic = 4,
    VIT_Showcase = 6,
};

struct VehicleSpawnParams {
    Matrix4x4f_t Transform = Matrix4x4f_t::Identity();
    bool bSetPosition = false;
    bool bCastToGround = false;
    bool bAddPhysicsPaused = false;
    bool bResetCar = false;
    bool bKeyframed = false;
    bool bCinematicMode = false;
    int32_t PhantomMode = 0;
};

struct VehicleInstallRequest {
    DBEntryCarPhysicsData *pCarData = nullptr;
    const DBEntryAftermarketPack *pPack = nullptr;
    CarState *pCarState = nullptr;
    PhysFactory *pFactory = nullptr;
    const void *pTorqueBinary = nullptr;
    Matrix4x4f_t Transform = Matrix4x4f_t::Identity();
    eVehicleInstallType InstallType = VIT_AI;
    int32_t PhantomMode = 0;
    bool bIsAI = false;
    bool bIsNetwork = false;
    bool bAddPhysicsPaused = false;
    bool bSetPosition = false;
    bool bCastToGround = false;
    bool bResetCar = false;
    bool bKeyframed = false;
    bool bCinematicMode = false;
};

class GSCarState : public GameSystem {
public:
    const char *getName() const override { return "CAR_STATE"; }
    inline CarState *getCarStates() { return carStates.data(); }
    inline bool isCarStateUsed( const int32_t carInstanceID ) const {
        return ( ( bUsedSlot >> carInstanceID ) & 1u ) != 0u;
    }

public:
    GSCarState();
    ~GSCarState();

    bool initialize( TestDriveGameInstance * ) override;
    void terminate() override;
    void tick( float totalTime, float deltaTime ) override;
    void draw( float deltaTime ) override;
    void pause() override;

    VehicleBaseResources *grabFreeResourceEntry();
    CarState &grabNextFreeCarState();
    CarState &grabCarState( const uint32_t slot );
    void registerCallback( CarState *param_2,
                           CarStateEventListener *pListener,
                           void *pData );
    void unregisterCallback( CarState *param_2,
                             CarStateEventListener *pListener );
    void setPoliceStateJob( CarState *pCarState, PoliceStateJob *pJob );

    void releaseResourceEntry( VehicleBaseResources *pResource );
    void releaseCarState( CarState *param_2, bool param_3 );
    void unloadVehicle( CarState *pCarState,
                        const bool bDeferred,
                        const bool bReleaseDriver );

    void notifyListeners( float param_1 );

    VehicleBaseResources *
    loadVehicleResources( const int32_t carID,
                          const int32_t tireID,
                          const int32_t rimID,
                          const uint64_t packHashcode,
                          const int32_t param_6,
                          const int32_t flags,
                          const eVehicleResourceType type );
    CarState *installVehicle( VehicleBaseResources *pResource,
                              const VehicleSpawnParams &params,
                              const eVehicleInstallType installType,
                              int32_t slot );
    CarState *spawnVehicle( const int32_t carID,
                            const int32_t tireID,
                            const int32_t rimID,
                            const uint64_t packHashcode,
                            const VehicleSpawnParams &params,
                            const int32_t slot,
                            const int32_t flags,
                            const eVehicleInstallType installType );

    CarState *findCarState( const PhysicsVehicle *pVehicle );
    void registerCollidedObject( void *pObject );

private:
    PointerSet pointerSet;
    std::array<CarState, kMaxNumVehicles> carStates;
    std::array<CarStateEventListener *, kMaxNumVehicles> eventListeners;
    std::array<void *, kMaxNumVehicles> eventListenersData;
    std::array<CarStateEventListener *, kMaxNumVehicles * 4> eventNotifiables;
    std::array<void *, kMaxNumVehicles * 4> eventNotifiablesData;
    std::array<CarStateEventListener *, 4> eventNotifiablesProcessed;
    std::array<void *, 4> eventListenersDataProcessed;
    std::array<PoliceStateJob *, kMaxNumVehicles> policeStateJobs;
    std::queue<CarState *> carToDeinstanciate;
    ResourcePool<VehicleBaseResources> vehicleResources;
    uint32_t bUsedSlot;
    uint32_t numCarStates;
};

extern GSCarState *gpCarState;
