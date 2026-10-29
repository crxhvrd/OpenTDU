#version 450
#extension GL_GOOGLE_include_directive : require

// Avatar props (avatar table): per pixel sun lighting of a base color and
// a textured layer, two lobe specular, reflection and occlusion.
//
// Permutations:
//   AVATAR_BASE_COLOR  gPSBaseColor (else white)
//   AVATAR_LAYER       textured layer (s1, UV set 0) tinted by
//                      gPSLayer1Color, blended over the base by its alpha
//   AVATAR_OCCLUSION   occlusion map (s0, UV set 0)
//   AVATAR_SPECULAR    specular (gPSSpecularPower / Coeff: x base, y layer)
//   AVATAR_GLOSS       gloss map (s2 red, UV set 1) masking the specular
//   AVATAR_ENVMAP      cube map reflection (s3)
//   AVATAR_SPHEREMAP   sphere map reflection (s3)
//   AVATAR_REFLECTION_ATTENUATION  reflection scaled by Texcoord4.w (the
//                                  car interior attenuation)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gPSSpecularPower D3D9_C( 3 )
#define gPSMaterialParam D3D9_C( 4 )
#define gPSBaseColor D3D9_C( 18 )
#define gPSLayer1Color D3D9_C( 19 )
#define gPSSpecularCoeff D3D9_C( 22 )

#ifdef AVATAR_OCCLUSION
D3D9_DECLARE_SAMPLER_2D( 0 ) // gOcclusionSampler
#endif
#ifdef AVATAR_LAYER
D3D9_DECLARE_SAMPLER_2D( 1 ) // gTextureSampler
#endif
#ifdef AVATAR_GLOSS
D3D9_DECLARE_SAMPLER_2D( 2 ) // gGlossSampler
#endif
#if defined( AVATAR_ENVMAP )
D3D9_DECLARE_SAMPLER_CUBE( 3 ) // gEnvMapSampler
#elif defined( AVATAR_SPHEREMAP )
D3D9_DECLARE_SAMPLER_2D( 3 ) // gEnvMapSampler
#endif

layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#ifdef AVATAR_SPECULAR
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#endif
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#if defined( AVATAR_LAYER ) || defined( AVATAR_OCCLUSION ) ||                  \
    defined( AVATAR_GLOSS )
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
#endif
#if defined( AVATAR_ENVMAP ) || defined( AVATAR_SPHEREMAP )
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
#endif
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
layout( location = D3D9_SLOT_TEXCOORD6 ) in vec4 in_Texcoord6;
#ifdef AVATAR_SPECULAR
layout( location = D3D9_SLOT_TEXCOORD7 ) in vec4 in_Texcoord7;
#endif
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec3 lightDir = normalize( d3d9_ps_texcoord( in_Texcoord2 ).xyz );
    vec3 normal = normalize( d3d9_ps_texcoord( in_Texcoord0 ).xyz );
    float diffuse = max( dot( lightDir, normal ), 0.0 );
    vec3 light = diffuse * d3d9_ps_texcoord( in_Texcoord6 ).xyz +
                 d3d9_ps_texcoord( in_Texcoord5 ).xyz;
    // OTDU_ADDITION: night lamps join the sun light.
    light += tdu_lamp_pixel( in_ViewPosition, in_ViewNormal );
#if defined( AVATAR_LAYER ) || defined( AVATAR_OCCLUSION ) ||                  \
    defined( AVATAR_GLOSS )
    vec4 t3 = d3d9_ps_texcoord( in_Texcoord3 );
#endif

#ifdef AVATAR_SPECULAR
    vec3 reflected = ( diffuse + diffuse ) * normal + -lightDir;
    float cosSpecular = max(
        dot( normalize( d3d9_ps_texcoord( in_Texcoord1 ).xyz ), reflected ),
        0.0 );
#ifdef AVATAR_LAYER
    vec2 specular = exp2( log2( abs( cosSpecular ) ) * gPSSpecularPower.xy ) *
                    gPSSpecularCoeff.xy;
#else
    vec2 specular = vec2( pow( abs( cosSpecular ), gPSSpecularPower.x ) *
                              gPSSpecularCoeff.x,
                          0.0 );
#endif
#ifdef AVATAR_GLOSS
    specular *= tex2D_s2( t3.zw ).xx;
#endif
    vec3 specularColor =
        d3d9_saturate( diffuse * 4.0 ) * d3d9_ps_texcoord( in_Texcoord7 ).xyz;
#endif

#ifdef AVATAR_BASE_COLOR
    vec4 base = vec4( gPSBaseColor.xyz * light, gPSBaseColor.w );
#else
    vec4 base = vec4( light, 1.0 );
#endif
#ifdef AVATAR_SPECULAR
    base.xyz = specularColor * specular.x + base.xyz;
#endif
    vec4 color = base;
#ifdef AVATAR_LAYER
    vec4 layer = tex2D_s1( t3.xy ) * gPSLayer1Color;
    vec3 layerColor = light * layer.xyz;
#ifdef AVATAR_SPECULAR
    layerColor = specularColor * specular.y + layerColor;
#endif
    color = vec4( d3d9_lerp( base.xyz, layerColor, layer.www ),
                  max( base.w, layer.w ) );
#endif

#if defined( AVATAR_ENVMAP ) || defined( AVATAR_SPHEREMAP )
#ifdef AVATAR_ENVMAP
    vec3 reflection = texCUBE_s3( d3d9_ps_texcoord( in_Texcoord4 ).xyz ).xyz;
#else
    vec3 reflection = tex2D_s3( d3d9_ps_texcoord( in_Texcoord4 ).xy ).xyz;
#endif
    reflection = exp2( log2( abs( reflection ) ) * gPSMaterialParam.yyy );
#ifdef AVATAR_REFLECTION_ATTENUATION
    color.xyz = reflection * gPSMaterialParam.xxx *
                    d3d9_ps_texcoord( in_Texcoord4 ).www +
                color.xyz;
#else
    color.xyz = reflection * gPSMaterialParam.xxx + color.xyz;
#endif
#endif
#ifdef AVATAR_OCCLUSION
    color.xyz *= tex2D_s0( t3.xy ).xyz;
#endif
    oC0 = color;
    d3d9_ps_alpha_test( oC0 );
}
