#version 450
#extension GL_GOOGLE_include_directive : require

// Vertex lit vehicle paint (table vehicle_paint): the sun light goes through
// Texcoord5, the two tone ramp color through Color1.
//
// Permutations:
//   PAINT_RAMP               ramp colors by facing (Color1)
//   PAINT_SPECULAR           sun specular (Texcoord0)
//   PAINT_UV0                UV set 0 (Texcoord1.xy)
//   PAINT_UV1                UV set 1 (Texcoord4)
//   PAINT_REFLECTION_CUBE    world reflection vector (Texcoord2)
//   PAINT_REFLECTION_SPHERE  sphere map coordinates (Texcoord2)

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
#define gVSLightSpecularParam c.f[98]
#define gVSRampColor1 c.f[114]
#define gVSRampColor2 c.f[115]
#define gVSRampColorPow c.f[116]
#define gVSCamPosInObj c.f[119]
#define gEnvMapCoeff c.f[239]

#if defined( PAINT_REFLECTION_CUBE ) || defined( PAINT_REFLECTION_SPHERE )
#define PAINT_REFLECTION
#endif

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
#ifdef PAINT_UV0
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#endif
#ifdef PAINT_UV1
layout( location = D3D9_IN_TEXCOORD( 1 ) ) in vec4 in_Texcoord1;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
#ifdef PAINT_SPECULAR
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
#endif
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
#ifdef PAINT_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
#endif
#ifdef PAINT_UV1
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
#endif
layout( location = D3D9_SLOT_TEXCOORD5 ) out vec4 out_Texcoord5;
layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) out vec4 out_ViewNormal;     // OTDU

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gVSWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 3 ) ) );

    vec3 normal = in_Normal0.xyz;
#if defined( PAINT_RAMP ) || defined( PAINT_SPECULAR ) ||                      \
    defined( PAINT_REFLECTION )
    vec4 toCamera4 = gVSCamPosInObj + -in_Position0;
    vec3 toCamera =
        toCamera4.xyz * inversesqrt( abs( d3d9_dot( toCamera4, toCamera4 ) ) );
    float viewDotNormal = d3d9_dot( toCamera, normal );
#endif

#ifdef PAINT_RAMP
    float ramp = pow( abs( max( viewDotNormal, 0.0 ) ), gVSRampColorPow.x );
    out_Color1 = ( 1.0 + -ramp ) * gVSRampColor2 + gVSRampColor1 * ramp;
#else
    out_Color1 = vec4( 0.0 );
#endif
#ifdef PAINT_SPECULAR
    float lightDotNormal = d3d9_dot( gVSLightDir.xyz, normal );
    vec3 lightReflection =
        normal * -( lightDotNormal + lightDotNormal ) + gVSLightDir.xyz;
    out_Texcoord0 =
        gVSLightSpecular *
        pow( abs( max( d3d9_dot( toCamera, lightReflection ), 0.0 ) ),
             gVSLightSpecularParam.x );
#endif

    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gVSWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 2 ) ) );
    float fog = tdu_vertex_fog( length( viewPosition ) );
#ifdef PAINT_UV0
    out_Texcoord1 = vec4( in_Texcoord0.xy, fog, 0.0 );
#else
    out_Texcoord1 = vec4( 0.0, 0.0, fog, 0.0 );
#endif

#ifdef PAINT_REFLECTION
#ifdef PAINT_REFLECTION_CUBE
    float mirrorDotNormal = d3d9_dot( normal, -toCamera );
    vec3 reflection = normalize(
        normal * -( mirrorDotNormal + mirrorDotNormal ) + -toCamera );
    out_Texcoord2 = vec4( d3d9_dot( reflection, gVSWorld( 2 ).xyz ),
                          d3d9_dot( reflection, gVSWorld( 1 ).xyz ),
                          d3d9_dot( reflection, gVSWorld( 0 ).xyz ), 0.0 );
#else
    vec3 reflection =
        normalize( toCamera + normal * -( viewDotNormal + viewDotNormal ) );
    out_Texcoord2 = vec4(
        d3d9_dot( reflection, gVSWorldView( 0 ).xyz ) * 0.5 + 0.5,
        d3d9_dot( reflection, gVSWorldView( 1 ).xyz ) * 0.5 + 0.5, 0.0, 0.0 );
#endif
    // The reflection stops at the silhouette (ceil, clamped to 1).
    float facing = viewDotNormal + gEnvMapCoeff.z;
    float fresnel =
        ( ( 1.0 + -gVSMaterialParam.z ) *
              pow( abs( 1.0 + -abs( viewDotNormal ) ), gVSMaterialParam.w ) +
          gVSMaterialParam.z ) *
        gEnvMapCoeff.x * gVSMaterialParam.x *
        min( facing + fract( -facing ), 1.0 );
    out_Texcoord3 = vec4( fresnel * in_Color0.xyz, gVSMaterialParam.y );
#endif
#ifdef PAINT_UV1
    out_Texcoord4 = vec4( in_Texcoord1.xy, 0.0, 0.0 );
#endif

    float diffuse = max( d3d9_dot( normal, -gVSLightDir.xyz ), 0.0 );
    out_Texcoord5 = vec4(
        ( gVSLightDiffuse.xyz * diffuse + gVSLightAmbient.xyz ) * in_Color0.xyz,
        gVSLightDiffuse.w );

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

    out_Color0 = vec4( 1.0 );
    out_Color1 = clamp( out_Color1, vec4( 0.0 ), vec4( 1.0 ) );
    d3d9_vs_epilogue();
}
