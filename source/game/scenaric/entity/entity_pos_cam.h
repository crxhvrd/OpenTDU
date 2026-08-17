#pragma once

#include "base_entity.h"

class EntityPosCam : public Entity {
public:
    const char *GetName() override { return "PosCam"; /* FUN_004258c0 */ }

public:
    EntityPosCam();
    ~EntityPosCam();

    void loadResources( ScenaricBank *param_2 ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;

protected:
    static constexpr int32_t kNumEntityRefs = 10;

protected:
    uint8_t *pVolumeRef;
    uint8_t *pEntityRefs[kNumEntityRefs];
    uint8_t *pZoneVolumeRef;
    uint8_t *pLateEntityRef;
    int32_t Mode = 0;
};
