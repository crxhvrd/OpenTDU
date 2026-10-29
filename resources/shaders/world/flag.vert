#version 450
#extension GL_GOOGLE_include_directive : require

// Flags.
//
// Permutations:
//   FLAG_LIT          two-sided sun lighting
//   FLAG_DOUBLE_SIDED outputs the view space normal (texture mirrored on the
//                     back face)

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gWorldViewProject( i ) D3D9_CR( 8, i )
#define gWorldView( i ) D3D9_CR( 20, i )
#define gVSLightDir D3D9_C( 93 )
#define gVSLightAmbient D3D9_C( 94 )
#define gVSLightDiffuse D3D9_C( 95 )

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
#if defined( FLAG_LIT ) || defined( FLAG_DOUBLE_SIDED )
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#else
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
#ifdef FLAG_DOUBLE_SIDED
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
#endif

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 3 ) ) );

    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gWorldView( 2 ) ) );

    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
    out_Texcoord1 =
        vec4( tdu_vertex_fog( length( viewPosition ) ), 0.0, 0.0, 0.0 );
#ifdef FLAG_DOUBLE_SIDED
    out_Texcoord2 =
        vec4( d3d9_dot( in_Normal0.xyz, gWorldView( 0 ).xyz ),
              d3d9_dot( in_Normal0.xyz, gWorldView( 1 ).xyz ),
              d3d9_dot( in_Normal0.xyz, gWorldView( 2 ).xyz ), 0.0 );
#endif

#ifdef FLAG_LIT
    float diffuse = abs( d3d9_dot( in_Normal0.xyz, -gVSLightDir.xyz ) );
    vec4 color = ( diffuse * gVSLightDiffuse + gVSLightAmbient ) * in_Color0;
#else
    vec4 color = in_Color0;
#endif
    out_Color0 = clamp( color, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
