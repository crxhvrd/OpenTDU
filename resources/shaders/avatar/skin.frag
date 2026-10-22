#version 450
#extension GL_GOOGLE_include_directive : require

// Avatar skin (avatar table): two skin targets (s0, s1) blended by
// gPSBlendFactor, wrapped diffuse modulated by the detail map (s2, red),
// sun specular and a back lit rim (gPSCoeffExpParam: x / y intensities,
// z / w exponents) masked by the albedo alpha, then the fog.
//
// Permutations:
//   SKIN_VERTEX_COLOR  diffuse and specular colors modulated by Color0

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gPSCoeffExpParam c.f[3]
#define gPSBlendFactor c.f[18]

D3D9_DECLARE_SAMPLER_2D( 0 ) // gSkinTargetBeginSampler
D3D9_DECLARE_SAMPLER_2D( 1 ) // gSkinTargetEndSampler
D3D9_DECLARE_SAMPLER_2D( 2 ) // gDetailSampler

#ifdef SKIN_VERTEX_COLOR
layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
#endif
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
layout( location = D3D9_SLOT_TEXCOORD6 ) in vec4 in_Texcoord6;
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 t0 = d3d9_ps_texcoord( in_Texcoord0 );
    vec4 t2 = d3d9_ps_texcoord( in_Texcoord2 );
    vec4 t3 = d3d9_ps_texcoord( in_Texcoord3 );

    vec3 normal = normalize( d3d9_ps_texcoord( in_Texcoord1 ).xyz );
    vec3 view = normalize( t2.xyz );
    vec3 lightDir = normalize( t0.xyz );
    float rim = -max( dot( normal, view ), 0.0 ) + 1.0;
    float cosLight = dot( normal, lightDir );
    vec3 reflected = ( cosLight + cosLight ) * normal + -lightDir;
    float cosSpecular = max( dot( reflected, view ), 0.0 );
    float cosBack = dot( view, lightDir );
    vec2 highlights =
        exp2( vec2( log2( abs( cosSpecular ) ), log2( abs( rim ) ) ) *
              gPSCoeffExpParam.zw ) *
        gPSCoeffExpParam.xy;

    vec4 albedo =
        d3d9_lerp( tex2D_s0( t3.xy ), tex2D_s1( t3.xy ), gPSBlendFactor );
    highlights *= albedo.ww;
    float backLight = cosBack * -0.5 + max( cosLight, 0.0 ) + 0.5;
    float specular = d3d9_saturate( cosLight * 4.0 ) * highlights.x +
                     highlights.y * backLight;
    float wrap =
        d3d9_saturate( ( cosLight + t0.w ) * ( 1.0 / ( t0.w + 1.0 ) ) );

    vec3 specularColor = d3d9_ps_texcoord( in_Texcoord6 ).xyz;
    vec3 diffuseColor = d3d9_ps_texcoord( in_Texcoord5 ).xyz;
#ifdef SKIN_VERTEX_COLOR
    specularColor = in_Color0.xyz * specularColor;
    diffuseColor = in_Color0.xyz * diffuseColor;
#endif
    vec3 light = wrap * diffuseColor * tex2D_s2( t3.zw ).xxx +
                 d3d9_ps_texcoord( in_Texcoord4 ).xyz;
    // OTDU_ADDITION: night lamps, modulated by the detail map.
    light += tdu_lamp_pixel( in_ViewPosition, in_ViewNormal ) *
             tex2D_s2( t3.zw ).xxx;
    vec3 color = albedo.xyz * light + specular * specularColor;
    oC0 = vec4( d3d9_lerp( gFogColorPS.xyz, color, t2.www ), 1.0 );
    d3d9_ps_alpha_test( oC0 );
}
