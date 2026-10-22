#version 450
#extension GL_GOOGLE_include_directive : require

// In car GPS screen: the map (s0) times the lit vertex color, scaled by the
// screen brightness (color and alpha), then the fog.

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

D3D9_DECLARE_SAMPLER_2D( 0 )
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
layout( location = 0 ) out vec4 oC0;

void main() {
    float brightness = d3d9_ps_texcoord( in_Texcoord3 ).x;
    vec4 color = tex2D_s0( d3d9_ps_texcoord( in_Texcoord1 ).xy ) *
                 d3d9_ps_texcoord( in_Texcoord0 );
    color *= vec4( brightness );
    oC0 = vec4( tdu_apply_fog( color.xyz, d3d9_ps_texcoord( in_Texcoord2 ).x ),
                color.w );
    d3d9_ps_alpha_test( oC0 );
}
