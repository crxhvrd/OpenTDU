#version 450
#extension GL_GOOGLE_include_directive : require

// Vehicle dirt layer (table vehicle_dirt): vertex sun light, dirt amount by
// sun facing (Color0.w), optional ramp colors and reflection.
//
// Permutations:
//   DIRT_RAMP        two tone ramp colors by facing (Texcoord0)
//   DIRT_REFLECTION  world reflection vector (Texcoord5) and fresnel
//                    (Texcoord6.w)

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gVSWorldViewProject( i ) c.f[8 + ( i )]
#define gVSWorld( i ) c.f[16 + ( i )]
#define gVSWorldView( i ) c.f[20 + ( i )]
#define gVSMaterialParam c.f[88]
#define gVSLightDir c.f[93]
#define gVSLightAmbient c.f[94]
#define gVSLightDiffuse c.f[95]
#define gVSRampColor1 c.f[114]
#define gVSRampColor2 c.f[115]
#define gVSCamPosInObj c.f[119]
#define gVSRampColorPow c.f[116]
#define gUserVector3 c.f[3]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
layout( location = D3D9_IN_TEXCOORD( 1 ) ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
#ifdef DIRT_RAMP
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
#endif
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
#ifdef DIRT_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD5 ) out vec4 out_Texcoord5;
layout( location = D3D9_SLOT_TEXCOORD6 ) out vec4 out_Texcoord6;
#endif
layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) out vec4 out_ViewNormal;     // OTDU

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gVSWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 3 ) ) );

    vec3 normal = in_Normal0.xyz;
    float sunFacing = d3d9_dot( -gVSLightDir.xyz, normal );
    vec3 light =
        gVSLightDiffuse.xyz * max( sunFacing, 0.0 ) + gVSLightAmbient.xyz;
    out_Color0 =
        vec4( light * in_Color0.xyz, max( sunFacing + -gUserVector3.y, 0.0 ) );

#if defined( DIRT_RAMP ) || defined( DIRT_REFLECTION )
    vec4 toCamera4 = -in_Position0 + gVSCamPosInObj;
    vec3 toCamera =
        toCamera4.xyz * inversesqrt( abs( d3d9_dot( toCamera4, toCamera4 ) ) );
    float facingSigned = d3d9_dot( toCamera, normal );
#endif
#ifdef DIRT_RAMP
    float ramp = pow( abs( max( facingSigned, 0.0 ) ), gVSRampColorPow.x );
    out_Texcoord0 = ramp * gVSRampColor1 + ( -ramp + 1.0 ) * gVSRampColor2;
#endif

#ifdef DIRT_REFLECTION
    out_Texcoord2 = vec4( in_Texcoord0.xy, gVSMaterialParam.xy );
#else
    out_Texcoord2 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#endif
    out_Texcoord3 = vec4( in_Texcoord1.xy, 0.0, 0.0 );

    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gVSWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 2 ) ) );
    out_Texcoord4 = vec4( tdu_vertex_fog( length( viewPosition ) ),
                          gUserVector3.x, gUserVector3.z, 0.0 );

#ifdef DIRT_REFLECTION
    float viewDotNormal = d3d9_dot( -toCamera, normal );
    vec3 reflection =
        normalize( normal * -( viewDotNormal + viewDotNormal ) + -toCamera );
    out_Texcoord5 = vec4( d3d9_dot( reflection, gVSWorld( 2 ).xyz ),
                          d3d9_dot( reflection, gVSWorld( 1 ).xyz ),
                          d3d9_dot( reflection, gVSWorld( 0 ).xyz ), 0.0 );
    float fresnel =
        pow( abs( -abs( facingSigned ) + 1.0 ), gVSMaterialParam.w );
    out_Texcoord6 =
        vec4( in_Color0.xyz,
              ( 1.0 + -gVSMaterialParam.z ) * fresnel + gVSMaterialParam.z );
#endif

    // OTDU_ADDITION: night lamps; the vertex color tints them.
    bool bLamps = tdu_lamps_enabled();
    out_ViewPosition =
        bLamps ? vec4( viewPosition,
                       dot( in_Color0.rgb, vec3( 0.299, 0.587, 0.114 ) ) )
               : vec4( 0.0 );
    out_ViewNormal =
        bLamps ? vec4( d3d9_dot( normal, gVSWorldView( 0 ).xyz ),
                       d3d9_dot( normal, gVSWorldView( 1 ).xyz ),
                       d3d9_dot( normal, gVSWorldView( 2 ).xyz ), 0.0 )
               : vec4( 0.0 );

    out_Color0 = clamp( out_Color0, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
