#version 450
#extension GL_GOOGLE_include_directive : require

// Bink movie frame: Y, Cr, Cb planes (s0, s1, s2) to RGB (BT.601, video
// range).
//
// Permutations:
//   BINK_ALPHA  alpha plane (s3), else opaque

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

D3D9_DECLARE_SAMPLER_2D( 0 ) // gTextureY
D3D9_DECLARE_SAMPLER_2D( 1 ) // gTextureCr
D3D9_DECLARE_SAMPLER_2D( 2 ) // gTextureCb
#ifdef BINK_ALPHA
D3D9_DECLARE_SAMPLER_2D( 3 ) // gTextureA
#endif
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = 0 ) out vec4 oC0;

void main() {
    vec2 uv = d3d9_ps_texcoord( in_Texcoord0 ).xy;
    vec4 ycc =
        vec4( tex2D_s0( uv ).x, tex2D_s1( uv ).x, tex2D_s2( uv ).x, 1.0 );
#ifdef BINK_ALPHA
    float alpha = tex2D_s3( uv ).x;
#else
    float alpha = 1.0;
#endif
    oC0 = vec4(
        d3d9_dot( vec4( 1.1641235, 1.5957947, 0.0, -0.87065506 ), ycc ),
        d3d9_dot( vec4( 1.1641235, -0.81347656, -0.39144897, 0.52970505 ),
                  ycc ),
        d3d9_dot( vec4( 1.1641235, 0.0, 2.0178223, -1.0816689 ), ycc ), alpha );
    d3d9_ps_alpha_test( oC0 );
}
