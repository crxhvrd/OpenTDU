#version 450
#extension GL_GOOGLE_include_directive : require

// Generic vertex lit materials (Lighting table).
//
// Permutations:
//   LIGHTING_TEXTURED    modulates the vertex lighting by the base map
//   LIGHTING_FOG         blends to gFogColorPS by Texcoord2.x
//   LIGHTING_FADE_OUT    alpha *= 1 - Texcoord3.x
//   LIGHTING_FADE_ALPHA  alpha *= Texcoord3.x

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#if defined( LIGHTING_FADE_OUT ) || defined( LIGHTING_FADE_ALPHA )
#define LIGHTING_FADE
#endif

#ifdef LIGHTING_TEXTURED
D3D9_DECLARE_SAMPLER_2D( 0 ) // base map
#endif

layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#ifdef LIGHTING_TEXTURED
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#endif
#ifdef LIGHTING_FOG
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#endif
#ifdef LIGHTING_FADE
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
#endif
#ifdef LIGHTING_TEXTURED
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
#endif
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 light = d3d9_ps_texcoord( in_Texcoord0 );

#ifdef LIGHTING_TEXTURED
    vec4 albedo = tex2D_s0( d3d9_ps_texcoord( in_Texcoord1 ).xy );
    vec4 color = albedo * light;
    // OTDU_ADDITION: night lamps on tinted draws (flat quads without a vertex
    // normal: the normal comes from the screen space derivatives).
    vec3 faceNormal =
        cross( dFdx( in_ViewPosition.xyz ), dFdy( in_ViewPosition.xyz ) );
    if ( in_ViewPosition.w > 0.0 && dot( faceNormal, faceNormal ) > 0.0 ) {
        color.rgb += albedo.rgb * in_ViewPosition.w *
                     tdu_lamp_light_sided( in_ViewPosition.xyz,
                                           normalize( faceNormal ), true );
    }
#else
    vec4 color = light;
#endif

#ifdef LIGHTING_FADE
    float fade = d3d9_ps_texcoord( in_Texcoord3 ).x;
#endif
#ifdef LIGHTING_FADE_OUT
    color.a = ( -fade + 1.0 ) * color.a;
#endif
#ifdef LIGHTING_FADE_ALPHA
    color.a = color.a * fade;
#endif

#ifdef LIGHTING_FOG
    color.rgb = tdu_apply_fog( color.rgb, d3d9_ps_texcoord( in_Texcoord2 ).x );
#endif

    oC0 = color;
    d3d9_ps_alpha_test( oC0 );
}
