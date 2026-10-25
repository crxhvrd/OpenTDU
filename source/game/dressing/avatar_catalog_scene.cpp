#include "shared.h"
#include "avatar_catalog_scene.h"

#include "tdu_instance.h"

#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "core/maths_helper.h"
#include "flash/mng_flash_localize.h"
#include "game/avatar/avatar_clothes.h"
#include "game/avatar/avatar_part.h"
#include "game/avatar/gs_avatar.h"
#include "game/scenaric/avatar_manager.h"
#include "game/scenaric/entity/entity_anm_set.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/entity/entity_camera.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/scenaric/entity/entity_factory.h"
#include "game/scenaric/gs_scenaric.h"
#include "player_data/achievement_checks.h"
#include "player_data/achievements_data.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_avatar.h"
#include "player_data/saved_extra_challenges.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"

static constexpr const char *kDressingInteriorBank = "Interior/IDR_A.bnk";
static constexpr const char *kDefaultCatalogInteriorBank = "Interior/IAS_A.bnk";

bool AvatarCatalogScene::IsPlaceholderMesh( const char *pMeshName ) {
    // FUN_008f3320
    static constexpr const char *kPlaceholderMeshes[] = {
        "M_NO_GLOVES",   "W_NO_GLOVES",  "M_NO_HELMET", "W_NO_HELMET",
        "M_NO_GLASSES",  "W_NO_GLASSES", "M_NO_HAIR",   "M_NO_PILOSITY",
        "W_NO_PILOSITY", "NO_MESH" };

    for ( const char *pPlaceholder : kPlaceholderMeshes ) {
        if ( strcmp( pPlaceholder, pMeshName ) == 0 ) {
            return true;
        }
    }
    return false;
}

const char *GetBrandVariableName( const char *pBrandName ) {
    // FUN_008f3240
    static constexpr const char *kBrands[][2] = {
        {"ECKO",       "Brand00"},
        {"ELLSON",     "Brand01"},
        {"RED_RABBIT", "Brand02"},
        {"FOXX",       "Brand03"},
        {"HCICK",      "Brand04"},
        {"SHERMAN",    "Brand05"},
        {"SEMPORIO",   "Brand06"},
        {"J_GAZZARA",  "Brand07"}
    };

    for ( const auto &brand : kBrands ) {
        if ( strcmp( pBrandName, brand[0] ) == 0 ) {
            return brand[1];
        }
    }
    return "none";
}

static inline AvatarClothePart *GetCursorItem( AvatarClotheCursor *pCursor ) {
    // FUN_0040ee30
    return static_cast<AvatarClothePart *>( pCursor->getResult() );
}

AvatarCatalogScene::AvatarCatalogScene()
    : scene(),
      interiorBank(),
      heelLayerBank(),
      heelLayer(),
      helmetLayer(),
      pFlashPlayer( nullptr ),
      movieName{},
      frameLabel{},
      helpParameter{},
      pBrandFilter( nullptr ),
      pAvatar( nullptr ),
      bSavedSeating( false ),
      bIgnoreOwnership( false ),
      pClothesDB( nullptr ),
      cursors{},
      activeCursors{},
      colors{},
      activeColors{} {
    // FUN_008f9310
}

AvatarCatalogScene::~AvatarCatalogScene() {
    // FUN_008f9280
}

RenderAnm *AvatarCatalogScene::findHeelAnim() {
    static constexpr uint32_t kHeelAnimID = 0x13;

    const ScenaricBank::eBankState state = heelLayerBank.getState();
    if ( state == ScenaricBank::BBS_Unused ||
         state == ScenaricBank::BBS_LoadFailed ) {
        return nullptr;
    }

    EntityAnmSet *pAnmSet = static_cast<EntityAnmSet *>(
        heelLayerBank.findEntity( eEntityType::ET_ANMS, "HeelLayer" ) );
    return ( pAnmSet != nullptr ) ? pAnmSet->findAnimation( kHeelAnimID )
                                  : nullptr;
}

void AvatarCatalogScene::createCursors() {
    for ( int32_t category = 0; category < AC_Count; category++ ) {
        for ( int32_t clotheSet = 0; clotheSet < kNumClotheTypes;
              clotheSet++ ) {
            for ( int32_t gender = 0; gender < 2; gender++ ) {
                cursors[gender][clotheSet][category] = createCursor(
                    pClothesDB, category, gender, pBrandFilter, clotheSet );
            }
        }
    }
}

void AvatarCatalogScene::releaseCursors() {
    for ( auto &genderCursors : cursors ) {
        for ( auto &setCursors : genderCursors ) {
            for ( AvatarClotheCursor *&pCursor : setCursors ) {
                delete pCursor;
                pCursor = nullptr;
            }
        }
    }
}

void AvatarCatalogScene::initialize() {
    // FUN_008f99a0
    pAvatar = gpScenaric->getAvatarManager().allocateAvatar( ~0ull, 1 );
    if ( pAvatar != nullptr ) {
        const AvatarConfiguration *pConfig =
            TestDrive::GetSavedAvatarConfiguration();
        const int32_t activeClotheSet =
            pAvatar->getPooledEntry()->ActiveClotheSet;
        if ( activeClotheSet == kNoClotheSet ) {
            gpAvatar->bakeAvatar( pAvatar, pConfig, 0, false, false );
        } else if ( activeClotheSet != 0 ) {
            gpAvatar->installClothes( pAvatar, pConfig, 0, false, false );
        }
    }
    pAvatar->addInstanceReference();

    pClothesDB = gpAvatar->getClothesDB();
    createCursors();

    EntityDriver *pDriver = pAvatar->getDriver();
    bSavedSeating = pDriver->isSeating();
    pDriver->setSeating( false );

    GSScenaric::LoadScenaricBank( "Scenarics/Avatars", "heellayer",
                                  &heelLayerBank );

    RenderAnm *pHeelAnim = findHeelAnim();
    if ( pHeelAnim != nullptr ) {
        heelLayer.setHeelAnim( pHeelAnim );
    }

    if ( pAvatar != nullptr ) {
        pAvatar->addLayer( &heelLayer );
        heelLayer.enable();
        pAvatar->addLayer( &helmetLayer );
        helmetLayer.enable();
    }

    loadInteriorBank();
}

void AvatarCatalogScene::loadInteriorBank() {
    if ( !interiorBank.isLoaded() ) {
        char bankPath[512] = {};
        if ( gpTestDriveInstance->getActiveGameMode() == GM_Dressing ) {
            snprintf( bankPath, sizeof( bankPath ), "%s%s",
                      gpConfig->getResRootPath(), kDressingInteriorBank );
        } else if ( !BuildInteriorBankPath( GM_Invalid, bankPath ) ) {
            snprintf( bankPath, sizeof( bankPath ), "%s%s",
                      gpConfig->getResRootPath(), kDefaultCatalogInteriorBank );
        }

        if ( interiorBank.loadBank( gpFile, bankPath ) ) {
            interiorBank.loadContentAsync();
        }
    }

    if ( interiorBank.isLoaded() ) {
        scene.enter( &interiorBank, false );
    }
}

void AvatarCatalogScene::destroy() {
    // FUN_008f9150
    if ( pAvatar != nullptr ) {
        pAvatar->removeLayer( &heelLayer );
        pAvatar->removeLayer( &helmetLayer );
    }

    releaseCursors();

    pAvatar->getDriver()->setSeating( bSavedSeating );
    pAvatar->removeInstanceReference();
    gpScenaric->getAvatarManager().releaseAvatar( pAvatar );

    gpScenaric->unloadBank( &heelLayerBank );
    scene.leave();
    interiorBank.unload();
}

void AvatarCatalogScene::reset() {
    // FUN_008f9000
    pFlashPlayer = gpFlash->getFlashPlayer( movieName );
    if ( pFlashPlayer != nullptr ) {
        pFlashPlayer->showFrame( frameLabel, "/", true );
        pFlashPlayer->setVariableValue( "/:IsChangeColor", 1 );
        syncCursorsToAvatar( pAvatar );
    }
}

void AvatarCatalogScene::pause() {
    // FUN_008f8f80
    pAvatar->setEnvMap( false, nullptr );
}

void AvatarCatalogScene::tick( float deltaTime, float totalTime ) {
    // FUN_008f9970
    if ( !scene.isEntered() ) {
        loadInteriorBank();
    }

    scene.tick();
    if ( scene.getState() == SceneInterior::SIS_Ready ) {
        onInteriorReady();
    }
    refreshChangeColorVariable();
}

void AvatarCatalogScene::onInteriorReady() {
    // FUN_008f8fb0
    setInteriorEnvMap( pAvatar );
}

void AvatarCatalogScene::setInteriorEnvMap( EntityAvatar *pTargetAvatar ) {
    Render2DB *pCubeMap =
        scene.getBankBitmap( GetIdentifier64bit( "CUBECAR" ) );
    if ( pCubeMap != nullptr && pTargetAvatar != nullptr ) {
        pTargetAvatar->setEnvMap( false, pCubeMap );
    }
}

bool AvatarCatalogScene::onMessage( FlashMessage *pMessage ) {
    // FUN_008f96f0
    switch ( *pMessage ) {
    case FlashMessage( "enav" ):
        gpScenaric->getIntroTutorial().onDressingFinished();
        return true;
    case FlashMessage( "drhe" ):
        updateHelpVariable( getCurrentCategory() );
        return true;
    case FlashMessage( "drin" ):
        resetToSavedOutfit();
        updateIsDressVariable();
        return true;
    case FlashMessage( "avou" ):
        gpAvatar->savePlayerAvatarConfiguration( pAvatar );
        gpPlayerData->requestSave( 2 );
        pFlashPlayer->stopClip( "/" );
        return true;
    case FlashMessage( "buit" ):
        updateItemVariables( getCurrentCategory() );
        if ( buyActiveItem( getCurrentCategory() ) ) {
            gpAvatar->savePlayerAvatarConfiguration( pAvatar );
        }
        return true;
    case FlashMessage( "came" ):
        setGameCamera( findSelectedCamera() );
        return true;
    case FlashMessage( "nsit" ):
        restoreSavedItem( getCurrentClotheSet(), getCurrentCategory() );
        updateIsDressVariable();
        return true;
    case FlashMessage( "neco" ): {
        const int32_t category = getCurrentCategory();
        if ( category < kNoCategory ) {
            applyNextColor( getCurrentClotheSet(), category );
        }
        updateItemVariables( category );
        return true;
    }
    case FlashMessage( "seit" ):
        gpAvatar->savePlayerAvatarConfiguration( pAvatar );
        updateIsDressVariable();
        return true;
    case FlashMessage( "clin" ):
        syncCursorsToAvatar( pAvatar );
        [[fallthrough]];
    case FlashMessage( "neit" ):
    case FlashMessage( "prit" ): {
        const int32_t category = getCurrentCategory();
        if ( category < kNoCategory ) {
            stepItem( getCurrentClotheSet(), category, *pMessage );
        }
        updateItemVariables( category );
        updateIsDressVariable();
        return true;
    }
    default:
        return false;
    }
}

int32_t AvatarCatalogScene::getCurrentCategory() {
    // FUN_008f84c0
    char itemCategory[32];
    pFlashPlayer->getVariableAsString( "/:WhatItemCat", itemCategory );

    int32_t category = kNoCategory;
    if ( strcmp( itemCategory, "tops" ) == 0 ) {
        category = AC_Top;
    }
    if ( strcmp( itemCategory, "bottoms" ) == 0 ) {
        category = AC_Bottom;
    }
    if ( strcmp( itemCategory, "shoes" ) == 0 ) {
        category = AC_Shoes;
    }
    if ( strcmp( itemCategory, "acc" ) == 0 ) {
        const int32_t activeClotheSet =
            pAvatar->getPooledEntry()->ActiveClotheSet;
        if ( activeClotheSet == 0 ) {
            category = AC_Glasses;
        }
        if ( activeClotheSet == 1 ) {
            category = AC_Helmet;
        }
    }
    if ( strcmp( itemCategory, "hands" ) == 0 ) {
        category = AC_Gloves;
    }
    return category;
}

int32_t AvatarCatalogScene::getCurrentClotheSet() {
    // FUN_008f8460
    char clotheSet[32];
    pFlashPlayer->getVariableAsString( "/:WhatClothes", clotheSet );
    if ( strcmp( clotheSet, "default" ) == 0 ) {
        return 0;
    }
    return ( strcmp( clotheSet, "bike" ) == 0 ) ? 1 : 0;
}

int32_t
AvatarCatalogScene::getAvatarGender( EntityAvatar *pTargetAvatar ) const {
    return pTargetAvatar->getPooledEntry()->Body.getAvatarPart()->Gender;
}

AvatarClothePart *
AvatarCatalogScene::getActiveItem( const int32_t category ) const {
    AvatarClotheCursor *pCursor = activeCursors[category];
    return ( pCursor != nullptr ) ? GetCursorItem( pCursor ) : nullptr;
}

AvatarClotheCursor *
AvatarCatalogScene::createCursor( AvatarPartDB *pDB,
                                  const int32_t category,
                                  const int32_t gender,
                                  const char *pBrand,
                                  const int32_t clotheSet ) {
    // FUN_008f8590
    if ( pBrand == nullptr ) {
        return nullptr;
    }

    AvatarClotheCursor *pCursor = new AvatarBrandClotheCursor(
        pDB, gender, static_cast<eAvatarCustomizable>( category ), clotheSet,
        pBrand );
    pCursor->findRange();
    if ( pDB->getCount() > 0 && pCursor->hasRange() ) {
        return pCursor;
    }

    delete pCursor;
    return nullptr;
}

void AvatarCatalogScene::syncCursorsToAvatar( EntityAvatar *pTargetAvatar ) {
    // FUN_008f8600
    PooledAvatarEntry *pEntry = pTargetAvatar->getPooledEntry();
    const int32_t activeClotheSet = pEntry->ActiveClotheSet;
    const int32_t gender = getAvatarGender( pTargetAvatar );

    for ( int32_t category = 0; category < AC_Count; category++ ) {
        activeCursors[category] = cursors[gender][activeClotheSet][category];
        activeColors[category] = &colors[gender][activeClotheSet][category];
    }

    for ( int32_t category = 0; category < AC_Count; category++ ) {
        for ( int32_t clotheSet = 0; clotheSet < kNumClotheTypes;
              clotheSet++ ) {
            AvatarClotheCursor *pCursor = cursors[gender][clotheSet][category];
            if ( pCursor == nullptr ) {
                continue;
            }

            const AvatarClothePart *pWorn = gpAvatar->findClotheByID(
                pEntry->pClothesHC[clotheSet][category] );
            if ( pWorn == nullptr || pWorn->Gender != gender ||
                 pWorn->Category != category ||
                 ( pWorn->DrivingType != clotheSet &&
                   pWorn->DrivingType != ADT_Mixte ) ) {
                continue;
            }

            const bool bListed =
                ( pBrandFilter == nullptr )
                    ? ( bIgnoreOwnership || pWorn->isOwned() )
                    : strcmp( pWorn->getBrandName(), pBrandFilter ) == 0;
            if ( bListed ) {
                pCursor->seekTo( pWorn->IDHashcode );
                colors[gender][clotheSet][category] =
                    pEntry->pColors[clotheSet][category];
            }
        }
    }
}

void AvatarCatalogScene::stepItem( const int32_t clotheSet,
                                   const int32_t category,
                                   const FlashMessage &message ) {
    // FUN_008f82a0
    if ( category == kNoCategory || activeCursors[category] == nullptr ) {
        return;
    }

    const bool bNext = message == FlashMessage( "neit" );
    const bool bPrevious = message == FlashMessage( "prit" );
    if ( bNext || bPrevious ) {
        if ( bNext ) {
            activeCursors[category]->next();
        }
        if ( bPrevious ) {
            activeCursors[category]->previous();
        }
        if ( activeColors[category] != nullptr ) {
            resetColor( getActiveItem( category ), activeColors[category] );
        }
    } else {
        const AvatarClothePart *pWorn = gpAvatar->findClotheByID(
            pAvatar->getPooledEntry()->pClothesHC[clotheSet][category] );
        if ( pWorn != nullptr && getActiveItem( category ) != pWorn ) {
            resetColor( getActiveItem( category ), activeColors[category] );
        }
    }

    applyActiveItem( clotheSet, category );
}

void AvatarCatalogScene::applyActiveItem( const int32_t clotheSet,
                                          const int32_t category ) {
    // FUN_008f7e50
    if ( pAvatar == nullptr || activeCursors[category] == nullptr ) {
        return;
    }

    AvatarClothePart *pItem = getActiveItem( category );
    PooledAvatarEntry *pEntry = pAvatar->getPooledEntry();
    if ( pEntry != nullptr ) {
        pEntry->installClothePart( pItem, *activeColors[category], clotheSet,
                                   true, true );
    }
}

bool AvatarCatalogScene::applyNextColor( const int32_t clotheSet,
                                         const int32_t category ) {
    // FUN_008f8220
    PooledAvatarEntry *pEntry = pAvatar->getPooledEntry();
    const AvatarClothePart *pWorn =
        gpAvatar->findClotheByID( pEntry->pClothesHC[clotheSet][category] );
    const uint8_t numColors = pEntry->Slots[category].NumColors;
    if ( activeColors[category] == nullptr || pWorn == nullptr ||
         numColors == 0 ) {
        return false;
    }

    if ( nextColor( pWorn, activeColors[category], numColors ) ) {
        applyActiveItem( clotheSet, category );
    }
    return true;
}

void AvatarCatalogScene::restoreSavedItem( const int32_t clotheSet,
                                           const int32_t category ) {
    // FUN_008f83e0
    IPlayerData *pLoadedSave = gpPlayerData->getLoadedSave();
    if ( pLoadedSave == nullptr || pLoadedSave->getAvatar() == nullptr ) {
        return;
    }

    const AvatarConfiguration &savedConfig = pLoadedSave->getAvatar()->Config;
    AvatarClothePart *pSaved =
        gpAvatar->findClotheByID( savedConfig.ClotheID[clotheSet][category] );
    if ( pSaved == nullptr ) {
        return;
    }

    PooledAvatarEntry *pEntry = pAvatar->getPooledEntry();
    if ( pEntry != nullptr ) {
        pEntry->installClothePart( pSaved,
                                   savedConfig.ColorID[clotheSet][category],
                                   clotheSet, true, true );
    }
    syncCursorsToAvatar( pAvatar );
}

int32_t AvatarCatalogScene::countItems( AvatarPartDB *pDB,
                                        int32_t gender,
                                        int32_t category,
                                        const char *pBrand,
                                        int32_t clotheSet,
                                        int32_t *pOutNumPlaceholders ) {
    // FUN_008f7f60
    OTDU_UNIMPLEMENTED; // TODO: GMGoodies
    return 0;
}

int32_t AvatarCatalogScene::indexOfItem( AvatarPartDB *pDB,
                                         const AvatarClothePart *pPart,
                                         int32_t clotheSet ) {
    // FUN_008f7ea0
    OTDU_UNIMPLEMENTED; // TODO: GMGoodies
    return 0;
}

bool AvatarCatalogScene::resetColor( const AvatarClothePart *pPart,
                                     uint8_t *pColorIndex ) {
    // FUN_008f7e30
    *pColorIndex = 0;
    return true;
}

bool AvatarCatalogScene::nextColor( const AvatarClothePart *pPart,
                                    uint8_t *pColorIndex,
                                    const uint8_t numColors ) {
    // FUN_008f7e00
    if ( numColors == 0 ) {
        return false;
    }

    *pColorIndex = static_cast<uint8_t>( ( *pColorIndex + 1 ) % numColors );
    return true;
}

void AvatarCatalogScene::resetToSavedOutfit() {
    // FUN_008f90a0
    const int32_t clotheSet = getCurrentClotheSet();
    gpAvatar->bakeAvatar( pAvatar, TestDrive::GetSavedAvatarConfiguration(),
                          clotheSet, false, true );
    syncCursorsToAvatar( pAvatar );
    updateItemCounts();
}

bool AvatarCatalogScene::buyActiveItem( const int32_t category ) {
    // FUN_008f95b0
    if ( category == kNoCategory || activeCursors[category] == nullptr ) {
        return false;
    }

    AvatarClothePart *pItem = getActiveItem( category );
    const uint8_t colorIndex = *activeColors[category];
    if ( pItem == nullptr ) {
        return false;
    }

    IPlayerData *pLoadedSave = gpPlayerData->getLoadedSave();
    Driver *pDriver = pLoadedSave->getDriver();
    if ( static_cast<int32_t>( pDriver->getNumCoupons() -
                               static_cast<uint32_t>( pItem->Price ) ) < 0 ) {
        return false;
    }

    if ( !pItem->isColorOwned( colorIndex ) ) {
        pLoadedSave->getExtraChallenges()->countClotheBought();
        pDriver->spendCoupons( static_cast<uint32_t>( pItem->Price ) );
        pFlashPlayer->setVariableValue( "/:Solded", 1 );
        pItem->setColorOwned( colorIndex );
        pLoadedSave->getAchievements()->notify(
            ACF_Challenge | ACF_Road | ACF_Collection, false );
    }

    applyActiveItem( pAvatar->getPooledEntry()->ActiveClotheSet, category );
    return true;
}

void AvatarCatalogScene::refreshChangeColorVariable() {
    // FUN_008f8f30
    const int32_t category = getCurrentCategory();
    if ( category == kNoCategory ) {
        return;
    }

    const bool bCanChangeColor =
        pAvatar->getPooledEntry()->Slots[category].NumColors > 1;
    pFlashPlayer->setVariableValue( "/:IsChangeColor",
                                    bCanChangeColor ? 1 : 0 );
}

void AvatarCatalogScene::updateItemVariables( const int32_t category ) {
    // FUN_008f8cc0
    AvatarClothePart *pItem = nullptr;
    if ( category < kNoCategory && pClothesDB != nullptr &&
         activeCursors[category] != nullptr ) {
        pItem = getActiveItem( category );
    }

    if ( pItem == nullptr ) {
        pFlashPlayer->setVariableValue( "/:CurrentItemText", "" );
        pFlashPlayer->setVariableValue( "/:Num_Item", 0 );
        pFlashPlayer->setVariableValue( "/:Nb_Items", 0 );
        pFlashPlayer->setVariableValue( "/:WhatBrand", "none" );
        pFlashPlayer->setVariableValue( "/:CurrentPrice", 0 );
        pFlashPlayer->setVariableValue( "/:Solded", 1 );
        pFlashPlayer->setVariableValue( "/:IsEnoughMoney", 0 );
        pFlashPlayer->setVariableValue( "/:StyleDescription", "" );
        return;
    }

    const int32_t activeClotheSet = pAvatar->getPooledEntry()->ActiveClotheSet;
    pFlashPlayer->setVariableValue( "/:CurrentItemText",
                                    pItem->getDisplayName() );
    pFlashPlayer->setVariableValue(
        "/:Num_Item", indexOfItem( pClothesDB, pItem, activeClotheSet ) );
    pFlashPlayer->setVariableValue(
        "/:Nb_Items", countItems( pClothesDB, pItem->Gender, category,
                                  pBrandFilter, activeClotheSet, nullptr ) );
    pFlashPlayer->setVariableValue(
        "/:WhatBrand", GetBrandVariableName( pItem->getBrandName() ) );
    pFlashPlayer->setVariableValue( "/:CurrentPrice", pItem->Price );
    pFlashPlayer->setVariableValue(
        "/:Solded", pItem->isColorOwned( *activeColors[category] ) ? 1 : 0 );

    const uint32_t coupons =
        gpPlayerData->getLoadedSave()->getDriver()->getNumCoupons();
    const bool bEnoughMoney =
        static_cast<int32_t>( coupons -
                              static_cast<uint32_t>( pItem->Price ) ) >= 0;
    pFlashPlayer->setVariableValue( "/:IsEnoughMoney", bEnoughMoney ? 1 : 0 );
    pFlashPlayer->setVariableValue( "/:StyleDescription", "" );
}

void AvatarCatalogScene::updateItemCounts() {
    // FUN_008f8ad0
    static constexpr const char *kCountVariables[] = {
        "/:Nb_Tops", "/:Nb_Bottoms", "/:Nb_Hands", "/:Nb_Shoes" };

    PooledAvatarEntry *pEntry = pAvatar->getPooledEntry();
    const int32_t gender = getAvatarGender( pAvatar );

    for ( int32_t i = 0; i < 4; i++ ) {
        int32_t numPlaceholders = 0;
        const int32_t numItems =
            countItems( pClothesDB, gender, AC_Top + i, pBrandFilter,
                        pEntry->ActiveClotheSet, &numPlaceholders );
        pFlashPlayer->setVariableValue( kCountVariables[i],
                                        numItems - numPlaceholders );
    }

    if ( pEntry->ActiveClotheSet == 0 || pEntry->ActiveClotheSet == 1 ) {
        const int32_t accessoryCategory =
            ( pEntry->ActiveClotheSet == 0 ) ? AC_Glasses : AC_Helmet;
        int32_t numPlaceholders = 0;
        const int32_t numItems =
            countItems( pClothesDB, gender, accessoryCategory, pBrandFilter,
                        pEntry->ActiveClotheSet, &numPlaceholders );
        pFlashPlayer->setVariableValue( "/:Nb_Acc",
                                        numItems - numPlaceholders );
    }
}

void AvatarCatalogScene::updateIsDressVariable() {
    // FUN_008f8e80
    PooledAvatarEntry *pEntry = pAvatar->getPooledEntry();
    const AvatarClothePart *pTop = gpAvatar->findClotheByID(
        pEntry->pClothesHC[pEntry->ActiveClotheSet][AC_Top] );
    if ( pTop != nullptr ) {
        pFlashPlayer->setVariableValue(
            "/:IsDress", static_cast<int32_t>( pTop->bIsPartBoth ) );
    }
}

void AvatarCatalogScene::updateHelpVariable( const int32_t category ) {
    // FUN_008f94e0
    int32_t numItems = 0;
    if ( category < kNoCategory && activeCursors[category] != nullptr ) {
        int32_t numPlaceholders = 0;
        numItems = countItems( pClothesDB, getActiveItem( category )->Gender,
                               category, pBrandFilter,
                               pAvatar->getPooledEntry()->ActiveClotheSet,
                               &numPlaceholders );
        numItems -= numPlaceholders;
    }

    // FUN_008f8060
    static constexpr uint64_t kHelpItems[] = {
        0x3a996b2, 0x3b8d8f2, 0x3310b52, 0x3404d92, 0x34f8fd2, 0x35ed212 };
    static constexpr uint64_t kHelpNoItems[] = {
        0x36e1452, 0x37d5692, 0x38c98d2, 0x39bdb12, 0x3ab1d52, 0x3ba5f92 };
    const uint64_t *pHelp = ( numItems != 0 ) ? kHelpItems : kHelpNoItems;

    char itemCategory[32];
    pFlashPlayer->getVariableAsString( "/:WhatItemCat", itemCategory );

    uint64_t helpHashcode = 0;
    if ( strcmp( itemCategory, "tops" ) == 0 ) {
        helpHashcode = pHelp[0];
    }
    if ( strcmp( itemCategory, "bottoms" ) == 0 ) {
        helpHashcode = pHelp[1];
    }
    if ( strcmp( itemCategory, "shoes" ) == 0 ) {
        helpHashcode = pHelp[3];
    }
    if ( strcmp( itemCategory, "acc" ) == 0 ) {
        const int32_t activeClotheSet =
            pAvatar->getPooledEntry()->ActiveClotheSet;
        if ( activeClotheSet == 0 ) {
            helpHashcode = pHelp[2];
        }
        if ( activeClotheSet == 1 ) {
            helpHashcode = pHelp[5];
        }
    }
    if ( strcmp( itemCategory, "hands" ) == 0 ) {
        helpHashcode = pHelp[4];
    }

    if ( helpHashcode == 0 ) {
        pFlashPlayer->setVariableValue( "/:dressing_help", "Error" );
        return;
    }

    snprintf( helpParameter, sizeof( helpParameter ), "%d", numItems );
    gpMngFlashLocalize->setVarLocalized( pFlashPlayer, "/:dressing_help",
                                         helpHashcode, helpParameter );
}

EntityCamera *AvatarCatalogScene::findSelectedCamera() {
    // FUN_008f8a80
    char cameraName[16];
    pFlashPlayer->getVariableAsString( "/:WhatCam", cameraName );
    return static_cast<EntityCamera *>(
        scene.getBank().findEntity( eEntityType::ET__CAM, cameraName ) );
}

void AvatarCatalogScene::setGameCamera( const EntityCamera *pCameraEntity ) {
    // FUN_008f8930
    static constexpr int32_t kCameraModeBase = 0;
    static constexpr int32_t kCameraModeCinematic = 10;

    if ( pCameraEntity == nullptr ) {
        gpCamera->setCameraMode( kCameraModeCinematic, 0, 1.0f );
        return;
    }

    gpCamera->setCameraMode( kCameraModeBase, 0, 1.0f );
    CameraGame *pCamera = gpCamera->findCameraWithMode(
        kCameraModeBase, gpCamera->getBoundCarState() );
    if ( pCamera != nullptr ) {
        PlaceCameraOnEntity( pCamera, pCameraEntity );
    }
}

void AvatarCatalogScene::PlaceCameraOnEntity(
    CameraGame *pCamera, const EntityCamera *pCameraEntity ) {
    const Matrix4x4f_t &cameraMatrix = pCameraEntity->getModelMatrix();

    const Eigen::Vector4f position = TDUMat4x4MulVec(
        cameraMatrix, Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ) );
    const Eigen::Vector4f target = TDUMat4x4MulVec(
        cameraMatrix, Eigen::Vector4f( 0.0f, 0.0f, 1.0f, 1.0f ) );
    const Eigen::Vector4f up = TDUMat4x4MulVec(
        cameraMatrix, Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f ) );

    pCamera->setPosition( position );
    pCamera->setTarget( target );
    pCamera->setUp( up );
    pCamera->setFOVAndAspectRatio( pCameraEntity->getRatio(),
                                   pCameraEntity->getFOV() );
    pCamera->updateMatrix( position.head<3>(), up.head<3>(), target.head<3>() );
}
