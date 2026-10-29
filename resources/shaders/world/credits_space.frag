#version 450
#extension GL_GOOGLE_include_directive : require

// Credits scene space background: a noise band between gPSMaterialParam.x and
// gPSMaterialParam.y, shaded by a sine of its position in the band.
//
// gPSMaterialParam = (band start, band end, intensity, -)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define gPSMaterialParam D3D9_C( 4 )

const float kPi = 3.1415927;

D3D9_DECLARE_SAMPLER_2D( 0 ) // noise

layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = 0 ) out vec4 oC0;

void main() {
    float noise = tex2D_s0( d3d9_ps_texcoord( in_Texcoord0 ).xy ).x;

    float toEnd = -noise + gPSMaterialParam.y;
    float fromStart = noise + -gPSMaterialParam.x;
    float outsideBand =
        ( ( toEnd >= 0.0 ) ? 0.0 : 1.0 ) + ( ( fromStart >= 0.0 ) ? 0.0 : 1.0 );
    float band =
        fromStart * ( 1.0 / ( -gPSMaterialParam.x + gPSMaterialParam.y ) );
    band = ( -outsideBand >= 0.0 ) ? band : 0.0;

    float phase = fract( band * 0.5 + 0.5 ) * ( 2.0 * kPi ) + -kPi;
    oC0 = vec4( vec3( band * ( sin( phase ) * gPSMaterialParam.z ) ), 1.0 );
    d3d9_ps_alpha_test( oC0 );
}
