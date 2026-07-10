#pragma once

#include "base_entity.h"
#include "game/avatar/avatar_configuration.h"

struct BinaryReader;

// Base class for freeride challenges.
class EntityExtraChallenge : public Entity {
public:
    const char *GetName() override {
        return "ExtraChallenge"; /* FUN_00427b10 */
    }

public:
    EntityExtraChallenge();
    ~EntityExtraChallenge();

    void initialize() override;
    void loadResources( ScenaricBank *param_2 ) override;
    void initializeInstance( void *param_2, void *param_3 ) override;
    void tick() override;
    void deserialize( BinaryReader *param_2 ) override;

protected:
    int32_t Index;
    float TimeLimit;
    SerializedTransform *pGoalMatrix;
    SerializedTransform *pGoodiesBinMatrix;
    Matrix4x4f_t GoodiesMatrix;
    uint64_t TriggerHashcode;
    Entity *pTrigger;
    uint32_t AvatarVoiceType;
    AvatarConfiguration AvatarConfig;
    bool bInZone;
    int32_t RoadID;
    int32_t RoadEntityID;
};
