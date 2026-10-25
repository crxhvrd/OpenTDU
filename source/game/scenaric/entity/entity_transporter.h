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
    inline Entity *getGoodies() const { return pGoodies; }
    // Charged at the start, refunded on the results screen
    inline uint32_t getDeposit() const {
        return uint32_t( double( Reward ) * double( kDepositRatio ) );
    }
    inline const AvatarConfiguration &getEndAvatarConfig() const {
        return EndAvatarConfig;
    }

private:
    static constexpr float kSpotMeshForwardOffset = 2.0f; // DAT_00f416b0
    static constexpr float kDepositRatio = 0.1f;          // FLOAT_00f41668

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
