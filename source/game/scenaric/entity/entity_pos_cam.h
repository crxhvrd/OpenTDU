#pragma once

#include "base_entity.h"

class Path;

class EntityPosCam : public Entity {
public:
    const char *GetName() override { return "PosCam"; /* FUN_004258c0 */ }

public:
    inline Entity *getCamera() const {
        return ( pEntityRefs[0] != nullptr )
                   ? *reinterpret_cast<Entity **>( pEntityRefs[0] )
                   : nullptr;
    }
    inline Path *getPath() const {
        return ( pPathRef != nullptr ) ? *reinterpret_cast<Path **>( pPathRef )
                                       : nullptr;
    }
    inline int32_t getMode() const { return Mode; }
    inline Entity *getZoneVolume() const {
        return ( pZoneVolumeRef != nullptr )
                   ? *reinterpret_cast<Entity **>( pZoneVolumeRef )
                   : nullptr;
    }

public:
    EntityPosCam();
    ~EntityPosCam();

    void loadResources( ScenaricBank *param_2 ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;

protected:
    static constexpr int32_t kNumEntityRefs = 10;

protected:
    uint8_t *pPathRef;
    uint8_t *pEntityRefs[kNumEntityRefs];
    uint8_t *pZoneVolumeRef;
    uint8_t *pLateEntityRef;
    int32_t Mode = 0;
};
