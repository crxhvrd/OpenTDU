#pragma once

#include "game/gs_base.h"
#include "filesystem/streamed_resource.h"

#include <array>

class CarState;
class EntityCarPlayer;
struct VehicleBaseResources;

// One showcased vehicle parked at a spot exterior
class BrandCar : public StreamedResource {
public:
    enum eInstanciateState : int32_t {
        IS_None = 0,
        IS_ToDo = 1,
        IS_WaitingCallback = 2,
        IS_Done = 3,
    };

public:
    inline eInstanciateState getState() const { return state; }
    inline const EntityCarPlayer *getSpawnPoint() const { return pSpawnPoint; }

public:
    BrandCar() = default;

    void instanciate() override;

    bool load( EntityCarPlayer *pSpawnPoint,
               int32_t carID,
               int32_t tuneID,
               const Matrix4x4f_t &spawn );
    void destroy();
    void refreshTransform();

private:
    friend class GSBrandCar;

private:
    eInstanciateState state = IS_None;
    EntityCarPlayer *pSpawnPoint = nullptr;
    CarState *pCarState = nullptr;
    VehicleBaseResources *pResource = nullptr;
    Matrix4x4f_t spawnMatrix = Matrix4x4f_t::Identity();

    bool bGrounded = false; // FIXME: Remove once placement is implemented
};

class GSBrandCar : public GameSystem {
public:
    static constexpr int32_t kMaxNumBrandCar = 16;

public:
    const char *getName() const override { return "Service : Brand Car"; }

public:
    GSBrandCar();
    ~GSBrandCar();

    bool initialize( TestDriveGameInstance * ) override;
    void terminate() override;
    void tick( float totalTime, float deltaTime ) override;
    void pause() override;

    void addCar( EntityCarPlayer *pSpawnPoint,
                 int32_t carID,
                 const Matrix4x4f_t &spawn );
    void removeCar( EntityCarPlayer *pSpawnPoint );

private:
    std::array<BrandCar, kMaxNumBrandCar> cars;
    uint8_t bSuspended : 1;
};

extern GSBrandCar *gpBrandCar;
