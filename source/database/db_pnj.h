#pragma once

struct DBEntryPNJ {
    uint64_t Hashcode;
    DBReference ConfigID;
    DBReference Sex;
    DBReference FileConfigID;
    DBReference Tops;
    DBReference Bottoms;
    DBReference Shoes;
    DBReference Gloves;
    DBReference Glasses;
    DBReference Helmets;
    int32_t TopColor;
    int32_t BottomColor;
    int32_t ShoesColor;
    int32_t GlovesColor;
    int32_t GlassesColor;
    int32_t HelmetColor;
    uint32_t Flags;
};
