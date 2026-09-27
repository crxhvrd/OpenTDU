#include "shared.h"
#include "radio_title.h"

#include "core/locale.h"
#include "config/gs_config.h"
#include "flash/flash_callbacks_register.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/gm_world.h"
#include "game/mng_hud.h"
#include "game/mng_ingame_menu.h"
#include "render/2db.h"
#include "render/2dm.h"
#include "render/draw_list.h"
#include "render/material.h"

static constexpr float kScrollInterval = 0.3f;     // FLOAT_00f416bc
static constexpr float kVolumeLevelScale = -10.0f; // DAT_00f418a8
static constexpr float kVolumeFrameScale = 10.0f;  // FLOAT_00f4166c
static constexpr uint32_t kDefaultVisibleChars = 0xc;
static constexpr const char *kLogoClipPath = "/HUD/radio/radio_lure";
static constexpr const char *kScrollSeparator = " --- ";

RadioTitle::RadioTitle()
    : LogoName{},
      bInitialized( false ),
      p2DMData( nullptr ),
      pRender2DM( nullptr ),
      pRender2DBs( nullptr ),
      pLogoMaterial( nullptr ),
      Num2DB( 0 ),
      Max2DB( 0 ),
      ScrollTimer( kScrollInterval ),
      VisibleChars( kDefaultVisibleChars ),
      FlashVariable{},
      Text{},
      ScrollText{},
      UTF8Text{},
      ScrollMode( RTSM_Marquee ),
      ScrollOffset( UINT32_MAX ),
      bScrollForward( true ),
      Volume( 0.0f ),
      bDirty( false ),
      LogoBank() {
    // FUN_007c9760
    initialize();
    gFlashCallbacksRegister.add( "radio_lure", &RadioTitle::DrawLogoCallback,
                                 this );
}

RadioTitle::~RadioTitle() {
    // FUN_007c9410
    gFlashCallbacksRegister.removeAllSynonyms( &RadioTitle::DrawLogoCallback );
    unload();
}

void RadioTitle::initialize() {
    // FUN_007c9490
    if ( bInitialized ) {
        return;
    }

    std::string bankPath = gpConfig->getResRootPath();
    bankPath += "Frontend/AllRes/RadioLogo.bnk";
    if ( !LogoBank.loadBootBank( bankPath.c_str(), nullptr ) ) {
        return;
    }

    Num2DB = 0;
    for ( int32_t i = 0; i < LogoBank.getNumElements(); i++ ) {
        ParsedBankEntry entry;
        if ( LogoBank.getEntry( i, entry ) != 0 || entry.Type != 1 ) {
            continue;
        }

        if ( entry.SubType == 2 ) {
            p2DMData = entry.pData;
            pRender2DM = new Render2DM();
            pRender2DM->initialize( p2DMData );
        } else if ( entry.SubType == 3 ) {
            Num2DB++;
        }
    }

    Max2DB = Num2DB;
    pRender2DBs = new Render2DB[Max2DB];

    FileCollection2D &collection = gpFlash->getCollection2D();

    int32_t index2DB = 0;
    for ( int32_t i = 0; i < LogoBank.getNumElements(); i++ ) {
        ParsedBankEntry entry;
        if ( LogoBank.getEntry( i, entry ) != 0 || entry.Type != 1 ||
             entry.SubType != 3 ) {
            continue;
        }

        pRender2DBs[index2DB].initialize( entry.pData );
        collection.register2DB( &pRender2DBs[index2DB] );
        index2DB++;
    }

    if ( pRender2DM != nullptr ) {
        collection.register2DM( pRender2DM );

        for ( int32_t i = 0; i < Max2DB; i++ ) {
            Material *pMaterial =
                pRender2DM->getMaterialAtIndex( static_cast<uint32_t>( i ) );
            if ( pMaterial != nullptr ) {
                pMaterial->OT = 0x2a;
                pMaterial->DepthTest = 1;
                pMaterial->DepthWrite = 2;
                pMaterial->AlphaTest = CF_Never;
            }
        }
    }

    bInitialized = true;
}

void RadioTitle::unload() {
    // FUN_007c8dc0
    if ( !bInitialized ) {
        return;
    }

    FileCollection2D &collection = gpFlash->getCollection2D();

    if ( pRender2DM != nullptr ) {
        collection.unregister2DM( pRender2DM );
        pRender2DM->destroy();
        delete pRender2DM;
        pRender2DM = nullptr;
    }

    for ( int32_t i = 0; i < Num2DB; i++ ) {
        collection.unregister2DB( &pRender2DBs[i] );
        pRender2DBs[i].destroy();
    }
    delete[] pRender2DBs;
    pRender2DBs = nullptr;

    LogoBank.unload();
    p2DMData = nullptr;
    pLogoMaterial = nullptr;
    bInitialized = false;
}

void RadioTitle::setFlashVariable( const char *pVariableName ) {
    strncpy( FlashVariable, pVariableName, sizeof( FlashVariable ) - 1 );
}

void RadioTitle::resetScroll() {
    // FUN_007c8cf0
    ScrollOffset = UINT32_MAX;
    ScrollTimer = kScrollInterval;
    bScrollForward = true;

    if ( ScrollMode == RTSM_Marquee && strlen( Text ) > VisibleChars ) {
        snprintf( ScrollText, sizeof( ScrollText ), "%s%s", Text,
                  kScrollSeparator );
        return;
    }

    strcpy( ScrollText, Text );
}

void RadioTitle::setText( const char *pText ) {
    // FUN_007c92a0
    if ( strcmp( pText, Text ) == 0 ) {
        return;
    }

    strncpy( Text, pText, sizeof( Text ) - 1 );
    resetScroll();
    ScrollTimer = 0.0f;

    if ( gpMngInGameMenu != nullptr ) {
        gpMngInGameMenu->changeMenu( IGM_Radio );
    }
}

void RadioTitle::setLogo( const char *pLogoName ) {
    // FUN_007c9330
    if ( pRender2DM == nullptr ) {
        return;
    }

    if ( strcmp( pLogoName, LogoName ) == 0 && pLogoMaterial != nullptr ) {
        return;
    }

    pLogoMaterial = pRender2DM->getMaterial( pLogoName );
    if ( pLogoMaterial == nullptr ) {
        pLogoMaterial = pRender2DM->getMaterial( "User radio" );
    }
    strncpy( LogoName, pLogoName, sizeof( LogoName ) - 1 );
    bDirty = true;
}

void RadioTitle::setVolume( const float volume ) {
    if ( Volume == volume ) {
        return;
    }

    Volume = volume;
    bDirty = true;

    if ( gpMngInGameMenu != nullptr ) {
        gpMngInGameMenu->changeMenu( IGM_Radio );
    }
}

void RadioTitle::update( const float deltaTime ) {
    // FUN_007c8fa0
    FlashPlayer *pPlayer = GetHudFlashPlayer();
    if ( pPlayer == nullptr ||
         ( gpActiveGM != nullptr && gpActiveGM->isPaused() ) ||
         gpMngInGameMenu == nullptr || !gpMngInGameMenu->canOpenMenu() ||
         gpMngInGameMenu->getOpenedMenu() != IGM_Radio ) {
        bDirty = false;
        return;
    }

    const char *pStatus = ( Volume == 0.0f ) ? "OFF" : "ON";
    pPlayer->setVariableValue(
        "/:RadioVolume",
        static_cast<int32_t>(
            1 - static_cast<int32_t>( Volume * kVolumeLevelScale ) ) );

    char currentStatus[16] = {};
    pPlayer->getVariableAsString( "/:RadioStatus", currentStatus );
    if ( StringCmpI( currentStatus, pStatus ) == 0 ) {
        if ( bDirty && gpMngInGameMenu->getState() == IGMS_Open ) {
            pPlayer->goToFrameAndPlay(
                static_cast<int32_t>( Volume * kVolumeFrameScale ),
                "/HUD/radio/text/volume", false );
        }
    } else {
        pPlayer->setVariableValueNonLinked( "/:RadioStatus", pStatus );
        if ( gpMngInGameMenu->getState() == IGMS_Open ) {
            pPlayer->showFrame( pStatus, "/HUD/radio", true );
        }
    }

    ScrollTimer -= deltaTime;
    if ( ScrollTimer >= 0.0f ) {
        bDirty = false;
        return;
    }
    ScrollTimer += kScrollInterval;

    char displayText[256] = {};
    const size_t textLength = strlen( Text );
    if ( textLength <= VisibleChars ) {
        strcpy( displayText, Text );
        pPlayer->setVariableValueNonLinked( FlashVariable, Text );
    } else if ( ScrollMode == RTSM_PingPong ) {
        if ( bScrollForward && ScrollOffset + VisibleChars < textLength ) {
            ScrollOffset++;
        } else if ( bScrollForward ) {
            bScrollForward = false;
        } else if ( ScrollOffset != 0 ) {
            ScrollOffset--;
        } else {
            bScrollForward = true;
        }
        strncpy( displayText, Text + ScrollOffset, VisibleChars );
    } else if ( ScrollMode == RTSM_Marquee ) {
        ScrollOffset = ( ScrollOffset + 1 ) %
                       static_cast<uint32_t>( strlen( ScrollText ) );

        const char *pScroll = ScrollText + ScrollOffset;
        const int32_t scrollLength = static_cast<int32_t>( strlen( pScroll ) );
        const int32_t wrapLength =
            static_cast<int32_t>( VisibleChars ) - scrollLength;

        strncpy( displayText, pScroll, VisibleChars );
        if ( wrapLength > 0 ) {
            strncpy( displayText + scrollLength, ScrollText, wrapLength );
        }
    }

    displayText[VisibleChars] = '\0';
    ToUTF8( sizeof( UTF8Text ), UTF8Text, displayText, sizeof( UTF8Text ) );
    pPlayer->setVariableValue( FlashVariable, UTF8Text );

    bDirty = false;
}

void RadioTitle::DrawLogoCallback( FlashRECT * /*pViewport*/,
                                   float /*depth*/,
                                   DrawList *pDrawList,
                                   void *pUser,
                                   FlashResource *pResource,
                                   const char * /*pClipName*/ ) {
    // FUN_007c8eb0
    FlashPlayer *pPlayer = GetHudFlashPlayer();
    if ( pPlayer == nullptr || !pPlayer->spriteExists( kLogoClipPath ) ) {
        return;
    }

    RadioTitle *pTitle = static_cast<RadioTitle *>( pUser );
    if ( pTitle->pLogoMaterial == nullptr ) {
        return;
    }

    FlashClip logoClip;
    if ( !pPlayer->getClipPlayer( kLogoClipPath, &logoClip ) ) {
        return;
    }

    FlashRECT viewport;
    logoClip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );

    pDrawList->setActiveMaterial( pTitle->pLogoMaterial );
    pDrawList->setDiffuse( 0, 0xffffffffu );

    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, viewport.X, viewport.Y,
                   viewport.X + viewport.Width, viewport.Y + viewport.Height,
                   pResource );
}
