#version 450
#extension GL_GOOGLE_include_directive : require

// Ocean pixel shader: two scrolling normal maps lit by the sun.
//
// Permutations:
//   OCEAN_VERTEX_COLOR       modulates by the vertex color
//   OCEAN_DETAIL             detail map (UV0) instead of gPSDiffuseFix
//   OCEAN_ENVMAP             reflection looked up in the environment map
//   OCEAN_PLANAR_REFLECTION  reflection looked up in the planar reflection
//
// gPSSeaParams = (reflection scale, reflection power, alpha, reflection blend)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gPSLightAmbient D3D9_C( 0 )
#define gPSLightDiffuse D3D9_C( 1 )
#define gPSLightSpecular D3D9_C( 2 )
#define gPSLightSpecularParam D3D9_C( 3 )
#define gPSWorld( i ) D3D9_CR( 8, i )
#define gPSSeaParams D3D9_C( 23 )
#define gPSDiffuseFix D3D9_C( 24 )
#define gPSReflectionBluriness D3D9_C( 26 )

#if defined( OCEAN_ENVMAP ) || defined( OCEAN_PLANAR_REFLECTION )
#define OCEAN_REFLECTION
#endif

#ifdef OCEAN_DETAIL
D3D9_DECLARE_SAMPLER_2D( 0 ) // detail map
#endif
D3D9_DECLARE_SAMPLER_2D( 1 ) // normal map 0
D3D9_DECLARE_SAMPLER_2D( 2 ) // normal map 1
#ifdef OCEAN_REFLECTION
D3D9_DECLARE_SAMPLER_2D( 3 ) // environment map / planar reflection
#endif

#ifdef OCEAN_VERTEX_COLOR
layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
#endif
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
#ifdef OCEAN_DETAIL
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
#endif
#ifdef OCEAN_PLANAR_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD6 ) in vec4 in_Texcoord6;
#endif
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 normalUV0 = d3d9_ps_texcoord( in_Texcoord0 );
    vec4 normalUV1 = d3d9_ps_texcoord( in_Texcoord1 );
    vec3 lightDirection = normalize( d3d9_ps_texcoord( in_Texcoord2 ).xyz );
    vec3 viewPosition = d3d9_ps_texcoord( in_Texcoord3 ).xyz;
    vec3 toCamera = normalize( d3d9_ps_texcoord( in_Texcoord4 ).xyz );

    vec3 normal = normalize( tex2D_s1( normalUV0.xy ).xyz +
                             tex2D_s2( normalUV1.xy ).xyz - 1.0 );

    float diffuse = max( dot( normal, -lightDirection ), 0.0 );
    vec3 lightReflection = normalize( reflect( lightDirection, normal ) );
    float specular = pow( abs( max( dot( lightReflection, toCamera ), 0.0 ) ),
                          gPSLightSpecularParam.x );

#ifdef OCEAN_DETAIL
    vec3 albedo = tex2D_s0( d3d9_ps_texcoord( in_Texcoord5 ).xy ).rgb;
#else
    vec3 albedo = gPSDiffuseFix.rgb;
#endif
    vec3 color =
        ( gPSLightDiffuse.rgb * diffuse + gPSLightAmbient.rgb ) * albedo;

#ifdef OCEAN_REFLECTION
#ifdef OCEAN_ENVMAP
    vec3 viewReflection = normalize( reflect( -toCamera, normal ) );
    vec2 reflectionUV =
        vec2( dot( viewReflection, gPSWorld( 0 ).xyz ) + 0.5,
              1.0 - ( dot( viewReflection, gPSWorld( 1 ).xyz ) + 0.5 ) );
#else
    vec4 reflectionPosition = d3d9_ps_texcoord( in_Texcoord6 );
    vec2 reflectionUV = reflectionPosition.xy * ( 1.0 / reflectionPosition.w ) +
                        ( 1.0 / gPSReflectionBluriness.x ) * normal.x;
#endif
    vec3 reflection =
        exp2( gPSSeaParams.y *
              log2( abs( gPSSeaParams.x * tex2D_s3( reflectionUV ).rgb ) ) );
    color = d3d9_lerp( color, reflection, gPSSeaParams.w );
#endif
    color += gPSLightSpecular.rgb * specular;

    float alpha = gPSSeaParams.z;
#ifdef OCEAN_VERTEX_COLOR
    color *= in_Color0.rgb;
    alpha *= in_Color0.a;
#endif

    float fog = tdu_pixel_fog( length( viewPosition ) );

    oC0 = vec4( tdu_apply_fog( color, fog ), alpha );
    d3d9_ps_alpha_test( oC0 );
}
