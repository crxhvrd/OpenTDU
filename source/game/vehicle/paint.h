#pragma once

#include "core/color.h"
#include "render/material.h"

enum class ePaintType {
    PT_Default = 0,
    PT_CustomSolid = 1,
    PT_CustomMetal = 2,
    PT_CustomSolidTwoTones = 3,
    PT_CustomMetalTwoTones = 4
};

enum eCarColorPart : int32_t {
    CCP_BodyColor = 0,
    CCP_InteriorColor1 = 1,
    CCP_InteriorColor2 = 2,
    CCP_LogoColor1 = 3,
    CCP_LogoColor2 = 4,
    CCP_InteriorMaterial = 5,
    CCP_BodyColor2 = 6,
};

struct VehiclePaint {
    ePaintType Type;

    int32_t PaintIndex;
    int32_t InteriorIndex;

    ColorRGBA CustomMainColor;
    ColorRGBA CustomSecondColor;
    ColorRGBA CustomTwoToneColor1;
    ColorRGBA CustomTwoToneColor2;

    uint8_t bSaved : 1;

    inline void clear() {
        Type = ePaintType::PT_Default;
        PaintIndex = 0;
        InteriorIndex = 0;
        CustomMainColor = 0u;
        CustomSecondColor = 0u;
        CustomTwoToneColor1 = 0u;
        CustomTwoToneColor2 = 0u;
        bSaved = false;
    }
};

static constexpr float kDeluxeColorScale =
    0.7f; // DAT_00fab978 / FLOAT_00fab980

// Deluxe paint shading params (uniforms bound to paint PS)
struct DeluxeSurfaceSettings {
    float RampExp;
    float SpecularExp;
    float EnvMap[4];
};

static DeluxeSurfaceSettings gSolidSurfaceSettings = {
    // DAT_f99470
    1.0f,
    9.0f,
    { 0.6f, 2.0f, 0.3f, 1.0f }
};
static DeluxeSurfaceSettings gMetalSurfaceSettings = {
    // DAT_f99488
    1.0f,
    4.0f,
    { 0.6f, 2.0f, 0.3f, 2.0f }
};

static ColorRGBA ScaleDeluxeColor( const ColorRGBA &color, const float scale ) {
    ColorRGBA scaled;
    for ( int32_t i = 0; i < 3; i++ ) {
        scaled[i] = (uint8_t)FloatToInt( (float)color[i] * scale );
    }
    scaled.A = color.A;
    return scaled;
}

static void ComputeDeluxeShades( const ColorRGBA &param_2,
                                 const ePaintType paintType,
                                 ColorRGBA *pOutDarkened,
                                 ColorRGBA *pOutSpecular ) {
    // FUN_0071f250
    if ( pOutDarkened == nullptr || paintType == ePaintType::PT_Default ) {
        return;
    }

    pOutDarkened->A = 0xff;
    for ( int32_t i = 0; i < 3; i++ ) {
        ( *pOutDarkened )[i] =
            (uint8_t)Clamp( (int32_t)param_2[i] - 100, 0, 255 );
    }

    if ( pOutSpecular == nullptr ) {
        return;
    }

    pOutSpecular->A = 0xff;
    if ( paintType == ePaintType::PT_CustomSolid ||
         paintType == ePaintType::PT_CustomSolidTwoTones ) {
        // Solid paints get a flat specular, whatever the colour.
        pOutSpecular->R = 0x1e;
        pOutSpecular->G = 0x1e;
        pOutSpecular->B = 0x1e;
    } else if ( paintType == ePaintType::PT_CustomMetal ||
                paintType == ePaintType::PT_CustomMetalTwoTones ) {
        for ( int32_t i = 0; i < 3; i++ ) {
            ( *pOutSpecular )[i] = (uint8_t)FloatToInt(
                (float)param_2[i] * kDeluxeColorScale ); // DAT_00fab978
        }
    }
}

static bool IsPaintShaderGUID( const uint64_t guid, const bool bNPaint ) {
    if ( !bNPaint ) {
        return guid == 0x183157b81c6c7357ull || guid == 0xb39c35015789e65cull ||
               guid == 0xabcdeffedcbaabcdull;
    }

    return guid == 0xc288dfc455ae5ef8ull || guid == 0x1b38348331d3942aull ||
           guid == 0x69f07f72719bf862ull || guid == 0xcab12190a00d5c4eull ||
           guid == 0x6638348331d66466ull;
}

static bool IsPaintOrTwoTonesShaderGUID( const uint64_t guid,
                                         const bool bNPaint ) {
    if ( IsPaintShaderGUID( guid, bNPaint ) ) {
        return true;
    }

    return ( !bNPaint ) ? ( guid == 0x213157b8bc6c7363ull )
                        : ( guid == 0x222157b8bc6c7222ull ||
                            guid == 0x111157b8bc6c7111ull );
}

static bool IsCarInteriorShaderGUID( const uint64_t guid ) {
    return guid == 0xb39c35015789e65cull || guid == 0x69f07f72719bf862ull ||
           guid == 0xcab12190a00d5c4eull;
}

static bool IsCarMaterialShaderGUID( const uint64_t guid ) {
    return guid == 0x183157b81c6c7357ull || guid == 0x1b38348331d3942aull ||
           guid == 0xc288dfc455ae5ef8ull;
}

static bool IsCarPaintShaderGUID( const uint64_t guid ) {
    return guid == 0x213157b8bc6c7363ull || guid == 0x222157b8bc6c7222ull ||
           guid == 0x111157b8bc6c7111ull;
}

struct InteriorTextureRemap {
    int32_t Dst;
    int32_t Src;
};

static const InteriorTextureRemap kRemapPoolA[2][5] = {
    {{ 1, 1 },
     { 2, 2 },
     { 5, 3 },
     { -1, -1 },
     { -1, -1 }                                        }, // non normal mapped
    {{ 1, 1 }, { 2, 2 }, { 3, 3 }, { 5, 4 }, { -1, -1 }}, // _N
};
static const InteriorTextureRemap kRemapPoolB[2][5] = {
    {{ 1, 1 },
     { 2, 2 },
     { 3, 5 },
     { -1, -1 },
     { -1, -1 }                                        }, // non normal mapped
    {{ 1, 1 }, { 2, 2 }, { 3, 3 }, { 4, 5 }, { -1, -1 }}, // _N
};

static void RefreshClonedMaterialFlags( Material *pMaterial,
                                        const bool bNPaint ) {
    // FUN_008b9980
    MaterialParameter *pParameters = ( pMaterial != nullptr )
                                         ? pMaterial->getParameterByIndex( 0 )
                                         : nullptr;
    if ( pParameters == nullptr ) {
        return;
    }

    MaterialLayer *pLayer = pParameters->getLayer( 0 );
    if ( pLayer == nullptr ) {
        return;
    }

    auto hasTexture = [&]( const int32_t slot ) {
        return pLayer->pLayerTextures[slot].Hashcode != 0ull;
    };

    MaterialParameterFlags &flags = pParameters->Flags;
    const uint64_t guid = pParameters->Hashcode;

    const bool bIsTwoTones =
        ( guid == 0x69f07f72719bf862ull || guid == 0x111157b8bc6c7111ull ||
          guid == 0x213157b8bc6c7363ull || guid == 0x222157b8bc6c7222ull );
    const bool bIsPaint =
        ( guid == 0xb39c35015789e65cull || guid == 0xcab12190a00d5c4eull );

    if ( bIsTwoTones ) {
        flags.pCustomFlags[3] = hasTexture( 1 );
        flags.bUsingGlossMap = hasTexture( bNPaint ? 3 : 2 );
        flags.pCustomFlags[4] = hasTexture( bNPaint ? 4 : 3 );

        flags.bUsingBinormal = 1;
        flags.bUsingTangent = 1;
        flags.bUsingNormals = 1;
        flags.NumPrelight = 1;
        return;
    }

    if ( !bIsPaint ) {
        return;
    }

    flags.pCustomFlags[1] = hasTexture( 1 );
    flags.bUsingGlossMap = hasTexture( bNPaint ? 3 : 2 );
    flags.pCustomFlags[3] = hasTexture( bNPaint ? 5 : 4 );
    if ( hasTexture( bNPaint ? 4 : 3 ) ) {
        flags.bUsingReflection = 1;
    }

    flags.bUsingNormals = 1;
    flags.NumPrelight = 1;
}
