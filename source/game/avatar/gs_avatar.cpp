#include "shared.h"
#include "gs_avatar.h"

#include "game/scenaric/entity/entity_avatar.h"

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

GSAvatar *gpAvatar = nullptr; // DAT_010e7460

GSAvatar::GSAvatar()
    : GameSystem(),
      pClothesDB( nullptr ),
      pHairDB( nullptr ),
      pBodyDB( nullptr ),
      pBodyLODDB( nullptr ),
      pStyleDB( nullptr ),
      pBodyMorphDB( nullptr ),
      pBeltDB( nullptr ),
      pEyeDB( nullptr ),
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
    buildBodyDB();
    buildBodyLODDB();
    buildStyleDB();
    buildEyeDB();
    OTDU_UNIMPLEMENTED;
    return true;
}

void GSAvatar::tick( float totalTime, float deltaTime ) {
    // FUN_008f7df0
    OTDU_UNIMPLEMENTED;
}

void GSAvatar::draw( float deltaTime ) {
    // FUN_008f6a10
    OTDU_UNIMPLEMENTED;
}

void GSAvatar::terminate() {
    // FUN_008f7950
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
    OTDU_UNIMPLEMENTED;
}

void GSAvatar::buildHairDB() {
    // FUN_008f5120
    OTDU_UNIMPLEMENTED;
}

void GSAvatar::buildBodyDB() {
    // FUN_008f4810
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
    OTDU_UNIMPLEMENTED;
}
