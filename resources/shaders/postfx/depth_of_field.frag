#version 450
#extension GL_GOOGLE_include_directive : require

// Depth of field: blends the sharp (s0) and blurred (s1) scenes by the blur
// amount derived from the depth (s2).
//
// Permutations:
//   DOF_PARAMS  blur amount = saturate(depth * x + y) (dof_params_after),
//               else depth squared

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define dof_params_after D3D9_C( 2 )

D3D9_DECLARE_SAMPLER_2D( 0 ) // sharp
D3D9_DECLARE_SAMPLER_2D( 1 ) // blurred
D3D9_DECLARE_SAMPLER_2D( 2 ) // depth
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = 0 ) out vec4 oC0;

void main() {
    vec2 uv = d3d9_ps_texcoord( in_Texcoord0 ).xy;
    float depth = tex2D_s2( uv ).x;
#ifdef DOF_PARAMS
    float blur = d3d9_saturate(
        d3d9_dot( vec2( depth, 1.0 ), dof_params_after.xy ) + 0.0 );
#else
    float blur = depth * depth;
#endif
    oC0 = d3d9_lerp( tex2D_s0( uv ), tex2D_s1( uv ), vec4( blur ) );
    d3d9_ps_alpha_test( oC0 );
}
