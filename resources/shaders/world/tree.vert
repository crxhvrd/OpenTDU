#version 450
#extension GL_GOOGLE_include_directive : require

// Normal mapped trees, lit in tangent space.
//
// Permutations:
//   TREE_FADE        outputs the LOD alpha fade
//   TREE_SHADOW      shadow texture coordinates (UV set 1)
//   TREE_WIND        bends the mesh along the wind (gVSMaterialRigidity)
//   TREE_UV_MATRIX0  UV set 0 goes through gUVMatrix0

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gVSWindParam c.f[0]
#define gWorldViewProject( i ) c.f[8 + ( i )]
#define gWorld( i ) c.f[16 + ( i )]
#define gWorldView( i ) c.f[20 + ( i )]
#define gInvWorld( i ) c.f[40 + ( i )]
#define gUVMatrix0( i ) c.f[84 + ( i )]
#define gVSLightDir c.f[93]
#define gVSLODDist c.f[118]
#define gVSCamPosInObj c.f[119]
#define gVSMaterialRigidity c.f[120]

const float kPi = 3.1415927;

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
layout( location = D3D9_IN_BINORMAL0 ) in vec4 in_Binormal0;
layout( location = D3D9_IN_TANGENT0 ) in vec4 in_Tangent0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#ifdef TREE_SHADOW
layout( location = D3D9_IN_TEXCOORD( 1 ) ) in vec4 in_Texcoord1;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
#ifdef TREE_SHADOW
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
#endif
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) out vec4 out_ViewNormal;     // OTDU

float wrapAngle( float angle ) {
    return fract( angle * 0.15915494 + 0.5 ) * ( 2.0 * kPi ) + -kPi;
}

void main() {
#ifdef TREE_WIND
    float instancePhase =
        wrapAngle( gWorld( 0 ).w + gWorld( 2 ).w + gVSWindParam.z );
    float bendAmount = ( 1.0 + -gVSLODDist.y ) * gVSMaterialRigidity.w;
    float bend = pow( abs( in_Position0.y ), gVSMaterialRigidity.y ) *
                 gVSMaterialRigidity.x * ( 1.0 / gVSMaterialRigidity.z );
    float sway = sin( instancePhase ) * bendAmount;
    float displacement = bend * bendAmount + sway * bend;

    vec4 windDirection = vec4( 1.0, 0.0, 1.0, 0.0 ) * gVSWindParam.xxyx;
    vec4 objectWind = vec4( d3d9_dot( windDirection, gInvWorld( 0 ) ),
                            d3d9_dot( windDirection, gInvWorld( 1 ) ),
                            d3d9_dot( windDirection, gInvWorld( 2 ) ),
                            d3d9_dot( windDirection, gInvWorld( 3 ) ) );
    vec4 position = objectWind * displacement + in_Position0;
#else
    vec4 position = in_Position0;
#endif

    gl_Position = vec4( d3d9_dot( position, gWorldViewProject( 0 ) ),
                        d3d9_dot( position, gWorldViewProject( 1 ) ),
                        d3d9_dot( position, gWorldViewProject( 2 ) ),
                        d3d9_dot( position, gWorldViewProject( 3 ) ) );

    // The view vector uses T x N where the light uses the vertex binormal.
    vec3 tangent = in_Tangent0.xyz;
    vec3 tangentCrossNormal = d3d9_cross( tangent, in_Normal0.xyz );
    vec3 toCamera = gVSCamPosInObj.xyz - position.xyz;
    out_Texcoord1 = vec4( d3d9_dot( toCamera, tangent ),
                          d3d9_dot( toCamera, tangentCrossNormal ),
                          d3d9_dot( toCamera, in_Normal0.xyz ), 0.0 );
    out_Texcoord2 = vec4( d3d9_dot( gVSLightDir.xyz, tangent ),
                          d3d9_dot( gVSLightDir.xyz, in_Binormal0.xyz ),
                          d3d9_dot( gVSLightDir.xyz, in_Normal0.xyz ), 0.0 );

#ifdef TREE_UV_MATRIX0
    out_Texcoord0 =
        vec4( TDU_APPLY_UV_MATRIX( in_Texcoord0.xy, gUVMatrix0 ), 0.0, 0.0 );
#else
    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#endif
#ifdef TREE_SHADOW
#ifdef TREE_WIND
    out_Texcoord3 = vec4( sway * 0.05 + in_Texcoord1.xy, 0.0, 0.0 );
#else
    out_Texcoord3 = vec4( in_Texcoord1.xy, 0.0, 0.0 );
#endif
#endif

    vec3 viewPosition = vec3( d3d9_dot( position, gWorldView( 0 ) ),
                              d3d9_dot( position, gWorldView( 1 ) ),
                              d3d9_dot( position, gWorldView( 2 ) ) );
    out_Texcoord4 = vec4( tdu_vertex_fog( length( viewPosition ) ),
                          gVSLODDist.y, 0.0, 0.0 );

    // OTDU_ADDITION: night lamps.
    bool bLamps = tdu_lamps_enabled();
    out_ViewPosition = bLamps ? vec4( viewPosition, 1.0 ) : vec4( 0.0 );
    out_ViewNormal =
        bLamps ? vec4( d3d9_dot( in_Normal0.xyz, gWorldView( 0 ).xyz ),
                       d3d9_dot( in_Normal0.xyz, gWorldView( 1 ).xyz ),
                       d3d9_dot( in_Normal0.xyz, gWorldView( 2 ).xyz ), 0.0 )
               : vec4( 0.0 );
#ifdef TREE_FADE
    float fade = -0.5 + gVSLODDist.y;
    out_Texcoord4.z = -max( fade + fade, 0.0 ) + 1.0;
#endif

    out_Color0 = clamp( in_Color0, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
