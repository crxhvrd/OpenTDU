#version 450
#extension GL_GOOGLE_include_directive : require

// Scene luminance for the tone mapping (geometric mean).
//
// Permutations:
//   LUMINANCE_LOG    average log luminance of 9 taps (first pass)
//   LUMINANCE_EXP    exp of the average of 16 taps, clamped by coeff.xy
//                    (last pass)
//   LUMINANCE_ADAPT  blends the clamped current luminance (s0) with the
//                    previous one (s1) by 1 - exp2(-k * coeff.x)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_postfx.glsl"

#define coeff D3D9_C( 8 )
#define sampleOffset( i ) D3D9_CR( 15, i )

D3D9_DECLARE_SAMPLER_2D( 0 )
#ifdef LUMINANCE_ADAPT
D3D9_DECLARE_SAMPLER_2D( 1 )
#else
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#endif
layout( location = 0 ) out vec4 oC0;

float logLuminance( vec2 uv ) {
    float luminance = d3d9_dot( tex2D_s0( uv ).xyz, TDU_LUMINANCE );
    return log2( abs( luminance + 0.0001 ) );
}

void main() {
#if defined( LUMINANCE_LOG )
    vec2 uv = d3d9_ps_texcoord( in_Texcoord0 ).xy;
    float sum = logLuminance( uv + sampleOffset( 1 ).xy ) * 0.6931472;
    sum = logLuminance( uv + sampleOffset( 0 ).xy ) * 0.6931472 + sum;
    for ( int i = 2; i < 9; i++ ) {
        sum = logLuminance( uv + sampleOffset( i ).xy ) * 0.6931472 + sum;
    }
    oC0 = vec4( sum ) * vec4( 0.11111111 );
#elif defined( LUMINANCE_EXP )
    vec2 uv = d3d9_ps_texcoord( in_Texcoord0 ).xy;
    float sum = tex2D_s0( uv + sampleOffset( 0 ).xy ).x +
                tex2D_s0( uv + sampleOffset( 1 ).xy ).x;
    for ( int i = 2; i < 16; i++ ) {
        sum = tex2D_s0( uv + sampleOffset( i ).xy ).x + sum;
    }
    // exp( sum / 16 )
    float luminance = exp2( sum * 0.09016844 );
    oC0 = min( coeff.yyyy, vec4( max( luminance, coeff.x ) ) );
#elif defined( LUMINANCE_ADAPT )
    float rate = -exp2( -0.029146345 * coeff.x ) + 1.0;
    float current = min( coeff.z, max( tex2D_s0( vec2( 0.5 ) ).x, coeff.y ) );
    float adapted = tex2D_s1( vec2( 0.5 ) ).x;
    oC0 = max( vec4( d3d9_lerp( current, adapted, rate ) ), vec4( 0.0 ) );
#endif
    d3d9_ps_alpha_test( oC0 );
}
