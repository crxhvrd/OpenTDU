#include "shared.h"
#include "ed_debug_text.h"

#include "render/text_renderer.h"

#include <cstdarg>
#include <cstdio>

edDebugText gEdDebugText = {};

ColorRGBA RetrieveDebugTextColor( const eDebugDrawColor colorIndex ) {
    // FUN_0042ec00
    static const uint32_t gDebugDrawColors[8] = {
        0xff000000, 0xffff0000, 0xff0000ff, 0xffff00ff,
        0xff00ff00, 0xffffffff, 0xff00ffff, 0xffffffff,
    };

    return ColorRGBA( gDebugDrawColors[colorIndex] );
}

static void DrawFormattedText( const int32_t x,
                               const int32_t y,
                               const ColorRGBA &color,
                               const char *pFormat,
                               va_list args ) {
    char buffer[256];
    vsnprintf( buffer, sizeof( buffer ), pFormat, args );

    gTextRenderer.setColor( color );
    DrawDebugText( x, y, buffer );
}

void edDebugText::setPosition( const int32_t x, const int32_t y ) {
    DrawPositionX = x;
    DrawPositionY = y;
}

void edDebugText::setColor( const eDebugDrawColor color ) {
    SelectedColor = color;
    DrawColor = RetrieveDebugTextColor( color );
}

void edDebugText::drawText( const char *pFormat, ... ) {
    // FUN_00457330
    const int32_t x = DrawPositionX;

    va_list args;
    va_start( args, pFormat );
    DrawFormattedText( DrawPositionX, DrawPositionY, DrawColor, pFormat, args );
    va_end( args );

    DrawPositionY += LineHeight;
    DrawPositionX = ( x == INT32_MIN ) ? 0 : x;
}

void edDebugText::drawTextAt( const int32_t x,
                              const int32_t y,
                              const char *pFormat,
                              ... ) {
    // FUN_00457290
    DrawPositionX = x;
    DrawPositionY = y;

    va_list args;
    va_start( args, pFormat );
    DrawFormattedText( x, y, DrawColor, pFormat, args );
    va_end( args );
}
