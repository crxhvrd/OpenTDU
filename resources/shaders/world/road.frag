#version 450
#extension GL_GOOGLE_include_directive : require

// Roads.
//
// Permutations:
//   ROAD_MARKS, ROAD_MARKS2   marks maps blended over the color map
//   ROAD_DETAIL               detail map
//   ROAD_SPECULAR             gloss masked sun specular
//   ROAD_SHADOW               sector shadow map, either alone or with:
//     ROAD_SHADOW_LIT         + shadow fade, modulated by the vertex color
//     ROAD_SUN_DIFFUSE        + fixed sun diffuse term (needs ROAD_SHADOW_LIT)
//   ROAD_SHADOW_CHANNEL_Y     shadow map channel (x by default)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gLightSpecularPS c.f[2]

#ifdef ROAD_SHADOW_CHANNEL_Y
#define SHADOW_CHANNEL y
#else
#define SHADOW_CHANNEL x
#endif

D3D9_DECLARE_SAMPLER_2D( 0 ) // color map
#ifdef ROAD_MARKS
D3D9_DECLARE_SAMPLER_2D( 1 ) // marks map
#endif
#ifdef ROAD_MARKS2
D3D9_DECLARE_SAMPLER_2D( 2 ) // marks map 2
#endif
#ifdef ROAD_DETAIL
D3D9_DECLARE_SAMPLER_2D( 3 ) // detail map
#endif
#ifdef ROAD_SPECULAR
D3D9_DECLARE_SAMPLER_2D( 4 ) // gloss map
#endif
#ifdef ROAD_SHADOW
D3D9_DECLARE_SAMPLER_2D( 5 ) // sector shadow map
#endif

#ifdef ROAD_SHADOW_LIT
layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
#endif
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#if defined( ROAD_MARKS ) || defined( ROAD_MARKS2 )
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#endif
#ifdef ROAD_DETAIL
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#endif
#ifdef ROAD_SHADOW
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
#endif
#if defined( ROAD_SPECULAR ) || defined( ROAD_SUN_DIFFUSE )
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
#endif
#ifdef ROAD_SPECULAR
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
layout( location = D3D9_SLOT_TEXCOORD6 ) in vec4 in_Texcoord6;
#endif
#ifdef ROAD_SUN_DIFFUSE
layout( location = D3D9_SLOT_TEXCOORD7 ) in vec4 in_Texcoord7;
#endif
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 uvBiasFog = d3d9_ps_texcoord( in_Texcoord0 );

    vec3 color = tex2D_s0( uvBiasFog.xy ).rgb;
#if defined( ROAD_MARKS ) || defined( ROAD_MARKS2 )
    vec4 marksUV = d3d9_ps_texcoord( in_Texcoord1 );
#endif
#ifdef ROAD_MARKS
    vec4 marks = tex2Dbias_s1( marksUV.xy, uvBiasFog.z );
    color = d3d9_lerp( color, marks.rgb, marks.aaa );
#endif
#ifdef ROAD_MARKS2
    vec4 marks2 = tex2Dbias_s2( marksUV.zw, uvBiasFog.z );
    color = d3d9_lerp( color, marks2.rgb, marks2.aaa );
#endif

#if defined( ROAD_SPECULAR ) || defined( ROAD_SUN_DIFFUSE )
    vec3 normal = normalize( d3d9_ps_texcoord( in_Texcoord4 ).xyz );
#endif
#ifdef ROAD_DETAIL
    vec2 detailUV = d3d9_ps_texcoord( in_Texcoord2 ).xy;
    vec3 detail = tex2D_s3( detailUV ).rgb;
#ifdef ROAD_SPECULAR
    vec3 lightDirection = d3d9_ps_texcoord( in_Texcoord5 ).xyz;
    float lightDotNormal = dot( lightDirection, normal );
    vec3 lightReflection =
        normal * -( lightDotNormal + lightDotNormal ) + lightDirection;
    float specular =
        max( dot( lightReflection,
                  normalize( d3d9_ps_texcoord( in_Texcoord6 ).xyz ) ),
             0.0 );
    specular *= specular;
    specular *= specular;
    vec3 gloss = tex2D_s4( detailUV ).rgb * ( specular * gLightSpecularPS.rgb );
    color = color * detail + gloss;
#else
    color *= detail;
#endif
#endif

    vec3 albedo = color;

#ifdef ROAD_SHADOW
    vec4 shadowUVFade = d3d9_ps_texcoord( in_Texcoord3 );
    float shadowMap =
        tdu_baked_shadow( tex2D_s5( shadowUVFade.xy ).SHADOW_CHANNEL );
#ifdef ROAD_SHADOW_LIT
#ifdef ROAD_SUN_DIFFUSE
    float sunDiffuse =
        max( dot( -d3d9_ps_texcoord( in_Texcoord7 ).xyz, normal ), 0.0 );
    float shadow = min( sunDiffuse + 0.2, shadowMap );
#else
    float shadow = min( shadowMap, 1.0 );
#endif
    color *= ( shadow + shadowUVFade.z ) * in_Color0.rgb;
#else
    color *= shadowMap;
#endif
#endif

    // OTDU_ADDITION: night lamps light the road (normal = up).
    if ( tdu_lamps_enabled() ) {
        color +=
            albedo * tdu_lamp_light( in_ViewPosition.xyz, tdu_view_up.xyz );
    }

    oC0 = vec4( tdu_apply_fog( color, uvBiasFog.w ), 1.0 );
    d3d9_ps_alpha_test( oC0 );
}
