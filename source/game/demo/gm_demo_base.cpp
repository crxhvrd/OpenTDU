#include "shared.h"
#include "gm_demo_base.h"

#include "config/gs_config.h"
#include "flash/flash_callbacks_register.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/mng_hud.h"
#include "render/draw_list.h"

GMDemoBase::GMDemoBase()
    : GameModeScene(), pDemoPlayer( nullptr ), photo(), pPicture( nullptr ) {
    // FUN_009d8bc0
    picturePrefix[0] = '\0';
}

GMDemoBase::~GMDemoBase() {
    // FUN_009d8b50
}

void GMDemoBase::initialize() {
    // FUN_009d8d70
    GameModeScene::initialize();
    photo.create( 1280, 512, VF_X8R8G8B8 );
}

void GMDemoBase::tick( float deltaTime, float totalTime ) {
    // FUN_009d8c60
    GameModeScene::tick( deltaTime, totalTime );
}

void GMDemoBase::destroy() {
    // FUN_009d8c10
    gFlashCallbacksRegister.removeAllSynonyms( &GMDemoBase::LureDrawCallback );
    photo.destroy();
    GameModeScene::destroy();
}

bool GMDemoBase::onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) {
    // FUN_009d8d20
    switch ( *pMessage ) {
    case FlashMessage( "afsc" ):
        loadNextPicture();
        return true;
    case FlashMessage( "ende" ):
        changeGameMode();
        return true;
    default:
        return false;
    }
}

void GMDemoBase::setPicturePrefix( const char *pPrefix ) {
    strncpy( picturePrefix, pPrefix, sizeof( picturePrefix ) - 1 );
    picturePrefix[sizeof( picturePrefix ) - 1] = '\0';
}

void GMDemoBase::loadNextPicture() {
    // FUN_009d8c70
    if ( pDemoPlayer == nullptr ) {
        return;
    }

    gFlashCallbacksRegister.removeAllSynonyms( &GMDemoBase::LureDrawCallback );

    const int32_t pictureIndex = pDemoPlayer->getVariableAsInt( "/:num_txt" );

    char picturePath[0x200];
    snprintf( picturePath, sizeof( picturePath ),
              "%sFrontEnd/AllRes/DemoBump/%s%02d.jpg",
              gpConfig->getResRootPath(), picturePrefix, pictureIndex );

    pPicture = photo.loadFromFile( picturePath, "" );
    if ( pPicture != nullptr ) {
        gFlashCallbacksRegister.add( "lure", &GMDemoBase::LureDrawCallback,
                                     this );
    }
}

void GMDemoBase::LureDrawCallback( FlashRECT * /*pViewport*/,
                                   float /*depth*/,
                                   DrawList *pDrawList,
                                   void *pUserData,
                                   FlashResource *pResource,
                                   const char * /*pClipName*/ ) {
    // FUN_009d8a80
    GMDemoBase *pDemo = static_cast<GMDemoBase *>( pUserData );
    if ( pDemo == nullptr || pDemo->pPicture == nullptr ||
         pDemo->pDemoPlayer == nullptr ) {
        return;
    }

    FlashClip lureClip;
    if ( !pDemo->pDemoPlayer->getClipPlayer( "/lure", &lureClip ) ) {
        return;
    }

    FlashRECT viewport;
    lureClip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );

    pDrawList->setActiveMaterial( pDemo->pPicture );
    pDrawList->setDiffuse( 0, 0xffffffffu );
    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, viewport.X, viewport.Y,
                   viewport.X + viewport.Width, viewport.Y + viewport.Height,
                   pResource );
}
