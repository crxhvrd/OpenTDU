#version 450
#extension GL_GOOGLE_include_directive : require

// Sky dome cloud sprites, rotated to face the camera.
//
// Permutations:
//   CLOUDS_VERTEX_COLOR  modulates the emissive color by the vertex color
//   CLOUDS_UV_MATRIX     texture coordinates go through gUVMatrix0

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gVSM4Proj( i ) c.f[12 + ( i )]
#define gVSM4WorldView( i ) c.f[20 + ( i )]
#define gUVMatrix0( i ) c.f[84 + ( i )]
#define gCamPosObjSpace c.f[119]
#define gVSMaterialEmissive c.f[241]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0; // corner offset in the sprite basis
#ifdef CLOUDS_VERTEX_COLOR
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#else
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;

void main() {
    // Unnormalized right vector (its length shrinks the sprite as the camera
    // looks up at it).
    vec3 toCamera = d3d9_normalize( gCamPosObjSpace.xyz - in_Position0.xyz );
    vec3 right = d3d9_cross( vec3( 0.0, 1.0, 0.0 ), toCamera );
    vec3 up = d3d9_cross( toCamera, right );

    vec4 position = vec4( in_Position0.xyz + in_Normal0.x * right +
                              in_Normal0.y * up + in_Normal0.z * toCamera,
                          1.0 );
    vec4 viewPosition = vec4( d3d9_dot( position, gVSM4WorldView( 0 ) ),
                              d3d9_dot( position, gVSM4WorldView( 1 ) ),
                              d3d9_dot( position, gVSM4WorldView( 2 ) ), 1.0 );
    gl_Position = vec4( d3d9_dot( viewPosition, gVSM4Proj( 0 ) ),
                        d3d9_dot( viewPosition, gVSM4Proj( 1 ) ),
                        d3d9_dot( viewPosition, gVSM4Proj( 2 ) ),
                        d3d9_dot( viewPosition, gVSM4Proj( 3 ) ) );

#ifdef CLOUDS_UV_MATRIX
    out_Texcoord0 =
        vec4( TDU_APPLY_UV_MATRIX( in_Texcoord0.xy, gUVMatrix0 ), 0.0, 0.0 );
#else
    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#endif

    vec4 color = gVSMaterialEmissive;
#ifdef CLOUDS_VERTEX_COLOR
    color *= in_Color0;
#endif
    out_Color0 = clamp( color, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
