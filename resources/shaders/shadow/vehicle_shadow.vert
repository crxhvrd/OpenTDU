#version 450
#extension GL_GOOGLE_include_directive : require

// Shadow Vehicle table section.
//
// Permutations:
//   VEHICLE_SHADOW_TEXTURED  forwards UV set 0

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gWorldViewProject( i ) c.f[8 + ( i )]
#define gWorldView( i ) c.f[20 + ( i )]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
#ifdef VEHICLE_SHADOW_TEXTURED
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
#ifdef VEHICLE_SHADOW_TEXTURED
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
#endif
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 3 ) ) );

    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gWorldView( 2 ) ) );
    out_Texcoord1 =
        vec4( tdu_vertex_fog( 1.0 / inversesqrt( abs( d3d9_dot(
                                        viewPosition, viewPosition ) ) ) ),
              0.0, 0.0, 0.0 );
#ifdef VEHICLE_SHADOW_TEXTURED
    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#endif

    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
