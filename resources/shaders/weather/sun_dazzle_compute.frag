#version 450
#extension GL_GOOGLE_include_directive : require

// Computes the dazzle coefficient (1x1 target) from the sun luminances.
//
// gPSMaterialParam = (decrease speed * dt, dt, threshold, 2)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define gPSMaterialParam D3D9_C( 4 )

D3D9_DECLARE_SAMPLER_2D( 0 ) // eye luminance
D3D9_DECLARE_SAMPLER_2D( 1 ) // previous eye luminance
D3D9_DECLARE_SAMPLER_2D( 2 ) // previous dazzle coefficient

layout( location = 0 ) out vec4 oC0;

void main() {
    float luminanceIncrease =
        tex2D_s0( vec2( 0.5 ) ).x - tex2D_s1( vec2( 0.5 ) ).x;
    float previousDazzle = tex2D_s2( vec2( 0.5 ) ).x;

    float dazzle = max( luminanceIncrease - gPSMaterialParam.z, 0.0 ) *
                       gPSMaterialParam.w +
                   ( previousDazzle - gPSMaterialParam.x );

    oC0 = vec4( d3d9_saturate( dazzle ) );
    d3d9_ps_alpha_test( oC0 );
}
