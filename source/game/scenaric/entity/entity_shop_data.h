#pragma once

#include "base_entity.h"

// Shop / dealership descriptor (ET_SHDA)
class EntityShopData : public Entity {
public:
    const char *GetName() override { return "ShopData"; /* FUN_00424170 */ }

public:
    EntityShopData();
    ~EntityShopData();

    void loadResources( ScenaricBank *param_2 ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;

protected:
    uint8_t *pVolumeRef;
    uint8_t *pEntityRef;
    uint32_t *pEntityRefList;
};
