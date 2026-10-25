#version 450
#extension GL_GOOGLE_include_directive : require

// Ocean height map passes (retail ocean table).
//
// Permutations:
//   OCEAN_HEIGHT_NOISE  render target quad from UV set 0; two noise octaves
//                       over the world position (gVSScale)
//   (default)           UV set 0 through gVSMaterialParam[0..3] (two UV sets)

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"

#define gVSM4WorldViewProj( i ) c.f[8 + ( i )]
#define gVSMaterialParam( i ) c.f[88 + ( i )]
#define gVSScale c.f[234]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;

void main() {
#ifdef OCEAN_HEIGHT_NOISE
    vec2 position = in_Position0.xz * gVSScale.xx;
    out_Texcoord0 = vec4( position * vec2( 0.007813 ), 0.0, 0.0 );
    out_Texcoord1 = vec4( position * vec2( 0.125 ), 0.0, 0.0 );
    gl_Position = in_Texcoord0.xyxx * vec4( 2.0, -2.0, 0.0, 0.0 ) +
                  vec4( -1.0, 1.0, 0.0, 1.0 );
#else
    gl_Position = vec4( d3d9_dot( in_Position0, gVSM4WorldViewProj( 0 ) ),
                        d3d9_dot( in_Position0, gVSM4WorldViewProj( 1 ) ),
                        d3d9_dot( in_Position0, gVSM4WorldViewProj( 2 ) ),
                        d3d9_dot( in_Position0, gVSM4WorldViewProj( 3 ) ) );
    out_Texcoord0 =
        vec4( d3d9_dot( in_Texcoord0, gVSMaterialParam( 0 ) ),
              d3d9_dot( in_Texcoord0, gVSMaterialParam( 1 ) ), 0.0, 0.0 );
    out_Texcoord1 =
        vec4( d3d9_dot( in_Texcoord0, gVSMaterialParam( 2 ) ),
              d3d9_dot( in_Texcoord0, gVSMaterialParam( 3 ) ), 0.0, 0.0 );
#endif
    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
