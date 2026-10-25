#version 450
#extension GL_GOOGLE_include_directive : require

// Vertex lit traffic vehicles (VehicleTraffic): sun light, two tone ramp
// colors by facing, optional specular and environment reflection.
//
// Permutations:
//   TRAFFIC_SPECULAR    per vertex specular (Texcoord2)
//   TRAFFIC_REFLECTION  world reflection vector (Texcoord4) and fresnel
//                       (Texcoord3.w)

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
#define gVSLightSpecular c.f[96]
#define gVSRampColor1 c.f[114]
#define gVSRampColor2 c.f[115]
#define gVSCamPosInObj c.f[119]
#define gEnvMapCoeff c.f[239]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
layout( location = D3D9_IN_TEXCOORD( 1 ) ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
#ifdef TRAFFIC_SPECULAR
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
#endif
#ifdef TRAFFIC_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
#endif
layout( location = D3D9_SLOT_TEXCOORD5 ) out vec4 out_Texcoord5;
layout( location = D3D9_SLOT_TEXCOORD6 ) out vec4 out_Texcoord6;
layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) out vec4 out_ViewNormal;     // OTDU

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gVSWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 3 ) ) );

    vec3 normal = in_Normal0.xyz;
    float diffuse = max( d3d9_dot( -gVSLightDir.xyz, normal ), 0.0 );
    vec3 light = gVSLightDiffuse.xyz * diffuse + gVSLightAmbient.xyz;
    out_Color0 = vec4( light * in_Color0.xyz, gVSLightDiffuse.w );

    vec4 toCamera4 = gVSCamPosInObj - in_Position0;
    vec3 toCamera =
        toCamera4.xyz * inversesqrt( abs( d3d9_dot( toCamera4, toCamera4 ) ) );
    float facingSigned = d3d9_dot( toCamera, normal );
    float facing = max( facingSigned, 0.0 );
    out_Color1 = facing * gVSRampColor1 + ( -facing + 1.0 ) * gVSRampColor2;

#ifdef TRAFFIC_SPECULAR
    float lightDotNormal = d3d9_dot( gVSLightDir.xyz, normal );
    vec3 lightReflection =
        normal * -( lightDotNormal + lightDotNormal ) + gVSLightDir.xyz;
    float specular = max( d3d9_dot( lightReflection, toCamera ), 0.0 );
    float specular2 = specular * specular;
    out_Texcoord2 = ( specular * ( specular2 * specular2 ) ) * gVSLightSpecular;
#endif

#ifdef TRAFFIC_REFLECTION
    float viewDotNormal = d3d9_dot( -toCamera, normal );
    vec3 reflection =
        normalize( normal * -( viewDotNormal + viewDotNormal ) + -toCamera );
    float fresnel =
        pow( abs( -abs( facingSigned ) + 1.0 ), gVSMaterialParam.w );
    fresnel = ( 1.0 + -gVSMaterialParam.z ) * fresnel + gVSMaterialParam.z;
    out_Texcoord3 = vec4( in_Color0.xyz, fresnel );
    out_Texcoord4 = vec4( d3d9_dot( reflection, gVSWorld( 2 ).xyz ),
                          d3d9_dot( reflection, gVSWorld( 1 ).xyz ),
                          d3d9_dot( reflection, gVSWorld( 0 ).xyz ), fresnel );
    out_Texcoord1 = vec4( in_Texcoord1.xy, gVSMaterialParam.xy );
#else
    out_Texcoord1 = in_Texcoord1;
#endif
    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );

    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gVSWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 2 ) ) );
    out_Texcoord5 =
        vec4( tdu_vertex_fog( length( viewPosition ) ), 1.0, 0.0, 0.0 );
    out_Texcoord6 = vec4( gEnvMapCoeff.x, 0.0, 0.0, 0.0 );

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
    out_Color1 = clamp( out_Color1, vec4( 0.0 ), vec4( 1.0 ) );
    d3d9_vs_epilogue();
}
