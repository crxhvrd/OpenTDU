#pragma once

#include "entity_extra_challenge.h"

class EntityConvoy : public EntityExtraChallenge {
public:
    const char *GetName() override { return "Convoy"; /* FUN_00427bd0 */ }

public:
    EntityConvoy();
    ~EntityConvoy();

    void deserialize( BinaryReader *param_2 ) override;
    const char *getChallengeName() const override {
        return pMapName; /* FUN_00427b90 */
    }

    inline uint32_t getReward() const { return Reward; }
    inline uint32_t getOffRoad() const { return OffRoad; }
    inline uint32_t getCarID() const { return CarID; }
    inline uint32_t getCarColorID() const { return CarColorID; }
    inline uint32_t getCarRimID() const { return CarRimID; }
    inline uint32_t getCarInteriorColorID() const { return CarInteriorColorID; }
    inline const AvatarConfiguration &getEndAvatarConfig() const {
        return EndAvatarConfig;
    }

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
