#version 450
#extension GL_GOOGLE_include_directive : require

// OTDU_ADDITION: wheel spin blur, the composite (render/wheel_blur.cpp).
//
// The accumulation target holds, per pixel, the sum over a wheel's turned
// copies of (colour in linear light, 1) x 1 / copies, counted only where the
// copy's own front-most surface was seen and the scene did not hide it:
// rgb / a = the copies' mean colour, a = the share of copies covering the
// pixel. Drawn over the scene with blending ONE, INV_SRC_ALPHA
// (premultiplied); the wheel itself was not drawn there, so the gaps between
// the spokes show what really is behind the wheel.

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#ifdef WB_MSAA
layout( set = 0, binding = D3D9_TEXTURE_BINDING( 0 ) ) uniform texture2DMS t0_2dms;
vec4 wb_accum( ivec2 pixel ) {
    int numSamples = textureSamples( sampler2DMS( t0_2dms, D3D9_SAMPLER( 0 ) ) );
    vec4 sum = vec4( 0.0 );
    for ( int i = 0; i < numSamples; i++ ) {
        sum += texelFetch( sampler2DMS( t0_2dms, D3D9_SAMPLER( 0 ) ), pixel, i );
    }
    return sum / float( max( numSamples, 1 ) );
}
#else
D3D9_DECLARE_SAMPLER_2D( 0 )
vec4 wb_accum( ivec2 pixel ) {
    return texelFetch( sampler2D( t0_2d, D3D9_SAMPLER( 0 ) ), pixel, 0 );
}
#endif

layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 accum = wb_accum( ivec2( gl_FragCoord.xy ) );
    float coverage = min( accum.a, 1.0 );
    if ( coverage <= 1e-4 ) {
        discard;
    }
    vec3 light = accum.rgb / max( accum.a, 1e-5 );
    vec3 colour = pow( max( light, vec3( 0.0 ) ), vec3( 1.0 / 2.2 ) );
    oC0 = vec4( colour * coverage, coverage );
}
