#version 450
#extension GL_GOOGLE_include_directive : require

// In car GPS screen: sun lit vertex color (Texcoord0), scaled UV set 0
// (gVSUVOffset.xy), vertex fog (Texcoord2.x), screen brightness
// (Texcoord3.x: gVSUVOffset.z).

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gWorldViewProject( i ) c.f[8 + ( i )]
#define gWorldView( i ) c.f[20 + ( i )]
#define gVSLightDir c.f[93]
#define gVSLightAmbient c.f[94]
#define gVSLightDiffuse c.f[95]
#define gVSUVOffset c.f[124]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 3 ) ) );
    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gWorldView( 2 ) ) );
    float viewDistance =
        1.0 / inversesqrt( abs( d3d9_dot( viewPosition, viewPosition ) ) );

    float diffuse = max( d3d9_dot( in_Normal0.xyz, -gVSLightDir.xyz ), 0.0 );
    vec3 light = gVSLightDiffuse.xyz * diffuse + gVSLightAmbient.xyz;
    out_Texcoord0 = vec4( light * in_Color0.xyz, in_Color0.w );
    out_Texcoord1 = vec4( in_Texcoord0.xy * gVSUVOffset.xy, 0.0, 0.0 );
    out_Texcoord2 = vec4( tdu_vertex_fog( viewDistance ), 0.0, 0.0, 0.0 );
    out_Texcoord3 = vec4( gVSUVOffset.z, 0.0, 0.0, 0.0 );
    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
