#version 450
#extension GL_GOOGLE_include_directive : require

// River vertex shader.
//
// Permutations:
//   RIVER_VERTEX_COLOR       forwards the vertex color
//   RIVER_FADE               forwards gVSFadeAlpha
//   RIVER_PLANAR_REFLECTION  projects the vertex in the reflection camera
//   RIVER_UV_MATRIX0..2      UV set N goes through gUVMatrixN instead of the
//                            gVSTweak[N + 1] scroll ((u, v) * z + (x, y))

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gVSFadeAlpha c.f[5]
#define gVSWorldViewProject( i ) c.f[8 + ( i )]
#define gVSWorld( i ) c.f[16 + ( i )]
#define gVSWorldView( i ) c.f[20 + ( i )]
#define gUVMatrix2( i ) c.f[76 + ( i )]
#define gUVMatrix1( i ) c.f[80 + ( i )]
#define gUVMatrix0( i ) c.f[84 + ( i )]
#define gVSLightDir c.f[93]
#define gVSCamPosInObj c.f[119]
#define gVSReflViewProjUV( i ) c.f[125 + ( i )]
#define gVSTweak( i ) c.f[234 + ( i )]

const float kLog2E = 1.442695;

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
#ifdef RIVER_VERTEX_COLOR
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
#endif
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
layout( location = D3D9_IN_TEXCOORD( 1 ) ) in vec4 in_Texcoord1;
layout( location = D3D9_IN_TEXCOORD( 2 ) ) in vec4 in_Texcoord2;

layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
layout( location = D3D9_SLOT_TEXCOORD5 ) out vec4 out_Texcoord5;
layout( location = D3D9_SLOT_TEXCOORD6 ) out vec4 out_Texcoord6;
layout( location = D3D9_SLOT_TEXCOORD7 ) out vec4 out_Texcoord7;

#define APPLY_UV_SCROLL( uv, n )                                               \
    ( ( uv ) * gVSTweak( n ).zz + gVSTweak( n ).xy )

// Row i of gVSReflViewProjUV * gVSWorld (combined per vertex, as retail does).
vec4 reflectionRow( vec4 projectionRow ) {
    vec4 row = gVSWorld( 1 ) * projectionRow.y;
    row = gVSWorld( 0 ) * projectionRow.x + row;
    row = gVSWorld( 2 ) * projectionRow.z + row;
    return gVSWorld( 3 ) * projectionRow.w + row;
}

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gVSWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 3 ) ) );

#ifdef RIVER_UV_MATRIX0
    vec2 uv0 = TDU_APPLY_UV_MATRIX( in_Texcoord0.xy, gUVMatrix0 );
#else
    vec2 uv0 = APPLY_UV_SCROLL( in_Texcoord0.xy, 1 );
#endif
#ifdef RIVER_UV_MATRIX1
    vec2 uv1 = TDU_APPLY_UV_MATRIX( in_Texcoord1.xy, gUVMatrix1 );
#else
    vec2 uv1 = APPLY_UV_SCROLL( in_Texcoord1.xy, 2 );
#endif
#ifdef RIVER_UV_MATRIX2
    vec2 uv2 = TDU_APPLY_UV_MATRIX( in_Texcoord2.xy, gUVMatrix2 );
#else
    vec2 uv2 = APPLY_UV_SCROLL( in_Texcoord2.xy, 3 );
#endif
    out_Texcoord0 = vec4( uv0, 0.0, 0.0 );
    out_Texcoord1 = vec4( uv1, 0.0, 0.0 );
    out_Texcoord2 = vec4( uv2, 0.0, 0.0 );

    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gVSWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 2 ) ) );
    float viewDistance = length( viewPosition );

    out_Texcoord3 = vec4( -gVSLightDir.xyz, 0.0 );
    out_Texcoord4 = vec4( viewPosition, 0.0 );
    out_Texcoord5 = vec4( gVSCamPosInObj.xyz - in_Position0.xyz, 0.0 );

#ifdef RIVER_PLANAR_REFLECTION
    out_Texcoord6 = vec4(
        d3d9_dot( in_Position0, reflectionRow( gVSReflViewProjUV( 0 ) ) ),
        d3d9_dot( in_Position0, reflectionRow( gVSReflViewProjUV( 1 ) ) ),
        d3d9_dot( in_Position0, reflectionRow( gVSReflViewProjUV( 2 ) ) ),
        d3d9_dot( in_Position0, reflectionRow( gVSReflViewProjUV( 3 ) ) ) );
#else
    out_Texcoord6 = vec4( 0.0 );
#endif

    // x: distortion falloff, y: fresnel falloff, z: fade
    vec2 falloff = exp2( viewDistance * -gVSTweak( 0 ).xy * kLog2E );
#ifdef RIVER_FADE
    float fade = gVSFadeAlpha.x;
#else
    float fade = 1.0;
#endif
    out_Texcoord7 = vec4( falloff, fade, 0.0 );

#ifdef RIVER_VERTEX_COLOR
    out_Color0 = clamp( in_Color0, vec4( 0.0 ), vec4( 1.0 ) );
#else
    out_Color0 = vec4( 1.0 );
#endif
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
