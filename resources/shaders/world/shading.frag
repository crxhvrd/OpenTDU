#version 450
#extension GL_GOOGLE_include_directive : require

// Vertex lit world geometry shadowed by the sector shadow map (Shading).
// The sun only lights what both the vertex diffuse term and the baked
// shadow map (channel z) see.
//
// Permutations:
//   SHADING_INSTANCE_LOD     sun colors and shadow cross-fade with the LOD
//                            (instance brightness in uv.z)
//   SHADING_DETAIL           detail map, faded out with the LOD
//   SHADING_PIXEL_SPECULAR   gloss masked per pixel specular
//   SHADING_VERTEX_SPECULAR  per vertex specular, scaled by the luminance

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gPSLightAmbient D3D9_C( 0 )
#define gPSLightDiffuse D3D9_C( 1 )
#define gPSLightSpecular D3D9_C( 2 )

const vec3 kLuminance = vec3( 0.299, 0.587, 0.114 );

D3D9_DECLARE_SAMPLER_2D( 0 ) // color map
#ifdef SHADING_DETAIL
D3D9_DECLARE_SAMPLER_2D( 1 ) // detail map
#endif
D3D9_DECLARE_SAMPLER_2D( 2 ) // sector shadow map
#ifdef SHADING_PIXEL_SPECULAR
D3D9_DECLARE_SAMPLER_2D( 3 ) // gloss map
#endif

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#ifdef SHADING_DETAIL
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#endif
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#if defined( SHADING_PIXEL_SPECULAR ) || defined( SHADING_VERTEX_SPECULAR )
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
#endif
#ifdef SHADING_PIXEL_SPECULAR
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
#endif
layout( location = D3D9_SLOT_TEXCOORD7 ) in vec4 in_Texcoord7;
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
layout( location = 0 ) out vec4 oC0;

#ifdef SHADING_INSTANCE_LOD
// lerp( color * brightness, color, lod ), in retail order.
vec3 lodColor( vec3 color, float brightness, float lod ) {
    return color * brightness + ( color * -brightness + color ) * lod;
}
#endif

void main() {
    vec4 uvBrightness = d3d9_ps_texcoord( in_Texcoord0 );
    vec4 shadowUVFadeLod = d3d9_ps_texcoord( in_Texcoord2 );
    float fog = d3d9_ps_texcoord( in_Texcoord7 ).x;

    float shadowMap = tdu_baked_shadow( tex2D_s2( shadowUVFadeLod.xy ).z );
    vec4 color = tex2D_s0( uvBrightness.xy );

#ifdef SHADING_INSTANCE_LOD
    float shadowTerm = d3d9_lerp( shadowMap + -0.2, 1.0, shadowUVFadeLod.w ) +
                       shadowUVFadeLod.z;
    vec3 sunDiffuse =
        lodColor( gPSLightDiffuse.rgb, uvBrightness.z, shadowUVFadeLod.w );
    vec3 sunSpecular =
        lodColor( gPSLightSpecular.rgb, uvBrightness.z, shadowUVFadeLod.w );
#else
    float shadowTerm = ( shadowMap + shadowUVFadeLod.z ) + -0.2;
    vec3 sunDiffuse = gPSLightDiffuse.rgb;
    vec3 sunSpecular = gPSLightSpecular.rgb;
#endif
    float sun = max( min( shadowTerm, in_Color0.a ), 0.0 );
    // OTDU_ADDITION: night lamps join the sun and ambient light.
    vec3 light = ( sunDiffuse * sun + gPSLightAmbient.rgb +
                   tdu_lamp_pixel( in_ViewPosition, in_ViewNormal ) ) *
                 in_Color0.rgb;
    color.rgb *= light;

#ifdef SHADING_DETAIL
    vec2 detailUV = d3d9_ps_texcoord( in_Texcoord1 ).xy;
    vec4 detail = tex2D_s1( detailUV );
    vec3 detailColor =
        d3d9_lerp( detail.rgb, vec3( 1.0 ), shadowUVFadeLod.www );
    color.a *= detail.a;
#endif

#ifdef SHADING_PIXEL_SPECULAR
    vec3 normal = normalize( d3d9_ps_texcoord( in_Texcoord3 ).xyz );
    vec3 lightDirection = normalize( d3d9_ps_texcoord( in_Texcoord4 ).xyz );
    float lightDotNormal = dot( lightDirection, normal );
    vec3 lightReflection =
        normal * -( lightDotNormal + lightDotNormal ) + lightDirection;
    float specular =
        max( dot( lightReflection,
                  normalize( d3d9_ps_texcoord( in_Texcoord5 ).xyz ) ),
             0.0 );
    specular *= specular;
    specular *= specular;
    vec3 gloss = tex2D_s3( detailUV ).rgb * ( specular * sunSpecular * sun );
    color.rgb = color.rgb * detailColor + gloss;
#else
#ifdef SHADING_DETAIL
    color.rgb *= detailColor;
#endif
#ifdef SHADING_VERTEX_SPECULAR
    vec4 specularTerm = d3d9_ps_texcoord( in_Texcoord3 );
    float luminanceScale =
        ( dot( color.rgb, kLuminance ) * specularTerm.y + -specularTerm.y ) +
        1.0;
    color.rgb =
        sunSpecular * ( sun * specularTerm.x ) * luminanceScale + color.rgb;
#endif
#endif

    oC0 = vec4( tdu_apply_fog( color.rgb, fog ), color.a );
    d3d9_ps_alpha_test( oC0 );
}
