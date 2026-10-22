#version 450
#extension GL_GOOGLE_include_directive : require

// Textured sprite (cloud billboards, sun flares).
//
// Permutations:
//   SPRITE_VERTEX_COLOR  modulates the texture by the vertex color

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

D3D9_DECLARE_SAMPLER_2D( 0 )

#ifdef SPRITE_VERTEX_COLOR
layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
#endif
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 uv = d3d9_ps_texcoord( in_Texcoord0 );

    oC0 = tex2D_s0( uv.xy );
#ifdef SPRITE_VERTEX_COLOR
    oC0 *= in_Color0;
#endif
    d3d9_ps_alpha_test( oC0 );
}
