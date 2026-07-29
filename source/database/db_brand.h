#pragma once

struct DBEntryBrand {
    uint64_t Hashcode;
    DBReference BrandID;
    DBReference BrandNameID;
    DBReference LogoHUDFile;
    int32_t LogoGlobalID;
    int32_t BrandSpecialParam; // Used by BrandCar/GSBrandCar
    uint32_t Flags;
};
