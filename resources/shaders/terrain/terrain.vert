#version 450
#extension GL_GOOGLE_include_directive : require

// Heightmap terrain vertex shader.
//
// Permutations:
//   TERRAIN_DETAIL     outputs the detail map coordinates (near LODs)
//   TERRAIN_SCALED_UV  color map coordinates are scaled by (16/11, 16/9)
//   TERRAIN_FADE       distance based alpha fade (LOD cross-fade)

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"

#define gColorContrast c.f[1]
#define gFadeAlpha c.f[5]
#define gWorldViewProject( i ) c.f[8 + ( i )]
#define gWorldView( i ) c.f[20 + ( i )]
#define gFogParametersVS c.f[107]
#define gHeightMapConst c.f[122]
#define gFogColorVS c.f[123]

// Shadow map coordinates fade out over this distance (in meters).
const float kShadowFadeDistance = 768.0;
// Without detail, the faded LOD is hidden below this distance.
const float kFadeStartDistance = 384.0;

layout( location = 0 ) in vec4 in_Texcoord0; // xy: heightmap grid position
layout( location = 1 ) in vec4 in_Texcoord1; // x: height
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
#ifdef TERRAIN_DETAIL
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
#endif
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;

void main() {
    vec4 position =
        vec4( in_Texcoord0.x, in_Texcoord1.x, in_Texcoord0.y, 1.0 );
    gl_Position = vec4( dot( position, gWorldViewProject( 0 ) ),
                        dot( position, gWorldViewProject( 1 ) ),
                        dot( position, gWorldViewProject( 2 ) ),
                        dot( position, gWorldViewProject( 3 ) ) );

    vec3 viewPosition = vec3( dot( position, gWorldView( 0 ) ),
                              dot( position, gWorldView( 1 ) ),
                              dot( position, gWorldView( 2 ) ) );
    float viewDistance = length( viewPosition );

    vec2 shadowUV = in_Texcoord0.xy / gHeightMapConst.y;
    vec2 colorUV = shadowUV;
#ifdef TERRAIN_SCALED_UV
    colorUV *= vec2( 16.0 / 11.0, 16.0 / 9.0 );
#endif
    out_Texcoord0 = vec4( colorUV, shadowUV );
#ifdef TERRAIN_DETAIL
    out_Texcoord1 = vec4( colorUV * gHeightMapConst.z, 0.0, 0.0 );
#endif

    float fog = clamp( ( gFogParametersVS.z - viewDistance ) / gFogParametersVS.w,
                       0.0, 1.0 );
    fog = max( fog, gFogColorVS.w );
    bool bFogEnabled = gFogParametersVS.x * gFogParametersVS.x > 0.0;

    float shadowFade = max( kShadowFadeDistance - viewDistance, 0.0 ) *
                       gColorContrast.w / kShadowFadeDistance;

    float fade = 0.0;
#ifdef TERRAIN_FADE
    fade = gFadeAlpha.y * viewDistance + gFadeAlpha.z;
#ifndef TERRAIN_DETAIL
    fade = ( viewDistance < kFadeStartDistance ) ? 0.0 : fade;
#endif
#endif
    out_Texcoord2 = vec4( bFogEnabled ? fog : 1.0, shadowFade, fade, 0.0 );

    out_Color0 = clamp( gColorContrast, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
