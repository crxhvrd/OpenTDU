#pragma once

#include "entity_extra_challenge.h"

class EntityHitchHiker : public EntityExtraChallenge {
public:
    const char *GetName() override { return "HitchHiker"; /* FUN_00427ba0 */ }

public:
    EntityHitchHiker();
    ~EntityHitchHiker();

    void loadResources( ScenaricBank *param_2 ) override;
    void deserialize( BinaryReader *param_2 ) override;

public:
    inline uint32_t getConditionType() const { return ConditionType; }
    inline uint32_t getConditionValue() const { return Condition.Value; }

protected:
    uint32_t OffRoad;
    const char *pFirstName;
    uint64_t GoodiesHashcode;
    Entity *pGoodies;
    uint32_t Reward;
    uint32_t Coupon;
    uint32_t ConditionType;
    ChallengeCondition Condition;
};
