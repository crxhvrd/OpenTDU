#pragma once

#include "binary_helpers.h"

struct ColorRGBA {
    union {
        struct {
            uint8_t R;
            uint8_t G;
            uint8_t B;
            uint8_t A;
        };
        uint32_t Color;
    };

    constexpr uint8_t &operator[]( const int32_t i ) {
        switch ( i ) {
        case 0:
            return R;
        case 1:
            return G;
        case 2:
            return B;
        case 3:
            return A;
        default:
            OTDU_ASSERT( false );
            return R;
        }
    }
    constexpr uint8_t operator[]( const int32_t i ) const {
        switch ( i ) {
        case 0:
            return R;
        case 1:
            return G;
        case 2:
            return B;
        case 3:
            return A;
        default:
            OTDU_ASSERT( false );
            return R;
        }
    }

    constexpr ColorRGBA( const uint8_t r = 0x00,
                         const uint8_t g = 0x00,
                         const uint8_t b = 0x00,
                         const uint8_t a = 0xff )
        : R( r ), G( g ), B( b ), A( a ) {}

    constexpr ColorRGBA( const uint32_t color ) : Color( color ) {}
};

static constexpr ColorRGBA gColorBlackNoAlpha( 0x00, 0x00, 0x00, 0x00 );
static constexpr ColorRGBA gColorBlack( 0x00, 0x00, 0x00 );
static constexpr ColorRGBA gColorRed( 0xff, 0x00, 0x00 );
static constexpr ColorRGBA gColorBlue( 0x00, 0xff, 0x00 );
static constexpr ColorRGBA gColorGreen( 0x00, 0x00, 0xff );
static constexpr ColorRGBA gColorWhite( 0xff, 0xff, 0xff );

static ColorRGBA PackFloatColor( const Eigen::Vector4f &color ) {
    // FUN_004300f0
    auto toByte = []( float channel ) -> uint8_t {
        float scaled = channel * 255.0f + 0.5f;
        if ( scaled < 0.0f ) {
            scaled = 0.0f;
        }
        if ( scaled > 255.0f ) {
            scaled = 255.0f;
        }
        return (uint8_t)scaled;
    };

    ColorRGBA packedColor;
    packedColor.R = toByte( color.x() );
    packedColor.G = toByte( color.y() );
    packedColor.B = toByte( color.z() );
    packedColor.A = toByte( color.w() );
    return packedColor;
}

static Eigen::Vector4f UnpackColorBytes( const ColorRGBA &color ) {
    static constexpr float kByteToFloat = 0.003921569f; // DAT_00f41680
    return Eigen::Vector4f( color.R * kByteToFloat, color.G * kByteToFloat,
                            color.B * kByteToFloat, color.A * kByteToFloat );
}

static ColorRGBA TruncateColorToBytes( const float *pColor ) {
    static constexpr float kFloatToByte = 255.0f; // DAT_00f41688
    ColorRGBA packedColor;
    packedColor.R = (uint8_t)FloatToInt( pColor[0] * kFloatToByte );
    packedColor.G = (uint8_t)FloatToInt( pColor[1] * kFloatToByte );
    packedColor.B = (uint8_t)FloatToInt( pColor[2] * kFloatToByte );
    packedColor.A = (uint8_t)FloatToInt( pColor[3] * kFloatToByte );
    return packedColor;
}

static ColorRGBA TruncateColorToBytesOpaque( const Eigen::Vector4f &color ) {
    ColorRGBA packedColor = TruncateColorToBytes( color.data() );
    packedColor.A = 0xff;
    return packedColor;
}
