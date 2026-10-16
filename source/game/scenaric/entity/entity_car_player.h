#pragma once

#include "base_entity.h"

class EntityCarPlayer : public Entity {
public:
    const char *GetName() override { return "Actor"; /* FUN_00423f40 */ }

    enum eSpawnKind : int32_t {
        SK_None = 0,
        SK_ParkedTraffic = 1, // brand is TRAFFIC, or the car is not in the DB
        SK_BrandCar = 2,      // a real branded car: the dealership showcase
    };

public:
    EntityCarPlayer();
    ~EntityCarPlayer();

    void initialize() override;
    void unloadResources() override;
    void tick( PlayerGameMech *pGameMech,
               float deltaTime,
               float totalTime ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;

    inline int32_t getSpawnTag() const {
        return *reinterpret_cast<const int32_t *>( pData + 4 );
    }

protected:
    int32_t Index;
    uint8_t *pData;
    uint32_t Value;

    eSpawnKind SpawnKind;
    bool bBrandCarSpawned;
};
