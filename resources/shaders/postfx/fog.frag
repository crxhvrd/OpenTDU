#version 450
#extension GL_GOOGLE_include_directive : require

// Fog overlay: fog_color, its opacity from the depth (s0) between the fog
// start and end (fog_params.xy), scaled by fog_params.w.

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define fog_params D3D9_C( 5 )
#define fog_color D3D9_C( 6 )

D3D9_DECLARE_SAMPLER_2D( 0 )
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = 0 ) out vec4 oC0;

void main() {
    float depth = tex2D_s0( d3d9_ps_texcoord( in_Texcoord0 ).xy ).x;
    float fog = d3d9_saturate( ( depth + -fog_params.x ) *
                               ( 1.0 / ( -fog_params.x + fog_params.y ) ) );
    oC0 = vec4( fog_color.xyz, fog * fog_params.w );
    d3d9_ps_alpha_test( oC0 );
}
