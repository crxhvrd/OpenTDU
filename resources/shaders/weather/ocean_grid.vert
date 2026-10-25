#version 450
#extension GL_GOOGLE_include_directive : require

// Projected grid ocean surface (retail ocean table), world space vertices.

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"

#define gVSView( i ) c.f[24 + ( i )]
#define gVSViewProject( i ) c.f[28 + ( i )]
#define gVSInvView( i ) c.f[48 + ( i )]
#define gVSLightDir c.f[93]
#define gVSReflWorldViewProj( i ) c.f[125 + ( i )]
#define gVSTweakParams( i ) c.f[234 + ( i )]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gVSViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gVSViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gVSViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gVSViewProject( 3 ) ) );

    // Two normal map scales.
    out_Texcoord0 = in_Position0.xzxz * gVSTweakParams( 1 ).xxyy;
    vec3 cameraPosition =
        vec3( gVSInvView( 0 ).w, gVSInvView( 1 ).w, gVSInvView( 2 ).w );
    out_Texcoord1 = vec4( cameraPosition + -in_Position0.xyz, 0.0 );
    out_Texcoord2 = vec4( d3d9_dot( in_Position0, gVSReflWorldViewProj( 0 ) ),
                          d3d9_dot( in_Position0, gVSReflWorldViewProj( 1 ) ),
                          d3d9_dot( in_Position0, gVSReflWorldViewProj( 2 ) ),
                          d3d9_dot( in_Position0, gVSReflWorldViewProj( 3 ) ) );
    out_Texcoord3 = vec4( ( in_Position0.xz + gVSTweakParams( 2 ).zw ) *
                              gVSTweakParams( 2 ).xy,
                          0.0, 0.0 );
    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gVSView( 0 ) ),
                              d3d9_dot( in_Position0, gVSView( 1 ) ),
                              d3d9_dot( in_Position0, gVSView( 2 ) ) );
    out_Texcoord4 = vec4( viewPosition, 0.0 );

    // Per channel exponential falloff with the distance (gVSTweakParams[3]).
    float viewDistance =
        1.0 / inversesqrt( abs( d3d9_dot( viewPosition, viewPosition ) ) );
    vec3 falloff = viewDistance * -gVSTweakParams( 3 ).xyz * vec3( 1.442695 );
    out_Color0 = vec4( exp2( falloff.x ) * gVSTweakParams( 1 ).z,
                       exp2( falloff.z ), exp2( falloff.y ), 0.0 );
    out_Color1 = vec4( -gVSLightDir.xyz, 0.0 );

    out_Color0 = clamp( out_Color0, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = clamp( out_Color1, vec4( 0.0 ), vec4( 1.0 ) );
    d3d9_vs_epilogue();
}
