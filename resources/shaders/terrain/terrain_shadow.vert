#version 450
#extension GL_GOOGLE_include_directive : require

// Terrain shadow map rendering vertex shader.

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"

#define gShadowContrast c.f[2]
#define gWorldViewProject( i ) c.f[8 + ( i )]

layout( location = 0 ) in vec4 in_Position0;
layout( location = 1 ) in vec4 in_Color0;
layout( location = 2 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;

void main() {
    gl_Position = vec4( dot( in_Position0, gWorldViewProject( 0 ) ),
                        dot( in_Position0, gWorldViewProject( 1 ) ),
                        dot( in_Position0, gWorldViewProject( 2 ) ),
                        dot( in_Position0, gWorldViewProject( 3 ) ) );

    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
    out_Texcoord1 = vec4( in_Position0.xy, 0.0, 0.0 );
    out_Texcoord2 = vec4( gShadowContrast.xyz, 0.0 );

    out_Color0 = clamp( in_Color0, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
