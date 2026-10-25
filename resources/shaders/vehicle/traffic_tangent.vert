#version 450
#extension GL_GOOGLE_include_directive : require

// Normal mapped traffic vehicles (VehicleTraffic): view and light vectors in
// tangent space, the tangent frame for the world space reflection.
//
// Permutations:
//   TRAFFIC_NORMAL_UV2  the normal map uses UV set 2 (Texcoord2)

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gVSWorldViewProject( i ) c.f[8 + ( i )]
#define gVSWorldView( i ) c.f[20 + ( i )]
#define gVSLightDir c.f[93]
#define gVSCamPosInObj c.f[119]
#define gEnvMapCoeff c.f[239]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
layout( location = D3D9_IN_TANGENT0 ) in vec4 in_Tangent0;
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
layout( location = D3D9_IN_TEXCOORD( 1 ) ) in vec4 in_Texcoord1;
#ifdef TRAFFIC_NORMAL_UV2
layout( location = D3D9_IN_TEXCOORD( 2 ) ) in vec4 in_Texcoord2;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
#ifdef TRAFFIC_NORMAL_UV2
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
#endif
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
layout( location = D3D9_SLOT_TEXCOORD5 ) out vec4 out_Texcoord5;
layout( location = D3D9_SLOT_TEXCOORD6 ) out vec4 out_Texcoord6;
layout( location = D3D9_SLOT_TEXCOORD7 ) out vec4 out_Texcoord7;
layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) out vec4 out_ViewNormal;     // OTDU

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gVSWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 3 ) ) );

    vec3 tangent = in_Tangent0.xyz;
    vec3 normal = in_Normal0.xyz;
    vec3 binormal = d3d9_cross( tangent, normal );
    vec3 toCamera = -in_Position0.xyz + gVSCamPosInObj.xyz;

    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gVSWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 2 ) ) );
    out_Texcoord3 =
        vec4( d3d9_dot( toCamera, tangent ), d3d9_dot( toCamera, binormal ),
              d3d9_dot( toCamera, normal ),
              tdu_vertex_fog( length( viewPosition ) ) );
    out_Texcoord4 = vec4( d3d9_dot( gVSLightDir.xyz, tangent ),
                          d3d9_dot( gVSLightDir.xyz, binormal ),
                          d3d9_dot( gVSLightDir.xyz, normal ), 0.0 );
    out_Texcoord5 = vec4( normal, 0.0 );
    out_Texcoord6 = vec4( binormal, 0.0 );
    out_Texcoord7 = vec4( tangent, 0.0 );

    out_Texcoord0 = vec4( in_Texcoord0.xy, gEnvMapCoeff.x, in_Texcoord0.w );
    out_Texcoord1 = vec4( in_Texcoord1.xy, 0.0, 0.0 );
#ifdef TRAFFIC_NORMAL_UV2
    out_Texcoord2 = vec4( in_Texcoord2.xy, 0.0, 0.0 );
#endif

    // OTDU_ADDITION: night lamps (the PS applies the vertex color).
    bool bLamps = tdu_lamps_enabled();
    out_ViewPosition = bLamps ? vec4( viewPosition, 1.0 ) : vec4( 0.0 );
    out_ViewNormal =
        bLamps ? vec4( d3d9_dot( normal, gVSWorldView( 0 ).xyz ),
                       d3d9_dot( normal, gVSWorldView( 1 ).xyz ),
                       d3d9_dot( normal, gVSWorldView( 2 ).xyz ), 0.0 )
               : vec4( 0.0 );

    out_Color0 = clamp( in_Color0, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
