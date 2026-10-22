#version 450
#extension GL_GOOGLE_include_directive : require

// Low detail skinned characters (avatar table NPCs): texture tinted by
// gPSBaseColor, lit by the vertex light, then the fog.

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gPSBaseColor c.f[18]

D3D9_DECLARE_SAMPLER_2D( 0 ) // gTextureSampler
layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 color = tex2D_s0( d3d9_ps_texcoord( in_Texcoord1 ).xy ) * gPSBaseColor;
    // OTDU_ADDITION: night lamps join the vertex light.
    vec3 light =
        in_Color0.xyz + tdu_lamp_pixel( in_ViewPosition, in_ViewNormal );
    oC0 = vec4(
        tdu_apply_fog( color.xyz * light, d3d9_ps_texcoord( in_Texcoord2 ).x ),
        color.w );
    d3d9_ps_alpha_test( oC0 );
}
