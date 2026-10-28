#version 450
#extension GL_GOOGLE_include_directive : require

// Vegetation impostors.
//
// Permutations:
//   IMPOSTOR_FADE  alpha is multiplied by the LOD fade

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

D3D9_DECLARE_SAMPLER_2D( 0 )

layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 light = d3d9_ps_texcoord( in_Texcoord1 );
    vec4 fogFade = d3d9_ps_texcoord( in_Texcoord3 );

    vec4 color = tex2D_s0( d3d9_ps_texcoord( in_Texcoord0 ).xy );
    float alpha = color.a * light.a;
#ifdef IMPOSTOR_FADE
    alpha *= fogFade.y;
#endif

    // OTDU_ADDITION: night lamps.
    vec3 lit = light.rgb * color.rgb +
               color.rgb * tdu_lamp_pixel( in_ViewPosition, in_ViewNormal );

    oC0 = vec4( tdu_apply_fog( lit, fogFade.x ), alpha );
    d3d9_ps_alpha_test( oC0 );
}
