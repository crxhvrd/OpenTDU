#version 450
#extension GL_GOOGLE_include_directive : require

// Sun flare sprites, offset in view space.
//
// Permutations:
//   FLARE_VERTEX_COLOR  forwards the vertex color

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"

#define gVSM4Proj( i ) D3D9_CR( 12, i )
#define gVSM4WorldView( i ) D3D9_CR( 20, i )

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0; // xy: view space corner offset
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#ifdef FLARE_VERTEX_COLOR
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;

void main() {
    vec4 position = vec4( in_Position0.xyz, 1.0 );
    vec4 viewPosition = vec4( d3d9_dot( position, gVSM4WorldView( 0 ) ),
                              d3d9_dot( position, gVSM4WorldView( 1 ) ),
                              d3d9_dot( position, gVSM4WorldView( 2 ) ),
                              d3d9_dot( position, gVSM4WorldView( 3 ) ) );
    viewPosition.xy += in_Normal0.xy;
    gl_Position = vec4( d3d9_dot( viewPosition, gVSM4Proj( 0 ) ),
                        d3d9_dot( viewPosition, gVSM4Proj( 1 ) ),
                        d3d9_dot( viewPosition, gVSM4Proj( 2 ) ),
                        d3d9_dot( viewPosition, gVSM4Proj( 3 ) ) );

    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );

#ifdef FLARE_VERTEX_COLOR
    out_Color0 = clamp( in_Color0, vec4( 0.0 ), vec4( 1.0 ) );
#else
    out_Color0 = vec4( 1.0 );
#endif
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
