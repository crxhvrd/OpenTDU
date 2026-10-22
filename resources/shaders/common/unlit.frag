#version 450
#extension GL_GOOGLE_include_directive : require

// Unlit geometry (default table fallbacks and billboards, impostors).
//
// Permutations:
//   UNLIT_TEXTURED      base texture (s0)
//   UNLIT_VERTEX_COLOR  modulated by Color0
//   UNLIT_FOG           vertex fog (Texcoord1.x)
//   UNLIT_SOLID=c       a constant color (missing shader placeholders) when
//                       none of the above

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#ifdef UNLIT_TEXTURED
D3D9_DECLARE_SAMPLER_2D( 0 )
#endif

#ifdef UNLIT_VERTEX_COLOR
layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
#endif
#ifdef UNLIT_TEXTURED
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#endif
#ifdef UNLIT_FOG
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#endif
layout( location = 0 ) out vec4 oC0;

void main() {
#if defined( UNLIT_TEXTURED )
    vec4 color = tex2D_s0( d3d9_ps_texcoord( in_Texcoord0 ).xy );
#ifdef UNLIT_VERTEX_COLOR
    color *= in_Color0;
#endif
#elif defined( UNLIT_VERTEX_COLOR )
    vec4 color = in_Color0;
#elif defined( UNLIT_FOG )
    vec4 color = vec4( 1.0 );
#else
    vec4 color = UNLIT_SOLID;
#endif
#ifdef UNLIT_FOG
    color.xyz = tdu_apply_fog( color.xyz, d3d9_ps_texcoord( in_Texcoord1 ).x );
#endif
    oC0 = color;
    d3d9_ps_alpha_test( oC0 );
}
