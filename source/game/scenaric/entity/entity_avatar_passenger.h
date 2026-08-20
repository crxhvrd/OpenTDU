#pragma once

#include "entity_avatar.h"
#include "render/animation/animation.h"

// Base class for passenger
class EntityAvatarPassenger : public EntityAvatar {
public:
    EntityAvatarPassenger();
    ~EntityAvatarPassenger();

    void loadResources( ScenaricBank *param_2 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;

protected:
    void deserializeAnims( BinaryReader *pReader, uint32_t minVersion );

protected:
    static constexpr uint32_t kNumAnimCar = 2;

protected:
    RenderAnm pAnimCar[kNumAnimCar];
    uint32_t NumAnimCar;
    int32_t CurrentBehavior;
    RenderAnm AnimHi;
    RenderAnm AnimPeepin;
};
