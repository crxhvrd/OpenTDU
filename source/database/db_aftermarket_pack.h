#pragma once

static constexpr uint64_t kNoPackHashcode = 0xffffffffffffffffull;

struct DBEntryAftermarketPack {
    uint64_t Hashcode;
    uint64_t PackID;
    uint64_t PackName;
    float SpeedRatio;
    float BHPRatio;
    float AccelerationRatio;
    float FreeRatio;
    float CxRatio;
    float WeightRatio;
    float SuspensionRatio;
    float BrakeRatio;
};
