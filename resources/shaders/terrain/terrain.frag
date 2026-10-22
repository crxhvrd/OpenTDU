#version 450
#extension GL_GOOGLE_include_directive : require

// Heightmap terrain pixel shader.
//
// Permutations:
//   TERRAIN_DETAIL  baked shadow map + two detail maps blended by the type map
//   TERRAIN_FADE    alpha comes from the vertex distance fade

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

D3D9_DECLARE_SAMPLER_2D( 0 ) // color map
#ifdef TERRAIN_DETAIL
D3D9_DECLARE_SAMPLER_2D( 1 ) // baked shadow map
D3D9_DECLARE_SAMPLER_2D( 2 ) // detail map (type 0)
D3D9_DECLARE_SAMPLER_2D( 3 ) // type map
D3D9_DECLARE_SAMPLER_2D( 4 ) // detail map (type 1)
#endif

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#ifdef TERRAIN_DETAIL
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#endif
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#ifdef TERRAIN_DETAIL
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
#endif
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 uv = d3d9_ps_texcoord( in_Texcoord0 );
    vec4 fogShadowFade = d3d9_ps_texcoord( in_Texcoord2 );

    vec4 color = tex2D_s0( uv.xy );
#ifdef TERRAIN_DETAIL
    vec4 detailUV = d3d9_ps_texcoord( in_Texcoord1 );
    float shadow =
        tdu_baked_shadow( min( fogShadowFade.y + tex2D_s1( uv.zw ).x, 1.0 ) );
    vec4 detail = d3d9_lerp( tex2D_s2( detailUV.xy ), tex2D_s4( detailUV.xy ),
                             tex2D_s3( uv.xy ).x );
    vec3 albedo = color.rgb * detail.rgb;
    color.rgb *= shadow;
    color *= detail;
#endif
    color.rgb *= in_Color0.rgb;
#ifdef TERRAIN_DETAIL
    // OTDU_ADDITION: night lamps light the ground (normal = up).
    if ( tdu_lamps_enabled() ) {
        color.rgb +=
            albedo * tdu_lamp_light( in_ViewPosition.xyz, tdu_view_up.xyz );
    }
#endif
#ifdef TERRAIN_FADE
    color.a = d3d9_saturate( fogShadowFade.z );
#endif

    oC0 = vec4( tdu_apply_fog( color.rgb, fogShadowFade.x ), color.a );

    d3d9_ps_alpha_test( oC0 );
}
