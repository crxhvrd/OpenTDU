#include "shared.h"
#include "mng_spot_picture.h"

#include "flash/flash_callbacks_register.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/mng_hud.h"
#include "game/scenaric/spots.h"
#include "render/draw_list.h"
#include "world/gs_world.h"
#include "world/streaming_manager.h"

MngSpotPicture::MngSpotPicture()
    : Manager(), pMovie( nullptr ), pClipPath(), bank() {
    // FUN_0076a6f0 (inlined)
}

MngSpotPicture::~MngSpotPicture() {}

bool MngSpotPicture::initialize( TestDriveGameInstance * ) {
    // FUN_009be5e0
    bank.setBuffer(
        TestDrive::AllocAligned( MapScreenshotBank::kBufferSize, 0x10 ) );
    return true;
}

void MngSpotPicture::terminate() {
    // FUN_009be7a0
    bank.release();
    if ( bank.getBuffer() != nullptr ) {
        TestDrive::FreeAligned( bank.getBuffer() );
        bank.setBuffer( nullptr );
    }
}

void MngSpotPicture::showSpot( const SpotMap *pSpot ) {
    // FUN_009be7d0
    gFlashCallbacksRegister.removeAllSynonyms(
        &MngSpotPicture::PictureDrawCallback );
    bank.release();

    if ( pSpot == nullptr ) {
        return;
    }

    char hashcodeName[9] = {};
    memcpy( hashcodeName, &pSpot->Hashcode, sizeof( pSpot->Hashcode ) );
    char sourceName[9] = {};
    memcpy( sourceName, &pSpot->SourceHashcode,
            sizeof( pSpot->SourceHashcode ) );

    GridPos2D sectorCell;
    GridPos2D areaCell;
    gpWorld->getStreamingManager()->sectorManager.computeCell(
        Eigen::Vector4f( pSpot->X, pSpot->Y, pSpot->Z, 1.0f ), sectorCell,
        areaCell );

    char bankName[0x40];
    MapScreenshotBank::eCategory category = MapScreenshotBank::C_Spots;
    if ( pSpot->Type == ST_Challenge ) {
        strcpy( bankName, hashcodeName );
        category = MapScreenshotBank::C_IGE;
    } else {
        snprintf( bankName, sizeof( bankName ), "%s_%d%d%d%d", sourceName,
                  areaCell.posX, areaCell.posY, sectorCell.posX,
                  sectorCell.posY );
    }

    bank.load( bankName, category, &MngSpotPicture::OnPictureLoaded, this );
}

bool MngSpotPicture::OnPictureLoaded( MapScreenshotBank * /*pBank*/,
                                      void *pUserData ) {
    // LAB_009be740
    gFlashCallbacksRegister.add( "pict", &MngSpotPicture::PictureDrawCallback,
                                 pUserData );
    return true;
}

void MngSpotPicture::PictureDrawCallback( FlashRECT * /*pViewport*/,
                                          float /*depth*/,
                                          DrawList *pDrawList,
                                          void *pUserData,
                                          FlashResource *pResource,
                                          const char * /*pClipName*/ ) {
    // FUN_009be640
    MngSpotPicture *pManager = static_cast<MngSpotPicture *>( pUserData );
    if ( pManager->pMovie == nullptr ) {
        return;
    }

    FlashClip pictureClip;
    if ( !pManager->pMovie->getClipPlayer( pManager->pClipPath,
                                           &pictureClip ) ) {
        return;
    }

    FlashRECT viewport;
    pictureClip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );

    Material *pMaterial = pManager->bank.getMaterial();
    if ( pMaterial == nullptr ) {
        return;
    }

    pDrawList->setActiveMaterial( pMaterial );
    pDrawList->setDiffuse( 0, 0xffffffffu );
    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, viewport.X, viewport.Y,
                   viewport.X + viewport.Width, viewport.Y + viewport.Height,
                   pResource );
}
