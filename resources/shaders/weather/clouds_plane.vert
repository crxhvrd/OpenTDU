#version 450
#extension GL_GOOGLE_include_directive : require

// Perlin noise cloud plane.

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"

#define gWorldViewProject( i ) D3D9_CR( 8, i )
#define gWorldView( i ) D3D9_CR( 20, i )
#define gVSMaterialEmissive D3D9_C( 241 )

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 3 ) ) );

    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gWorldView( 2 ) ) );

    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
    out_Texcoord1 = vec4( length( viewPosition ), 0.0, 0.0, 0.0 );

    out_Color0 =
        clamp( in_Color0 * gVSMaterialEmissive, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
