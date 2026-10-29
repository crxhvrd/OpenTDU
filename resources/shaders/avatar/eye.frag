#version 450
#extension GL_GOOGLE_include_directive : require

// Avatar eyes (avatar table): the lighting lookup (s2, from the head space
// normal: x ambient, y diffuse, z specular, w reflection) weights the
// light; specular sphere map (s3) and reflection masked by the gloss map
// (s1, UV set 1); gPSSpecEnvParam: x / z intensities, y / w exponents.
//
// Permutations:
//   EYE_ENVMAP     cube map reflection (s4)
//   EYE_SPHEREMAP  sphere map reflection (s4)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gPSSpecEnvParam D3D9_C( 3 )

D3D9_DECLARE_SAMPLER_2D( 0 ) // gTextureSampler
D3D9_DECLARE_SAMPLER_2D( 1 ) // gGlossSampler
D3D9_DECLARE_SAMPLER_2D( 2 ) // gOcclusionSampler
D3D9_DECLARE_SAMPLER_2D( 3 ) // gSpecularSampler
#if defined( EYE_ENVMAP )
D3D9_DECLARE_SAMPLER_CUBE( 4 ) // gEnvMapSampler
#elif defined( EYE_SPHEREMAP )
D3D9_DECLARE_SAMPLER_2D( 4 ) // gEnvMapSampler
#endif

layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#if defined( EYE_ENVMAP ) || defined( EYE_SPHEREMAP )
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
#endif
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
layout( location = D3D9_SLOT_TEXCOORD6 ) in vec4 in_Texcoord6;
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
layout( location = 0 ) out vec4 oC0;

vec3 powColor( vec3 color, float exponent ) {
    return exp2( log2( abs( color ) ) * vec3( exponent ) );
}

void main() {
    vec4 t0 = d3d9_ps_texcoord( in_Texcoord0 );
    vec4 t1 = d3d9_ps_texcoord( in_Texcoord1 );
    vec4 lookup = tex2D_s2( t0.xy );
    vec3 highlights =
        powColor( tex2D_s3( d3d9_ps_texcoord( in_Texcoord2 ).xy ).xyz,
                  gPSSpecEnvParam.y );
#if defined( EYE_ENVMAP ) || defined( EYE_SPHEREMAP )
#ifdef EYE_ENVMAP
    vec3 reflection = texCUBE_s4( d3d9_ps_texcoord( in_Texcoord3 ).xyz ).xyz;
#else
    vec3 reflection = tex2D_s4( d3d9_ps_texcoord( in_Texcoord3 ).xy ).xyz;
#endif
    reflection = powColor( reflection, gPSSpecEnvParam.w ) *
                 ( lookup.w * gPSSpecEnvParam.z );
    highlights = ( lookup.z * gPSSpecEnvParam.x ) * highlights + reflection;
#else
    highlights = highlights * ( lookup.z * gPSSpecEnvParam.x );
#endif
    vec3 specular = highlights * ( tex2D_s1( t1.zw ).xxx *
                                   d3d9_ps_texcoord( in_Texcoord6 ).xyz );
    vec3 light = lookup.yyy * d3d9_ps_texcoord( in_Texcoord5 ).xyz +
                 lookup.xxx * d3d9_ps_texcoord( in_Texcoord4 ).xyz;
    // OTDU_ADDITION: night lamps, weighted like the sun diffuse.
    light += lookup.yyy * tdu_lamp_pixel( in_ViewPosition, in_ViewNormal );
    vec3 color = light * tex2D_s0( t1.xy ).xyz + specular;
    oC0 = vec4( d3d9_lerp( gFogColorPS.xyz, color, t0.www ), 1.0 );
    d3d9_ps_alpha_test( oC0 );
}
