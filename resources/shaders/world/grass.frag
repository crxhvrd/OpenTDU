#version 450
#extension GL_GOOGLE_include_directive : require

// Tall grass, shaded with the sector color and baked shadow maps.

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

D3D9_DECLARE_SAMPLER_2D( 0 ) // grass atlas
D3D9_DECLARE_SAMPLER_2D( 1 ) // sector color map
D3D9_DECLARE_SAMPLER_2D( 2 ) // sector baked shadow map

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 grassUVFog = d3d9_ps_texcoord( in_Texcoord0 );
    vec4 sectorUVShadowFade = d3d9_ps_texcoord( in_Texcoord1 );

    float shadow = tdu_baked_shadow( tex2D_s2( sectorUVShadowFade.xy ).x ) +
                   sectorUVShadowFade.z;
    vec4 grass = tex2D_s0( grassUVFog.xy );
    vec3 color = tex2D_s1( sectorUVShadowFade.xy ).rgb * grass.rgb;
    vec3 albedo = color;
    color = shadow * color * in_Color0.rgb;
    // OTDU_ADDITION: night lamps light the blades (normal = up).
    if ( tdu_lamps_enabled() ) {
        color +=
            albedo * tdu_lamp_light( in_ViewPosition.xyz, tdu_view_up.xyz );
    }

    oC0 = vec4( tdu_apply_fog( color, grassUVFog.z ), grass.a * in_Color0.a );
    d3d9_ps_alpha_test( oC0 );
}
