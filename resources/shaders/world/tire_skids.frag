#version 450
#extension GL_GOOGLE_include_directive : require

// Tire skid marks: the tarmac (s0) and ground (s1) marks blended by
// Texcoord1.x, times the vertex color, then the fog.

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

D3D9_DECLARE_SAMPLER_2D( 0 ) // gTarmacSampler
D3D9_DECLARE_SAMPLER_2D( 1 ) // gGroundSampler

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
layout( location = 0 ) out vec4 oC0;

void main() {
    vec2 uv = d3d9_ps_texcoord( in_Texcoord0 ).xy;
    vec4 color = d3d9_lerp( tex2D_s0( uv ), tex2D_s1( uv ),
                            d3d9_ps_texcoord( in_Texcoord1 ).xxxx );
    color *= in_Color0;
    oC0 = vec4( tdu_apply_fog( color.xyz, d3d9_ps_texcoord( in_Texcoord3 ).x ),
                color.w );
    d3d9_ps_alpha_test( oC0 );
}
