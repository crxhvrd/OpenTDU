#version 450
#extension GL_GOOGLE_include_directive : require

// Avatar clothes (avatar table): the base color under up to 3 tinted
// layers, lit by the vertex light (Texcoord0) times the occlusion map plus
// the vertex specular, then the fog.
//
// Permutations:
//   CLOTHES_LAYER1..3          tinted layer (s1..s3), blended by its alpha
//   CLOTHES_DETAIL             detail map (s4 red, Texcoord5) multiplying
//                              the albedo before layer 3
//   CLOTHES_DETAIL_SPECULAR    the detail map masks the specular instead

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"
#include "tdu_avatar.glsl"

#define gPSBaseColor c.f[18]
#define gPSLayer1Color c.f[19]
#define gPSLayer2Color c.f[20]
#define gPSLayer3Color c.f[21]

#if defined( CLOTHES_DETAIL ) || defined( CLOTHES_DETAIL_SPECULAR )
#define CLOTHES_DETAIL_MAP
#endif

D3D9_DECLARE_SAMPLER_2D( 0 ) // gOcclusionSampler
#ifdef CLOTHES_LAYER1
D3D9_DECLARE_SAMPLER_2D( 1 ) // gLayer1Sampler
#endif
#ifdef CLOTHES_LAYER2
D3D9_DECLARE_SAMPLER_2D( 2 ) // gLayer2Sampler
#endif
#ifdef CLOTHES_LAYER3
D3D9_DECLARE_SAMPLER_2D( 3 ) // gLayer3Sampler
#endif
#ifdef CLOTHES_DETAIL_MAP
D3D9_DECLARE_SAMPLER_2D( 4 ) // gDetailSampler
#endif

layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#ifdef CLOTHES_LAYER1
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#endif
#ifdef CLOTHES_LAYER2
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
#endif
#ifdef CLOTHES_LAYER3
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
#endif
#ifdef CLOTHES_DETAIL_MAP
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
#endif
layout( location = D3D9_SLOT_TEXCOORD6 ) in vec4 in_Texcoord6;
layout( location = D3D9_SLOT_TEXCOORD7 ) in vec4 in_Texcoord7;
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 t0 = d3d9_ps_texcoord( in_Texcoord0 );
#ifdef CLOTHES_DETAIL_MAP
    float detail = tex2D_s4( d3d9_ps_texcoord( in_Texcoord5 ).xy ).x;
#endif

    vec3 albedo = gPSBaseColor.xyz;
    float alpha = gPSBaseColor.w;
#ifdef CLOTHES_LAYER1
    vec4 layer1 = tex2D_s1( d3d9_ps_texcoord( in_Texcoord2 ).xy );
    float coverage1 = layer1.w * gPSLayer1Color.w;
    albedo = coverage1 * ( gPSLayer1Color.xyz * layer1.xyz + -albedo ) + albedo;
    alpha = max( alpha, coverage1 );
#endif
#ifdef CLOTHES_LAYER2
    vec4 layer2 = tex2D_s2( d3d9_ps_texcoord( in_Texcoord3 ).xy );
    float coverage2 = layer2.w * gPSLayer2Color.w;
    albedo = coverage2 * ( gPSLayer2Color.xyz * layer2.xyz + -albedo ) + albedo;
    alpha = max( alpha, coverage2 );
#endif
#ifdef CLOTHES_DETAIL
    albedo = detail * albedo;
#endif
#ifdef CLOTHES_LAYER3
    vec4 layer3 = tex2D_s3( d3d9_ps_texcoord( in_Texcoord4 ).xy );
    float coverage3 = layer3.w * gPSLayer3Color.w;
    albedo = coverage3 * ( gPSLayer3Color.xyz * layer3.xyz + -albedo ) + albedo;
    alpha = max( alpha, coverage3 );
#endif

#ifdef CLOTHES_DETAIL_SPECULAR
    float specular = detail * t0.w;
#else
    float specular = t0.w;
#endif
    float occlusion = tex2D_s0( d3d9_ps_texcoord( in_Texcoord1 ).xy ).x;
    // OTDU_ADDITION: night lamps join the vertex light.
    vec3 light = occlusion * ( t0.xyz + tdu_lamp_pixel( in_ViewPosition,
                                                        in_ViewNormal ) ) +
                 specular * d3d9_ps_texcoord( in_Texcoord6 ).xyz;
    vec3 color = tdu_avatar_shade( light, albedo );
    oC0 = vec4( d3d9_lerp( gFogColorPS.xyz, color,
                           d3d9_ps_texcoord( in_Texcoord7 ).xxx ),
                alpha );
    d3d9_ps_alpha_test( oC0 );
}
