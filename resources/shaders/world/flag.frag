#version 450
#extension GL_GOOGLE_include_directive : require

// Flags.
//
// Permutations:
//   FLAG_ROTATED_UV    the texture is sampled with (v, u)
//   FLAG_DOUBLE_SIDED  the texture is mirrored on the back face

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

D3D9_DECLARE_SAMPLER_2D( 0 )

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#ifdef FLAG_DOUBLE_SIDED
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#endif
layout( location = 0 ) out vec4 oC0;

void main() {
    vec2 uv = d3d9_ps_texcoord( in_Texcoord0 ).xy;
    float fog = d3d9_ps_texcoord( in_Texcoord1 ).x;

#ifdef FLAG_ROTATED_UV
    vec2 frontUV = uv.yx;
    vec2 backUV = vec2( uv.y * 1.0 + 0.0, uv.x * -1.0 + 1.0 );
#else
    vec2 frontUV = uv;
    vec2 backUV = uv * vec2( -1.0, 1.0 ) + vec2( 1.0, 0.0 );
#endif
#ifdef FLAG_DOUBLE_SIDED
    bool bFrontFace = d3d9_ps_texcoord( in_Texcoord2 ).z >= 0.0;
    vec4 color = tex2D_s0( bFrontFace ? frontUV : backUV );
#else
    vec4 color = tex2D_s0( frontUV );
#endif

    color.rgb *= in_Color0.rgb;
    color.a *= in_Color0.a;
    oC0 = vec4( tdu_apply_fog( color.rgb, fog ), color.a );
    d3d9_ps_alpha_test( oC0 );
}
