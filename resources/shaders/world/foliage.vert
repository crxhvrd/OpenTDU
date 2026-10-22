#version 450
#extension GL_GOOGLE_include_directive : require

// Foliage (vertex lit vegetation meshes).
//
// Permutations:
//   FOLIAGE_FADE            outputs the LOD alpha fade
//   FOLIAGE_SHADOW          shadow texture coordinates (UV set 1)
//   FOLIAGE_WIND            bends the mesh along the wind (material_rigidity)
//   FOLIAGE_INSTANCE_COLOR  blends the lighting towards the instance color
//   FOLIAGE_UV_MATRIX0      UV set 0 goes through gUVMatrix0

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define v_windparam c.f[0]
#define gWorldViewProject( i ) c.f[8 + ( i )]
#define gWorld( i ) c.f[16 + ( i )]
#define gWorldView( i ) c.f[20 + ( i )]
#define gInvWorld( i ) c.f[40 + ( i )]
#define gUVMatrix0( i ) c.f[84 + ( i )]
#define gVSLightDir c.f[93]
#define gVSLightAmbient c.f[94]
#define gVSLightDiffuse c.f[95]
#define gVSLODDist c.f[118]
#define material_rigidity c.f[120]
#define gVSInstanceColor c.f[238]
#define gVSLightAmbient1 c.f[247]

const float kPi = 3.1415927;

layout( location = 0 ) in vec4 in_Position0;
layout( location = 1 ) in vec4 in_Color0;
layout( location = 2 ) in vec4 in_Normal0;
layout( location = 3 ) in vec4 in_Texcoord0;
#ifdef FOLIAGE_SHADOW
layout( location = 4 ) in vec4 in_Texcoord1;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
#ifdef FOLIAGE_SHADOW
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
#endif
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) out vec4 out_ViewNormal;     // OTDU

float wrapAngle( float angle ) {
    return fract( angle * 0.15915494 + 0.5 ) * ( 2.0 * kPi ) + -kPi;
}

void main() {
#ifdef FOLIAGE_WIND
    float instancePhase =
        wrapAngle( gWorld( 0 ).w + gWorld( 2 ).w + v_windparam.z );
    float bendAmount = ( 1.0 + -gVSLODDist.y ) * material_rigidity.w;
    float bend = pow( abs( in_Position0.y ), material_rigidity.y ) *
                 material_rigidity.x * ( 1.0 / material_rigidity.z );
    float sway = sin( instancePhase ) * bendAmount;
    float displacement = bend * bendAmount + sway * bend;

    vec4 windDirection = vec4( 1.0, 0.0, 1.0, 0.0 ) * v_windparam.xxyx;
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
    vec3 viewPosition = vec3( d3d9_dot( position, gWorldView( 0 ) ),
                              d3d9_dot( position, gWorldView( 1 ) ),
                              d3d9_dot( position, gWorldView( 2 ) ) );

    float diffuse = max( d3d9_dot( in_Normal0.xyz, -gVSLightDir.xyz ), 0.0 );
    vec3 light = gVSLightDiffuse.rgb * diffuse + gVSLightAmbient.rgb;
#ifdef FOLIAGE_INSTANCE_COLOR
    light = d3d9_lerp( light,
                       gVSLightAmbient1.rgb * gVSInstanceColor.rgb +
                           gVSLightAmbient.rgb,
                       gVSLODDist.yyy );
#endif
    out_Texcoord0 = vec4( light * in_Color0.rgb, in_Color0.a );

#ifdef FOLIAGE_UV_MATRIX0
    out_Texcoord1 =
        vec4( TDU_APPLY_UV_MATRIX( in_Texcoord0.xy, gUVMatrix0 ), 0.0, 0.0 );
#else
    out_Texcoord1 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#endif
#ifdef FOLIAGE_SHADOW
#ifdef FOLIAGE_WIND
    out_Texcoord2 = vec4( sway * 0.05 + in_Texcoord1.xy, 0.0, 0.0 );
#else
    out_Texcoord2 = vec4( in_Texcoord1.xy, 0.0, 0.0 );
#endif
#endif

    out_Texcoord3 =
        vec4( tdu_vertex_fog( length( viewPosition ) ), 0.0, 0.0, 0.0 );

    // OTDU_ADDITION: night lamps.
    bool bLamps = tdu_lamps_enabled();
    out_ViewPosition =
        bLamps ? vec4( viewPosition,
                       dot( in_Color0.rgb, vec3( 0.299, 0.587, 0.114 ) ) )
               : vec4( 0.0 );
    out_ViewNormal =
        bLamps ? vec4( d3d9_dot( in_Normal0.xyz, gWorldView( 0 ).xyz ),
                       d3d9_dot( in_Normal0.xyz, gWorldView( 1 ).xyz ),
                       d3d9_dot( in_Normal0.xyz, gWorldView( 2 ).xyz ), 0.0 )
               : vec4( 0.0 );
#ifdef FOLIAGE_FADE
    float fade = gVSLODDist.y + -0.5;
    out_Texcoord3.y = -max( fade + fade, 0.0 ) + 1.0;
#endif

    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
