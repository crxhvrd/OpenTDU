#pragma once

struct DBEntryHouse {
    uint64_t Hashcode;
    DBReference SpotName;
    DBReference RealtorHashcode;
    DBReference HouseName;
    int32_t Level;
    int32_t Model;
    int32_t NumGarageSlot;
    int32_t PriceBuy;
    int32_t PriceSell;
};
