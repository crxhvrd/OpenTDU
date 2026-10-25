#version 450
#extension GL_GOOGLE_include_directive : require

// Ocean vertex shader.
//
// Permutations:
//   OCEAN_VERTEX_COLOR       forwards the vertex color
//   OCEAN_DETAIL_UV          three UV sets: detail (UV0), normal maps (UV1/UV2)
//   OCEAN_PLANAR_REFLECTION  projects the vertex in the reflection camera
//   OCEAN_UV_MATRIX0..2      UV set N goes through gUVMatrixN

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gVSWorldViewProject( i ) c.f[8 + ( i )]
#define gVSWorldView( i ) c.f[20 + ( i )]
#define gUVMatrix2( i ) c.f[76 + ( i )]
#define gUVMatrix1( i ) c.f[80 + ( i )]
#define gUVMatrix0( i ) c.f[84 + ( i )]
#define gVSLightDir c.f[93]
#define gVSCamPosInObj c.f[119]
#define gVSReflWorldViewProj( i ) c.f[125 + ( i )]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
#ifdef OCEAN_VERTEX_COLOR
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
#endif
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
layout( location = D3D9_IN_TEXCOORD( 1 ) ) in vec4 in_Texcoord1;
#ifdef OCEAN_DETAIL_UV
layout( location = D3D9_IN_TEXCOORD( 2 ) ) in vec4 in_Texcoord2;
#endif

layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
#ifdef OCEAN_DETAIL_UV
layout( location = D3D9_SLOT_TEXCOORD5 ) out vec4 out_Texcoord5;
#endif
#ifdef OCEAN_PLANAR_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD6 ) out vec4 out_Texcoord6;
#endif

vec4 transform( vec4 v, vec4 row0, vec4 row1, vec4 row2, vec4 row3 ) {
    return vec4( d3d9_dot( v, row0 ), d3d9_dot( v, row1 ), d3d9_dot( v, row2 ),
                 d3d9_dot( v, row3 ) );
}

void main() {
    gl_Position = transform( in_Position0, gVSWorldViewProject( 0 ),
                             gVSWorldViewProject( 1 ), gVSWorldViewProject( 2 ),
                             gVSWorldViewProject( 3 ) );

    vec2 uv0 = in_Texcoord0.xy;
#ifdef OCEAN_UV_MATRIX0
    uv0 = TDU_APPLY_UV_MATRIX( uv0, gUVMatrix0 );
#endif
    vec2 uv1 = in_Texcoord1.xy;
#ifdef OCEAN_UV_MATRIX1
    uv1 = TDU_APPLY_UV_MATRIX( uv1, gUVMatrix1 );
#endif
#ifdef OCEAN_DETAIL_UV
    vec2 uv2 = in_Texcoord2.xy;
#ifdef OCEAN_UV_MATRIX2
    uv2 = TDU_APPLY_UV_MATRIX( uv2, gUVMatrix2 );
#endif
    out_Texcoord0 = vec4( uv1, 0.0, 0.0 );
    out_Texcoord1 = vec4( uv2, 0.0, 0.0 );
    out_Texcoord5 = vec4( uv0, 0.0, 0.0 );
#else
    out_Texcoord0 = vec4( uv0, 0.0, 0.0 );
    out_Texcoord1 = vec4( uv1, 0.0, 0.0 );
#endif

    out_Texcoord2 = vec4( gVSLightDir.xyz, 0.0 );
    out_Texcoord3 =
        transform( in_Position0, gVSWorldView( 0 ), gVSWorldView( 1 ),
                   gVSWorldView( 2 ), gVSWorldView( 3 ) );
    out_Texcoord4 = gVSCamPosInObj - in_Position0;
#ifdef OCEAN_PLANAR_REFLECTION
    out_Texcoord6 = transform(
        in_Position0, gVSReflWorldViewProj( 0 ), gVSReflWorldViewProj( 1 ),
        gVSReflWorldViewProj( 2 ), gVSReflWorldViewProj( 3 ) );
#endif

#ifdef OCEAN_VERTEX_COLOR
    out_Color0 = clamp( in_Color0, vec4( 0.0 ), vec4( 1.0 ) );
#else
    out_Color0 = vec4( 1.0 );
#endif
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
