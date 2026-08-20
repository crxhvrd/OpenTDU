#pragma once

#include "entity_avatar_passenger.h"

class EntityAvatarConvoy : public EntityAvatarPassenger {
public:
    EntityAvatarConvoy();
    ~EntityAvatarConvoy();

    void loadResources( ScenaricBank *param_2 ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;

protected:
    int32_t CurrentFreerideAnim;
};
