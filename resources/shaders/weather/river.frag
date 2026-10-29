#version 450
#extension GL_GOOGLE_include_directive : require

// River pixel shader: three scrolling normal maps distort a reflection that
// is blended over the ambient color with a fresnel term.
//
// Permutations:
//   RIVER_VERTEX_COLOR  modulates by the vertex color (alpha is blended too)
//   RIVER_ENVMAP        reflection looked up in the environment map instead
//                       of the planar reflection
//   RIVER_FADE          alpha is multiplied by the vertex fade
//
// gPSParams = (fresnel bias, fresnel scale, fresnel power, -)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gPSLightAmbient D3D9_C( 0 )
#define gPSLightSpecular D3D9_C( 2 )
#define gPSLightSpecularParam D3D9_C( 3 )
#define gPSWorld( i ) D3D9_CR( 8, i )
#define gPSParams D3D9_C( 23 )
#define gPSDiffuseFix D3D9_C( 24 )

D3D9_DECLARE_SAMPLER_2D( 0 ) // normal map 0
D3D9_DECLARE_SAMPLER_2D( 1 ) // normal map 1
D3D9_DECLARE_SAMPLER_2D( 2 ) // normal map 2
D3D9_DECLARE_SAMPLER_2D( 3 ) // planar reflection / environment map

#ifdef RIVER_VERTEX_COLOR
layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
#endif
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
#ifndef RIVER_ENVMAP
layout( location = D3D9_SLOT_TEXCOORD6 ) in vec4 in_Texcoord6;
#endif
layout( location = D3D9_SLOT_TEXCOORD7 ) in vec4 in_Texcoord7;
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 normalUV0 = d3d9_ps_texcoord( in_Texcoord0 );
    vec4 normalUV1 = d3d9_ps_texcoord( in_Texcoord1 );
    vec4 normalUV2 = d3d9_ps_texcoord( in_Texcoord2 );
    vec3 toLight = normalize( d3d9_ps_texcoord( in_Texcoord3 ).xyz );
    vec3 viewPosition = d3d9_ps_texcoord( in_Texcoord4 ).xyz;
    vec3 toCamera = normalize( d3d9_ps_texcoord( in_Texcoord5 ).xyz );
    vec4 falloff = d3d9_ps_texcoord( in_Texcoord7 );

    vec3 normalSum =
        tex2D_s2( normalUV2.xy ).xyz +
        ( tex2D_s0( normalUV0.xy ).xyz + tex2D_s1( normalUV1.xy ).xyz );
    vec3 normal = normalize( normalSum * ( 2.0 / 3.0 ) - 1.0 );

    float facing = dot( -toCamera, normal );
    vec3 viewReflection =
        normalize( normal * -( facing + facing ) + -toCamera );
    float specular = pow( abs( max( dot( viewReflection, toLight ), 0.0 ) ),
                          gPSLightSpecularParam.x );

#ifdef RIVER_ENVMAP
    vec2 reflectionUV =
        vec2( normal.x * falloff.x +
                  ( dot( viewReflection, gPSWorld( 0 ).xyz ) * 0.5 + 0.5 ),
              1.0 - ( dot( viewReflection, gPSWorld( 1 ).xyz ) * 0.5 + 0.5 ) );
#else
    vec4 reflectionPosition = d3d9_ps_texcoord( in_Texcoord6 );
    vec2 projected = reflectionPosition.xy * ( 1.0 / reflectionPosition.w );
    vec2 reflectionUV = vec2( normal.x * falloff.x + projected.x, projected.y );
#endif
    vec4 reflection = tex2D_s3( reflectionUV );

    float fresnel = min( pow( abs( facing + 1.0 ), gPSParams.z ) * gPSParams.y +
                             gPSParams.x,
                         1.0 ) *
                    falloff.y;

    // lerp( ambient, specular + reflection, fresnel ), in retail order
    vec4 color =
        vec4( gPSLightSpecular.rgb * specular + reflection.rgb, reflection.a );
    color = gPSDiffuseFix * -gPSLightAmbient + color;
    color = fresnel * color;
    color = gPSDiffuseFix * gPSLightAmbient + color;
#ifdef RIVER_VERTEX_COLOR
    color *= in_Color0;
    float alpha = color.a;
#else
    float alpha = gPSDiffuseFix.a;
#endif
#ifdef RIVER_FADE
    alpha *= falloff.z;
#endif

    float fog = tdu_pixel_fog( length( viewPosition ) );

    oC0 = vec4( tdu_apply_fog( color.rgb, fog ), alpha );
    d3d9_ps_alpha_test( oC0 );
}
