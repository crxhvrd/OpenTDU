#include "shared.h"
#include "gm_goodies.h"

#include "database/gs_database.h"
#include "flash/implementation/flash_player.h"
#include "game/avatar/avatar_clothes.h"
#include "game/avatar/avatar_part.h"
#include "game/avatar/gs_avatar.h"
#include "game/mng_music.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "world/details/gs_details.h"

static constexpr uint32_t kBikeClothesShopFlag = 1u << 2;

static inline DBEntryCarShop *GetCurrentShop() {
    SpotMap *pSpot = gpScenaric->getSpotList().getWorldSpotChecked(
        gpScenaric->getCurrentSpotIndex() );
    return ( pSpot != nullptr )
               ? gpDatabase->getCarShopBySpotName( pSpot->Hashcode )
               : nullptr;
}

static inline void SetDetailEnabled( const uint32_t detailHash,
                                     const bool bEnabled ) {
    TDUDetail *pDetail = gpDetails->getDetailByHash( detailHash );
    if ( pDetail != nullptr ) {
        pDetail->setEnabled( bEnabled );
    }
}

static inline int32_t GetNumColors( const AvatarClotheCursor &cursor ) {
    return static_cast<const AvatarClothePart *>( cursor.getResult() )
        ->getNumColors();
}

GMGoodies::GMGoodies()
    : GameModeWorldBase(), AvatarCatalogScene(), mngReflection() {
    // FUN_009d3a20
    setTransitionFlags( 0x27 );

    registerManager( &mngReflection );
}

GMGoodies::~GMGoodies() {
    // FUN_009d39b0
}

void GMGoodies::initialize() {
    // FUN_009d3c60
    GameModeWorldBase::initialize();

    DBEntryCarShop *pShop = GetCurrentShop();
    if ( pShop != nullptr ) {
        pBrandFilter = pShop->Label.pAsString;
    }

    AvatarCatalogScene::initialize();

    gpMngMusic->setPlayFlags( 1 );
}

void GMGoodies::reset() {
    // FUN_009d3910
    GameModeScene::reset();

    strcpy( frameLabel, "goodies" );
    strcpy( movieName, "SHOPS" );
    AvatarCatalogScene::reset();

    SetDetailEnabled( kDetailHashBirds, false );
    SetDetailEnabled( kDetailHashPapers, false );
}

void GMGoodies::tick( float deltaTime, float totalTime ) {
    // FUN_009d3c20
    GameModeScene::tick( deltaTime, totalTime );
    AvatarCatalogScene::tick( deltaTime, totalTime );
}

void GMGoodies::pause() {
    // FUN_009d3880
    SetDetailEnabled( kDetailHashBirds, true );
    SetDetailEnabled( kDetailHashPapers, true );

    AvatarCatalogScene::pause();
    GameModeScene::pause();
}

void GMGoodies::destroy() {
    // FUN_009d3aa0
    AvatarCatalogScene::destroy();
    GameModeWorldBase::destroy();
}

bool GMGoodies::onMessage( FlashMessage *pMessage ) {
    // FUN_009d3af0
    AvatarCatalogScene::onMessage( pMessage );

    switch ( *pMessage ) {
    case FlashMessage( "sbdd" ):
        updateShopTitle();
        return false;
    case FlashMessage( "tryo" ):
        return true;
    case FlashMessage( "buit" ): {
        const int32_t price =
            pFlashPlayer->getVariableAsInt( "/:CurrentPrice" );
        const int32_t money = pFlashPlayer->getVariableAsInt( "/:money" );
        const bool bBought =
            pFlashPlayer->getVariableAsInt( "/:IsItemBought" ) != 0;
        pFlashPlayer->setVariableValue(
            "/:IsBuyOK", ( price <= money && !bBought ) ? 1 : 0 );
        return true;
    }
    case FlashMessage( "clin" ):
    case FlashMessage( "neit" ):
    case FlashMessage( "prit" ):
        return true;
    default:
        return false;
    }
}

void GMGoodies::updateShopTitle() {
    // FUN_009d3330
    DBEntryCarShop *pShop = GetCurrentShop();
    if ( pShop == nullptr ) {
        return;
    }

    if ( ( pShop->Flags & kBikeClothesShopFlag ) != 0 ) {
        pFlashPlayer->setVariableValueNonLinked( "/:WhatClothes", "bike" );
    }
    pFlashPlayer->setVariableValueNonLinked( "/:Title", "Error" );

    const GameDatabase &gameDB = gpDatabase->getGameDB();
    for ( uint32_t i = 0; i < gameDB.pNumItems[6]; i++ ) {
        const DBEntryBrand &brand = gameDB.DBBrand[i];
        if ( strcmp( brand.BrandID.pAsString, pShop->Label.pAsString ) == 0 ) {
            pFlashPlayer->setVariableValueNonLinked(
                "/:Title", brand.BrandNameID.pAsString );
            return;
        }
    }
}

void GMGoodies::onInteriorReady() {
    // FUN_009d3850
    AvatarCatalogScene::onInteriorReady();
    installCinematics( scene.getSourceSpot(), &scene.getBank() );
}

void GMGoodies::stepItem( const int32_t clotheSet,
                          const int32_t category,
                          const FlashMessage &message ) {
    // FUN_009d3440
    if ( category == kNoCategory || activeCursors[category] == nullptr ) {
        return;
    }

    AvatarClotheCursor *pCursor = activeCursors[category];
    uint8_t *pColorIndex = activeColors[category];

    if ( message == FlashMessage( "neit" ) ) {
        ( *pColorIndex )++;
        if ( GetNumColors( *pCursor ) <= *pColorIndex ) {
            pCursor->next();
            resetColor( getActiveItem( category ), pColorIndex );
        }
    } else if ( message == FlashMessage( "prit" ) ) {
        if ( *pColorIndex == 0 ) {
            pCursor->previous();
            *pColorIndex = static_cast<uint8_t>( GetNumColors( *pCursor ) - 1 );
        } else {
            ( *pColorIndex )--;
        }
    } else {
        const AvatarClothePart *pWorn = gpAvatar->findClotheByID(
            pAvatar->getPooledEntry()->pClothesHC[clotheSet][category] );
        if ( pWorn != nullptr && getActiveItem( category ) != pWorn ) {
            resetColor( getActiveItem( category ), pColorIndex );
        }
    }

    applyActiveItem( clotheSet, category );
}

int32_t GMGoodies::countItems( AvatarPartDB *pDB,
                               const int32_t gender,
                               const int32_t category,
                               const char *pBrand,
                               const int32_t clotheSet,
                               int32_t *pOutNumPlaceholders ) {
    // FUN_009d3710
    if ( pOutNumPlaceholders != nullptr ) {
        *pOutNumPlaceholders = 0;
    }
    if ( pBrand == nullptr ) {
        return 0;
    }

    AvatarBrandClotheCursor cursor(
        pDB, gender, static_cast<eAvatarCustomizable>( category ), clotheSet,
        pBrand );
    cursor.findRange();
    if ( pDB->getCount() <= 0 || !cursor.hasRange() ) {
        return 0;
    }

    int32_t numItems = 0;
    while ( !cursor.isAtEnd() ) {
        if ( pOutNumPlaceholders != nullptr &&
             IsPlaceholderMesh( cursor.getResult()->getMeshName() ) ) {
            numItems++;
            ( *pOutNumPlaceholders )++;
        } else {
            numItems += GetNumColors( cursor );
        }
        cursor.next();
    }
    return numItems;
}

int32_t GMGoodies::indexOfItem( AvatarPartDB *pDB,
                                const AvatarClothePart *pPart,
                                const int32_t clotheSet ) {
    // FUN_009d35f0
    AvatarBrandClotheCursor cursor( pDB, pPart->Gender, pPart->Category,
                                    clotheSet, pPart->getBrandName() );
    cursor.findRange();
    if ( pDB->getCount() <= 0 || !cursor.hasRange() ) {
        return 0;
    }

    int32_t index = 0;
    while ( !cursor.isAtEnd() ) {
        if ( cursor.getResult() == pPart ) {
            return index + 1 + *activeColors[pPart->Category];
        }
        index += GetNumColors( cursor );
        cursor.next();
    }
    return index;
}
