#version 450
#extension GL_GOOGLE_include_directive : require

// Fullscreen copy.
//
// Permutations:
//   COPY_DOWNSCALE  average of 4 taps at uvOffset[i].xy, LOD bias
//                   uvOffset[i].w

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define uvOffset( i ) D3D9_CR( 8, i )

D3D9_DECLARE_SAMPLER_2D( 0 )
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = 0 ) out vec4 oC0;

vec4 tap( vec2 uv, int i ) {
    return tex2Dbias_s0( uv + uvOffset( i ).xy, uvOffset( i ).w );
}

void main() {
    vec2 uv = d3d9_ps_texcoord( in_Texcoord0 ).xy;
#ifdef COPY_DOWNSCALE
    vec4 color = tap( uv, 0 ) + tap( uv, 1 );
    color = tap( uv, 2 ) + color;
    color = tap( uv, 3 ) + color;
    oC0 = color * vec4( 0.25 );
#else
    oC0 = tex2D_s0( uv );
#endif
    d3d9_ps_alpha_test( oC0 );
}
