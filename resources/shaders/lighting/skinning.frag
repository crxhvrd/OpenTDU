#version 450
#extension GL_GOOGLE_include_directive : require

// Vertex lit materials (Skinning table): light (Color0) times the base
// texture, vertex fog (Texcoord1.x).
//
// Permutations:
//   SKIN_TEXTURED  base texture (s0, Texcoord0)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#ifdef SKIN_TEXTURED
D3D9_DECLARE_SAMPLER_2D( 0 )
#endif

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
#ifdef SKIN_TEXTURED
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#endif
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = 0 ) out vec4 oC0;

void main() {
    float fog = d3d9_ps_texcoord( in_Texcoord1 ).x;
#ifdef SKIN_TEXTURED
    vec4 color = tex2D_s0( d3d9_ps_texcoord( in_Texcoord0 ).xy ) * in_Color0;
#else
    vec4 color = in_Color0;
#endif
    oC0 = vec4( tdu_apply_fog( color.xyz, fog ), color.w );
    d3d9_ps_alpha_test( oC0 );
}
