#version 450
#extension GL_GOOGLE_include_directive : require

// Electrical network / power line wires: black, fogged and faded.

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 fogFade = d3d9_ps_texcoord( in_Texcoord0 );
    oC0 = vec4( tdu_apply_fog( vec3( 0.0 ), fogFade.x ), fogFade.y );
    d3d9_ps_alpha_test( oC0 );
}
