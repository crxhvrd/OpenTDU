#version 450
#extension GL_GOOGLE_include_directive : require

// Sun core and halo quads. The sprite zooms out and fades with the eye
// luminance (SUN_EYE_LUM, 1x1), which is how occlusion hides the sun.
//
// Permutations:
//   SUN_HALO  halo response curve (1 - (1 - lum)^2) floored by
//             gPSMaterialParam.x for the zoom
//
// gPSMaterialParam = (minimum halo size, intensity, sprite morph, 0)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define gPSMaterialParam c.f[4]

D3D9_DECLARE_SAMPLER_2D( 0 ) // current day period sprite
D3D9_DECLARE_SAMPLER_2D( 1 ) // next day period sprite
D3D9_DECLARE_SAMPLER_2D( 2 ) // eye luminance

layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 uv = d3d9_ps_texcoord( in_Texcoord0 );
    vec4 color = d3d9_ps_texcoord( in_Texcoord1 );

    float eyeLuminance = tex2D_s2( vec2( 0.5 ) ).x;
#ifdef SUN_HALO
    float visibility = eyeLuminance * ( 2.0 - eyeLuminance );
    float size = visibility + max( gPSMaterialParam.x - visibility, 0.0 );
#else
    float visibility = eyeLuminance;
    float size = visibility;
#endif
    vec2 spriteUV = ( uv.xy - 0.5 ) * ( 1.0 / ( size + 0.001 ) ) + 0.5;

    vec4 sprite = d3d9_lerp( tex2D_s0( spriteUV ), tex2D_s1( spriteUV ),
                             gPSMaterialParam.z );
    sprite.rgb *= gPSMaterialParam.y;
    sprite *= color;
    sprite.a *= visibility;

    oC0 = sprite;
    d3d9_ps_alpha_test( oC0 );
}
