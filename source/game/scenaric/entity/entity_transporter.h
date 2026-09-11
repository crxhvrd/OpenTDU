#pragma once

#include "entity_extra_challenge.h"

class EntityTransporter : public EntityExtraChallenge {
public:
    const char *GetName() override { return "Transporter"; /* FUN_00427bc0 */ }

public:
    EntityTransporter();
    ~EntityTransporter();

    void loadResources( ScenaricBank *param_2 ) override;
    void deserialize( BinaryReader *param_2 ) override;
    Eigen::Vector4f getSpotMeshPosition() const override;

private:
    static constexpr float kSpotMeshForwardOffset = 2.0f; // DAT_00f416b0

private:
    const char *pMapName;
    uint32_t Reward;
    uint32_t Coupon;
    uint64_t GoodiesHashcode;
    Entity *pGoodies;
    AvatarConfiguration EndAvatarConfig;
    uint32_t ConditionType;
    ChallengeCondition Condition;
};
