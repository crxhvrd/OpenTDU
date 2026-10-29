#version 450
#extension GL_GOOGLE_include_directive : require

// Eye adaptation pass: a single pixel quad.

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"

#define gVSM4WorldViewProj( i ) D3D9_CR( 8, i )

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gVSM4WorldViewProj( 0 ) ),
                        d3d9_dot( in_Position0, gVSM4WorldViewProj( 1 ) ),
                        d3d9_dot( in_Position0, gVSM4WorldViewProj( 2 ) ),
                        d3d9_dot( in_Position0, gVSM4WorldViewProj( 3 ) ) );
    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
