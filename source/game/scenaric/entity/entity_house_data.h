#pragma once

#include "base_entity.h"

// Buyable-house descriptor (ET_HODA)
class EntityHouseData : public Entity {
public:
    const char *GetName() override { return "HouseData"; /* FUN_004241a0 */ }

public:
    EntityHouseData();
    ~EntityHouseData();

    void loadResources( ScenaricBank *param_2 ) override;
    void deserialize( BinaryReader *param_2 ) override;

protected:
    uint8_t *pEntityRef0;
    uint8_t *pEntityRef1;
    uint32_t *pVolumeList;
};
