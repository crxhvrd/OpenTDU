#version 450
#extension GL_GOOGLE_include_directive : require

// Shadow casters, rendered from the light into the shadow map.
//
// Permutations:
//   SHADOW_ALPHA_TEST   discards texels whose alpha is below Color0.a
//   SHADOW_FLOAT_DEPTH  float depth target (cockpit shadow map, R32F)
//
// The default A8R8G8B8 target packs the depth as (floor(z * 255) / 255,
// shadow color, translucency, fract(z * 255)); bright texels of alpha tested
// casters cast a lighter shadow.

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#ifdef SHADOW_ALPHA_TEST
D3D9_DECLARE_SAMPLER_2D( 0 ) // base map
#endif

#if defined( SHADOW_ALPHA_TEST ) || !defined( SHADOW_FLOAT_DEPTH )
layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
#endif
#ifdef SHADOW_ALPHA_TEST
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#endif
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = 0 ) out vec4 oC0;

void main() {
    float depth = d3d9_ps_texcoord( in_Texcoord1 ).x;

#ifdef SHADOW_ALPHA_TEST
    vec4 base = tex2D_s0( d3d9_ps_texcoord( in_Texcoord0 ).xy );
    if ( base.a + -in_Color0.a < 0.0 ) {
        demote;
    }
#endif

#ifdef SHADOW_FLOAT_DEPTH
    oC0 = vec4( depth, 1.0, 1.0, 1.0 );
#else
    float depthFraction = fract( depth * 255.0 );
    float depthHigh = ( depth * 255.0 + -depthFraction ) * 0.003921569;
#ifdef SHADOW_ALPHA_TEST
    float luminance = d3d9_dot( base.rgb, vec3( 0.299, 0.587, 0.114 ) );
    float translucency = ( -luminance + 1.0 ) * -in_Color0.y + 1.0;
#else
    float translucency = 1.0;
#endif
    oC0 = vec4( depthHigh, in_Color0.y, translucency, depthFraction );
#endif
    d3d9_ps_alpha_test( oC0 );
}
