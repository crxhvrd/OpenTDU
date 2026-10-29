#version 450
#extension GL_GOOGLE_include_directive : require

// HDR passes.
//
// Permutations:
//   HDR_LUMINANCE      grey scale
//   HDR_BRIGHT_PASS    keeps the pixels brighter than lumThreshold
//   HDR_BRIGHT_SCALE   bloom source: (color - gCoeff.x) scaled by
//                      gCoeff.y / scene luminance (s1)
//   HDR_SATURATION     saturation (gSatThreshold.x) and brightness (.y)
//   HDR_COLOR_BALANCE  saturation, then color balance (gBalanceCoeff) and
//                      contrast (gSatThreshold.zy)
//   HDR_COMPOSE        tone mapped scene (s0 / luminance s1) plus the bloom
//                      (s2) and the light streaks (s3), then saturation,
//                      color balance and contrast
//   HDR_POISSON_BLUR   8 tap poisson disk scaled by gScale
//
// OTDU_ADDITION: HDR_BRIGHT_SCALE clips to 1.0 like its retail 8-bit target
// OTDU_ADDITION: HDR_COMPOSE rolls the highlights off below 1.0 instead of
// clipping them when gSoftHighlights.x (the knee) is set (SDR option)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_postfx.glsl"

#define gScale D3D9_C( 3 )
#define gCoeff( i ) D3D9_CR( 8, i )
#define gBalanceCoeff( i ) D3D9_CR( 8, i )
#define lumThreshold D3D9_C( 14 )
#define gSatThreshold D3D9_C( 14 )
// OTDU_ADDITION: kSoftHighlightsRegister (retail PS constants stop at c30)
#define gSoftHighlights D3D9_C( 221 )

D3D9_DECLARE_SAMPLER_2D( 0 )
#if defined( HDR_BRIGHT_SCALE ) || defined( HDR_COMPOSE )
D3D9_DECLARE_SAMPLER_2D( 1 ) // scene luminance
#endif
#ifdef HDR_COMPOSE
D3D9_DECLARE_SAMPLER_2D( 2 ) // bloom
D3D9_DECLARE_SAMPLER_2D( 3 ) // light streaks
#endif
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = 0 ) out vec4 oC0;

vec3 saturation( vec3 color, float amount ) {
    float grey = ( -amount + 1.0 ) * d3d9_dot( color, TDU_LUMINANCE );
    return color * vec3( amount ) + vec3( grey );
}

// Identity up to the knee, then an exponential shoulder (unit slope at the
// knee) approaching 1.0. Per channel like the clip it replaces: very bright
// colors still bleach to white (a hue preserving curve reveals the tint of the
// light streaks, which retail always clips)
vec3 softHighlights( vec3 color, float knee ) {
    float range = 1.0 - knee;
    vec3 rolled = vec3( knee ) +
                  range * ( vec3( 1.0 ) -
                            exp( -max( color - vec3( knee ), vec3( 0.0 ) ) /
                                 range ) );
    return mix( color, rolled, step( vec3( knee ), color ) );
}

vec3 colorBalance(
    vec3 color, vec4 scale, vec4 bias, float contrast, float brightness ) {
    color = color * scale.xyz + bias.xyz;
    return vec3( contrast ) * color + vec3( brightness );
}

void main() {
    vec2 uv = d3d9_ps_texcoord( in_Texcoord0 ).xy;
#if defined( HDR_LUMINANCE )
    vec4 color = tex2D_s0( uv );
    oC0 = vec4( vec3( d3d9_dot( color.xyz, TDU_LUMINANCE ) ), color.w );
#elif defined( HDR_BRIGHT_PASS )
    vec4 color = tex2D_s0( uv );
    float luminance = d3d9_dot( color.xyz, TDU_LUMINANCE );
    oC0 =
        vec4( ( luminance + -lumThreshold.x >= 0.0 ) ? color.xyz : vec3( 0.0 ),
              color.w );
#elif defined( HDR_BRIGHT_SCALE )
    float scale =
        ( 1.0 / ( tex2D_s1( vec2( 0.5 ) ).x + 0.001 ) ) * gCoeff( 0 ).y;
    vec4 color = tex2D_s0( uv );
    color = max( vec4( color.xyz + -gCoeff( 0 ).xxx, color.w ), vec4( -0.0 ) );
    // OTDU_ADDITION: the retail 8-bit target clips here; FP16 targets (HDR
    // output) would feed the blur and the streaks with unbounded highlights
    oC0 = vec4( min( vec3( scale ) * color.xyz, vec3( 1.0 ) ), color.w );
#elif defined( HDR_SATURATION )
    vec4 color = tex2D_s0( uv );
    oC0 = vec4( saturation( color.xyz, gSatThreshold.x ) * gSatThreshold.yyy,
                color.w );
#elif defined( HDR_COLOR_BALANCE )
    vec4 color = tex2D_s0( uv );
    color.xyz = saturation( color.xyz, gSatThreshold.x );
    oC0 = vec4( colorBalance( color.xyz, gBalanceCoeff( 0 ), gBalanceCoeff( 1 ),
                              gSatThreshold.z, gSatThreshold.y ),
                color.w );
#elif defined( HDR_COMPOSE )
    float exposure =
        ( 1.0 / ( tex2D_s1( vec2( 0.5 ) ).x + 0.001 ) ) * gCoeff( 0 ).x;
    vec4 color = tex2D_s0( uv );
    vec3 bloom = tex2D_s2( uv ).xyz * gCoeff( 1 ).xxx;
    color.xyz = color.xyz * vec3( exposure ) + bloom;
    color.xyz = gCoeff( 1 ).yyy * tex2D_s3( uv ).xyz + color.xyz;
    color.xyz = saturation( color.xyz, gCoeff( 0 ).y );
    color.xyz = colorBalance( color.xyz, gCoeff( 2 ), gCoeff( 3 ),
                              gCoeff( 0 ).w, gCoeff( 0 ).z );
    if ( gSoftHighlights.x > 0.0 ) {
        color.xyz = softHighlights( color.xyz, gSoftHighlights.x );
    }
    oC0 = color;
#elif defined( HDR_POISSON_BLUR )
    const vec2 kPoisson[7] =
        vec2[]( vec2( 0.527837, -0.085868 ), vec2( -0.040088, 0.536087 ),
                vec2( -0.670445, -0.179949 ), vec2( -0.419418, -0.616039 ),
                vec2( 0.440453, -0.639399 ), vec2( -0.757088, 0.349334 ),
                vec2( 0.574619, 0.685879 ) );
    vec4 color = tex2D_s0( gScale.xy * kPoisson[0] + uv ) + tex2D_s0( uv );
    for ( int i = 1; i < 7; i++ ) {
        color = tex2D_s0( gScale.xy * kPoisson[i] + uv ) + color;
    }
    oC0 = color * vec4( 0.125 );
#endif
    d3d9_ps_alpha_test( oC0 );
}
