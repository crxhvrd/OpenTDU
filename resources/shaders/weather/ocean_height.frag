#version 450
#extension GL_GOOGLE_include_directive : require

// Ocean height map passes (retail ocean table).
//
// Permutations:
//   OCEAN_HEIGHT_NOISE  sum of two noise octaves (s0, s1), centered on 0
//   (default)           s0 * gPSMaterialParam + gPSMaterialParam

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define gPSMaterialParam c.f[4]

D3D9_DECLARE_SAMPLER_2D( 0 )
#ifdef OCEAN_HEIGHT_NOISE
D3D9_DECLARE_SAMPLER_2D( 1 )
#endif
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#ifdef OCEAN_HEIGHT_NOISE
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#endif
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 noise = tex2D_s0( d3d9_ps_texcoord( in_Texcoord0 ).xy );
#ifdef OCEAN_HEIGHT_NOISE
    float height = noise.x + tex2D_s1( d3d9_ps_texcoord( in_Texcoord1 ).xy ).x;
    oC0 = vec4( height ) + vec4( -0.5 );
#else
    oC0 = noise * gPSMaterialParam + gPSMaterialParam;
#endif
    d3d9_ps_alpha_test( oC0 );
}
