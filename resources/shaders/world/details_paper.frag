#version 450
#extension GL_GOOGLE_include_directive : require

// Ground litter (details table): base texture times the vertex color, the
// sector shadow map (s1 blue) plus the distance fade both scale color and
// alpha, then the fog.

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

D3D9_DECLARE_SAMPLER_2D( 0 ) // gTexSampler
D3D9_DECLARE_SAMPLER_2D( 1 ) // gTexSamplerShadowMap

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 t1 = d3d9_ps_texcoord( in_Texcoord1 );
    float shade = tex2D_s1( t1.xy ).z + t1.z;
    vec4 color = tex2D_s0( d3d9_ps_texcoord( in_Texcoord0 ).xy ) * in_Color0;
    color = vec4( color.xyz * shade, shade * color.w );
    oC0 = vec4( tdu_apply_fog( color.xyz, d3d9_ps_texcoord( in_Texcoord2 ).x ),
                color.w );
    d3d9_ps_alpha_test( oC0 );
}
