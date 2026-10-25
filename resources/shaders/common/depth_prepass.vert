#version 450
#extension GL_GOOGLE_include_directive : require

// Depth prepass.
//
// Permutations:
//   DEPTH_SKINNED  4 bone skinning

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gVSWorldViewProject( i ) c.f[8 + ( i )]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
#ifdef DEPTH_SKINNED
layout( location = D3D9_IN_BLENDINDICES0 ) in vec4 in_BlendIndices0;
layout( location = D3D9_IN_BLENDWEIGHT0 ) in vec4 in_BlendWeight0;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;

void main() {
#ifdef DEPTH_SKINNED
    vec4 position =
        tdu_skin_position( in_Position0, tdu_skin_bones( in_BlendIndices0 ),
                           tdu_skin_weights( in_BlendWeight0 ) );
#else
    vec4 position = in_Position0;
#endif
    gl_Position = vec4( d3d9_dot( position, gVSWorldViewProject( 0 ) ),
                        d3d9_dot( position, gVSWorldViewProject( 1 ) ),
                        d3d9_dot( position, gVSWorldViewProject( 2 ) ),
                        d3d9_dot( position, gVSWorldViewProject( 3 ) ) );
    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
