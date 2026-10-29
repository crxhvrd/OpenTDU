#version 450
#extension GL_GOOGLE_include_directive : require

// Normal mapped road decals, shadowed by the sector shadow map.
//
// Permutations:
//   ROAD_SHADOW_CHANNEL_Y  shadow map channel (x by default)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gPSLightSpecular D3D9_C( 2 )

#ifdef ROAD_SHADOW_CHANNEL_Y
#define SHADOW_CHANNEL y
#else
#define SHADOW_CHANNEL x
#endif

D3D9_DECLARE_SAMPLER_2D( 0 ) // color map
D3D9_DECLARE_SAMPLER_2D( 1 ) // normal map
D3D9_DECLARE_SAMPLER_2D( 2 ) // sector shadow map
D3D9_DECLARE_SAMPLER_2D( 3 ) // gloss map

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec2 uv = d3d9_ps_texcoord( in_Texcoord0 ).xy;
    vec4 shadowUVFade = d3d9_ps_texcoord( in_Texcoord1 );
    vec3 lightDirection = d3d9_ps_texcoord( in_Texcoord2 ).xyz;
    vec3 sunDirection = d3d9_ps_texcoord( in_Texcoord4 ).xyz;
    float fog = d3d9_ps_texcoord( in_Texcoord5 ).x;

    vec3 normalSample = tex2D_s1( uv ).rgb + vec3( -0.5 );
    vec3 normal = normalize( normalSample + normalSample );

    float lightDotNormal = dot( lightDirection, normal );
    vec3 lightReflection =
        normal * -( lightDotNormal + lightDotNormal ) + lightDirection;
    float specular =
        max( dot( lightReflection,
                  normalize( d3d9_ps_texcoord( in_Texcoord3 ).xyz ) ),
             0.0 );
    specular *= specular;
    specular *= specular;

    float sunDiffuse = d3d9_saturate( dot( -sunDirection, normal ) + 0.2 );
    float shadow =
        min( sunDiffuse,
             tdu_baked_shadow( tex2D_s2( shadowUVFade.xy ).SHADOW_CHANNEL ) );

    vec4 color = tex2D_s0( uv );
    vec3 albedo = color.rgb;
    color.rgb =
        ( specular * gPSLightSpecular.x ) * tex2D_s3( uv ).rgb + color.rgb;
    color.rgb *= ( shadow + shadowUVFade.z ) * in_Color0.rgb;
    // OTDU_ADDITION: night lamps light the decal like the road (normal = up).
    if ( tdu_lamps_enabled() ) {
        color.rgb +=
            albedo * tdu_lamp_light( in_ViewPosition.xyz, tdu_view_up.xyz );
    }

    oC0 = vec4( tdu_apply_fog( color.rgb, fog ), color.a * in_Color0.a );
    d3d9_ps_alpha_test( oC0 );
}
