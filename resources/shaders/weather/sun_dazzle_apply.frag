#version 450
#extension GL_GOOGLE_include_directive : require

// Applies the dazzle coefficient as a white full screen overlay.

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

D3D9_DECLARE_SAMPLER_2D( 0 ) // dazzle coefficient

layout( location = 0 ) out vec4 oC0;

void main() {
    oC0 = vec4( vec3( 1.0 ), tex2D_s0( vec2( 0.5 ) ).x );
    d3d9_ps_alpha_test( oC0 );
}
