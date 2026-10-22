#version 450
#extension GL_GOOGLE_include_directive : require

// Textured vehicle quad (table vehicle_common): the position is scaled and
// offset in object space (gVSMaterialParam[0]) and in clip space
// (gVSMaterialParam[1]); the scaled position is the texture coordinate.

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"

#define gVSM4WorldViewProj( i ) c.f[8 + ( i )]
#define gVSMaterialParam( i ) c.f[88 + ( i )]

layout( location = 0 ) in vec4 in_Position0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;

void main() {
    vec4 position = vec4( in_Position0.xy * gVSMaterialParam( 0 ).zw +
                              gVSMaterialParam( 0 ).xy,
                          in_Position0.zw );
    gl_Position = vec4( d3d9_dot( position, gVSM4WorldViewProj( 0 ) ) +
                            -gVSMaterialParam( 1 ).x,
                        d3d9_dot( position, gVSM4WorldViewProj( 1 ) ) +
                            gVSMaterialParam( 1 ).y,
                        d3d9_dot( position, gVSM4WorldViewProj( 2 ) ),
                        d3d9_dot( position, gVSM4WorldViewProj( 3 ) ) );
    out_Texcoord0 = vec4( position.xy, 0.0, 0.0 );

    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
