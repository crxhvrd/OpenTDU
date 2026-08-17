#pragma once

#include "entity_extra_challenge.h"

class EntityConvoy : public EntityExtraChallenge {
public:
    const char *GetName() override { return "Convoy"; /* FUN_00427bd0 */ }

public:
    EntityConvoy();
    ~EntityConvoy();

    void deserialize( BinaryReader *param_2 ) override;

protected:
    const char *pMapName;
    uint32_t OffRoad;
    uint32_t Reward;
    uint32_t MinReward;
    uint32_t Coupon;
    uint32_t CarID;
    uint32_t CarColorID;
    uint32_t CarRimID;
    uint32_t CarInteriorColorID;
    AvatarConfiguration EndAvatarConfig;
};
