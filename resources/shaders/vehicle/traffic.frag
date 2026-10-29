#version 450
#extension GL_GOOGLE_include_directive : require

// Vertex lit traffic vehicles (VehicleTraffic).
//
// Permutations:
//   TRAFFIC_DECAL       decal map (s1) blended over the painted base
//   TRAFFIC_REFLECTION  environment cube map (s3) with the vertex fresnel
//   TRAFFIC_CAR_LIGHTS  car light mask (s4) and light map (s5)
//   TRAFFIC_SPECULAR    per vertex specular

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"
#include "tdu_vehicle.glsl"

#define gRampCarLights D3D9_C( 14 )

D3D9_DECLARE_SAMPLER_2D( 0 ) // color map
#ifdef TRAFFIC_DECAL
D3D9_DECLARE_SAMPLER_2D( 1 ) // decal map
#endif
#ifdef TRAFFIC_REFLECTION
D3D9_DECLARE_SAMPLER_CUBE( 3 ) // reflection
#endif
#ifdef TRAFFIC_CAR_LIGHTS
D3D9_DECLARE_SAMPLER_2D( 4 ) // car light mask
D3D9_DECLARE_SAMPLER_2D( 5 ) // car light colors
#endif

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
layout( location = D3D9_SLOT_COLOR1 ) centroid in vec4 in_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#if defined( TRAFFIC_DECAL ) || defined( TRAFFIC_REFLECTION ) ||               \
    defined( TRAFFIC_CAR_LIGHTS )
#define TRAFFIC_UV1
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#endif
#ifdef TRAFFIC_SPECULAR
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#endif
#ifdef TRAFFIC_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
layout( location = D3D9_SLOT_TEXCOORD6 ) in vec4 in_Texcoord6;
#endif
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 colorUV = d3d9_ps_texcoord( in_Texcoord0 );
#ifdef TRAFFIC_UV1
    vec4 decalUVParams = d3d9_ps_texcoord( in_Texcoord1 );
#endif
    float fog = d3d9_ps_texcoord( in_Texcoord5 ).x;

    vec4 albedo = tex2D_s0( colorUV.xy );
    vec3 base = albedo.xyz * in_Color1.xyz;
#ifdef TRAFFIC_DECAL
    vec4 decal = tex2D_s1( decalUVParams.xy );
    base = d3d9_lerp( base, decal.xyz, decal.www );
#endif

    // OTDU_ADDITION: night lamps join the vertex light.
    vec3 light =
        in_Color0.xyz + tdu_lamp_pixel( in_ViewPosition, in_ViewNormal );
#ifdef TRAFFIC_CAR_LIGHTS
    TDUCarLights carLights =
        tdu_car_lights( tex2D_s4( decalUVParams.xy ),
                        tex2D_s5( decalUVParams.xy ), gRampCarLights );
    vec3 color = carLights.mask * ( base * light ) + carLights.emissive;
#else
    vec3 color = base * light;
#endif

#ifdef TRAFFIC_REFLECTION
    vec4 colorFresnel = d3d9_ps_texcoord( in_Texcoord3 );
    vec4 environment = texCUBE_s3( d3d9_ps_texcoord( in_Texcoord4 ).xyz );
    vec3 reflected = exp2( log2( abs( environment.xyz ) ) * decalUVParams.www );
    reflected = reflected * decalUVParams.zzz * colorFresnel.xyz *
                colorFresnel.www * albedo.xyz;
    color = reflected * d3d9_ps_texcoord( in_Texcoord6 ).xxx + color;
#endif
#ifdef TRAFFIC_SPECULAR
    color = d3d9_ps_texcoord( in_Texcoord2 ).xyz * albedo.xyz + color;
#endif

    oC0 = vec4( tdu_apply_fog( color, fog ), in_Color0.w );
    d3d9_ps_alpha_test( oC0 );
}
