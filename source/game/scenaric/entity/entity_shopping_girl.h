#pragma once

#include "entity_extra_challenge.h"

class EntityShoppingGirl : public EntityExtraChallenge {
public:
    const char *GetName() override { return "ShoppingGirl"; /* FUN_00427b80 */ }

public:
    EntityShoppingGirl();
    ~EntityShoppingGirl();

    void loadResources( ScenaricBank *param_2 ) override;
    void deserialize( BinaryReader *param_2 ) override;
    const char *getChallengeName() const override {
        return pGirlName; /* FUN_00427b90 */
    }

public:
    inline uint32_t getOffRoad() const { return OffRoad; }
    inline uint32_t getCoupon() const { return Coupon; }
    inline uint32_t getConditionType() const { return ConditionType; }
    inline uint32_t getConditionValue() const { return Condition.Value; }
    inline const ChallengeCondition &getCondition() const { return Condition; }

protected:
    const char *pGirlName;
    uint32_t OffRoad;
    uint32_t Coupon;
    uint32_t ConditionType;
    ChallengeCondition Condition;
    uint64_t GoodiesBinHashcode;
    Entity *pGoodiesBin;
};
