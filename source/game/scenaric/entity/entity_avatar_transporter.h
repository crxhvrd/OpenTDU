#pragma once

#include "entity_avatar_passenger.h"

class EntityAvatarTransporter : public EntityAvatarPassenger {
public:
    EntityAvatarTransporter();
    ~EntityAvatarTransporter();

    void loadResources( ScenaricBank *param_2 ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;
    void resetFreerideAnim() override {
        // FUN_007f2cf0
        CurrentFreerideAnim = -1;
    }

protected:
    int32_t CurrentFreerideAnim;
};
