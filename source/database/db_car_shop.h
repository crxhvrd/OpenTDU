#pragma once

struct DBEntryCarShop {
    static constexpr int32_t kNumSlots = 15;
    static constexpr int32_t kFirstSlotFlagBit = 3;

    uint64_t Hashcode;
    DBReference SpotName;
    DBReference Label;
    DBReference Slots[kNumSlots];
    uint32_t Flags;
    uint32_t __PADDING__;
};
OTDU_SIZE_MUST_MATCH( DBEntryCarShop, 0x98 );
