#version 450
#extension GL_GOOGLE_include_directive : require

// Light streaks.
//
// Permutations:
//   STREAK_MERGE  average of the 4 streak directions (s0..s3), else one
//                 streak pass: the taps of light_streak.vert weighted by
//                 lightStreakModulator

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define lightStreakModulator( i ) D3D9_CR( 8, i )

D3D9_DECLARE_SAMPLER_2D( 0 )
#ifdef STREAK_MERGE
D3D9_DECLARE_SAMPLER_2D( 1 )
D3D9_DECLARE_SAMPLER_2D( 2 )
D3D9_DECLARE_SAMPLER_2D( 3 )
#endif
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#ifndef STREAK_MERGE
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#endif
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 t0 = d3d9_ps_texcoord( in_Texcoord0 );
#ifdef STREAK_MERGE
    vec4 color = tex2D_s0( t0.xy ) + tex2D_s1( t0.xy );
    color = tex2D_s2( t0.xy ) + color;
    color = tex2D_s3( t0.xy ) + color;
    oC0 = color * vec4( 0.25 );
#else
    vec4 t1 = d3d9_ps_texcoord( in_Texcoord1 );
    vec4 color = tex2D_s0( t0.zw ) * lightStreakModulator( 1 );
    color = lightStreakModulator( 0 ) * tex2D_s0( t0.xy ) + color;
    color = lightStreakModulator( 2 ) * tex2D_s0( t1.xy ) + color;
    color = lightStreakModulator( 3 ) * tex2D_s0( t1.zw ) + color;
    oC0 = color;
#endif
    d3d9_ps_alpha_test( oC0 );
}
