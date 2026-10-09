#include "shared.h"
#include "mng_logo_display.h"

#include "database/gs_database.h"
#include "flash/flash_callbacks_register.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/game_mechanism_player.h"
#include "game/mng_hud.h"
#include "game/scenaric/entity/base_entity.h"
#include "game/scenaric/spots.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "render/2db.h"
#include "render/draw_list.h"
#include "render/material.h"
#include "world/gs_mng_resources.h"

MngLogoDisplay *gpMngLogoDisplay = nullptr; // DAT_010e7ef0

static constexpr const char *kDefaultLogoClipName = "logo";
static constexpr const char *kLogoAtlasMaterialName = "logos_brands";
static constexpr float kLogoCellWidth = 128.0f; // DAT_00f4176c
static constexpr float kLogoCellHeight = 64.0f; // DAT_00f416d8
static constexpr float kAlphaToByte = 2.55f;    // DAT_00f41ecc

static inline void StripTrailingSlash( char *pPath ) {
    const size_t length = strlen( pPath );
    if ( length != 0 && pPath[length - 1] == '/' ) {
        pPath[length - 1] = '\0';
    }
}

static inline void GetPathBasename( const char *pPath, char *pOutName ) {
    // FUN_00715850
    const char *pSeparator = strrchr( pPath, '/' );
    strcpy( pOutName, ( pSeparator != nullptr ) ? pSeparator + 1 : pPath );
}

static inline float GetClipAlpha( FlashClip &clip ) {
    // FUN_00441330
    return float(
               int32_t( clip.pSprite->getTransform()->Transform.MulTerm[3] ) *
               100 ) *
           ( 1.0f / 256.0f );
}

MngLogoDisplay::MngLogoDisplay() : Manager(), Bank() {
    // FUN_00715930
    for ( LogoEntry &logo : pLogoPaths ) {
        logo.pPath[0] = '\0';
        logo.pFlash = nullptr;
        logo.pBrand = nullptr;
    }
    gpMngLogoDisplay = this;
}

MngLogoDisplay::~MngLogoDisplay() {
    // FUN_007158d0
    gpMngLogoDisplay = nullptr;
}

void MngLogoDisplay::terminate() {
    // FUN_00716aa0
    gFlashCallbacksRegister.removeAllSynonyms(
        &MngLogoDisplay::DrawLogoCallback );

    for ( LogoEntry &logo : pLogoPaths ) {
        logo.pPath[0] = '\0';
        logo.pFlash = nullptr;
        logo.pBrand = nullptr;
    }
}

void MngLogoDisplay::setLogo( FlashPlayer *pFlash,
                              const char *pBrandName,
                              char *pPath ) {
    // FUN_007166e0
    if ( pFlash == nullptr || pBrandName[0] == '\0' ) {
        return;
    }

    char clipName[32];
    if ( pPath == nullptr ) {
        strcpy( clipName, kDefaultLogoClipName );
    } else {
        StripTrailingSlash( pPath );
        GetPathBasename( pPath, clipName );
    }

    int32_t freeSlot = -1;
    int32_t slot = -1;
    for ( int32_t i = 0; i < 4; i++ ) {
        LogoEntry &logo = pLogoPaths[i];
        if ( logo.pPath[0] == '\0' && logo.pFlash == nullptr &&
             freeSlot == -1 ) {
            freeSlot = i;
        }

        const bool bSamePath = ( pPath == nullptr )
                                   ? logo.pPath[0] == '\0'
                                   : strcmp( pPath, logo.pPath ) == 0;
        if ( bSamePath && logo.pFlash == pFlash ) {
            slot = i;
            break;
        }
    }
    if ( slot == -1 ) {
        slot = freeSlot;
    }
    if ( slot == -1 ) {
        return;
    }

    const GameDatabase &gameDB = gpDatabase->getGameDB();
    DBEntryBrand *pBrand = nullptr;
    for ( uint32_t i = 0; i < gameDB.pNumItems[6]; i++ ) {
        if ( strcmp( gameDB.DBBrand[i].BrandID.pAsString, pBrandName ) == 0 ) {
            pBrand = &gameDB.DBBrand[i];
            break;
        }
    }

    LogoEntry &logo = pLogoPaths[slot];
    logo.pBrand = pBrand;
    if ( logo.pBrand == nullptr ) {
        OTDU_LOG_DEBUG( "Can't find logo for brand %s\n", pBrandName );
        return;
    }

    if ( pPath == nullptr ) {
        logo.pPath[0] = '\0';
    } else {
        strcpy( logo.pPath, pPath );
    }
    logo.pFlash = pFlash;

    gFlashCallbacksRegister.remove( &MngLogoDisplay::DrawLogoCallback,
                                    clipName );
    gFlashCallbacksRegister.add( clipName, &MngLogoDisplay::DrawLogoCallback,
                                 this );
}

void MngLogoDisplay::clearLogo( FlashPlayer *pFlash, char *pPath ) {
    // FUN_00716960
    if ( pFlash == nullptr ) {
        return;
    }

    char clipName[32];
    if ( pPath == nullptr ) {
        strcpy( clipName, kDefaultLogoClipName );
    } else {
        StripTrailingSlash( pPath );
        GetPathBasename( pPath, clipName );
    }

    for ( LogoEntry &logo : pLogoPaths ) {
        const bool bSamePath = ( pPath == nullptr )
                                   ? logo.pPath[0] == '\0'
                                   : strcmp( pPath, logo.pPath ) == 0;
        if ( bSamePath && logo.pFlash == pFlash ) {
            gFlashCallbacksRegister.remove( &MngLogoDisplay::DrawLogoCallback,
                                            clipName );
            logo.pPath[0] = '\0';
            logo.pFlash = nullptr;
            logo.pBrand = nullptr;
            return;
        }
    }
}

void MngLogoDisplay::GetCarShopBrands( DBEntryBrand **ppOutBrands,
                                       const uint64_t spotHashcode ) {
    // FUN_00716b00
    memset( ppOutBrands, 0, sizeof( DBEntryBrand * ) * kMaxNumCarShopBrands );

    DBEntryCarShop *pCarShop = gpDatabase->getCarShopBySpotName( spotHashcode );
    if ( pCarShop == nullptr ) {
        return;
    }

    for ( int32_t i = 0; i < DBEntryCarShop::kNumSlots; i++ ) {
        if ( !gpDatabase->isCarShopSlotAvailable( pCarShop, i ) ) {
            continue;
        }

        DBEntryBrand *pBrand = reinterpret_cast<DBEntryCarPhysicsData *>(
                                   pCarShop->Slots[i].pAsRawPointer )
                                   ->Brand;

        // FUN_0042e860
        for ( int32_t j = 0; j < kMaxNumCarShopBrands; j++ ) {
            if ( ppOutBrands[j] == pBrand ) {
                break;
            }
            if ( ppOutBrands[j] == nullptr ) {
                ppOutBrands[j] = pBrand;
                break;
            }
        }
    }
}

DBEntryBrand *MngLogoDisplay::PickCarShopBrand( DBEntryBrand **ppBrands,
                                                const uint64_t spotHashcode,
                                                const float cycleTimer ) {
    // FUN_00716bf0
    GetCarShopBrands( ppBrands, spotHashcode );

    int32_t numBrands = 0;
    while ( numBrands < kMaxNumCarShopBrands &&
            ppBrands[numBrands] != nullptr ) {
        numBrands++;
    }
    if ( numBrands < 1 ) {
        return nullptr;
    }

    int32_t index = int32_t( cycleTimer ) % numBrands;
    if ( index < 0 || index >= kMaxNumCarShopBrands ) {
        index = 0;
    }
    return ppBrands[index];
}

Material *MngLogoDisplay::GetIconAtlasMaterial( const int32_t atlas ) {
    // FUN_007159a0
    LogoResources &logoResources = gpMngResources->getLogoResources();
    if ( !logoResources.MngResources::isLoaded() ) {
        return nullptr;
    }

    switch ( atlas ) {
    case 0:
        return logoResources.getMaterial( kLogoAtlasMaterialName );
    case 1:
        return logoResources.getMaterial( "icons_map" );
    case 2:
    case 3:
        return logoResources.getMaterial( "icons_map_lod" );
    default:
        return nullptr;
    }
}

Material *MngLogoDisplay::GetIconAtlasUVs( const int32_t atlas,
                                           const int32_t iconIndex,
                                           float &u0,
                                           float &u1,
                                           float &v0,
                                           float &v1 ) {
    // FUN_00715a00
    if ( !gpMngResources->getLogoResources().MngResources::isLoaded() ) {
        return nullptr;
    }

    Material *pAtlas = MngLogoDisplay::GetIconAtlasMaterial( atlas );
    float cellWidth = 0.0f;
    float cellHeight = 0.0f;
    switch ( atlas ) {
    case 0:
        cellWidth = kLogoCellWidth;
        cellHeight = kLogoCellHeight;
        break;
    case 1:
        cellWidth = 64.0f;
        cellHeight = 64.0f;
        break;
    case 2:
    case 3:
        cellWidth = 32.0f;
        cellHeight = 32.0f;
        break;
    default:
        break;
    }

    if ( pAtlas == nullptr ) {
        return nullptr;
    }

    const Texture *pAtlasTexture = pAtlas->getTexture( 0, 0 );
    if ( pAtlasTexture == nullptr ) {
        return nullptr;
    }

    const float atlasWidth = float( pAtlasTexture->Width );
    const float atlasHeight = float( pAtlasTexture->Height );
    const int32_t numColumns = int32_t( floorf( atlasWidth / cellWidth ) );
    const int32_t numRows = int32_t( floorf( atlasHeight / cellHeight ) );
    if ( iconIndex >= numRows * numColumns ) {
        return nullptr;
    }

    const int32_t row = int32_t( floor( double( iconIndex ) / numColumns ) );
    const int32_t column = iconIndex - row * numColumns;
    u0 = ( float( column ) * cellWidth ) / atlasWidth;
    u1 = ( float( column + 1 ) * cellWidth ) / atlasWidth;
    v0 = ( float( row ) * cellHeight ) / atlasHeight;
    v1 = ( float( row + 1 ) * cellHeight ) / atlasHeight;
    return pAtlas;
}

static Material *GetSpotIconAtlasUVs( const int32_t spotIcon,
                                      const int32_t iconSize,
                                      float &u0,
                                      float &u1,
                                      float &v0,
                                      float &v1 ) {
    // FUN_00715bb0
    switch ( iconSize ) {
    case 1:
        return MngLogoDisplay::GetIconAtlasUVs( 1, spotIcon + 0x40, u0, u1, v0,
                                                v1 );
    case 2:
        return MngLogoDisplay::GetIconAtlasUVs( 2, spotIcon, u0, u1, v0, v1 );
    case 3: {
        // Spot icon -> LOD icon
        static constexpr int32_t kLodIcons[36] = {
            0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
            0x28, 0x28, 0x29, 0x29, 0x2a, 0x2a, 0x2b, 0x2c, 0x2c,
            0x2d, 0x2d, 0x2e, 0x2e, 0x2e, 0x2f, 0x2f, 0x2f, 0x2f,
            0x30, 0x31, 0x1d, -1,   -1,   -1,   -1,   -1,   -1,
        };
        if ( spotIcon < 0 || spotIcon >= 36 || kLodIcons[spotIcon] == -1 ) {
            return nullptr;
        }
        return MngLogoDisplay::GetIconAtlasUVs( 3, kLodIcons[spotIcon], u0, u1,
                                                v0, v1 );
    }
    default:
        return nullptr;
    }
}

static inline Material *GetSizedIconAtlasUVs( const int32_t iconSize,
                                              const int32_t icon1,
                                              const int32_t icon2,
                                              const int32_t icon3,
                                              float &u0,
                                              float &u1,
                                              float &v0,
                                              float &v1 ) {
    switch ( iconSize ) {
    case 1:
        return MngLogoDisplay::GetIconAtlasUVs( 1, icon1, u0, u1, v0, v1 );
    case 2:
        return MngLogoDisplay::GetIconAtlasUVs( 2, icon2, u0, u1, v0, v1 );
    case 3:
        return MngLogoDisplay::GetIconAtlasUVs( 3, icon3, u0, u1, v0, v1 );
    default:
        return nullptr;
    }
}

static int32_t GetChallengeSpotIcon( const SpotMap *pSpot ) {
    const int32_t challengeKind = GetSpotChallengeKind( pSpot );
    switch ( GetSpotGameModeID( pSpot ) ) {
    case 0xc0c0:
        if ( challengeKind == 0 ) {
            return 0x16;
        }
        if ( challengeKind == 1 ) {
            return 0x14;
        }
        return ( challengeKind == 3 ) ? 0x15 : -1;
    case 0xc0c1:
        if ( challengeKind == 1 ) {
            return 0x12;
        }
        return ( challengeKind == 3 ) ? 0x13 : -1;
    case 0xc0c2:
        if ( challengeKind == 1 ) {
            return 0x10;
        }
        return ( challengeKind == 3 ) ? 0x11 : -1;
    default:
        return -1;
    }
}

Material *MngLogoDisplay::GetSpotIconUVs( const MinimapSpotIcon *pIcon,
                                          const int32_t iconSize,
                                          float &u0,
                                          float &u1,
                                          float &v0,
                                          float &v1 ) {
    // FUN_00716200
    if ( pIcon == nullptr ) {
        return nullptr;
    }

    switch ( pIcon->Info.Kind ) {
    case 1:
        if ( pIcon->isExtraChallenge() && pIcon->pEntity != nullptr ) {
            switch ( pIcon->pEntity->getType() ) {
            case eEntityType::ET_TRAN:
                return GetSizedIconAtlasUVs( iconSize, 0x5a, 0x1a, 0x2f, u0, u1,
                                             v0, v1 );
            case eEntityType::ET_SHOG:
                return GetSizedIconAtlasUVs( iconSize, 0x59, 0x19, 0x2f, u0, u1,
                                             v0, v1 );
            case eEntityType::ET_HIHI:
                return GetSizedIconAtlasUVs( iconSize, 0x58, 0x18, 0x2f, u0, u1,
                                             v0, v1 );
            case eEntityType::ET_CVOY:
                return GetSizedIconAtlasUVs( iconSize, 0x57, 0x17, 0x2f, u0, u1,
                                             v0, v1 );
            default:
                return nullptr;
            }
        }
        break;
    case 4:
        return GetSizedIconAtlasUVs( iconSize, 0x5b, 0x1b, 0x30, u0, u1, v0,
                                     v1 );
    case 8:
        return GetSizedIconAtlasUVs( iconSize, 0x4f, 0xf, 0x2b, u0, u1, v0,
                                     v1 );
    default:
        break;
    }

    const SpotMap *pSpot = pIcon->pSpot;
    if ( pSpot == nullptr ) {
        return nullptr;
    }

    const bool bBrandLogo =
        pIcon->pBrand != nullptr && ( iconSize == 0 || iconSize == 1 );

    int32_t spotIcon = -1;
    switch ( pSpot->Type ) {
    case ST_House:
        spotIcon = int32_t( pIcon->Info.SpotFlags & 1u ) | 0xa;
        break;
    case ST_CarDealer:
        if ( bBrandLogo ) {
            // FUN_00715d80
            return MngLogoDisplay::GetIconAtlasUVs(
                iconSize, pIcon->pBrand->LogoGlobalID, u0, u1, v0, v1 );
        }
        spotIcon = 1;
        break;
    case ST_BikeDealer:
        if ( bBrandLogo ) {
            // FUN_00715d80
            return MngLogoDisplay::GetIconAtlasUVs(
                iconSize, pIcon->pBrand->LogoGlobalID, u0, u1, v0, v1 );
        }
        spotIcon = 0;
        break;
    case ST_Goodies:
        spotIcon = 2;
        break;
    case ST_AfterMarket:
        spotIcon = 8;
        break;
    case ST_Club: {
        const Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();
        const uint64_t clubSpotHashcode =
            uint64_t( pDriver->TeamSpotID ) |
            ( uint64_t( pDriver->field4_0x24 ) << 32 );
        spotIcon = ( clubSpotHashcode == pSpot->Hashcode ) ? 0xc : 0xd;
    } break;
    case ST_DriveIn:
        spotIcon = 0xe;
        break;
    case ST_Challenge:
        spotIcon = GetChallengeSpotIcon( pSpot );
        break;
    case ST_Unknown8:
        switch ( pSpot->SubType ) {
        case 0:
            spotIcon = 9;
            break;
        case 1:
            spotIcon = 5;
            break;
        case 2:
        case 3:
            spotIcon = 6;
            break;
        case 4:
            spotIcon = 3;
            break;
        case 9:
            spotIcon = 4;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }

    if ( spotIcon == -1 ) {
        return nullptr;
    }
    return GetSpotIconAtlasUVs( spotIcon, iconSize, u0, u1, v0, v1 );
}

void MngLogoDisplay::DrawLogoCallback( FlashRECT * /*pViewport*/,
                                       float /*depth*/,
                                       DrawList *pDrawList,
                                       void *pUser,
                                       FlashResource *pResource,
                                       const char *pClipName ) {
    // FUN_00715e40
    MngLogoDisplay *pDisplay = static_cast<MngLogoDisplay *>( pUser );

    LogoEntry *pLogo = nullptr;
    char clipPath[512];
    if ( strcmp( pClipName, kDefaultLogoClipName ) == 0 ) {
        for ( LogoEntry &logo : pDisplay->pLogoPaths ) {
            if ( logo.pPath[0] == '\0' && logo.pFlash != nullptr ) {
                pLogo = &logo;
                break;
            }
        }
        if ( pLogo == nullptr ) {
            return;
        }

        char logoPathExt[512];
        pLogo->pFlash->getVariableAsString( "/:LogoPath", clipPath );
        pLogo->pFlash->getVariableAsString( "/:LogoPathExt", logoPathExt );
        strcat( clipPath, logoPathExt );
    } else {
        for ( LogoEntry &logo : pDisplay->pLogoPaths ) {
            if ( logo.pPath[0] == '\0' ) {
                continue;
            }

            char logoClipName[512];
            GetPathBasename( logo.pPath, logoClipName );
            if ( strcmp( pClipName, logoClipName ) == 0 ) {
                pLogo = &logo;
                break;
            }
        }
        if ( pLogo == nullptr ) {
            return;
        }

        strcpy( clipPath, pLogo->pPath );
    }

    FlashPlayer *pFlash = pLogo->pFlash;
    if ( pFlash == nullptr ||
         pFlash->getBoundFlashResource()->getDrawMode() == 2 ) {
        return;
    }

    FlashClip logoClip;
    if ( !pFlash->getClipPlayer( clipPath, &logoClip ) ) {
        return;
    }

    FlashRECT viewport;
    logoClip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );
    const float alpha = GetClipAlpha( logoClip ) * kAlphaToByte;

    if ( pLogo->pBrand == nullptr ) {
        return;
    }

    LogoResources &logoResources = gpMngResources->getLogoResources();
    Material *pAtlas = logoResources.getMaterial( kLogoAtlasMaterialName );
    if ( pAtlas == nullptr ) {
        return;
    }

    const Texture *pAtlasTexture = pAtlas->getTexture( 0, 0 );
    if ( pAtlasTexture == nullptr ) {
        return;
    }

    const float atlasWidth = float( pAtlasTexture->Width );
    const float atlasHeight = float( pAtlasTexture->Height );
    const int32_t numColumns = int32_t( floorf( atlasWidth / kLogoCellWidth ) );
    const int32_t numRows = int32_t( floorf( atlasHeight / kLogoCellHeight ) );

    const int32_t logoIndex = pLogo->pBrand->LogoGlobalID;
    if ( numColumns == 0 || logoIndex >= numRows * numColumns ) {
        return;
    }

    const int32_t row =
        int32_t( floorf( float( logoIndex ) / float( numColumns ) ) );
    const int32_t column = logoIndex - row * numColumns;

    pDrawList->setActiveMaterial( pAtlas );
    pDrawList->setDiffuse( 0, ( uint32_t( alpha ) << 24 ) | 0xffffffu );

    DrawGaugeQuad( float( column ) * kLogoCellWidth / atlasWidth,
                   float( row ) * kLogoCellHeight / atlasHeight,
                   float( column + 1 ) * kLogoCellWidth / atlasWidth,
                   float( row + 1 ) * kLogoCellHeight / atlasHeight, viewport.X,
                   viewport.Y, viewport.X + viewport.Width,
                   viewport.Y + viewport.Height, pResource );
}
