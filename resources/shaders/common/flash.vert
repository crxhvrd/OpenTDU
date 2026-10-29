#version 450
#extension GL_GOOGLE_include_directive : require

// Flash UI shapes: the color goes through Texcoord0.
//
// Permutations:
//   FLASH_TEXCOORD  forwards UV set 0 (Texcoord1)

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"

#define gWorldViewProject( i ) D3D9_CR( 8, i )

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
#ifdef FLASH_TEXCOORD
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
#ifdef FLASH_TEXCOORD
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
#endif

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 3 ) ) );
    out_Texcoord0 = in_Color0;
#ifdef FLASH_TEXCOORD
    out_Texcoord1 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#endif
    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
