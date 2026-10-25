#version 450
#extension GL_GOOGLE_include_directive : require

// Vertex lit vehicle materials (table vehicle_materials): sun light, two
// light specular, environment reflection.
//
// Permutations:
//   MATERIALS_SPECULAR          specular of both lights (Texcoord0), the second
//                               at half the exponent and intensity
//   MATERIALS_REFLECTION_CUBE   world reflection vector (cube map)
//   MATERIALS_REFLECTION_SPHERE sphere map coordinates (view space)
//   MATERIALS_UV_MATERIAL       UV set 0 for the material map (Texcoord1)
//   MATERIALS_UV_DETAIL         UVs for the detail / gloss / shadow maps
//                               (Texcoord2.xy): set 1 with the material map,
//                               else set 0

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
#define gVSLightDir1 c.f[101]
#define gVSLightSpecular1 c.f[104]
#define gVSCamPosInObj c.f[119]
#define gEnvMapCoeff c.f[239]

#if defined( MATERIALS_REFLECTION_CUBE ) ||                                    \
    defined( MATERIALS_REFLECTION_SPHERE )
#define MATERIALS_REFLECTION
#endif

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
#if defined( MATERIALS_UV_MATERIAL ) || defined( MATERIALS_UV_DETAIL )
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#endif
#if defined( MATERIALS_UV_MATERIAL ) && defined( MATERIALS_UV_DETAIL )
layout( location = D3D9_IN_TEXCOORD( 1 ) ) in vec4 in_Texcoord1;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
#ifdef MATERIALS_SPECULAR
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
#endif
#ifdef MATERIALS_UV_MATERIAL
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
#endif
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
#ifdef MATERIALS_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
#endif
layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) out vec4 out_ViewNormal;     // OTDU

// Specular of a light: (normalized view vector . reflected light) ^ exponent.
float specularTerm( vec3 lightDirection,
                    vec3 normal,
                    vec3 toCamera,
                    float exponent ) {
    float lightDotNormal = d3d9_dot( lightDirection, normal );
    vec3 reflected =
        normal * -( lightDotNormal + lightDotNormal ) + lightDirection;
    return pow( abs( max( d3d9_dot( toCamera, reflected ), 0.0 ) ), exponent );
}

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gVSWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 3 ) ) );

    vec3 normal = in_Normal0.xyz;
    float diffuse = max( d3d9_dot( normal, -gVSLightDir.xyz ), 0.0 );
    out_Color0 = vec4( ( gVSLightDiffuse.xyz * diffuse + gVSLightAmbient.xyz ) *
                           in_Color0.xyz,
                       1.0 );

#if defined( MATERIALS_SPECULAR ) || defined( MATERIALS_REFLECTION )
    vec4 toCamera4 = gVSCamPosInObj + -in_Position0;
    vec3 toCamera =
        toCamera4.xyz * inversesqrt( abs( d3d9_dot( toCamera4, toCamera4 ) ) );
#endif
#ifdef MATERIALS_SPECULAR
    float specular0 = specularTerm( gVSLightDir.xyz, normal, toCamera,
                                    gVSLightSpecularParam.x );
    float specular1 = specularTerm( gVSLightDir1.xyz, normal, toCamera,
                                    gVSLightSpecularParam.x * 0.5 );
    out_Texcoord0 =
        gVSLightSpecular1 * specular1 * 0.5 + gVSLightSpecular * specular0;
#endif

#ifdef MATERIALS_UV_MATERIAL
    out_Texcoord1 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#endif
    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gVSWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 2 ) ) );
    float fog = tdu_vertex_fog( length( viewPosition ) );
#if defined( MATERIALS_UV_MATERIAL ) && defined( MATERIALS_UV_DETAIL )
    out_Texcoord2 = vec4( in_Texcoord1.xy, fog, 0.0 );
#elif defined( MATERIALS_UV_DETAIL )
    out_Texcoord2 = vec4( in_Texcoord0.xy, fog, 0.0 );
#else
    out_Texcoord2 = vec4( 0.0, 0.0, fog, 0.0 );
#endif

#ifdef MATERIALS_REFLECTION
    float viewDotNormal = d3d9_dot( toCamera, normal );
#ifdef MATERIALS_REFLECTION_CUBE
    float mirrorDotNormal = d3d9_dot( normal, -toCamera );
    vec3 reflection = normalize(
        normal * -( mirrorDotNormal + mirrorDotNormal ) + -toCamera );
    out_Texcoord3 = vec4( d3d9_dot( reflection, gVSWorld( 2 ).xyz ),
                          d3d9_dot( reflection, gVSWorld( 1 ).xyz ),
                          d3d9_dot( reflection, gVSWorld( 0 ).xyz ), 0.0 );
#else
    vec3 reflection =
        normalize( toCamera + normal * -( viewDotNormal + viewDotNormal ) );
    out_Texcoord3 = vec4(
        d3d9_dot( reflection, gVSWorldView( 0 ).xyz ) * 0.5 + 0.5,
        d3d9_dot( reflection, gVSWorldView( 1 ).xyz ) * 0.5 + 0.5, 0.0, 0.0 );
#endif
    float fresnel =
        ( ( 1.0 + -gVSMaterialParam.z ) *
              pow( abs( 1.0 + -abs( viewDotNormal ) ), gVSMaterialParam.w ) +
          gVSMaterialParam.z ) *
        gVSMaterialParam.x * gEnvMapCoeff.x;
    out_Texcoord4 = vec4( fresnel * in_Color0.xyz, gVSMaterialParam.y );
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
