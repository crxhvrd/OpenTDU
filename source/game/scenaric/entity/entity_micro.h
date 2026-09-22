#pragma once

#include "base_entity.h"

class EntityMicro : public Entity {
public:
    const char *GetName() override { return "Actor"; /* FUN_00423f40 */ }

public:
    EntityMicro();
    ~EntityMicro();

    void initialize() override;
    void loadResources( ScenaricBank *param_2 ) override;
    void unloadResources() override;
    void tick( PlayerGameMech *pGameMech,
               float deltaTime,
               float totalTime ) override;
    bool draw( RenderScene *pScene ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void initializeForCinematicKeyframed( void *param_2,
                                          void *param_3 ) override;
    void setTransform( Matrix4x4f_t &transformMatrix, bool param_1 ) override;
    void setPosition( Eigen::Vector4f &position, bool param_1 ) override;
    void reset() override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;

protected:
    uint8_t Flags;
};
