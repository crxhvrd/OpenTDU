#version 450
#extension GL_GOOGLE_include_directive : require

// Radial blur: 6 taps along the direction to the blur center, the step
// scaled by a radial mask (s1, centered on the blur center).

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define radialBlurParams c.f[7]

D3D9_DECLARE_SAMPLER_2D( 0 ) // scene
D3D9_DECLARE_SAMPLER_2D( 1 ) // radial mask
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = 0 ) out vec4 oC0;

void main() {
    vec2 uv = d3d9_ps_texcoord( in_Texcoord0 ).xy;
    vec2 toCenter = -uv + radialBlurParams.xy;
    toCenter *= inversesqrt( abs( d3d9_dot( toCenter, toCenter ) + 0.0 ) );
    float mask = tex2D_s1( uv + -radialBlurParams.xy + vec2( 0.5 ) ).x;
    vec2 step = toCenter * mask;

    vec2 tap1 = step * radialBlurParams.zz + uv;
    vec2 tap2 = step * radialBlurParams.zz + tap1;
    vec2 tap3 = step * radialBlurParams.zz + tap2;
    vec2 tap4 = step * radialBlurParams.zz + tap3;
    vec2 tap5 = step * radialBlurParams.zz + tap4;
    vec4 center = tex2D_s0( uv );
    vec4 blurred = tex2D_s0( tap1 ) + center;
    blurred = tex2D_s0( tap2 ) + blurred;
    blurred = tex2D_s0( tap3 ) + blurred;
    blurred = tex2D_s0( tap4 ) + blurred;
    blurred = tex2D_s0( tap5 ) + blurred;
    blurred *= vec4( 0.16666667 );
    oC0 = ( -( mask * mask ) >= 0.0 ) ? center : blurred;
    d3d9_ps_alpha_test( oC0 );
}
