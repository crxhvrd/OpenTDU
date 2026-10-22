#version 450
#extension GL_GOOGLE_include_directive : require

// Normal mapped trees, lit in tangent space.
//
// Permutations:
//   TREE_FADE            alpha is multiplied by the LOD fade
//   TREE_SHADOW          modulated by the shadow texture
//   TREE_INSTANCE_COLOR  lighting blends towards the instance color with
//                        the LOD
//   TREE_SPECULAR        specular scaled by the albedo luminance, faded out
//                        with the LOD

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gPSLightAmbient c.f[0]
#define gPSLightDiffuse c.f[1]
#define gPSLightSpecular c.f[2]
#define gPSLightSpecularParam c.f[3]
#define gPSInstanceColor c.f[27]
#define gPSLightAmbient1 c.f[29]

const vec3 kLuminance = vec3( 0.299, 0.587, 0.114 );

D3D9_DECLARE_SAMPLER_2D( 0 ) // color map
D3D9_DECLARE_SAMPLER_2D( 1 ) // normal map
#ifdef TREE_SHADOW
D3D9_DECLARE_SAMPLER_2D( 2 ) // shadow texture
#endif

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#ifdef TREE_SPECULAR
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#endif
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#ifdef TREE_SHADOW
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
#endif
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec2 uv = d3d9_ps_texcoord( in_Texcoord0 ).xy;
    vec3 lightDirection = normalize( d3d9_ps_texcoord( in_Texcoord2 ).xyz );
    vec4 fogLodFade = d3d9_ps_texcoord( in_Texcoord4 );

    vec4 albedo = tex2D_s0( uv );
    vec3 normal = normalize( 2.0 * tex2D_s1( uv ).xyz + vec3( -1.0 ) );

    float diffuse = max( dot( normal, -lightDirection ), 0.0 );
    vec3 light = gPSLightDiffuse.rgb * diffuse + gPSLightAmbient.rgb;
#ifdef TREE_INSTANCE_COLOR
    light = d3d9_lerp( light,
                       gPSLightAmbient1.rgb * gPSInstanceColor.rgb +
                           gPSLightAmbient.rgb,
                       fogLodFade.yyy );
#endif

#ifdef TREE_SPECULAR
    float lightDotNormal = dot( lightDirection, normal );
    vec3 lightReflection =
        normal * -( lightDotNormal + lightDotNormal ) + lightDirection;
    float specular =
        pow( abs( max( dot( lightReflection,
                            normalize( d3d9_ps_texcoord( in_Texcoord1 ).xyz ) ),
                       0.0 ) ),
             gPSLightSpecularParam.x );
    vec3 specularColor =
        specular * gPSLightSpecular.rgb * dot( albedo.rgb, kLuminance );
    specularColor = fogLodFade.yyy * -specularColor + specularColor;
    light = in_Color0.rgb * light + specularColor;
#else
    light *= in_Color0.rgb;
#endif

    vec3 color = albedo.rgb * light;
    float alpha = albedo.a;
#ifdef TREE_SHADOW
    vec4 shadow = tex2D_s2( d3d9_ps_texcoord( in_Texcoord3 ).xy );
    color *= tdu_baked_shadow( shadow.rgb );
    alpha = shadow.a * alpha;
#endif
    // OTDU_ADDITION: night lamps, not shadowed by the baked sun shadow.
    color += albedo.rgb * in_Color0.rgb *
             tdu_lamp_pixel( in_ViewPosition, in_ViewNormal );
#ifdef TREE_FADE
    alpha *= fogLodFade.z;
#endif

    oC0 = vec4( tdu_apply_fog( color, fogLodFade.x ), alpha );
    d3d9_ps_alpha_test( oC0 );
}
