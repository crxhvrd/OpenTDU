#include "shared.h"
#include "mng_collection.h"

#include "filesystem/gs_file.h"
#include "flash/entities/flash_entity.h"
#include "flash/modal_message_box.h"
#include "flash/flash_callbacks_register.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/freeride/mng_photo_mode.h"
#include "game/mng_hud.h"
#include "game/vehicle/car_desc.h"
#include "gm_living.h"
#include "player_data/achievements_data.h"
#include "player_data/saved_extra_challenges.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "render/gs_render.h"

MngCollection *gpMngCollection = nullptr; // DAT_010e7d10

static constexpr uint64_t kNoStorageHashcode = 0x55aff4ull; // DAT_0055aff4
static constexpr uint64_t kUploadFailedHashcode = 0x5d5114ull;
static constexpr uint64_t kWaitPopupHashcode = 0x38aeb22ull;
static constexpr float kWaitPopupDuration = 3.0f;

static constexpr const char *kThumbnailClipName = "lurephoto";
static constexpr const char *kBigPhotoClipName = "lurebig";

MngCollection::MngCollection() : Manager() {
    // FUN_009d1700
    gpMngCollection = this;
}

MngCollection::~MngCollection() {
    // FUN_0042d6a0
    gpMngCollection = nullptr;
}

FlashPlayer *MngCollection::getFlashPlayer() const {
    return ( pOwner != nullptr ) ? pOwner->getFlashPlayer() : nullptr;
}

SavedExtraChallenges *MngCollection::getStatistics() const {
    IPlayerData *pSave =
        ( gpPlayerData != nullptr ) ? gpPlayerData->getLoadedSave() : nullptr;
    return ( pSave != nullptr ) ? pSave->getExtraChallenges() : nullptr;
}

bool MngCollection::initialize( TestDriveGameInstance * ) {
    // FUN_009d16b0
    for ( AlbumSlot &slot : albumSlots ) {
        slot.Shot.create( kThumbnailWidth, kThumbnailHeight, VF_X8R8G8B8 );
    }

    bigPhoto.create( gpRender->getWidth(), gpRender->getHeight(), VF_X8R8G8B8 );
    return true;
}

void MngCollection::tick( float /*deltaTime*/, float /*totalTime*/ ) {
    // FUN_009d2530
    drawSlotCounter = 1;

    if ( bUploadPending ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (photo upload result)
    }
}

void MngCollection::terminate() {
    // FUN_009d1be0
    gFlashCallbacksRegister.removeAllSynonyms(
        &MngCollection::ThumbnailDrawCallback );
    gFlashCallbacksRegister.removeAllSynonyms(
        &MngCollection::BigPhotoDrawCallback );

    for ( AlbumSlot &slot : albumSlots ) {
        slot.Shot.destroy();
        slot.pMaterial = nullptr;
    }

    bigPhoto.destroy();
    pBigPhotoMaterial = nullptr;

    OTDU_UNIMPLEMENTED_ONCE; // TODO: GMLIVING (statistics screen)
}

void MngCollection::clearAlbumMaterials() {
    for ( AlbumSlot &slot : albumSlots ) {
        slot.pMaterial = nullptr;
    }
    pBigPhotoMaterial = nullptr;
}

int32_t MngCollection::getSelectedSlot() const {
    FlashPlayer *pPlayer = getFlashPlayer();
    if ( pPlayer == nullptr ) {
        return 0;
    }

    char selection[64] = {};
    if ( !pPlayer->getVariableAsString( "/:mySelection", selection ) ) {
        return 0;
    }

    for ( int32_t i = 0; i < kNumPhotosPerPage; i++ ) {
        char clipName[8];
        snprintf( clipName, sizeof( clipName ), "ph%d", i + 1 );
        if ( strcmp( selection, clipName ) == 0 ) {
            return i;
        }
    }

    return 0;
}

void MngCollection::openAlbum() {
    // FUN_009d2340
    clearAlbumMaterials();

    FlashPlayer *pPlayer = getFlashPlayer();

    const eContextState contextState =
        gpFile->ensureContext( kPhotoContext, kPhotoContextDisplay );

    if ( contextState == eContextState::CST_Error ) {
        TestDrive::DisplayModalPrompt( true, kNoStorageHashcode, 0xb, nullptr,
                                       nullptr, nullptr, 0, nullptr, true,
                                       false, MPT_Warning, gpFlashMessageBox );
    }

    if ( contextState != eContextState::CST_Ready ) {
        if ( pPlayer != nullptr ) {
            pPlayer->setVariableValue( "/:NbPagePhoto", int32_t( 0 ) );
            pPlayer->setVariableValue( "/:LastPageNbPhoto", int32_t( 0 ) );
        }
        return;
    }

    const int32_t numPhotos =
        gpFile->countFilesInContext( kPhotoContext, kPhotoPattern );

    OTDU_LOG_DEBUG( "Photo album: %d photo(s) in context '%s'\n", numPhotos,
                    kPhotoContext );

    if ( pPlayer == nullptr ) {
        return;
    }

    pPlayer->setVariableValue( "/:IsUploadPhoto", int32_t( 0 ) );

    const int32_t lastPageCount = numPhotos % kNumPhotosPerPage;
    pPlayer->setVariableValue(
        "/:LastPageNbPhoto",
        float( ( lastPageCount != 0 ) ? lastPageCount : kNumPhotosPerPage ) );

    const float numPages = float( numPhotos ) / float( kNumPhotosPerPage );
    pPlayer->setVariableValue( "/:NbPagePhoto",
                               int32_t( std::ceil( numPages ) ) );

    gFlashCallbacksRegister.add( kThumbnailClipName,
                                 &MngCollection::ThumbnailDrawCallback, this );

    // NOTE: Omitted Xbox360 memory card lookup
}

void MngCollection::closeAlbum() {
    // FUN_009d1b90
    clearAlbumMaterials();

    gFlashCallbacksRegister.removeAllSynonyms(
        &MngCollection::ThumbnailDrawCallback );
    gFlashCallbacksRegister.removeAllSynonyms(
        &MngCollection::BigPhotoDrawCallback );
}

void MngCollection::loadAlbumSlot( const int32_t slot, const int32_t page ) {
    // FUN_009d2860
    if ( slot < 0 || slot >= kNumPhotosPerPage ) {
        return;
    }

    char filename[512] = {};
    const int32_t photoIndex = slot + page * kNumPhotosPerPage;
    if ( !gpFile->getFileNameInContext( "CURRENT", kPhotoPattern, photoIndex,
                                        filename, sizeof( filename ) ) ) {
        return;
    }

    AlbumSlot &albumSlot = albumSlots[slot];

    char thumbnailName[sizeof( filename ) + 8];
    snprintf( thumbnailName, sizeof( thumbnailName ), "%s.small", filename );

    char metaInfos[kMaxPhotoMetaInfosLength] = {};

    albumSlot.pMaterial = albumSlot.Shot.loadFromFile(
        thumbnailName, "CURRENT", metaInfos, sizeof( metaInfos ) );

    OTDU_LOG_DEBUG( "Photo album: slot %d <- '%s' (%s)\n", slot, thumbnailName,
                    ( albumSlot.pMaterial != nullptr ) ? "loaded" : "FAILED" );

    if ( albumSlot.pMaterial == nullptr ) {
        return;
    }

    CarDesc shotCar;
    int32_t shotFormat = -1;

    char caption[kMaxPhotoMetaInfosLength] = {};
    if ( !ParsePhotoMetaInfos( metaInfos, caption, &shotCar, &shotFormat ) ) {
        return;
    }

    ReplacePhotoMetaChars( albumSlot.Title, sizeof( albumSlot.Title ), caption,
                           kPhotoMetaEscapes, kPhotoMetaDelimiters );

    FlashPlayer *pPlayer = getFlashPlayer();
    if ( pPlayer != nullptr ) {
        char variableName[64];
        snprintf( variableName, sizeof( variableName ), "/:title_photo%d",
                  slot + 1 );
        pPlayer->setVariableValue( variableName, albumSlot.Title );
    }
}

void MngCollection::loadAlbumPage() {
    // FUN_009d29f0
    FlashPlayer *pPlayer = getFlashPlayer();
    if ( pPlayer == nullptr ) {
        return;
    }

    const int32_t page = pPlayer->getVariableAsInt( "/:indexPhoto" );

    const eContextState contextState =
        gpFile->ensureContext( kPhotoContext, kPhotoContextDisplay );
    if ( contextState == eContextState::CST_Empty ) {
        return;
    }
    if ( contextState == eContextState::CST_Error ) {
        TestDrive::DisplayModalPrompt( true, kNoStorageHashcode, 0xb, nullptr,
                                       nullptr, nullptr, 0, nullptr, true,
                                       false, MPT_Warning, gpFlashMessageBox );
        return;
    }

    if ( !gpFile->openContextAsCurrent( kPhotoContext, kPhotoContextDisplay,
                                        true ) ) {
        return;
    }

    for ( int32_t slot = 0; slot < kNumPhotosPerPage; slot++ ) {
        loadAlbumSlot( slot, page );
    }

    gpFile->closeCurrentContext();
}

void MngCollection::showBigPhoto() {
    // FUN_009d2750
    FlashPlayer *pPlayer = getFlashPlayer();
    if ( pPlayer == nullptr ) {
        return;
    }

    const int32_t page = pPlayer->getVariableAsInt( "/:indexPhoto" );

    if ( !gpFile->openContextAsCurrent( kPhotoContext, kPhotoContextDisplay,
                                        true ) ) {
        return;
    }

    const int32_t slot = pPlayer->getVariableAsInt( "/:photoBig" ) - 1;

    char filename[512] = {};
    const int32_t photoIndex = slot + page * kNumPhotosPerPage;
    if ( gpFile->getFileNameInContext( "CURRENT", kPhotoPattern, photoIndex,
                                       filename, sizeof( filename ) ) ) {
        pBigPhotoMaterial = bigPhoto.loadFromFile( filename, "CURRENT" );
    }

    gFlashCallbacksRegister.removeAllSynonyms(
        &MngCollection::BigPhotoDrawCallback );
    if ( pBigPhotoMaterial != nullptr ) {
        gFlashCallbacksRegister.add(
            kBigPhotoClipName, &MngCollection::BigPhotoDrawCallback, this );
    }

    gpFile->closeCurrentContext();
}

void MngCollection::hideBigPhoto() {
    // FUN_009d1d30
    gFlashCallbacksRegister.removeAllSynonyms(
        &MngCollection::BigPhotoDrawCallback );
    pBigPhotoMaterial = nullptr;
}

void MngCollection::deleteSelectedPhoto() {
    // FUN_009d25e0
    FlashPlayer *pPlayer = getFlashPlayer();
    if ( pPlayer == nullptr ) {
        return;
    }

    const int32_t page = pPlayer->getVariableAsInt( "/:indexPhoto" );
    const int32_t slot = getSelectedSlot();

    if ( gpFile->openContextAsCurrent( kPhotoContext, kPhotoContextDisplay,
                                       false ) ) {
        gpFile->deleteFileInContext( kPhotoContext, kPhotoPattern,
                                     page * kNumPhotosPerPage + slot );
        gpFile->closeCurrentContext();
    }

    closeAlbum();
    openAlbum();
}

void MngCollection::publishAchievementHeader() {
    // FUN_009d1ca0
    FlashPlayer *pPlayer = getFlashPlayer();
    if ( pPlayer == nullptr || gpPlayerData == nullptr ||
         gpPlayerData->getLoadedSave() == nullptr ) {
        return;
    }

    AchievementsData *pAchievements =
        gpPlayerData->getLoadedSave()->getAchievements();
    if ( pAchievements == nullptr ) {
        return;
    }

    pPlayer->setVariableValue( "/:CurLevelNb",
                               pAchievements->getPlayerLevel() + 1 );
    pPlayer->setVariableValue( "/:GamerScore",
                               pAchievements->getAchievedPoints() );

    pAchievements->publishLevelSteps( pPlayer );
    pAchievements->publishCheckStates( pPlayer );
}

void MngCollection::publishAchievementDetails() {
    // FUN_009d1c40
    FlashPlayer *pPlayer = getFlashPlayer();
    if ( pPlayer == nullptr ) {
        return;
    }

    const int32_t menuIndex = pPlayer->getVariableAsInt( "/:num_menu" );
    const int32_t rowIndex = pPlayer->getVariableAsInt( "/:num_row" );
    const int32_t columnIndex = pPlayer->getVariableAsInt( "/:num_col" );
    const int32_t panelID = ( menuIndex * 10 + rowIndex ) * 10 + columnIndex;

    gpPlayerData->getLoadedSave()->getAchievements()->publishAchievementDetails(
        pPlayer, panelID );
}

void MngCollection::ThumbnailDrawCallback( FlashRECT * /*pViewport*/,
                                           float /*depth*/,
                                           DrawList *pDrawList,
                                           void *pUser,
                                           FlashResource *pResource,
                                           const char * /*pClipName*/ ) {
    // FUN_009d1a60
    MngCollection *pManager = static_cast<MngCollection *>( pUser );

    const int32_t slot = pManager->drawSlotCounter;
    pManager->drawSlotCounter++;

    if ( slot < 1 || slot > kNumPhotosPerPage ) {
        return;
    }

    Material *pMaterial = pManager->albumSlots[slot - 1].pMaterial;
    if ( pMaterial == nullptr ) {
        return;
    }

    FlashPlayer *pPlayer = pManager->getFlashPlayer();
    if ( pPlayer == nullptr ) {
        return;
    }

    char clipPath[64];
    snprintf( clipPath, sizeof( clipPath ),
              "/ss_menu/album/photo/ph%d/lurephoto", slot );

    FlashClip clip;
    if ( !pPlayer->getClipPlayer( clipPath, &clip ) ) {
        return;
    }

    FlashRECT rect;
    clip.getPropertyViewport( eFlashProperty::FP_Viewport, rect );

    pDrawList->setActiveMaterial( pMaterial );
    pDrawList->setDiffuse( 0, 0xffffffffu );

    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, rect.X, rect.Y, rect.Width + rect.X,
                   rect.Height + rect.Y, pResource );
}

void MngCollection::BigPhotoDrawCallback( FlashRECT * /*pViewport*/,
                                          float /*depth*/,
                                          DrawList *pDrawList,
                                          void *pUser,
                                          FlashResource *pResource,
                                          const char *pClipName ) {
    // FUN_009d1950
    MngCollection *pManager = static_cast<MngCollection *>( pUser );

    if ( pManager->pBigPhotoMaterial == nullptr ) {
        return;
    }

    FlashPlayer *pPlayer = pManager->getFlashPlayer();
    if ( pPlayer == nullptr ) {
        return;
    }

    char clipPath[128];
    snprintf( clipPath, sizeof( clipPath ), "/ss_menu/album/photo/bigger/%s",
              ( pClipName != nullptr ) ? pClipName : kBigPhotoClipName );

    FlashClip clip;
    if ( !pPlayer->getClipPlayer( clipPath, &clip ) ) {
        return;
    }

    FlashRECT rect;
    clip.getPropertyViewport( eFlashProperty::FP_Viewport, rect );

    pDrawList->setActiveMaterial( pManager->pBigPhotoMaterial );
    pDrawList->setDiffuse( 0, 0xffffffffu );

    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, rect.X, rect.Y, rect.Width + rect.X,
                   rect.Height + rect.Y, pResource );
}

bool MngCollection::onMessage( FlashMessage *pMessage ) {
    // FUN_009d2ac0
    switch ( *pMessage ) {
    case FlashMessage( "pgin" ): // the album page opened
        openAlbum();
        return true;

    case FlashMessage( "pgou" ): // the album page closed
        closeAlbum();
        return true;

    case FlashMessage( "chph" ): // page turned
        loadAlbumPage();
        return true;

    case FlashMessage( "phbg" ): // enlarge the selected photo
        showBigPhoto();
        return true;

    case FlashMessage( "phbs" ): // back to the thumbnails
        hideBigPhoto();
        return true;

    case FlashMessage( "deph" ): // delete the selected photo
        deleteSelectedPhoto();
        return true;

    case FlashMessage( "acin" ): // the achievements panel opened
        publishAchievementHeader();
        return true;

    case FlashMessage( "pgwa" ): // "please wait" while a page loads
        if ( gpFlashMessageBox != nullptr ) {
            gpFlashMessageBox->display( kWaitPopupHashcode, kWaitPopupDuration,
                                        0, nullptr, nullptr, false );
        }
        return true;

    case FlashMessage( "acda" ): // one achievement selected
        publishAchievementDetails();
        return true;

    case FlashMessage( "stin" ): // the statistics screen opened
        if ( SavedExtraChallenges *pStats = getStatistics() ) {
            pStats->openStatisticsScreen( getFlashPlayer() );
        }
        return true;

    case FlashMessage( "stou" ): // ... and closed
        if ( SavedExtraChallenges *pStats = getStatistics() ) {
            pStats->closeStatisticsScreen();
        }
        return true;

    case FlashMessage( "nest" ): // next statistics category
        if ( SavedExtraChallenges *pStats = getStatistics() ) {
            pStats->nextStatisticsCategory();
        }
        return true;

    case FlashMessage( "prst" ): // previous statistics category
        if ( SavedExtraChallenges *pStats = getStatistics() ) {
            pStats->previousStatisticsCategory();
        }
        return true;

    case FlashMessage( "upph" ): // upload the selected photo
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (photo upload)
        return true;

    default:
        break;
    }

    return false;
}
