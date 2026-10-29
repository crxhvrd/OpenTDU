#version 450
#extension GL_GOOGLE_include_directive : require

// Normal mapped traffic vehicles (VehicleTraffic): per pixel sun light and
// ramp colors from the bumped normal (gPSMaterialParam.z = bump strength).
//
// Permutations:
//   TRAFFIC_NORMAL_UV2  normal map on UV set 2 (else UV set 1)
//   TRAFFIC_DECAL       decal map (s1) blended over the painted base
//   TRAFFIC_REFLECTION  environment cube map (s3), world space reflection
//   TRAFFIC_CAR_LIGHTS  car light mask (s4) and light map (s5)
//   TRAFFIC_SPECULAR    per pixel specular

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"
#include "tdu_vehicle.glsl"

#define gPSLightAmbient D3D9_C( 0 )
#define gPSLightDiffuse D3D9_C( 1 )
#define gPSLightSpecular D3D9_C( 2 )
#define gPSMaterialParam D3D9_C( 4 )
#define gPSWorld( i ) D3D9_CR( 8, i )
#define gRampCarLights D3D9_C( 14 )
#define gPSRampColor1 D3D9_C( 15 )
#define gPSRampColor2 D3D9_C( 16 )

D3D9_DECLARE_SAMPLER_2D( 0 ) // color map
#ifdef TRAFFIC_DECAL
D3D9_DECLARE_SAMPLER_2D( 1 ) // decal map
#endif
D3D9_DECLARE_SAMPLER_2D( 2 ) // normal map
#ifdef TRAFFIC_REFLECTION
D3D9_DECLARE_SAMPLER_CUBE( 3 ) // reflection
#endif
#ifdef TRAFFIC_CAR_LIGHTS
D3D9_DECLARE_SAMPLER_2D( 4 ) // car light mask
D3D9_DECLARE_SAMPLER_2D( 5 ) // car light colors
#endif

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#ifdef TRAFFIC_NORMAL_UV2
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#endif
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
#ifdef TRAFFIC_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
layout( location = D3D9_SLOT_TEXCOORD6 ) in vec4 in_Texcoord6;
layout( location = D3D9_SLOT_TEXCOORD7 ) in vec4 in_Texcoord7;
#endif
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 colorUV = d3d9_ps_texcoord( in_Texcoord0 );
    vec4 decalUV = d3d9_ps_texcoord( in_Texcoord1 );
#ifdef TRAFFIC_NORMAL_UV2
    vec2 normalUV = d3d9_ps_texcoord( in_Texcoord2 ).xy;
#else
    vec2 normalUV = decalUV.xy;
#endif
    vec4 toCameraFog = d3d9_ps_texcoord( in_Texcoord3 );
    vec3 lightDirection = d3d9_ps_texcoord( in_Texcoord4 ).xyz;

    // Bumped normal: the unit map normal scaled toward +z.
    vec3 mapNormal = tex2D_s2( normalUV ).xyz + vec3( -0.5 );
    mapNormal = mapNormal + mapNormal;
    float invLength = inversesqrt( abs( dot( mapNormal, mapNormal ) ) );
    vec3 normal =
        vec3( mapNormal.xy * invLength * gPSMaterialParam.zz,
              gPSMaterialParam.z * ( mapNormal.z * invLength + -1.0 ) + 1.0 );

    vec3 toCamera = normalize( toCameraFog.xyz );
    float facing = max( dot( toCamera, normal ), -0.0 );
    float diffuse = max( dot( -lightDirection, normal ), -0.0 );

    // OTDU_ADDITION: night lamps join the sun light (vertex normal).
    vec3 light = ( diffuse * gPSLightDiffuse.xyz + gPSLightAmbient.xyz +
                   tdu_lamp_pixel( in_ViewPosition, in_ViewNormal ) ) *
                 in_Color0.xyz;

    vec4 albedo = tex2D_s0( colorUV.xy );
    vec3 ramp =
        facing * gPSRampColor1.xyz + ( -facing + 1.0 ) * gPSRampColor2.xyz;
    vec3 base = ramp * albedo.xyz;
#ifdef TRAFFIC_DECAL
    vec4 decal = tex2D_s1( decalUV.xy );
    base = d3d9_lerp( base, decal.xyz, decal.www );
#endif

#ifdef TRAFFIC_CAR_LIGHTS
    TDUCarLights carLights = tdu_car_lights(
        tex2D_s4( decalUV.xy ), tex2D_s5( decalUV.xy ), gRampCarLights );
    vec3 color = carLights.mask * ( light * base ) + carLights.emissive;
#else
    vec3 color = light * base;
#endif

#if defined( TRAFFIC_REFLECTION ) || defined( TRAFFIC_SPECULAR )
    float viewDotNormal = dot( -toCamera, normal );
    vec3 reflection = normal * -( viewDotNormal + viewDotNormal ) + -toCamera;
#endif
#ifdef TRAFFIC_REFLECTION
    vec3 objectReflection =
        reflection.yyy * d3d9_ps_texcoord( in_Texcoord6 ).xyz;
    objectReflection = reflection.xxx * d3d9_ps_texcoord( in_Texcoord7 ).xyz +
                       objectReflection;
    objectReflection = reflection.zzz * d3d9_ps_texcoord( in_Texcoord5 ).xyz +
                       objectReflection;
    vec3 cubeDirection =
        vec3( d3d9_dot( objectReflection, gPSWorld( 2 ).xyz ),
              d3d9_dot( objectReflection, gPSWorld( 1 ).xyz ),
              d3d9_dot( objectReflection, gPSWorld( 0 ).xyz ) );
    vec3 reflected = exp2( log2( abs( texCUBE_s3( cubeDirection ).xyz ) ) *
                           gPSMaterialParam.yyy ) *
                     gPSMaterialParam.xxx * in_Color0.xyz * albedo.xyz;
    color = reflected * colorUV.zzz + color;
#endif
#ifdef TRAFFIC_SPECULAR
    float specular = max( dot( -reflection, lightDirection ), -0.0 );
    float specular2 = specular * specular;
    color = ( specular * ( specular2 * specular2 ) ) * gPSLightSpecular.xyz *
                albedo.xyz +
            color;
#endif

    oC0 = vec4( tdu_apply_fog( color, toCameraFog.w ), gPSLightDiffuse.w );
    d3d9_ps_alpha_test( oC0 );
}
