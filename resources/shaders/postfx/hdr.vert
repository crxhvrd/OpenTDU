#version 450
#extension GL_GOOGLE_include_directive : require

// HDR passes: the quad UV remapped into [uv_offset_01, 1 - uv_offset_01].

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_postfx.glsl"

#define uv_offset_01 c.f[58]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;

float remap( float t, float border ) {
    return ( -t + 1.0 ) * border + t * ( 1.0 + -border );
}

void main() {
    gl_Position = tdu_postfx_position( in_Position0 );
    out_Texcoord0 =
        vec4( remap( in_Position0.x * 0.5 + 0.5, uv_offset_01.x ),
              remap( in_Position0.y * -0.5 + 0.5, uv_offset_01.y ), 0.0, 0.0 );
    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
