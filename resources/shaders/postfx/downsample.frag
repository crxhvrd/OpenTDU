#version 450
#extension GL_GOOGLE_include_directive : require

// Down sampling: the average of the taps at sampleOffset.
//
// Permutations:
//   DOWNSAMPLE_TAPS=N  4 or 16 taps
//   DOWNSAMPLE_RED     averages the red channel only (luminance chains)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define sampleOffset( i ) c.f[15 + ( i )]

D3D9_DECLARE_SAMPLER_2D( 0 )
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = 0 ) out vec4 oC0;

void main() {
    vec2 uv = d3d9_ps_texcoord( in_Texcoord0 ).xy;
#ifdef DOWNSAMPLE_RED
    float sum = tex2D_s0( uv + sampleOffset( 0 ).xy ).x +
                tex2D_s0( uv + sampleOffset( 1 ).xy ).x;
    for ( int i = 2; i < DOWNSAMPLE_TAPS; i++ ) {
        sum = tex2D_s0( uv + sampleOffset( i ).xy ).x + sum;
    }
    oC0 = vec4( sum ) * vec4( 1.0 / float( DOWNSAMPLE_TAPS ) );
#else
    vec4 sum = tex2D_s0( uv + sampleOffset( 0 ).xy ) +
               tex2D_s0( uv + sampleOffset( 1 ).xy );
    for ( int i = 2; i < DOWNSAMPLE_TAPS; i++ ) {
        sum = tex2D_s0( uv + sampleOffset( i ).xy ) + sum;
    }
    oC0 = sum * vec4( 1.0 / float( DOWNSAMPLE_TAPS ) );
#endif
    d3d9_ps_alpha_test( oC0 );
}
