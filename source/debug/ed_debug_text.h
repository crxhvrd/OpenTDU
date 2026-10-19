#pragma once

#include "core/color.h"

enum eDebugDrawColor {
    DDC_Black = 0,
    DDC_Blue = 1,
    DDC_Red = 2,
    DDC_Magenta = 3,
    DDC_Green = 4,
    DDC_White_NoAlpha = 5,
    DDC_Yellow = 6,
    DDC_White = 7,
};

// Text render helper owned by edDebug (in the original pcode)
struct edDebugText {
    int32_t DrawPositionX = 0;
    int32_t DrawPositionY = 0;
    int32_t LineHeight = 12;
    eDebugDrawColor SelectedColor = DDC_White;
    ColorRGBA DrawColor;

    void setPosition( const int32_t x, const int32_t y );
    void setColor( const eDebugDrawColor color );

    void drawText( const char *pFormat, ... );
    void
    drawTextAt( const int32_t x, const int32_t y, const char *pFormat, ... );
};

extern edDebugText gEdDebugText;

ColorRGBA RetrieveDebugTextColor( const eDebugDrawColor colorIndex );
