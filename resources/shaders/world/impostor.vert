#version 450
#extension GL_GOOGLE_include_directive : require

// Vegetation impostors: camera facing quads swaying in the wind, lit with a
// spherical normal around the impostor center.
//
// Permutations:
//   IMPOSTOR_FADE            outputs the LOD alpha fade
//   IMPOSTOR_RIGIDITY        bends the quad along the wind (material_rigidity)
//   IMPOSTOR_INSTANCE_COLOR  blends the lighting towards the instance color
//
// gVSParam = (center height, sway frequency, sway amplitude, -)
// v_windparam = (direction.xy, time, strength)
// material_rigidity = (bend scale, bend exponent, bend height, bend amount)

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define v_windparam c.f[0]
#define gWorld( i ) c.f[16 + ( i )]
#define gWorldView( i ) c.f[20 + ( i )]
#define gViewProject( i ) c.f[28 + ( i )]
#define gInvView( i ) c.f[48 + ( i )]
#define gVSLightDir c.f[93]
#define gVSLightAmbient c.f[94]
#define gVSLightDiffuse c.f[95]
#define gVSParam c.f[114]
#define gVSLODDist c.f[118]
#define material_rigidity c.f[120]
#define gVSInstanceColor c.f[238]
#define gVSLightAmbient1 c.f[247]

const float kPi = 3.1415927;

layout( location = 0 ) in vec4 in_Position0;
layout( location = 1 ) in vec4 in_Color0;
layout( location = 2 ) in vec4 in_Normal0; // xy: corner offset, z: depth offset
layout( location = 3 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;

float wrapAngle( float angle ) {
    return fract( angle * 0.15915494 + 0.5 ) * ( 2.0 * kPi ) + -kPi;
}

void main() {
#ifdef IMPOSTOR_RIGIDITY
    float instancePhase =
        wrapAngle( gWorld( 0 ).w + gWorld( 2 ).w + v_windparam.z );
    float bendAmount = ( 1.0 + -gVSLODDist.y ) * material_rigidity.w;
    float bend = pow( abs( in_Position0.y ), material_rigidity.y ) *
                 material_rigidity.x * ( 1.0 / material_rigidity.z );
    bend = bend * bendAmount + ( sin( instancePhase ) * bendAmount ) * bend;
#endif

    float swayPhase = wrapAngle( v_windparam.z * gVSParam.y +
                                 ( -in_Position0.z + in_Position0.x ) );
    float swayAngle =
        wrapAngle( sin( swayPhase ) * gVSParam.z * v_windparam.w );
    vec2 sway = vec2( cos( swayAngle ), sin( swayAngle ) );
    vec3 corner =
        vec3( in_Normal0.x * sway.x + -( sway.y * in_Normal0.y ),
              sway.y * in_Normal0.x + sway.x * in_Normal0.y, in_Normal0.z );

    vec3 viewCenter = vec3( d3d9_dot( in_Position0, gWorldView( 0 ) ),
                            d3d9_dot( in_Position0, gWorldView( 1 ) ),
                            d3d9_dot( in_Position0, gWorldView( 2 ) ) );
    vec4 viewPosition = vec4( corner * gWorld( 1 ).y + viewCenter, 1.0 );
    vec4 worldPosition = vec4( d3d9_dot( viewPosition, gInvView( 0 ) ),
                               d3d9_dot( viewPosition, gInvView( 1 ) ),
                               d3d9_dot( viewPosition, gInvView( 2 ) ),
                               d3d9_dot( viewPosition, gInvView( 3 ) ) );
#ifdef IMPOSTOR_RIGIDITY
    worldPosition.xz = v_windparam.xy * bend + worldPosition.xz;
#endif
    gl_Position = vec4( d3d9_dot( worldPosition, gViewProject( 0 ) ),
                        d3d9_dot( worldPosition, gViewProject( 1 ) ),
                        d3d9_dot( worldPosition, gViewProject( 2 ) ),
                        d3d9_dot( worldPosition, gViewProject( 3 ) ) );

    vec4 fromCenter =
        vec4( in_Position0.x, in_Position0.y + -gVSParam.x, in_Position0.zw );
    vec3 normal = fromCenter.xyz *
                  inversesqrt( abs( d3d9_dot( fromCenter, fromCenter ) ) );
    float diffuse = max( d3d9_dot( normal, -gVSLightDir.xyz ), 0.0 );
    vec3 light = gVSLightDiffuse.rgb * diffuse + gVSLightAmbient.rgb;
#ifdef IMPOSTOR_INSTANCE_COLOR
    light = d3d9_lerp( light,
                       gVSLightAmbient1.rgb * gVSInstanceColor.rgb +
                           gVSLightAmbient.rgb,
                       gVSLODDist.yyy );
#endif

    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
    out_Texcoord1 = vec4( light * in_Color0.rgb, in_Color0.a );
    out_Texcoord3 =
        vec4( tdu_vertex_fog( length( viewCenter ) ), 0.0, 0.0, 0.0 );
#ifdef IMPOSTOR_FADE
    float fade = -0.5 + gVSLODDist.y;
    out_Texcoord3.y = -max( fade + fade, 0.0 ) + 1.0;
#endif

    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
