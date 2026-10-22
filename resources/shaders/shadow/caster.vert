#version 450
#extension GL_GOOGLE_include_directive : require

// Shadow casters, rendered from the light into the shadow map.
//
// Permutations:
//   SHADOW_TEXTURED  forwards UV set 0 (alpha tested casters)
//   SHADOW_SKINNED   4 bone skinning (gVSWorldMatrixArray)
//
// gVSShadowColor = (-, shadow color, depth scale, -), copied to Color0.

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gWorldViewProject( i ) c.f[8 + ( i )]
#define gVSShadowColor c.f[117]

layout( location = 0 ) in vec4 in_Position0;
#ifdef SHADOW_SKINNED
layout( location = 1 ) in vec4 in_BlendIndices0;
layout( location = 2 ) in vec4 in_BlendWeight0;
#define TEXCOORD_LOCATION 3
#else
#define TEXCOORD_LOCATION 1
#endif
#ifdef SHADOW_TEXTURED
layout( location = TEXCOORD_LOCATION ) in vec4 in_Texcoord0;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
#ifdef SHADOW_TEXTURED
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
#endif
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;

void main() {
#ifdef SHADOW_SKINNED
    vec4 position =
        tdu_skin_position( in_Position0, tdu_skin_bones( in_BlendIndices0 ),
                           tdu_skin_weights( in_BlendWeight0 ) );
#else
    vec4 position = in_Position0;
#endif

    gl_Position = vec4( d3d9_dot( position, gWorldViewProject( 0 ) ),
                        d3d9_dot( position, gWorldViewProject( 1 ) ),
                        d3d9_dot( position, gWorldViewProject( 2 ) ),
                        d3d9_dot( position, gWorldViewProject( 3 ) ) );

#ifdef SHADOW_TEXTURED
    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#endif
    out_Texcoord1 = vec4( gl_Position.z * gVSShadowColor.z, 0.0, 0.0, 0.0 );

    out_Color0 = clamp( gVSShadowColor, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
