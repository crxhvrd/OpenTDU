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
    const char *getChallengeName() const override {
        return pMapName; /* FUN_00427b90 */
    }

public:
    inline uint32_t getConditionType() const { return ConditionType; }
    inline uint32_t getConditionValue() const { return Condition.Value; }
    inline const ChallengeCondition &getCondition() const { return Condition; }
    inline uint32_t getReward() const { return Reward; }

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
