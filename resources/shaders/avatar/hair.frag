#version 450
#extension GL_GOOGLE_include_directive : require

// Avatar hair (avatar table): the hair map (s0: x occlusion, y primary to
// secondary color, z specular mask, w alpha), wrapped diffuse and an
// anisotropic (tangent) specular (gPSExpParam: x intensity, y exponent),
// then the fog.
//
// Permutations:
//   HAIR_VERTEX_COLOR  the light is modulated by Color0

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"
#include "tdu_avatar.glsl"

#define gPSExpParam D3D9_C( 3 )
#define gPSHairPrimaryColor D3D9_C( 18 )
#define gPSHairSecondaryColor D3D9_C( 19 )

D3D9_DECLARE_SAMPLER_2D( 0 ) // gHairSampler

#ifdef HAIR_VERTEX_COLOR
layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
#endif
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
layout( location = D3D9_SLOT_TEXCOORD6 ) in vec4 in_Texcoord6;
layout( location = D3D9_SLOT_TEXCOORD7 ) in vec4 in_Texcoord7;
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 t2 = d3d9_ps_texcoord( in_Texcoord2 );
    vec4 hair = tex2D_s0( d3d9_ps_texcoord( in_Texcoord4 ).xy );

    vec3 lightDir = normalize( d3d9_ps_texcoord( in_Texcoord0 ).xyz );
    vec3 halfway = normalize(
        t2.xyz * inversesqrt( abs( dot( t2.xyz, t2.xyz ) ) ) + lightDir );
    float cosTangent =
        dot( normalize( d3d9_ps_texcoord( in_Texcoord3 ).xyz ), halfway );
    float sinTangent =
        1.0 / inversesqrt( abs( cosTangent * -cosTangent + 1.0 ) );
    float specular =
        hair.z * ( pow( abs( sinTangent ), gPSExpParam.y ) * gPSExpParam.x );
    float cosLight =
        dot( normalize( d3d9_ps_texcoord( in_Texcoord1 ).xyz ), lightDir );

    vec3 specularColor = d3d9_saturate( cosLight * 1.75 + 0.25 ) *
                         d3d9_ps_texcoord( in_Texcoord7 ).xyz;
    vec3 diffuse = d3d9_ps_texcoord( in_Texcoord6 ).xyz *
                       d3d9_saturate( cosLight * 0.75 + 0.25 ) +
                   d3d9_ps_texcoord( in_Texcoord5 ).xyz;
    // OTDU_ADDITION: night lamps join the diffuse light.
    diffuse += tdu_lamp_pixel( in_ViewPosition, in_ViewNormal );
    vec3 light = diffuse * hair.xxx + specular * specularColor;
#ifdef HAIR_VERTEX_COLOR
    light *= in_Color0.xyz;
#endif
    vec3 albedo =
        hair.yyy * ( -gPSHairPrimaryColor.xyz + gPSHairSecondaryColor.xyz ) +
        gPSHairPrimaryColor.xyz;
    oC0 = vec4(
        d3d9_lerp( gFogColorPS.xyz, tdu_avatar_shade( light, albedo ), t2.www ),
        hair.w );
    d3d9_ps_alpha_test( oC0 );
}
