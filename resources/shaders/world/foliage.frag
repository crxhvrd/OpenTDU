#version 450
#extension GL_GOOGLE_include_directive : require

// Foliage (vertex lit vegetation meshes).
//
// Permutations:
//   FOLIAGE_FADE    alpha is multiplied by the LOD fade
//   FOLIAGE_SHADOW  modulated by the shadow texture

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

D3D9_DECLARE_SAMPLER_2D( 0 ) // base texture
#ifdef FOLIAGE_SHADOW
D3D9_DECLARE_SAMPLER_2D( 1 ) // shadow texture
#endif

layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#ifdef FOLIAGE_SHADOW
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#endif
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 light = d3d9_ps_texcoord( in_Texcoord0 );
    vec4 fogFade = d3d9_ps_texcoord( in_Texcoord3 );

    vec4 albedo = tex2D_s0( d3d9_ps_texcoord( in_Texcoord1 ).xy );
    vec4 color = albedo * light;
#ifdef FOLIAGE_SHADOW
    vec4 shadow = tex2D_s1( d3d9_ps_texcoord( in_Texcoord2 ).xy );
    color.rgb *= tdu_baked_shadow( shadow.rgb );
    color.a = shadow.a * color.a;
#endif
#ifdef FOLIAGE_FADE
    color.a *= fogFade.y;
#endif
    // OTDU_ADDITION: night lamps, not shadowed by the baked sun shadow.
    color.rgb += albedo.rgb *
                 tdu_lamp_pixel_sided( in_ViewPosition, in_ViewNormal, true );

    oC0 = vec4( tdu_apply_fog( color.rgb, fogFade.x ), color.a );
    d3d9_ps_alpha_test( oC0 );
}
