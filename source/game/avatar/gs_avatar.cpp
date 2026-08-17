#include "shared.h"
#include "gs_avatar.h"

#include "config/gs_config.h"
#include "database/gs_database.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/gs_scenaric.h"

// Index of TDU_Clothes in GameDatabase::pNumItems (kDatabasesToLoad).
static constexpr int32_t kClotheDatabaseIndex = 9;

uint64_t gHashcodeNoMesh = 0;
uint64_t gHashcodeArms = 0;
uint64_t gHashcodeEyes = 0;
uint64_t gHashcodeFeet = 0;
uint64_t gHashcodeHands = 0;
uint64_t gHashcodeHead = 0;
uint64_t gHashcodeLegs = 0;
uint64_t gHashcodeSexe = 0;
uint64_t gHashcodeTeeth = 0;
uint64_t gHashcodeTorso = 0;
uint64_t gHashcodeHideable = 0;

float gAvatarInCarLightFactor[6] = {};       // DAT_010c44d8
float gAvatarConvertibleLightFactor[6] = {}; // DAT_010c44c0
float gAvatarOutCarLightFactor[6] = {};      // DAT_010c44f0

GSAvatar *gpAvatar = nullptr; // DAT_010e7460

static eAvatarCustomizable GetClotheCategory( const char *pPartType ) {
    // FUN_008f35f0
    if ( strncmp( pPartType, "PART_HAIR", 10 ) == 0 ) {
        return AC_Hair;
    }
    if ( strncmp( pPartType, "PART_PILOSITY", 14 ) == 0 ) {
        return AC_FacePilosity;
    }
    if ( strncmp( pPartType, "PART_EYEBROWS", 14 ) == 0 ) {
        return AC_Eyebrows;
    }
    if ( strncmp( pPartType, "PART_TOP", 9 ) == 0 ) {
        return AC_Top;
    }
    if ( strncmp( pPartType, "PART_BOTTOM", 12 ) == 0 ) {
        return AC_Bottom;
    }
    if ( strncmp( pPartType, "PART_HANDS", 11 ) == 0 ) {
        return AC_Gloves;
    }
    if ( strncmp( pPartType, "PART_SHOES", 11 ) == 0 ) {
        return AC_Shoes;
    }
    if ( strncmp( pPartType, "PART_GLASSES", 13 ) == 0 ) {
        return AC_Glasses;
    }
    if ( strncmp( pPartType, "PART_HELMET", 12 ) == 0 ) {
        return AC_Helmet;
    }
    // A "both" garment covers the top and the bottom; it is categorized as a
    // top and flagged separately (see below).
    if ( strncmp( pPartType, "PART_BOTH", 10 ) == 0 ) {
        return AC_Top;
    }
    return AC_Count;
}

static eAvatarClotheLength GetClotheLength( const char *pLength ) {
    // FUN_008f3720
    if ( strncmp( pLength, "SHORT_CLOTHE", 13 ) == 0 ) {
        return ACL_Short;
    }
    if ( strncmp( pLength, "LONG_CLOTHE", 12 ) == 0 ) {
        return ACL_Long;
    }
    if ( strncmp( pLength, "NO_LENGHT", 10 ) == 0 ) {
        return ACL_None;
    }
    return ACL_Invalid;
}

static eAvatarDrivingType GetDrivingType( const char *pDrivingType ) {
    // FUN_008f3530
    if ( strncmp( pDrivingType, "Car", 4 ) == 0 ) {
        return ADT_Car;
    }
    if ( strncmp( pDrivingType, "Bike", 5 ) == 0 ) {
        return ADT_Bike;
    }
    if ( strncmp( pDrivingType, "Mixte", 6 ) == 0 ) {
        return ADT_Mixte;
    }
    return ADT_Invalid;
}

static bool ConvertClotheDBEntry( int32_t rowIndex,
                                  const DBEntryClothe &row,
                                  AvatarClothePart &outPart ) {
    // FUN_008f3770
    if ( ( row.Flags & 1 ) == 0 ) {
        return false;
    }

    outPart.IDHashcode = static_cast<uint16_t>( row.NumID );
    outPart.Index = rowIndex;
    outPart.Category = GetClotheCategory( row.PartType.pAsString );

    outPart.Gender = 2;
    if ( strncmp( row.Sex.pAsString, "Male", 5 ) == 0 ) {
        outPart.Gender = 0;
    } else if ( strncmp( row.Sex.pAsString, "Female", 7 ) == 0 ) {
        outPart.Gender = 1;
    }

    outPart.Length = GetClotheLength( row.Length.pAsString );

    outPart.Size = ACS_Any;
    if ( strncmp( row.Size.pAsString, "Small", 6 ) == 0 ) {
        outPart.Size = ACS_Small;
    } else if ( strncmp( row.Size.pAsString, "Large", 6 ) == 0 ) {
        outPart.Size = ACS_Large;
    }

    outPart.Price = row.Price;
    outPart.DrivingType = GetDrivingType( row.DrivingType.pAsString );

    outPart.bIsBodyPart =
        ( outPart.Category == AC_Hair || outPart.Category == AC_FacePilosity ||
          outPart.Category == AC_Eyebrows || outPart.Category == AC_Gloves ||
          outPart.Category == AC_Helmet || outPart.Category == AC_Glasses );

    if ( strncmp( row.PartType.pAsString, "PART_BOTH", 10 ) == 0 ) {
        outPart.bIsPartBoth = true;
    }

    return true;
}

GSAvatar::GSAvatar()
    : GameSystem(),
      pClothesDB( nullptr ),
      pDefaultPartsDB( nullptr ),
      pBodyDB( nullptr ),
      pBodyLODDB( nullptr ),
      pStyleDB( nullptr ),
      pBodyMorphDB( nullptr ),
      pBeltDB( nullptr ),
      pEyeDB( nullptr ),
      maxClotheID( 0 ),
      faceParameterCount( 0 ) {
    // FUN_008f6f00
    OTDU_UNIMPLEMENTED;
    gpAvatar = this;
}

GSAvatar::~GSAvatar() {
    // FUN_008f6de0
    OTDU_UNIMPLEMENTED;
    gpAvatar = nullptr;
}

bool GSAvatar::initialize( TestDriveGameInstance * ) {
    // FUN_008f7d10
    buildClothesDB();
    buildDefaultPartsDB();
    buildBodyDB();
    buildBodyMorphDB();
    buildBodyLODDB();
    buildStyleDB();
    buildBeltDB();
    buildEyeDB();
    loadConfigBanks();

    GSScenaric::LoadScenaricBank( "scenarics/Avatars", "facemodifset",
                                  &faceParametersBank );

    OTDU_UNIMPLEMENTED;

    pool.initialize();

    return true;
}

void GSAvatar::tick( float totalTime, float deltaTime ) {
    // FUN_008f7df0
    pool.tick();
}

void GSAvatar::draw( float deltaTime ) {
    // FUN_008f6a10
    OTDU_UNIMPLEMENTED;
}

void GSAvatar::terminate() {
    // FUN_008f7950
    pool.terminate();

    OTDU_UNIMPLEMENTED;
}

void GSAvatar::readAvatarConfig( const EntityAvatar *pAvatar,
                                 AvatarConfiguration &outConfig ) {
    // FUN_007f76d0
    const PooledAvatarEntry *pRsc = pAvatar->pPooledAvatarEntry;
    if ( pRsc == nullptr ) {
        return;
    }

    if ( pRsc->pStylePart != nullptr ) {
        outConfig.StyleID = pRsc->pStylePart->IDHashcode;
    }
    if ( pRsc->pSkinResource != nullptr ) {
        outConfig.SkinID = pRsc->pSkinResource->IDHashcode;
    }
    if ( pRsc->pEyeResource != nullptr ) {
        outConfig.EyeID = pRsc->pEyeResource->IDHashcode;
    }

    for ( int s = 0; s < kNumClotheTypes; ++s ) {
        for ( int p = 0; p < AC_Count; ++p ) {
            outConfig.ClotheID[s][p] = pRsc->pClothesHC[s][p];
            outConfig.ColorID[s][p] = pRsc->pColors[s][p];
        }
    }

    OTDU_UNIMPLEMENTED;

    outConfig.bHashMotoGearsOnBike = pRsc->bHasMotoGearsOnBike;
}

bool GSAvatar::loadFaceModifSet( AvatarConfiguration &outConfig,
                                 const char *pFileConfigName ) {
    // FUN_008f6050
    OTDU_UNIMPLEMENTED_ONCE;
    return false;
}

bool GSAvatar::applyPNJConfiguration( AvatarConfiguration &outConfig,
                                      const DBEntryPNJ &pnjEntry ) {
    // FUN_008f6110
    if ( !loadFaceModifSet( outConfig, pnjEntry.FileConfigID.pAsString ) ) {
        return false;
    }

    // Every clothe slot of the PNJ references a TDU_Clothes row; the part worn
    // is that row's NumID, and it is used for both clothe sets.
    struct PNJClothe {
        const DBReference &Part;
        const int32_t &Color;
        eAvatarCustomizable Slot;
    };

    const PNJClothe pnjClothes[6] = {
        {pnjEntry.Tops,    pnjEntry.TopColor,     AC_Top    },
        {pnjEntry.Bottoms, pnjEntry.BottomColor,  AC_Bottom },
        {pnjEntry.Shoes,   pnjEntry.ShoesColor,   AC_Shoes  },
        {pnjEntry.Gloves,  pnjEntry.GlovesColor,  AC_Gloves },
        {pnjEntry.Glasses, pnjEntry.GlassesColor, AC_Glasses},
        {pnjEntry.Helmets, pnjEntry.HelmetColor,  AC_Helmet },
    };

    for ( const PNJClothe &clothe : pnjClothes ) {
        const DBEntryClothe *pClothe =
            static_cast<const DBEntryClothe *>( clothe.Part.pAsRawPointer );

        for ( int32_t set = 0; set < kNumClotheTypes; set++ ) {
            outConfig.ClotheID[set][clothe.Slot] =
                static_cast<uint16_t>( pClothe->NumID );
            outConfig.ColorID[set][clothe.Slot] =
                static_cast<uint8_t>( clothe.Color );
        }
    }

    return true;
}

void GSAvatar::installStyleResource( EntityAvatar *pAvatar,
                                     const AvatarConfiguration *pConfig ) {
    // FUN_008f6610
    if ( pStyleDB == nullptr ) {
        return;
    }
    AvatarGenericCursor cursor( pStyleDB );
    cursor.findRange();
    if ( cursor.hasRange() && cursor.findById( pConfig->StyleID ) ) {
        pAvatar->pPooledAvatarEntry->pStylePart =
            reinterpret_cast<AvatarStylePart *>( cursor.getResult() );
    }
}

void GSAvatar::installSkinResource( EntityAvatar *pAvatar,
                                    const AvatarConfiguration *pConfig ) {
    // FUN_008f67c0
    OTDU_UNIMPLEMENTED;
}

void GSAvatar::installBodyResource( EntityAvatar *pAvatar,
                                    const AvatarConfiguration *pConfig ) {
    // FUN_008f6ba0
    OTDU_UNIMPLEMENTED;
}

void GSAvatar::installEyeResource( EntityAvatar *pAvatar,
                                   const AvatarConfiguration *pConfig ) {
    // FUN_008f6b10
    if ( pEyeDB == nullptr ) {
        return;
    }
    AvatarGenericCursor cursor( pEyeDB );
    cursor.findRange();
    if ( cursor.hasRange() && cursor.findById( pConfig->EyeID ) ) {
        pAvatar->pPooledAvatarEntry->pEyeResource =
            (AvatarEyePart *)cursor.getResult();
        OTDU_UNIMPLEMENTED;
    }
}

void GSAvatar::installBodySkin( EntityAvatar *pAvatar,
                                const AvatarConfiguration *pConfig ) {
    // FUN_008f66a0
    OTDU_UNIMPLEMENTED;
}

void GSAvatar::installClothes( EntityAvatar *pAvatar,
                               int32_t bodyType,
                               bool bForceReskin,
                               bool bForceFullBake ) {
    // FUN_008f6870
    if ( pAvatar == nullptr || pAvatar->pPooledAvatarEntry == nullptr ) {
        return;
    }

    PooledAvatarEntry *pRsc = pAvatar->pPooledAvatarEntry;
    pRsc->ActiveClotheSet = bodyType;

    for ( int i = 0; i < AC_Count; ++i ) {
        OTDU_UNIMPLEMENTED; // TODO: AVATAR — clothe DB lookup
        (void)bForceReskin;
        (void)bForceFullBake;
    }
}

void GSAvatar::bakeAvatar( EntityAvatar *pAvatar,
                           const AvatarConfiguration *pConfig,
                           int32_t bodyType,
                           bool bForceReskin,
                           bool bForceFullBake ) {
    // FUN_008f6c20
    if ( pAvatar == nullptr || pConfig == nullptr ) {
        return;
    }

    PooledAvatarEntry *pRsc = pAvatar->pPooledAvatarEntry;
    if ( pRsc == nullptr ) {
        return;
    }

    AvatarConfiguration currentConfig;
    readAvatarConfig( pAvatar, currentConfig );

    if ( currentConfig.isSameAs( *pConfig ) &&
         pRsc->ActiveClotheSet == bodyType ) {
        return;
    }

    for ( int i = 0; i < AC_Count; ++i ) {
        pRsc->Slots[i].decreaseReferenceCount();
    }

    pRsc->decreaseRenderRef();
    pRsc->pUnknown = nullptr;
    pRsc->decreaseRenderRef();

    installStyleResource( pAvatar, pConfig );
    installSkinResource( pAvatar, pConfig );
    installBodyResource( pAvatar, pConfig );
    installEyeResource( pAvatar, pConfig );
    installBodySkin( pAvatar, pConfig );
    installClothes( pAvatar, bodyType, bForceReskin, bForceFullBake );

    OTDU_UNIMPLEMENTED;
    pAvatar->bIsToUpdate = true;

    bool bMotoHelmet = IsHelmetClotheID( pConfig->ClotheID[0][AC_Top] ) ||
                       IsHelmetClotheID( pConfig->ClotheID[0][AC_Bottom] );

    pRsc->bHasMotoGearsOnBike =
        bMotoHelmet ? true : ( pConfig->bHashMotoGearsOnBike != 0 );
}

void GSAvatar::buildClothesDB() {
    // FUN_008f40d0
    const GameDatabase &gameDB = gpDatabase->getGameDB();

    const int32_t numRows =
        static_cast<int32_t>( gameDB.pNumItems[kClotheDatabaseIndex] );
    const DBEntryClothe *pRows = ( numRows != 0 ) ? gameDB.DBClothe : nullptr;

    pClothesDB = new AvatarPartDB();
    pClothesDB->reserve( numRows + 1, sizeof( AvatarClothePart ) );

    if ( pRows == nullptr ) {
        return;
    }

    for ( int32_t i = 0; i < numRows; i++ ) {
        AvatarClothePart part = {};
        part.Gender = 2;
        part.Category = AC_Count;
        part.Length = ACL_Invalid;
        part.Size = ACS_Any;
        part.DrivingType = ADT_Invalid;
        part.Index = -1;

        if ( !ConvertClotheDBEntry( i, pRows[i], part ) ) {
            continue;
        }

        if ( part.IDHashcode > maxClotheID ) {
            maxClotheID = part.IDHashcode;
        }

        AvatarClothePart *pSlot =
            static_cast<AvatarClothePart *>( pClothesDB->addSlot() );
        if ( pSlot == nullptr ) {
            break;
        }
        *pSlot = part;
    }
}

void GSAvatar::buildDefaultPartsDB() {
    // FUN_008f5120
    // clang-format off
    static const struct {
        uint16_t ID;
        int32_t Gender;
        eAvatarCustomizable Category;
        eAvatarDrivingType DrivingType;
        eAvatarClotheLength Length;
        eAvatarClotheSize Size;
        int32_t Price;
        const char *pMeshName;
        const char *pDisplayName;
        const char *pBrandName;
        int32_t BrandID;
    } kDefaultParts[] = {
        {1125, 0, AC_Hair,         ADT_Mixte, ACL_None,  ACS_Small, 99, "M_NO_HAIR",                "SHAVED HAIR",        "COMMON",     0},
        {1132, 0, AC_FacePilosity, ADT_Mixte, ACL_None,  ACS_Small, 99, "M_NO_PILOSITY",            "NO PILOSITY",        "COMMON",     0},
        {1145, 0, AC_Eyebrows,     ADT_Mixte, ACL_None,  ACS_Small, 99, "M_EYEBROWS01_COMMON",      "EYEBROWS 4",         "COMMON",     4},
        {200,  0, AC_Glasses,      ADT_Car,   ACL_None,  ACS_Small, 1,  "M_SUN_GLASSES_ELLSON",     "LUNETTES DE SOLEIL", "ELLSON",     1},
        {0,    0, AC_Top,          ADT_Car,   ACL_Long,  ACS_Small, 1,  "M_BLOUSON_ECKO",           "BLOUSON",            "ECKO",       3},
        {20,   0, AC_Bottom,       ADT_Car,   ACL_Long,  ACS_Small, 1,  "M_BAGGY_ECKO",             "BAGGY",              "ECKO",       2},
        {850,  0, AC_Gloves,       ADT_Car,   ACL_Short, ACS_Small, 1,  "M_LEATHER_MITT_SEMPORIO",  "MITTAINES DE CUIR",  "SEMPORIO",   2},
        {35,   0, AC_Shoes,        ADT_Car,   ACL_Long,  ACS_Small, 1,  "M_BASKET_ECKO",            "BASKETS",            "ECKO",       1},
        {273,  0, AC_Helmet,       ADT_Bike,  ACL_Long,  ACS_Small, 1,  "M_HELMET_RED_RABBIT",      "HELMET",             "RED_RABBIT", 8},
        {277,  0, AC_Top,          ADT_Bike,  ACL_Long,  ACS_Small, 1,  "M_MOTO_TOP_RED_RABBIT",    "BLOUSON DE MOTO",    "RED_RABBIT", 5},
        {275,  0, AC_Bottom,       ADT_Bike,  ACL_Long,  ACS_Small, 1,  "M_MOTO_BOTTOM_RED_RABBIT", "MOTO TROUSERS",      "RED_RABBIT", 5},
        {276,  0, AC_Gloves,       ADT_Bike,  ACL_Long,  ACS_Small, 1,  "M_MOTO_GLOVES_RED_RABBIT", "GANTS DE MOTO",      "RED_RABBIT", 5},
        {274,  0, AC_Shoes,        ADT_Bike,  ACL_Long,  ACS_Small, 1,  "M_MOTO_BOOT_RED_RABBIT",   "MOTO SHOES",         "RED_RABBIT", 5},
        {1127, 1, AC_Hair,         ADT_Mixte, ACL_None,  ACS_Small, 99, "W_LONG_HAIR01_COMMON",     "LONG HAIR 2",        "COMMON",     7},
        {1131, 1, AC_FacePilosity, ADT_Mixte, ACL_None,  ACS_Small, 99, "W_NO_PILOSITY",            "NO PILOSITY",        "COMMON",     0},
        {1154, 1, AC_Eyebrows,     ADT_Mixte, ACL_None,  ACS_Small, 99, "W_EYEBROWS01_COMMON",      "EYEBROWS 3",         "COMMON",     4},
        {520,  1, AC_Glasses,      ADT_Car,   ACL_None,  ACS_Small, 1,  "W_SUN_GLASSES01_FOXX",     "LUNETTES DE SOLEIL", "FOXX",       7},
        {84,   1, AC_Top,          ADT_Car,   ACL_Short, ACS_Small, 1,  "W_TANK_ECKO",              "TANK",               "ECKO",       3},
        {103,  1, AC_Bottom,       ADT_Car,   ACL_Long,  ACS_Small, 1,  "W_JEANS_LOW_ECKO",         "JEANS TAILLE BASSE", "ECKO",       1},
        {919,  1, AC_Gloves,       ADT_Car,   ACL_Short, ACS_Small, 1,  "W_LEATHER_MITT_SEMPORIO",  "MITTAINES DE CUIR",  "SEMPORIO",   2},
        {112,  1, AC_Shoes,        ADT_Car,   ACL_Long,  ACS_Small, 1,  "W_BASKET_ECKO",            "BASKETS",            "ECKO",       1},
        {350,  1, AC_Helmet,       ADT_Bike,  ACL_Long,  ACS_Small, 1,  "W_HELMET_RED_RABBIT",      "HELMET",             "RED_RABBIT", 8},
        {354,  1, AC_Top,          ADT_Bike,  ACL_Long,  ACS_Small, 1,  "W_MOTO_TOP_RED_RABBIT",    "BLOUSON DE MOTO",    "RED_RABBIT", 5},
        {352,  1, AC_Bottom,       ADT_Bike,  ACL_Long,  ACS_Small, 1,  "W_MOTO_BOTTOM_RED_RABBIT", "MOTO TROUSERS",      "RED_RABBIT", 5},
        {353,  1, AC_Gloves,       ADT_Bike,  ACL_Long,  ACS_Small, 1,  "W_MOTO_GLOVES_RED_RABBIT", "GANTS DE MOTO",      "RED_RABBIT", 5},
        {351,  1, AC_Shoes,        ADT_Bike,  ACL_Long,  ACS_Small, 1,  "W_MOTO_BOOT_RED_RABBIT",   "MOTO SHOES",         "RED_RABBIT", 5},
    };
    // clang-format on

    pDefaultPartsDB = new AvatarPartDB();
    pDefaultPartsDB->reserve( 0x1a, sizeof( AvatarDefaultPart ) );

    for ( const auto &desc : kDefaultParts ) {
        AvatarDefaultPart *pPart =
            static_cast<AvatarDefaultPart *>( pDefaultPartsDB->addSlot() );
        if ( pPart == nullptr ) {
            break;
        }

        pPart->IDHashcode = desc.ID;
        pPart->Gender = desc.Gender;
        pPart->Category = desc.Category;
        pPart->Length = desc.Length;
        pPart->Size = desc.Size;
        pPart->DrivingType = desc.DrivingType;
        pPart->Price = desc.Price;
        pPart->bIsBodyPart =
            ( desc.Category == AC_Hair || desc.Category == AC_FacePilosity ||
              desc.Category == AC_Eyebrows || desc.Category == AC_Gloves ||
              desc.Category == AC_Helmet || desc.Category == AC_Glasses );
        pPart->bIsPartBoth = false;
        pPart->Index = -1;
        pPart->pMeshName = desc.pMeshName;
        pPart->pDisplayName = desc.pDisplayName;
        pPart->pBrandName = desc.pBrandName;
        pPart->BrandID = desc.BrandID;
    }
}

void GSAvatar::buildBodyDB() {
    // FUN_008f4810
    // clang-format off
    static const struct {
        uint16_t ID;
        int32_t Gender;
        const char *Name;
    } kBodies[] = {
        {0, 0, "MAN_BODY"  },
        {1, 1, "WOMAN_BODY"},
    };

    pBodyDB = new AvatarPartDB();
    pBodyDB->reserve( 2, sizeof( AvatarNamedPartPart ) );
    for ( const auto &body : kBodies ) {
        AvatarNamedPartPart *pPart =
            static_cast<AvatarNamedPartPart *>( pBodyDB->addSlot() );
        if ( pPart == nullptr ) {
            break;
        }
        pPart->IDHashcode = body.ID;
        pPart->Gender = body.Gender;
        pPart->pName = body.Name;
    }
}

void GSAvatar::buildBodyMorphDB() {
    // FUN_008f4e60
    OTDU_UNIMPLEMENTED;
}

void GSAvatar::buildBodyLODDB() {
    // FUN_008f4770
    // clang-format off
    static const struct {
        uint16_t ID;
        int32_t Gender;
        const char *Name;
    } kBodies[] = {
        {0, 1, "WOMAN_LOD"},
        {1, 0, "MAN_LOD"  },
    };

    pBodyLODDB = new AvatarPartDB();
    pBodyLODDB->reserve( 2, sizeof( AvatarNamedPartPart ) );
    for ( const auto &body : kBodies ) {
        AvatarNamedPartPart *pPart =
            static_cast<AvatarNamedPartPart *>( pBodyLODDB->addSlot() );
        if ( pPart == nullptr ) {
            break;
        }
        pPart->IDHashcode = body.ID;
        pPart->Gender = body.Gender;
        pPart->pName = body.Name;
    }
}

void GSAvatar::buildStyleDB() {
    // FUN_008f4510
    // clang-format off
    static const struct {
        uint16_t ID;
        int32_t Gender;
        uint32_t NameHashcode;
    } kStyles[] = {
        {0,  0, 0x0340c2cf},
        {1,  0, 0x0350050f},
        {2,  0, 0x035f474f},
        {3,  0, 0x036e898f},
        {4,  0, 0x037dcbcf},
        {5,  0, 0x038d0e0f},
        {6,  0, 0x039c504f},
        {7,  0, 0x03ab928f},
        {8,  1, 0x03bad4cf},
        {9,  1, 0x0333072f},
        {10, 1, 0x0342496f},
        {11, 1, 0x03518baf},
    };

    pStyleDB = new AvatarPartDB();
    pStyleDB->reserve( 0xc, sizeof( AvatarStylePart ) );
    for ( const auto &style : kStyles ) {
        AvatarStylePart *pPart =
            static_cast<AvatarStylePart *>( pStyleDB->addSlot() );
        if ( pPart == nullptr ) {
            break;
        }
        pPart->IDHashcode = style.ID;
        pPart->Gender = style.Gender;
        pPart->NameHashcode = style.NameHashcode;
    }
}

void GSAvatar::buildBeltDB() {
    // FUN_008f3970
    OTDU_UNIMPLEMENTED;
}

void GSAvatar::buildEyeDB() {
    // FUN_008f4200
    static const uint64_t kEyeNameHC[8] = {
        0xda33, 0xde1b, 0xe203, 0xe5eb, 0xe9d3, 0xedbb, 0xf1a3, 0xf58b,
    };

    pEyeDB = new AvatarPartDB();
    pEyeDB->reserve( 0x10, sizeof( AvatarEyePart ) );

    for ( int32_t pass = 0; pass < 4; ++pass ) {
        const uint64_t nameA = kEyeNameHC[pass * 2];
        const uint64_t nameB = kEyeNameHC[pass * 2 + 1];
        const float uvX = pass * 0.25f;
        const uint16_t baseID = static_cast<uint16_t>( pass * 2 );

        const struct {
            uint16_t ID;
            int32_t Gender;
            uint64_t NameHC;
            float uvY;
        } entries[4] = {
            {baseID,                              0, nameA, 0.0f},
            {static_cast<uint16_t>( baseID + 8 ), 1, nameA, 0.0f},
            {static_cast<uint16_t>( baseID + 1 ), 0, nameB, 0.5f},
            {static_cast<uint16_t>( baseID + 9 ), 1, nameB, 0.5f},
        };

        for ( const auto &entry : entries ) {
            AvatarEyePart *pPart =
                static_cast<AvatarEyePart *>( pEyeDB->addSlot() );
            if ( pPart == nullptr ) {
                break;
            }
            pPart->IDHashcode = entry.ID;
            pPart->Gender = entry.Gender;
            pPart->NameHC = entry.NameHC;
            pPart->UVOffset[0] = uvX;
            pPart->UVOffset[1] = entry.uvY;
            pPart->UVOffset[2] = 0.0f;
            pPart->UVOffset[3] = 0.0f;
        }
    }
}

void GSAvatar::loadConfigBanks() {
    // FUN_008f7c70
    std::string bankPath = gpConfig->getResRootPath();
    bankPath += "Avatar/CONFIG/STYLES.bnk";
    styleBank.loadBootBank( bankPath.c_str(), nullptr );

    bankPath = gpConfig->getResRootPath();
    bankPath += "Avatar/CONFIG/PNJ.bnk";
    pnjBank.loadBootBank( bankPath.c_str(), nullptr );

    buildDefaultConfig();
}

void GSAvatar::buildDefaultConfig() {
    // FUN_008f4c10
    pDefaultConfig[0].StyleID = 0;
    pDefaultConfig[0].BodyID = 0;
    pDefaultConfig[0].SkinID = 0;
    pDefaultConfig[0].EyeID = 0;
    pDefaultConfig[1].StyleID = 8;
    pDefaultConfig[1].BodyID = 1;
    pDefaultConfig[1].SkinID = 0x14;
    pDefaultConfig[1].EyeID = 8;

    for ( int32_t gender = 0; gender < kNumGenders; gender++ ) {
        AvatarConfiguration &config = pDefaultConfig[gender];

        for ( int32_t set = 0; set < kNumClotheTypes; set++ ) {
            for ( int32_t slot = 0; slot < AC_Count; slot++ ) {
                AvatarGenericCursor cursor( pDefaultPartsDB );
                cursor.findRange();
                if ( !cursor.hasRange() ) {
                    continue;
                }

                for ( ; !cursor.isAtEnd(); cursor.advance() ) {
                    const AvatarClothePart *pPart =
                        static_cast<const AvatarClothePart *>(
                            cursor.getResult() );
                    if ( pPart == nullptr || pPart->Gender != gender ||
                         pPart->Category != slot ) {
                        continue;
                    }
                    if ( pPart->DrivingType != ADT_Mixte &&
                         pPart->DrivingType != set ) {
                        continue;
                    }

                    config.ClotheID[set][slot] = pPart->IDHashcode;
                    break;
                }
            }
        }

        memset( config.FaceParams, kDefaultFaceParam,
                sizeof( config.FaceParams ) );

        static constexpr eAvatarCustomizable kSharedSlots[] = {
            AC_Hair, AC_FacePilosity, AC_Glasses, AC_Eyebrows };
        for ( const eAvatarCustomizable slot : kSharedSlots ) {
            config.ClotheID[1][slot] = config.ClotheID[0][slot];
            config.ColorID[1][slot] = config.ColorID[0][slot];
        }

        config.ClotheID[0][AC_Helmet] = config.ClotheID[1][AC_Helmet];
        config.ColorID[0][AC_Helmet] = config.ColorID[1][AC_Helmet];
    }
}
