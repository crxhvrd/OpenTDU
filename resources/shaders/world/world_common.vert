#version 450
#extension GL_GOOGLE_include_directive : require

// Normal mapped world geometry (World Common), lit in tangent space.
//
// Permutations:
//   WORLD_UV_MATRIX0..1           UV set N goes through gUVMatrixN
//   WORLD_TANGENT_REFLECTION_FIX  OTDU_ADDITION: forwards the tangent basis so
//                                 the reflection can be rebuilt in object
//                                 space (retail reflects the tangent space
//                                 vector, see PatchTangentReflectionVS)

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gVSWorldViewProject( i ) c.f[8 + ( i )]
#define gVSWorldView( i ) c.f[20 + ( i )]
#define gUVMatrix1( i ) c.f[80 + ( i )]
#define gUVMatrix0( i ) c.f[84 + ( i )]
#define gVSLightDir c.f[93]
#define gVSCamPosInObj c.f[119]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
layout( location = D3D9_IN_BINORMAL0 ) in vec4 in_Binormal0;
layout( location = D3D9_IN_TANGENT0 ) in vec4 in_Tangent0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
layout( location = D3D9_IN_TEXCOORD( 1 ) ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
#ifdef WORLD_TANGENT_REFLECTION_FIX
layout( location = D3D9_SLOT_TEXCOORD5 ) out vec4 out_Texcoord5;
layout( location = D3D9_SLOT_TEXCOORD6 ) out vec4 out_Texcoord6;
layout( location = D3D9_SLOT_TEXCOORD7 ) out vec4 out_Texcoord7;
#endif

layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) out vec4 out_ViewNormal;     // OTDU

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gVSWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 3 ) ) );

#ifdef WORLD_UV_MATRIX0
    out_Texcoord0 =
        vec4( TDU_APPLY_UV_MATRIX( in_Texcoord0.xy, gUVMatrix0 ), 0.0, 0.0 );
#else
    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#endif
#ifdef WORLD_UV_MATRIX1
    out_Texcoord1 =
        vec4( TDU_APPLY_UV_MATRIX( in_Texcoord1.xy, gUVMatrix1 ), 0.0, 0.0 );
#else
    out_Texcoord1 = vec4( in_Texcoord1.xy, 0.0, 0.0 );
#endif

    // The view vector uses T x N where the light uses the vertex binormal.
    vec3 tangent = in_Tangent0.xyz;
    vec3 tangentCrossNormal = d3d9_cross( tangent, in_Normal0.xyz );
    vec3 toCamera = gVSCamPosInObj.xyz - in_Position0.xyz;
    out_Texcoord2 = vec4( d3d9_dot( toCamera, tangent ),
                          d3d9_dot( toCamera, tangentCrossNormal ),
                          d3d9_dot( toCamera, in_Normal0.xyz ), 0.0 );
    out_Texcoord3 = vec4( d3d9_dot( gVSLightDir.xyz, tangent ),
                          d3d9_dot( gVSLightDir.xyz, in_Binormal0.xyz ),
                          d3d9_dot( gVSLightDir.xyz, in_Normal0.xyz ), 0.0 );

    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gVSWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 2 ) ) );
    out_Texcoord4 =
        vec4( tdu_vertex_fog( length( viewPosition ) ), 0.0, 0.0, 0.0 );

    // OTDU_ADDITION: night lamps.
    bool bLamps = tdu_lamps_enabled();
    out_ViewPosition = bLamps ? vec4( viewPosition, 1.0 ) : vec4( 0.0 );
    out_ViewNormal =
        bLamps ? vec4( d3d9_dot( in_Normal0.xyz, gVSWorldView( 0 ).xyz ),
                       d3d9_dot( in_Normal0.xyz, gVSWorldView( 1 ).xyz ),
                       d3d9_dot( in_Normal0.xyz, gVSWorldView( 2 ).xyz ), 0.0 )
               : vec4( 0.0 );

#ifdef WORLD_TANGENT_REFLECTION_FIX
    out_Texcoord5 = vec4( tangent, 0.0 );
    out_Texcoord6 = vec4( tangentCrossNormal, 0.0 );
    out_Texcoord7 = vec4( in_Normal0.xyz, 0.0 );
#endif

    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
