#version 450
#extension GL_GOOGLE_include_directive : require

// Normal mapped road decals.

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gVSColorContrast D3D9_C( 1 )
#define gVSDiffuseFixedSun D3D9_C( 4 )
#define gVSWorldViewProject( i ) D3D9_CR( 8, i )
#define gVSWorldView( i ) D3D9_CR( 20, i )
#define gVSLightDir D3D9_C( 93 )
#define gVSCamPosInObj D3D9_C( 119 )

const float kSectorSize = 768.0;

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
layout( location = D3D9_IN_TANGENT0 ) in vec4 in_Tangent0;
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
layout( location = D3D9_SLOT_TEXCOORD5 ) out vec4 out_Texcoord5;
layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gVSWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 3 ) ) );

    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gVSWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 2 ) ) );
    float viewDistance = length( viewPosition );

    float shadowFade = max( -viewDistance + kSectorSize, 0.0 ) *
                       gVSColorContrast.w * ( 1.0 / kSectorSize );
    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
    out_Texcoord1 =
        vec4( in_Position0.xz * ( 1.0 / kSectorSize ), shadowFade, 0.0 );

    vec3 tangent = in_Tangent0.xyz;
    vec3 tangentCrossNormal = d3d9_cross( tangent, in_Normal0.xyz );
    out_Texcoord2 = vec4( d3d9_dot( gVSLightDir.xyz, tangent ),
                          d3d9_dot( gVSLightDir.xyz, tangentCrossNormal ),
                          d3d9_dot( gVSLightDir.xyz, in_Normal0.xyz ), 0.0 );
    vec3 toCamera = gVSCamPosInObj.xyz - in_Position0.xyz;
    out_Texcoord3 = vec4( d3d9_dot( toCamera, tangent ),
                          d3d9_dot( toCamera, tangentCrossNormal ),
                          d3d9_dot( toCamera, in_Normal0.xyz ), 0.0 );
    out_Texcoord4 = vec4( gVSDiffuseFixedSun.xyz, 0.0 );
    out_Texcoord5 = vec4( tdu_vertex_fog( viewDistance ), 0.0, 0.0, 0.0 );
    // OTDU_ADDITION: night lamps.
    out_ViewPosition =
        tdu_lamps_enabled() ? vec4( viewPosition, 1.0 ) : vec4( 0.0 );

    out_Color0 = clamp( vec4( gVSColorContrast.rgb, in_Color0.a ), vec4( 0.0 ),
                        vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
