#version 450
#extension GL_GOOGLE_include_directive : require

// Fullscreen blends.
//
// Permutations:
//   BLEND_COLOR_RAMP         per channel color correction curve (s1)
//   BLEND_COLOR_RAMP_LERP    two curves (s1, s2) blended by interpo_coeff
//   BLEND_INVERT             1 - s0
//   BLEND_WEIGHTED           s0 * x + s1 * y (gBlendingParams)
//   BLEND_ADD                s0 + s1
//   BLEND_MODULATE_ADD       s0 + s1 * s2

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define interpo_coeff c.f[8]
#define gBlendingParams c.f[8]

D3D9_DECLARE_SAMPLER_2D( 0 )
#if !defined( BLEND_INVERT )
D3D9_DECLARE_SAMPLER_2D( 1 )
#endif
#if defined( BLEND_COLOR_RAMP_LERP ) || defined( BLEND_MODULATE_ADD )
D3D9_DECLARE_SAMPLER_2D( 2 )
#endif
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = 0 ) out vec4 oC0;

void main() {
    vec2 uv = d3d9_ps_texcoord( in_Texcoord0 ).xy;
#if defined( BLEND_COLOR_RAMP )
    vec4 color = tex2D_s0( uv );
    vec4 rampB = tex2D_s1( color.zz );
    oC0 = vec4( tex2D_s1( color.xx ).x, tex2D_s1( color.yy ).y, rampB.z,
                color.w );
#elif defined( BLEND_COLOR_RAMP_LERP )
    vec4 color = tex2D_s0( uv );
    vec4 rampR = tex2D_s1( color.xx );
    vec4 targetR = tex2D_s2( color.xx );
    vec4 rampG = tex2D_s1( color.yy );
    vec4 targetG = tex2D_s2( color.yy );
    vec4 ramp = tex2D_s1( color.zz );
    vec4 target = tex2D_s2( color.zz );
    ramp.xy = vec2( rampR.x, rampG.y );
    target.xy = vec2( targetR.x, targetG.y );
    oC0 = vec4( d3d9_lerp( ramp.xyz, target.xyz, interpo_coeff.xxx ), color.w );
#elif defined( BLEND_INVERT )
    oC0 = -tex2D_s0( uv ) + vec4( 1.0 );
#elif defined( BLEND_WEIGHTED )
    vec4 layer = tex2D_s1( uv ) * gBlendingParams.yyyy;
    oC0 = gBlendingParams.xxxx * tex2D_s0( uv ) + layer;
#elif defined( BLEND_ADD )
    oC0 = tex2D_s0( uv ) + tex2D_s1( uv );
#elif defined( BLEND_MODULATE_ADD )
    vec4 base = tex2D_s0( uv );
    oC0 = tex2D_s1( uv ) * tex2D_s2( uv ) + base;
#endif
    d3d9_ps_alpha_test( oC0 );
}
