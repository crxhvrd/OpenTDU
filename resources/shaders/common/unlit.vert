#version 450
#extension GL_GOOGLE_include_directive : require

// Unlit geometry (default table fallbacks and billboards, impostors).
//
// Permutations:
//   UNLIT_FOG           vertex fog (Texcoord1.x)
//   UNLIT_VERTEX_COLOR  forwards Color0 (else white)
//   UNLIT_AMBIENT       Color0 * gVSLightAmbient1 + gVSLightAmbient0
//   UNLIT_TEXCOORD      forwards UV set 0
//   UNLIT_BILLBOARD     camera facing quad: Normal0 holds the corner offset
//                       in view space

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gWorldViewProject( i ) D3D9_CR( 8, i )
#define gVSM4World( i ) D3D9_CR( 16, i )
#define gWorldView( i ) D3D9_CR( 20, i )
#define gVSM4ViewProj( i ) D3D9_CR( 28, i )
#define gVSM4InvView( i ) D3D9_CR( 48, i )
#define gVSLightAmbient0 D3D9_C( 94 )
#define gVSLightAmbient1 D3D9_C( 247 )

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
#ifdef UNLIT_BILLBOARD
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
#endif
#ifdef UNLIT_VERTEX_COLOR
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
#endif
#ifdef UNLIT_TEXCOORD
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
#ifdef UNLIT_TEXCOORD
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
#endif
#ifdef UNLIT_FOG
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
#endif

void main() {
#ifdef UNLIT_BILLBOARD
    vec3 corner = vec3( d3d9_dot( in_Normal0.xyz, gVSM4InvView( 0 ).xyz ),
                        d3d9_dot( in_Normal0.xyz, gVSM4InvView( 1 ).xyz ),
                        d3d9_dot( in_Normal0.xyz, gVSM4InvView( 2 ).xyz ) );
    vec4 position =
        vec4( corner + vec3( d3d9_dot( in_Position0, gVSM4World( 0 ) ),
                             d3d9_dot( in_Position0, gVSM4World( 1 ) ),
                             d3d9_dot( in_Position0, gVSM4World( 2 ) ) ),
              d3d9_dot( in_Position0, gVSM4World( 3 ) ) );
    gl_Position = vec4( d3d9_dot( position, gVSM4ViewProj( 0 ) ),
                        d3d9_dot( position, gVSM4ViewProj( 1 ) ),
                        d3d9_dot( position, gVSM4ViewProj( 2 ) ),
                        d3d9_dot( position, gVSM4ViewProj( 3 ) ) );
#else
    gl_Position = vec4( d3d9_dot( in_Position0, gWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 3 ) ) );
#endif

#ifdef UNLIT_FOG
    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gWorldView( 2 ) ) );
    float viewDistance =
        1.0 / inversesqrt( abs( d3d9_dot( viewPosition, viewPosition ) ) );
    out_Texcoord1 = vec4( tdu_vertex_fog( viewDistance ), 0.0, 0.0, 0.0 );
#endif
#ifdef UNLIT_TEXCOORD
    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#endif
#if defined( UNLIT_AMBIENT )
    out_Color0 =
        vec4( in_Color0.xyz * gVSLightAmbient1.xyz + gVSLightAmbient0.xyz,
              in_Color0.w );
#elif defined( UNLIT_VERTEX_COLOR )
    out_Color0 = in_Color0;
#else
    out_Color0 = vec4( 1.0 );
#endif
    out_Color0 = clamp( out_Color0, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
