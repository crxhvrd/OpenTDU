#pragma once

struct DBEntryInterior {
    uint64_t Hashcode;
    uint64_t BrandID;
    DBReference InteriorName;
    DBReference InteriorColorID1;
    DBReference InteriorColorID2;
    DBReference Material;
    int32_t Price;
};
