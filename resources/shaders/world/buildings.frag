#version 450
#extension GL_GOOGLE_include_directive : require

// Vertex lit buildings.
//
// Permutations:
//   BUILDING_DETAIL     detail map, faded out with the LOD distance
//   BUILDING_GLOSS      reflection and specular scaled by the gloss map
//   BUILDING_ENVMAP     cube map reflection
//   BUILDING_SPHEREMAP  sphere map reflection
//   BUILDING_SPECULAR   vertex specular, attenuated on dark texels
//   BUILDING_FADE       alpha scaled by the LOD fade
//
// gPSMaterialParam = (reflection scale, reflection power, -, -)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gPSMaterialParam D3D9_C( 4 )

#if defined( BUILDING_ENVMAP ) || defined( BUILDING_SPHEREMAP )
#define BUILDING_REFLECTION
#endif

D3D9_DECLARE_SAMPLER_2D( 0 ) // color map
#ifdef BUILDING_DETAIL
D3D9_DECLARE_SAMPLER_2D( 1 ) // detail map
#endif
#ifdef BUILDING_GLOSS
D3D9_DECLARE_SAMPLER_2D( 2 ) // gloss map
#endif
#ifdef BUILDING_ENVMAP
D3D9_DECLARE_SAMPLER_CUBE( 3 ) // reflection
#elif defined( BUILDING_SPHEREMAP )
D3D9_DECLARE_SAMPLER_2D( 3 ) // reflection
#endif

layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#if defined( BUILDING_DETAIL ) || defined( BUILDING_GLOSS )
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#endif
#ifdef BUILDING_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
#endif
#ifdef BUILDING_SPECULAR
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
#endif
layout( location = D3D9_SLOT_TEXCOORD6 ) in vec4 in_Texcoord6;
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
#ifdef BUILDING_GLOSS
// OTDU
layout( location = OTDU_SLOT_OBJECT_POSITION ) in vec4 in_ObjectPosition;
#endif
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 light = d3d9_ps_texcoord( in_Texcoord0 );
    // OTDU_ADDITION: night lamps join the vertex light.
    light.rgb += tdu_lamp_pixel( in_ViewPosition, in_ViewNormal );
    vec4 colorUV = d3d9_ps_texcoord( in_Texcoord1 );
#if defined( BUILDING_DETAIL ) || defined( BUILDING_GLOSS )
    vec4 detailUV = d3d9_ps_texcoord( in_Texcoord2 );
#endif
    vec4 fog = d3d9_ps_texcoord( in_Texcoord6 );

    vec4 albedo = tex2D_s0( colorUV.xy );
    vec3 color = albedo.rgb;
#ifdef BUILDING_DETAIL
    color *= d3d9_lerp( tex2D_s1( detailUV.xy ).rgb, vec3( 1.0 ), detailUV.z );
#endif
#ifdef BUILDING_GLOSS
    float gloss = tex2D_s2( detailUV.xy ).x;
#endif
#ifdef BUILDING_SPECULAR
    float luminance = dot( color, vec3( 0.299, 0.587, 0.114 ) );
#endif

#ifdef BUILDING_REFLECTION
    vec4 reflectionUV = d3d9_ps_texcoord( in_Texcoord4 );
#ifdef BUILDING_ENVMAP
    vec4 environment = texCUBE_s3( reflectionUV.xyz );
#else
    vec4 environment = tex2D_s3( reflectionUV.xy );
#endif
    vec3 reflected =
        exp2( log2( abs( environment.rgb ) ) * gPSMaterialParam.y ) *
        gPSMaterialParam.x;
#ifdef BUILDING_GLOSS
    reflected = gloss * reflected;
#endif
    color = reflected * reflectionUV.w + color;
#endif

#ifdef BUILDING_SPECULAR
    vec4 specularParam = d3d9_ps_texcoord( in_Texcoord5 );
    vec3 specular =
        ( ( luminance * specularParam.w - specularParam.w ) + 1.0 ) *
        specularParam.rgb;
#ifdef BUILDING_GLOSS
    specular = gloss * specular;
#endif
    color = color * light.rgb + specular;
#else
    color = color * light.rgb;
#endif

#ifdef BUILDING_GLOSS
    // OTDU_ADDITION: lit windows at night. The gloss map marks the windows; a
    // window is a gloss texture tile on one floor (3.5 m) of the instance.
    if ( tdu_window_params.x > 0.0 ) {
        vec3 cell = vec3( floor( detailUV.xy ),
                          floor( in_ObjectPosition.y * ( 1.0 / 3.5 ) ) );
        vec3 random = tdu_hash3( cell + vec3( tdu_window_params.y ) );
        // Glass is grey: saturated glossy texels (paint, colored trims) are
        // not windows.
        float albedoMax = max( albedo.r, max( albedo.g, albedo.b ) );
        float albedoMin = min( albedo.r, min( albedo.g, albedo.b ) );
        float saturation = ( albedoMax - albedoMin ) / max( albedoMax, 1e-3 );
        float glass = 1.0 - smoothstep( 0.3, 0.7, saturation );
        // Windows are in walls: glossy roofs, domes and water face up.
        glass *= 1.0 - smoothstep( 0.35, 0.6, abs( in_ObjectPosition.w ) );
        if ( random.x < tdu_window_warm.w && glass > 0.0 ) {
            vec3 tint = ( random.y < tdu_window_cool.w ) ? tdu_window_cool.rgb
                                                         : tdu_window_warm.rgb;
            color += tint * ( glass * gloss * tdu_window_params.x *
                              ( 0.6 + 0.4 * random.z ) );
        }
    }
#endif
    float alpha = albedo.a;
#ifdef BUILDING_FADE
    alpha *= fog.y;
#endif

    oC0 = vec4( tdu_apply_fog( color, fog.x ), alpha );
    d3d9_ps_alpha_test( oC0 );
}
