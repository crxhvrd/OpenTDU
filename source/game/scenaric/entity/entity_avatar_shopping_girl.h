#pragma once

#include "entity_avatar_passenger.h"

class EntityAvatarShoppingGirl : public EntityAvatarPassenger {
public:
    EntityAvatarShoppingGirl();
    ~EntityAvatarShoppingGirl();

    void loadResources( ScenaricBank *param_2 ) override;
    bool draw( RenderScene *pScene ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;

protected:
    uint32_t RandomSeed;
};
