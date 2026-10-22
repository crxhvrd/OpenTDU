#version 450
#extension GL_GOOGLE_include_directive : require

// Credits scene space background.

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"

#define gWVPMat( i ) c.f[8 + ( i )]

layout( location = 0 ) in vec4 in_Position0;
layout( location = 1 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gWVPMat( 0 ) ),
                        d3d9_dot( in_Position0, gWVPMat( 1 ) ),
                        d3d9_dot( in_Position0, gWVPMat( 2 ) ),
                        d3d9_dot( in_Position0, gWVPMat( 3 ) ) );
    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );

    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
