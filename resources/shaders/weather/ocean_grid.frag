#version 450
#extension GL_GOOGLE_include_directive : require

// Projected grid ocean surface (retail ocean table): two normal map layers
// (s1), sun specular, blended over the detail map (s2) by a fresnel term.
//
// Permutations:
//   OCEAN_GRID_REFLECTION  planar reflection (s0) distorted by the normal,
//                          else a flat color (gPSShadingParams[1])

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define gPSLightSpecular D3D9_C( 2 )
#define gPSShadingParams( i ) D3D9_CR( 23, i )

#ifdef OCEAN_GRID_REFLECTION
D3D9_DECLARE_SAMPLER_2D( 0 ) // samplerReflection
#endif
D3D9_DECLARE_SAMPLER_2D( 1 ) // samplerNormalMap
D3D9_DECLARE_SAMPLER_2D( 2 ) // samplerDetailMap

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
layout( location = D3D9_SLOT_COLOR1 ) centroid in vec4 in_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#ifdef OCEAN_GRID_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#endif
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 t0 = d3d9_ps_texcoord( in_Texcoord0 );
    vec3 normal =
        ( tex2D_s1( t0.zw ).xyz + vec3( -0.5 ) ) * gPSShadingParams( 0 ).zzz;
    normal =
        gPSShadingParams( 0 ).yyy * ( tex2D_s1( t0.xy ).xyz + vec3( -0.5 ) ) +
        normal;
    normal.xz *= in_Color0.yy;
    normal = normalize( normal );

    vec3 toCamera = normalize( d3d9_ps_texcoord( in_Texcoord1 ).xyz );
    float cosView = dot( -toCamera, normal );
    vec3 reflected = normalize( normal * -( cosView + cosView ) + -toCamera );
    float specular =
        pow( abs( max( dot( reflected, normalize( in_Color1.xyz ) ), 0.0 ) ),
             gPSShadingParams( 0 ).x );

#ifdef OCEAN_GRID_REFLECTION
    vec4 t2 = d3d9_ps_texcoord( in_Texcoord2 );
    vec2 reflectionUV = ( 1.0 / t2.w ) * t2.xy;
    reflectionUV.x = in_Color0.x * normal.x + reflectionUV.x;
    vec4 color = specular * gPSLightSpecular + tex2D_s0( reflectionUV );
#else
    vec4 color = specular * gPSLightSpecular + gPSShadingParams( 1 );
#endif

    float fresnel = gPSShadingParams( 2 ).y *
                        pow( abs( cosView + 1.0 ), gPSShadingParams( 2 ).z ) +
                    gPSShadingParams( 2 ).x;
    fresnel = min( fresnel, 1.0 ) * in_Color0.z;
    oC0 = d3d9_lerp( tex2D_s2( d3d9_ps_texcoord( in_Texcoord3 ).xy ), color,
                     vec4( fresnel ) );
    d3d9_ps_alpha_test( oC0 );
}
