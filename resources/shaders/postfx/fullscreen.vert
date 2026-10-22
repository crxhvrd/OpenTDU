#version 450
#extension GL_GOOGLE_include_directive : require

// Fullscreen quad.

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_postfx.glsl"

layout( location = 0 ) in vec4 in_Position0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;

void main() {
    gl_Position = tdu_postfx_position( in_Position0 );
    out_Texcoord0 = vec4( tdu_postfx_uv( in_Position0 ), 0.0, 0.0 );
    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
