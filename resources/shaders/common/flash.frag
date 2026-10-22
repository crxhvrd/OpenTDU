#version 450
#extension GL_GOOGLE_include_directive : require

// Flash UI shapes: the color comes through Texcoord0.
//
// Permutations:
//   FLASH_TEXTURED    bitmap fill (s0, Texcoord1) modulated by the color
//   FLASH_ALPHA_MASK  the color, its alpha scaled by the red channel of s0

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#if defined( FLASH_TEXTURED ) || defined( FLASH_ALPHA_MASK )
D3D9_DECLARE_SAMPLER_2D( 0 )
#endif
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#if defined( FLASH_TEXTURED ) || defined( FLASH_ALPHA_MASK )
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#endif
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 color = d3d9_ps_texcoord( in_Texcoord0 );
#if defined( FLASH_TEXTURED )
    oC0 = tex2D_s0( d3d9_ps_texcoord( in_Texcoord1 ).xy ) * color;
#elif defined( FLASH_ALPHA_MASK )
    oC0 = vec4( color.xyz,
                tex2D_s0( d3d9_ps_texcoord( in_Texcoord1 ).xy ).x * color.w );
#else
    oC0 = color;
#endif
    d3d9_ps_alpha_test( oC0 );
}
