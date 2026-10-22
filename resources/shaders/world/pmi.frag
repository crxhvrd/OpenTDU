#version 450
#extension GL_GOOGLE_include_directive : require

// Instanced world props (PMI).
//
// Permutations:
//   PMI_TEXTURED  modulates the vertex color by the base texture

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#ifdef PMI_TEXTURED
D3D9_DECLARE_SAMPLER_2D( 0 )
#endif

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
#ifdef PMI_TEXTURED
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#endif
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    float fog = d3d9_ps_texcoord( in_Texcoord1 ).x;
    float lodFade = d3d9_ps_texcoord( in_Texcoord2 ).x;

#ifdef PMI_TEXTURED
    vec4 color = tex2D_s0( d3d9_ps_texcoord( in_Texcoord0 ).xy );
    // OTDU_ADDITION: night lamps on top of the clamped vertex light.
    vec3 lamps = tdu_lamp_pixel( in_ViewPosition, in_ViewNormal );
    color = vec4( ( in_Color0.rgb + lamps ) * color.rgb,
                  color.a * in_Color0.a * ( -lodFade + 1.0 ) );
#else
    vec4 color =
        vec4( in_Color0.rgb + tdu_lamp_pixel( in_ViewPosition, in_ViewNormal ),
              ( -lodFade + 1.0 ) * in_Color0.a );
#endif

    oC0 = vec4( tdu_apply_fog( color.rgb, fog ), color.a );
    d3d9_ps_alpha_test( oC0 );
}
