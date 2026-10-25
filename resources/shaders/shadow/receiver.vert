#version 450
#extension GL_GOOGLE_include_directive : require

// Shadow receivers: redraws world geometry and projects the shadow map on it.
// gl_Position must stay bit identical to the pass that drew the geometry.
//
// Permutations:
//   RECEIVER_NORMAL       only faces lit by the sun receive the shadow
//   RECEIVER_SKINNED      4 bone skinning (gVSWorldMatrixArray)
//   RECEIVER_DEPTH_SCALE  depth scaled by gVSShadowColor.z, constant shadow
//                         value gVSShadowColor.y (cockpit)
//
// gUVMat = world to shadow map UV, gLightMat row 2 = world to light depth,
// gVSShadowColor.x = shadow map size.

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gWorldViewProject( i ) c.f[8 + ( i )]
#define gWorld( i ) c.f[16 + ( i )]
#define gWorldView( i ) c.f[20 + ( i )]
#define gInvWorld( i ) c.f[40 + ( i )]
#define gVSLightDir c.f[109]
#define gVSShadowColor c.f[117]
#define gUVMat( i ) c.f[226 + ( i )]
#define gLightMat( i ) c.f[230 + ( i )]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
#ifdef RECEIVER_NORMAL
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
#endif
#ifdef RECEIVER_SKINNED
layout( location = D3D9_IN_BLENDINDICES0 ) in vec4 in_BlendIndices0;
layout( location = D3D9_IN_BLENDWEIGHT0 ) in vec4 in_BlendWeight0;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;

void main() {
#ifdef RECEIVER_SKINNED
    ivec4 bones = tdu_skin_bones( in_BlendIndices0 );
    vec4 weights = tdu_skin_weights( in_BlendWeight0 );
    vec4 position = tdu_skin_position( in_Position0, bones, weights );
#else
    vec4 position = in_Position0;
#endif

    gl_Position = vec4( d3d9_dot( position, gWorldViewProject( 0 ) ),
                        d3d9_dot( position, gWorldViewProject( 1 ) ),
                        d3d9_dot( position, gWorldViewProject( 2 ) ),
                        d3d9_dot( position, gWorldViewProject( 3 ) ) );
#ifdef RECEIVER_DEPTH_SCALE
    gl_Position.z *= gVSShadowColor.z;
#endif

    vec4 worldPosition = vec4(
        d3d9_dot( position, gWorld( 0 ) ), d3d9_dot( position, gWorld( 1 ) ),
        d3d9_dot( position, gWorld( 2 ) ), d3d9_dot( position, gWorld( 3 ) ) );
    out_Texcoord0 = vec4( d3d9_dot( worldPosition, gUVMat( 0 ) ),
                          d3d9_dot( worldPosition, gUVMat( 1 ) ), 0.0, 0.0 );

#ifdef RECEIVER_NORMAL
#ifdef RECEIVER_SKINNED
    vec3 normal = tdu_skin_normal( in_Normal0.xyz, bones, weights );
#else
    vec3 normal = in_Normal0.xyz;
#endif
    vec3 lightDirection =
        vec3( d3d9_dot( gVSLightDir.xyz, gInvWorld( 0 ).xyz ),
              d3d9_dot( gVSLightDir.xyz, gInvWorld( 1 ).xyz ),
              d3d9_dot( gVSLightDir.xyz, gInvWorld( 2 ).xyz ) );
    float facing = d3d9_dot( normal, lightDirection );
    float facingSign = float( -facing < facing ) + -float( facing < -facing );
    float shadowFloor = float( facingSign >= 0.0 );
#elif defined( RECEIVER_DEPTH_SCALE )
    float shadowFloor = 0.0 * gVSShadowColor.y;
#else
    float shadowFloor = 0.0 * gVSShadowColor.x;
#endif
#ifdef RECEIVER_DEPTH_SCALE
    float shadowValue = gVSShadowColor.y;
#else
    float shadowValue = 0.0 * gVSShadowColor.x;
#endif
    out_Texcoord1 =
        vec4( shadowValue, shadowFloor,
              d3d9_dot( worldPosition, gLightMat( 2 ) ), gVSShadowColor.x );

    // The fog distance uses the unskinned position, and is sqrt(3) times the
    // view distance (a scalar widened to float3).
    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gWorldView( 2 ) ) );
    float viewDistance =
        1.0 / inversesqrt( abs( d3d9_dot( viewPosition, viewPosition ) ) );
    float fogDistance =
        1.0 / inversesqrt( abs(
                  d3d9_dot( vec3( viewDistance ), vec3( viewDistance ) ) ) );
    out_Texcoord2 = vec4( -tdu_vertex_fog( fogDistance ) + 1.0,
                          1.0 / gVSShadowColor.x, 0.0, 0.0 );

    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
