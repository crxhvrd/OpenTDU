#include "shared.h"
#include "mng_logo_display.h"

#include "database/gs_database.h"
#include "flash/flash_callbacks_register.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/mng_hud.h"
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
