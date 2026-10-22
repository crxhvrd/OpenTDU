#version 450
#extension GL_GOOGLE_include_directive : require

// Procedural noise textures (noise table).
//
// Permutations:
//   NOISE_COMPOSITION  3 slices (gPSMaterialParam[0].xyz) of a volume noise
//                      (s0) weighted by gPSMaterialParam[1].xyz
//   NOISE_ASSEMBLY     8 octaves of a 2D noise (s0), halving amplitudes

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define gPSMaterialParam( i ) c.f[4 + ( i )]

#ifdef NOISE_COMPOSITION
D3D9_DECLARE_SAMPLER_3D( 0 ) // gNoiseSlice
#else
D3D9_DECLARE_SAMPLER_2D( 0 ) // gNoiseBase
#endif
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = 0 ) out vec4 oC0;

void main() {
    vec2 uv = d3d9_ps_texcoord( in_Texcoord0 ).xy;
#ifdef NOISE_COMPOSITION
    vec4 noise = tex3D_s0( vec3( uv, gPSMaterialParam( 0 ).y ) ) *
                 gPSMaterialParam( 1 ).yyyy;
    noise = tex3D_s0( vec3( uv, gPSMaterialParam( 0 ).x ) ) *
                gPSMaterialParam( 1 ).xxxx +
            noise;
    oC0 = tex3D_s0( vec3( uv, gPSMaterialParam( 0 ).z ) ) *
              gPSMaterialParam( 1 ).zzzz +
          noise;
#else
    float noise = tex2D_s0( uv + uv ).x * 0.5 + tex2D_s0( uv ).x;
    float frequency = 4.0;
    float amplitude = 0.25;
    for ( int octave = 2; octave < 8; octave++ ) {
        noise = tex2D_s0( uv * vec2( frequency ) ).x * amplitude + noise;
        frequency *= 2.0;
        amplitude *= 0.5;
    }
    oC0 = vec4( noise ) * vec4( 0.5019608 );
#endif
    d3d9_ps_alpha_test( oC0 );
}
