#pragma once

#include "base_entity.h"

class EntityCamera : public Entity {
public:
    const char *GetName() override { return "Camera"; /* FUN_00412180 */ }

public:
    inline float getFOV() const { return FOV; }
    inline float getRatio() const { return Ratio; }

public:
    EntityCamera();
    ~EntityCamera();

    void deserialize( BinaryReader *param_2 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;

protected:
    float FOV;
    float Ratio;
};
