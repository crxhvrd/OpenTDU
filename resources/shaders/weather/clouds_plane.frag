#version 450
#extension GL_GOOGLE_include_directive : require

// Perlin noise cloud plane.
//
// gPSMaterialParam = (cover, sharpness, -1 / alphaLength,
//                     (alphaStart + alphaLength) / alphaLength)
// gPSMaterialParamBis.x = opacifier

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define gPSMaterialParam c.f[4]
#define gPSMaterialParamBis c.f[14]

D3D9_DECLARE_SAMPLER_2D( 0 ) // assembled perlin noise

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 uv = d3d9_ps_texcoord( in_Texcoord0 );
    float viewDistance = d3d9_ps_texcoord( in_Texcoord1 ).x;

    float density = max( tex2D_s0( uv.xy ).x - gPSMaterialParam.x, 0.0 );
    float shade = 1.0 - pow( abs( gPSMaterialParam.y ), density );
    float distanceFade =
        d3d9_saturate( gPSMaterialParam.z * viewDistance + gPSMaterialParam.w );

    oC0 = vec4( shade * in_Color0.rgb, d3d9_saturate( density * distanceFade *
                                                      gPSMaterialParamBis.x ) );
    d3d9_ps_alpha_test( oC0 );
}
