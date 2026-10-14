#include "shared.h"
#include "mng_color_palette.h"

#include "config/gs_config.h"
#include "flash/flash_callbacks_register.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/mng_hud.h"
#include "input/gs_device.h"
#include "input/input_device.h"
#include "render/2dm.h"
#include "render/draw_list.h"
#include "render/material.h"

MngColorPalette *gpMngColorPalette = nullptr; // DAT_010e7ff0

// The left part of the palette is a grey ramp, the rest a hue spectrum.
static constexpr float kGreyStripWidth = 0.08f;         // DAT_00f418cc
static constexpr float kHueScale = 1.0f / 0.92f;        // DAT_00f418c8
static constexpr float kHueSegment = 1.0f / 6.0f;       // DAT_00f418c4
static constexpr float kNumHueSegments = 6.0f;          // DAT_00f41840
static constexpr float kSearchGridStep = 1.0f / 256.0f; // DAT_00f41da8
static constexpr float kSearchGridSize = 256.0f;        // DAT_00f419c0
static constexpr float kCursorDeadZone = 0.3f;          // FLOAT_00fabf48
static constexpr float kCursorSpeed = 80.0f;            // FLOAT_00fabf44
static constexpr uint64_t kPaletteShaderHashcode = 0x73078865ca37336dull;
static constexpr uint32_t kPaletteBitmapFlags = 0x12u;

static inline uint8_t ToByte( const float value ) {
    return uint8_t( uint64_t( value ) );
}

static bool ReadAnalogAction( DeviceAction *pAction, float &outValue ) {
    if ( pAction == nullptr || pAction->NumAddedChannels == 0 ) {
        return false;
    }
    outValue = pAction->FUN_00434bc0( kCursorDeadZone, 1.0f );
    return outValue > 0.0f;
}

static bool ReadDigitalAction( DeviceAction *pAction, float &outValue ) {
    if ( pAction == nullptr || pAction->NumAddedChannels == 0 ) {
        return false;
    }
    outValue = float( pAction->Flags & 1 );
    return outValue > 0.0f;
}

MngColorPalette::MngColorPalette()
    : Manager(),
      pMaterial( nullptr ),
      p2DMContent( nullptr ),
      render2DM(),
      collection2D(),
      paletteBitmap(),
      keyColors{
          {0xff, 0x00, 0x00, 0xff},
          {0xff, 0xff, 0x00, 0xff},
          {0x00, 0xff, 0x00, 0xff},
          {0x00, 0xff, 0xff, 0xff},
          {0x00, 0x00, 0xff, 0xff},
          {0xff, 0x00, 0xff, 0xff},
          {0xff, 0x00, 0x00, 0xff}
},
      clipPath{},
      clipName{},
      cursorPath{},
      pFlashPlayer( nullptr ),
      clipWidth( 0.0f ),
      clipHeight( 0.0f ),
      cursorX( 0.0f ),
      cursorY( 0.0f ),
      clipViewport() {
    // FUN_006cc250
    gpMngColorPalette = this;
}

MngColorPalette::~MngColorPalette() {
    if ( gpMngColorPalette == this ) {
        gpMngColorPalette = nullptr;
    }
}

bool MngColorPalette::initialize( TestDriveGameInstance * ) {
    // FUN_006cc490
    create( kPaletteSize, kPaletteSize );
    return true;
}

void MngColorPalette::create( const uint32_t width, const uint32_t height ) {
    // FUN_006cc370
    p2DMContent = TestDrive::Alloc( Render2DM::CalcSize( 0, 0 ) );
    Render2DM::Create( p2DMContent, kPaletteShaderHashcode, 1, 0, 0, 0 );

    if ( !paletteBitmap.allocateAndCreate( width, height, 1, 1,
                                           VF_SRGB_R8G8B8A8,
                                           kPaletteBitmapFlags, "PALETTE" ) ) {
        return;
    }

    fillPalette();
    Render2DB::PrepareForUpload( paletteBitmap.getFirstBitmap(), true );

    render2DM.initialize( p2DMContent );
    Material *pPaletteMaterial = render2DM.getMaterialAtIndex( 0 );
    if ( pPaletteMaterial != nullptr ) {
        pPaletteMaterial->OT = 0x2a;
        pPaletteMaterial->DepthTest = 1;
        pPaletteMaterial->DepthWrite = 2;
        pPaletteMaterial->AlphaTest = eComparisonFunction::CF_Never;

        MaterialLayer *pLayer =
            pPaletteMaterial->getParameterByIndex( 0 )->getLayer( 0 );
        pLayer->pLayerTextures[0].Hashcode = GetIdentifier64bit( "PALETTE" );
    }

    collection2D.register2DB( &paletteBitmap );
    collection2D.register2DM( &render2DM );
    pMaterial = render2DM.getMaterialAtIndex( 0 );
}

void MngColorPalette::terminate() {
    // FUN_006cc970
    gFlashCallbacksRegister.removeAllSynonyms(
        &MngColorPalette::PaletteCallback );
    collection2D.unregister2DM( &render2DM );
    collection2D.unregister2DB( &paletteBitmap );
    paletteBitmap.destroy();
    render2DM.destroy();

    if ( p2DMContent != nullptr ) {
        TestDrive::Free( p2DMContent );
    }
    p2DMContent = nullptr;
    pMaterial = nullptr;
}

ColorRGBA MngColorPalette::getPaletteColor( const float hue,
                                            const float brightness ) const {
    // FUN_006cbea0
    ColorRGBA color = {};
    for ( int32_t i = 0; i < kNumKeyColors - 1; i++ ) {
        if ( hue > float( i + 1 ) * kHueSegment ) {
            continue;
        }

        const float t = ( hue - float( i ) * kHueSegment ) * kNumHueSegments;
        const float invT = 1.0f - t;
        const ColorRGBA &from = keyColors[i];
        const ColorRGBA &to = keyColors[i + 1];
        color.A = 0xff;
        color.R = ToByte( float( to.R ) * t + float( from.R ) * invT );
        color.G = ToByte( float( from.G ) * invT + float( to.G ) * t );
        color.B = ToByte( float( from.B ) * invT + float( to.B ) * t );
        break;
    }

    auto ApplyBrightness = [brightness]( const uint8_t channel ) {
        if ( 0.5f <= brightness ) {
            const float range = ( 255.0f - float( channel ) ) * 2.0f;
            return uint8_t(
                int32_t( ( 255.0f - range ) + range * brightness ) );
        }
        return uint8_t( int32_t( float( channel ) * 2.0f * brightness ) );
    };

    color.R = ApplyBrightness( color.R );
    color.G = ApplyBrightness( color.G );
    color.B = ApplyBrightness( color.B );
    color.A = 0xff;
    return color;
}

void MngColorPalette::fillPalette() {
    // FUN_006cc0e0
    Texture *pTexture = paletteBitmap.getFirstBitmap();
    uint32_t *pTexels =
        static_cast<uint32_t *>( Render2DB::GetTexelsPointer( pTexture, 0 ) );

    const uint32_t width = pTexture->Width;
    const uint32_t height = pTexture->Height;
    for ( uint32_t y = 0; y < height; y++ ) {
        for ( uint32_t x = 0; x < width; x++ ) {
            const float u = float( x ) / float( width );
            const float v = float( y ) / float( height );

            ColorRGBA color;
            if ( u < kGreyStripWidth ) {
                const uint8_t grey = ToByte( v * 255.0f );
                color = { grey, grey, grey, 0xff };
            } else {
                color =
                    getPaletteColor( v, ( u - kGreyStripWidth ) * kHueScale );
            }

            pTexels[width * x + y] = PackFlashDiffuse( color );
        }
    }
}

void MngColorPalette::readClipBounds() {
    // FUN_006cc5a0
    char path[128];
    snprintf( path, sizeof( path ), "%s/%s", clipPath, clipName );

    FlashClip clip;
    if ( pFlashPlayer != nullptr &&
         pFlashPlayer->getClipPlayer( path, &clip ) ) {
        FlashRECT bounds;
        clip.getPropertyViewport( eFlashProperty::FP_BoundingBox, bounds );
        clipWidth = bounds.Width;
        clipHeight = bounds.Height;
        clip.getPropertyViewport( eFlashProperty::FP_Viewport, clipViewport );
        return;
    }

    clipWidth = -1.0f;
    clipHeight = -1.0f;
    clipViewport = { -1.0f, -1.0f, -1.0f, -1.0f };
}

void MngColorPalette::bindClip( const char *pPath, const char *pClipName ) {
    // FUN_006cc680
    strncpy( clipPath, pPath, kPathLength );
    strncpy( clipName, pClipName, kClipNameLength );

    gFlashCallbacksRegister.removeAllSynonyms(
        &MngColorPalette::PaletteCallback );
    gFlashCallbacksRegister.add( pClipName, &MngColorPalette::PaletteCallback,
                                 this );

    readClipBounds();
    cursorY = 0.0f;
    cursorX = 0.0f;
}

bool MngColorPalette::setCursorFromColor( const ColorRGBA &color ) {
    // FUN_006cc6e0
    if ( clipWidth <= 0.0f || clipHeight <= 0.0f ) {
        readClipBounds();
    }
    if ( clipWidth <= 0.0f || clipHeight <= 0.0f ) {
        return false;
    }

    float bestDistance = std::numeric_limits<float>::max();
    int32_t bestX = -1;
    int32_t bestY = -1;
    for ( int32_t x = 0; float( x ) < kSearchGridSize; x++ ) {
        const float hue = float( x ) * kSearchGridStep;
        for ( int32_t y = 0; float( y ) < kSearchGridSize; y++ ) {
            const float brightness = float( y ) * kSearchGridStep;

            ColorRGBA candidate;
            if ( kGreyStripWidth <= brightness ) {
                candidate = getPaletteColor(
                    hue, ( brightness - kGreyStripWidth ) * kHueScale );
            } else {
                const uint8_t grey = ToByte( hue * 255.0f );
                candidate = { grey, grey, grey, 0xff };
            }

            const float distance =
                float(
                    std::abs( int32_t( color.R ) - int32_t( candidate.R ) ) ) +
                float(
                    std::abs( int32_t( color.G ) - int32_t( candidate.G ) ) ) +
                float(
                    std::abs( int32_t( color.B ) - int32_t( candidate.B ) ) );
            if ( distance < bestDistance ) {
                bestDistance = distance;
                bestX = x;
                bestY = y;
            }
        }
    }

    cursorX = float( bestX ) * kSearchGridStep * clipWidth;
    cursorY = float( bestY ) * kSearchGridStep * clipHeight;
    return true;
}

ColorRGBA MngColorPalette::getColorAtCursor() {
    // FUN_006cc8d0
    if ( clipWidth <= 0.0f || clipHeight <= 0.0f ) {
        readClipBounds();
    }

    const float brightness = cursorY / clipHeight;
    const float hue = cursorX / clipWidth;
    if ( brightness < kGreyStripWidth ) {
        const uint8_t grey = ToByte( hue * 255.0f );
        return { grey, grey, grey, 0xff };
    }
    return getPaletteColor( hue, ( brightness - kGreyStripWidth ) * kHueScale );
}

void MngColorPalette::tick( float deltaTime, float /*totalTime*/ ) {
    // FUN_006ccd80
    if ( pFlashPlayer == nullptr ) {
        return;
    }

    FlashClip cursorClip;
    if ( !pFlashPlayer->getClipPlayer( cursorPath, &cursorClip ) ) {
        return;
    }

    PlayerConfiguration *pPlayerConfig = gpConfig->pPlayerConfig;
    if ( clipWidth <= 0.0f || clipHeight <= 0.0f ) {
        readClipBounds();
    }

    float value = 0.0f;
    const float step = kCursorSpeed * deltaTime;
    if ( ReadAnalogAction( pPlayerConfig->pMapCursorUp, value ) ) {
        cursorY -= ( value - kCursorDeadZone ) * step;
    } else if ( ReadAnalogAction( pPlayerConfig->pMapCursorDown, value ) ) {
        cursorY += ( value - kCursorDeadZone ) * step;
    } else if ( ReadDigitalAction( pPlayerConfig->pMenuUp, value ) ) {
        cursorY -= ( value - kCursorDeadZone ) * step;
    } else if ( ReadDigitalAction( pPlayerConfig->pMenuDown, value ) ) {
        cursorY += ( value - kCursorDeadZone ) * step;
    }
    cursorY = ( cursorY < 0.0f ) ? 0.0f : Min( cursorY, clipHeight );

    if ( ReadAnalogAction( pPlayerConfig->pMapCursorLeft, value ) ) {
        cursorX -= ( value - kCursorDeadZone ) * step;
    } else if ( ReadAnalogAction( pPlayerConfig->pMapCursorRight, value ) ) {
        cursorX += ( value - kCursorDeadZone ) * step;
    } else if ( ReadDigitalAction( pPlayerConfig->pMenuLeft, value ) ) {
        cursorX -= ( value - kCursorDeadZone ) * step;
    } else if ( ReadDigitalAction( pPlayerConfig->pMenuRight, value ) ) {
        cursorX += ( value - kCursorDeadZone ) * step;
    }
    cursorX = ( cursorX < 0.0f ) ? 0.0f : Min( cursorX, clipWidth );

    const MouseState &mouse = gpDevice->getMouseState();
    const bool bMouseActive = mouse.FUN_00980000();
    const float mouseX = gEdFlash.Input.MouseCoordinates[0];
    const float mouseY = gEdFlash.Input.MouseCoordinates[1];
    const bool bMouseInside = mouseX >= clipViewport.X &&
                              mouseX <= clipViewport.X + clipViewport.Width &&
                              mouseY >= clipViewport.Y &&
                              mouseY <= clipViewport.Y + clipViewport.Height;
    if ( bMouseActive && bMouseInside && mouse.pChannelButton0 != nullptr &&
         ( mouse.pChannelButton0->Flags & 0x10 ) != 0 ) {
        cursorX =
            ( ( mouseX - clipViewport.X ) / clipViewport.Width ) * clipWidth;
        cursorY =
            ( ( mouseY - clipViewport.Y ) / clipViewport.Height ) * clipHeight;
    }

    cursorClip.setProperty( eFlashProperty::FP_X, cursorX );
    cursorClip.setProperty( eFlashProperty::FP_Y, cursorY );
}

void MngColorPalette::PaletteCallback( FlashRECT * /*pRect*/,
                                       float /*deltaTime*/,
                                       DrawList *pDrawList,
                                       void *pUserData,
                                       FlashResource *pResource,
                                       const char *pClipName ) {
    // FUN_006cc4b0
    MngColorPalette *pPalette = static_cast<MngColorPalette *>( pUserData );
    if ( pPalette == nullptr || pPalette->pFlashPlayer == nullptr ) {
        return;
    }

    char path[128];
    snprintf( path, sizeof( path ), "%s/%s", pPalette->clipPath, pClipName );

    FlashClip clip;
    if ( !pPalette->pFlashPlayer->getClipPlayer( path, &clip ) ) {
        return;
    }

    FlashRECT viewport;
    clip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );
    if ( pPalette->pMaterial == nullptr ) {
        return;
    }

    pDrawList->setActiveMaterial( pPalette->pMaterial );
    pDrawList->setDiffuse( 0, 0xffffffffu );
    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, viewport.X, viewport.Y,
                   viewport.X + viewport.Width, viewport.Y + viewport.Height,
                   pResource );
}
