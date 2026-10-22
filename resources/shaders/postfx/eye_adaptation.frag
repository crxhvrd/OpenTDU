#version 450
#extension GL_GOOGLE_include_directive : require

// Eye adaptation: moves the adapted luminance (s1) towards the current one
// (s0), only when the scene gets darker; brighter scenes apply at once.

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define gPSMaterialParam c.f[4]

D3D9_DECLARE_SAMPLER_2D( 0 ) // current luminance
D3D9_DECLARE_SAMPLER_2D( 1 ) // adapted luminance
layout( location = 0 ) out vec4 oC0;

void main() {
    float current = tex2D_s0( vec2( 0.5 ) ).x;
    float adapted = tex2D_s1( vec2( 0.5 ) ).x;
    float delta = current + -adapted;
    float adaptation = delta * gPSMaterialParam.x + adapted;
    oC0 = vec4( ( -delta >= 0.0 ) ? adaptation : current );
    d3d9_ps_alpha_test( oC0 );
}
