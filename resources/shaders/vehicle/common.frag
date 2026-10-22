#version 450
#extension GL_GOOGLE_include_directive : require

// Textured vehicle quad (table vehicle_common): opacity from
// gPSMaterialParam.x.

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define gPSMaterialParam c.f[4]

D3D9_DECLARE_SAMPLER_2D( 0 )

layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = 0 ) out vec4 oC0;

void main() {
    oC0 = vec4( tex2D_s0( d3d9_ps_texcoord( in_Texcoord0 ).xy ).xyz,
                gPSMaterialParam.x );
    d3d9_ps_alpha_test( oC0 );
}
