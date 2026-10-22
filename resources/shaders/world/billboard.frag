#version 450
#extension GL_GOOGLE_include_directive : require

// Camera facing billboards (retail billboards table).
//
// Permutations:
//   BILLBOARD_FADE           alpha scaled by Texcoord2.x (else plain white)
//   BILLBOARD_TEXTURED       base texture (s0)
//   BILLBOARD_VERTEX_COLOR   modulated by Color0 (untextured and uncolored
//                            fading sprites draw red)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#ifdef BILLBOARD_TEXTURED
D3D9_DECLARE_SAMPLER_2D( 0 )
#endif

#ifdef BILLBOARD_VERTEX_COLOR
layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
#endif
#ifdef BILLBOARD_TEXTURED
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#endif
#ifdef BILLBOARD_FADE
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#endif
layout( location = 0 ) out vec4 oC0;

void main() {
#ifdef BILLBOARD_FADE
#if defined( BILLBOARD_TEXTURED )
    vec4 color = tex2D_s0( d3d9_ps_texcoord( in_Texcoord0 ).xy );
#ifdef BILLBOARD_VERTEX_COLOR
    color *= in_Color0;
#endif
#elif defined( BILLBOARD_VERTEX_COLOR )
    vec4 color = in_Color0;
#else
    vec4 color = vec4( 1.0, 0.0, 0.0, 1.0 );
#endif
    color.w *= d3d9_ps_texcoord( in_Texcoord2 ).x;
    oC0 = color;
#else
    oC0 = vec4( 1.0 );
#endif
    d3d9_ps_alpha_test( oC0 );
}
