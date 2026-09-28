#pragma once

#include "base_entity.h"

class Path;

// Shop / dealership descriptor (ET_SHDA)
class EntityShopData : public Entity {
public:
    const char *GetName() override { return "ShopData"; /* FUN_00424170 */ }

public:
    inline Path *getCameraPath() const {
        return *reinterpret_cast<Path **>( pPathRef );
    }
    inline Entity *getCamera() const {
        return *reinterpret_cast<Entity **>( pEntityRef );
    }
    inline int32_t getNumPositions() const {
        return static_cast<int32_t>( *pEntityRefList );
    }
    inline Entity *getPosition( const int32_t index ) const {
        return *reinterpret_cast<Entity **>(
            reinterpret_cast<uint64_t *>( pEntityRefList + 1 ) + index );
    }

public:
    EntityShopData();
    ~EntityShopData();

    void loadResources( ScenaricBank *param_2 ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;

protected:
    uint8_t *pPathRef;
    uint8_t *pEntityRef;
    uint32_t *pEntityRefList;
};
