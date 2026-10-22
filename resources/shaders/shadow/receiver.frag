#version 450
#extension GL_GOOGLE_include_directive : require

// Shadow receivers: bilinear percentage closer filtering of the shadow map.
//
// Permutations:
//   SHADOW_FLOAT_DEPTH  float depth target (cockpit): 3x3 taps box filtered to
//                       2x2, shadow value Texcoord1.x
//
// Without it the A8R8G8B8 target is unpacked (see caster.frag): 2x2 taps,
// shadow value = translucency * shadow color.
//
// Texcoord1 = (shadow value, shadow floor, light depth, shadow map size),
// Texcoord2 = (fog, texel size).

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

D3D9_DECLARE_SAMPLER_2D( 0 ) // shadow map

layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
layout( location = 0 ) out vec4 oC0;

vec4 shadowParams;

#ifdef SHADOW_FLOAT_DEPTH
float shadowTap( vec2 uv ) {
    return ( tex2D_s0( uv ).x + -shadowParams.z >= 0.0 ) ? 1.0 : shadowParams.x;
}
#else
float shadowTap( vec2 uv ) {
    vec4 texel = tex2D_s0( uv );
    float depth = ( texel.x * 255.0 + texel.w ) * 0.003921569 + -shadowParams.z;
    return ( depth >= 0.0 ) ? 1.0 : texel.z * texel.y;
}
#endif

void main() {
    vec2 uv = d3d9_ps_texcoord( in_Texcoord0 ).xy;
    shadowParams = d3d9_ps_texcoord( in_Texcoord1 );
    vec4 fogParams = d3d9_ps_texcoord( in_Texcoord2 );
    float texel = fogParams.y;
    vec2 weights = fract( uv * shadowParams.ww );

#ifdef SHADOW_FLOAT_DEPTH
    float tap00 = shadowTap( uv );
    float tap10 = shadowTap( vec2( uv.x + texel, uv.y ) );
    float tap20 = shadowTap( vec2( 2.0 * texel + uv.x, uv.y ) );
    float tap01 = shadowTap( vec2( uv.x, uv.y + texel ) );
    float tap11 = shadowTap( uv + vec2( texel ) );
    float tap21 = shadowTap( vec2( texel ) * vec2( 2.0, 1.0 ) + uv );
    float tap02 = shadowTap( vec2( uv.x, 2.0 * texel + uv.y ) );
    float tap12 = shadowTap( vec2( texel ) * vec2( 1.0, 2.0 ) + uv );
    float tap22 = shadowTap( vec2( 2.0 ) * vec2( texel ) + uv );

    float top00 = ( ( tap10 + tap00 ) + tap01 + tap11 ) * 0.25;
    float top10 = ( tap20 + tap10 + tap11 + tap21 ) * 0.25;
    float bottom01 = ( tap11 + tap01 + tap02 + tap12 ) * 0.25;
    float bottom11 = ( tap11 + tap21 + tap12 + tap22 ) * 0.25;
    float top = d3d9_lerp( top00, top10, weights.x );
    float bottom = d3d9_lerp( bottom01, bottom11, weights.x );
#else
    float tap00 = shadowTap( uv );
    float tap10 = shadowTap( vec2( uv.x + texel, uv.y ) );
    float tap01 = shadowTap( vec2( uv.x, uv.y + texel ) );
    float tap11 = shadowTap( uv + vec2( texel ) );

    float top = d3d9_lerp( tap00, tap10, weights.x );
    float bottom = d3d9_lerp( tap01, tap11, weights.x );
#endif
    float shadow = d3d9_lerp( top, bottom, weights.y );

    shadow = max( shadow, min( shadowParams.y, 1.0 ) );
    oC0 = d3d9_lerp( vec4( shadow ), vec4( 1.0 ), fogParams.xxxx );
    d3d9_ps_alpha_test( oC0 );
}
