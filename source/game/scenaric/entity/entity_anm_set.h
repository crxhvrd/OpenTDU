#pragma once

#include "base_entity.h"
#include "render/animation/animation.h"

class EntityAnmSet : public Entity {
public:
    const char *GetName() override { return "AnmSet"; /* FUN_004245e0 */ }

public:
    EntityAnmSet();
    ~EntityAnmSet();

    void initialize() override;
    void loadResources( ScenaricBank *param_2 ) override;
    void
    tick( GameMechanism *pGameMech, float deltaTime, float totalTime ) override;
    bool draw( RenderScene *pScene ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;
    void deinstanciate() override;

    RenderAnm *findAnimation( uint32_t animID );
    inline int32_t getNumAnimations() const {
        return static_cast<int32_t>( animations.size() );
    }

protected:
    std::vector<RenderAnm> animations;
};
