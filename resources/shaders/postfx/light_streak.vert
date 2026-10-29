#version 450
#extension GL_GOOGLE_include_directive : require

// Light streak pass: 4 taps along texelInterval (0 to 3 steps).

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_postfx.glsl"

#define texelInterval D3D9_C( 58 )

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;

void main() {
    gl_Position = tdu_postfx_position( in_Position0 );
    vec2 uv = tdu_postfx_uv( in_Position0 );
    out_Texcoord0 = vec4( uv, uv + texelInterval.xy );
    out_Texcoord1 = vec4( vec2( 2.0 ) * texelInterval.xy + uv,
                          texelInterval.xy * vec2( 3.0 ) + uv );
    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
