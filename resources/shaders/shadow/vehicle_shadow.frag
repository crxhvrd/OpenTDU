#version 450
#extension GL_GOOGLE_include_directive : require

// Shadow Vehicle table section, multiplied over the ground: fades to
// white in the fog and with gPSLightDiffuse.w (sun intensity).
//
// Permutations:
//   VEHICLE_SHADOW_TEXTURED  blob texture (alpha = texture alpha)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gPSLightDiffuse c.f[1]

#ifdef VEHICLE_SHADOW_TEXTURED
D3D9_DECLARE_SAMPLER_2D( 0 ) // blob
#endif

#ifdef VEHICLE_SHADOW_TEXTURED
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#endif
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = 0 ) out vec4 oC0;

void main() {
    float fog = d3d9_ps_texcoord( in_Texcoord1 ).x;

#ifdef VEHICLE_SHADOW_TEXTURED
    vec4 blob = tex2D_s0( d3d9_ps_texcoord( in_Texcoord0 ).xy );
#else
    vec4 blob = vec4( 1.0 );
#endif
    vec3 color = d3d9_lerp( gFogColorPS.rgb, blob.rgb, fog );

    oC0 = vec4( color * gPSLightDiffuse.www + ( -gPSLightDiffuse.w + 1.0 ),
                blob.a );
    d3d9_ps_alpha_test( oC0 );
}
