#pragma once

#include <cstdint>

class edStore;

enum eAvatarCustomizable : int32_t {
    AC_Hair = 0,
    AC_FacePilosity = 1,
    AC_Eyebrows = 2,
    AC_Top = 3,
    AC_Bottom = 4,
    AC_Gloves = 5,
    AC_Shoes = 6,
    AC_Glasses = 7,
    AC_Helmet = 8,

    AC_Count = 9
};

static constexpr int32_t kNumClotheTypes = 2;
static constexpr int32_t kNoClotheSet = 3;
static constexpr int32_t kNumFaceParams = 31;
static constexpr int32_t kNumGenders = 2;
static constexpr uint8_t kDefaultFaceParam = 8;

struct AvatarConfiguration {
    uint16_t StyleID;
    uint16_t BodyID; // body mesh (MAN_BODY / WOMAN_BODY), not the skin tone
    uint16_t SkinID; // skin tone, resolved against the (procedural) skin DB
    uint16_t EyeID;
    uint16_t ClotheID[kNumClotheTypes][AC_Count]; // 0 - Regular 1 - Bike Only
    uint8_t ColorID[kNumClotheTypes][AC_Count];   // 0 - Regular 1 - Bike Only
    uint8_t FaceParams[kNumFaceParams];
    uint8_t bHashMotoGearsOnBike;

    AvatarConfiguration();
    bool isSameAs( const AvatarConfiguration &other ) const;
    bool deserialize( edStore &store );
    bool deserializePacked( edStore &store );
    void serializePacked( edStore &store ) const;

    void syncSharedSlots();
    bool hasMotoGears() const;

private:
    void deserializeClothes( edStore &store );
    void deserializeFaceParams( edStore &store );
};

bool IsHelmetClotheID( uint16_t id );
