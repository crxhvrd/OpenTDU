#pragma once

struct DBEntryClothe {
    uint64_t Hashcode;
    uint64_t BankReference;
    int32_t NumID;
    int32_t Price;
    DBReference Brand;
    DBReference ClotheName;
    DBReference Sex;
    DBReference DrivingType;
    DBReference Length;
    DBReference PartType;
    DBReference Size;
    int32_t NumColors;
    uint32_t Flags;
};
