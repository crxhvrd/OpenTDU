#version 450
#extension GL_GOOGLE_include_directive : require

// Camera facing billboards (retail billboards table): Normal0 holds the
// corner offset in view space.
//
// Permutations:
//   BILLBOARD_SCALED         world space sprite grown with the distance
//                            (gVSParam: start, range, growth, camera pull),
//                            vertex fog and a depth bias
//   BILLBOARD_ANGLE_FADE     size and camera pull scaled by the view angle
//                            (object space camera direction z below -0.5)
//   BILLBOARD_TEXCOORD       forwards UV set 0
//   BILLBOARD_VERTEX_COLOR   forwards Color0 (else white)

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gVSM4World( i ) c.f[16 + ( i )]
#define gVSM4WorldView( i ) c.f[20 + ( i )]
#define gVSM4ViewProj( i ) c.f[28 + ( i )]
#define gVSM4InvView( i ) c.f[48 + ( i )]
#define gVSLightDiffuse c.f[95]
#define gFogParametersVS c.f[107]
#define gVSParam c.f[114]
#define gVSCamPosInObj c.f[119]
#define gFogColorVS c.f[123]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
#ifdef BILLBOARD_TEXCOORD
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#endif
#ifdef BILLBOARD_VERTEX_COLOR
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
#ifdef BILLBOARD_TEXCOORD
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
#endif
#ifdef BILLBOARD_SCALED
layout( location = D3D9_SLOT_FOG ) out float oFog;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
#endif

vec3 viewToObject( vec3 v ) {
    return vec3( d3d9_dot( v, gVSM4InvView( 0 ).xyz ),
                 d3d9_dot( v, gVSM4InvView( 1 ).xyz ),
                 d3d9_dot( v, gVSM4InvView( 2 ).xyz ) );
}

void main() {
#ifdef BILLBOARD_SCALED
    vec4 toCamera = -in_Position0 + gVSCamPosInObj;
    float invDistance = inversesqrt( abs( d3d9_dot( toCamera, toCamera ) ) );
    float distance = 1.0 / invDistance;
#ifdef BILLBOARD_ANGLE_FADE
    float angle = max( toCamera.z * invDistance * -2.0 + -1.0, 0.0 );
    float pull = angle * gVSParam.w;
#else
    float pull = gVSParam.w;
#endif
    vec3 worldZ =
        vec3( gVSM4World( 0 ).z, gVSM4World( 1 ).z, gVSM4World( 2 ).z );
    vec3 center = vec3( d3d9_dot( in_Position0, gVSM4World( 0 ) ),
                        d3d9_dot( in_Position0, gVSM4World( 1 ) ),
                        d3d9_dot( in_Position0, gVSM4World( 2 ) ) );
    center = worldZ * distance * -pull + center;
    float growth = min( max( distance + -gVSParam.x, 0.0 ), gVSParam.y ) *
                   ( 1.0 / gVSParam.y );
    growth = growth * gVSParam.z + 1.0;
    vec3 corner = viewToObject( in_Normal0.xyz ) * gVSM4World( 1 ).yyy;
#ifdef BILLBOARD_ANGLE_FADE
    vec4 position = vec4( vec3( growth ) * corner * -angle + center,
#else
    vec4 position = vec4( corner * -growth + center,
#endif
                          d3d9_dot( in_Position0, gVSM4World( 3 ) ) );

    vec3 viewPosition = vec3( d3d9_dot( position, gVSM4WorldView( 0 ) ),
                              d3d9_dot( position, gVSM4WorldView( 1 ) ),
                              d3d9_dot( position, gVSM4WorldView( 2 ) ) );
    float clipZ = d3d9_dot( position, gVSM4ViewProj( 2 ) );
    gl_Position = vec4( d3d9_dot( position, gVSM4ViewProj( 0 ) ),
                        d3d9_dot( position, gVSM4ViewProj( 1 ) ),
                        ( 1.0 / max( clipZ, 1.0 ) ) * -0.03125 + clipZ,
                        d3d9_dot( position, gVSM4ViewProj( 3 ) ) );
    float viewDistance =
        1.0 / inversesqrt( abs( d3d9_dot( viewPosition, viewPosition ) ) );
    oFog = d3d9_saturate( tdu_vertex_fog( viewDistance ) );
    out_Texcoord1 = vec4( 0.0 );
    out_Texcoord2 = vec4( gVSLightDiffuse.w, 0.0, 0.0, 0.0 );
#else
    vec4 position = vec4( viewToObject( in_Normal0.xyz ) + in_Position0.xyz,
                          in_Position0.w );
    gl_Position = vec4( d3d9_dot( position, gVSM4ViewProj( 0 ) ),
                        d3d9_dot( position, gVSM4ViewProj( 1 ) ),
                        d3d9_dot( position, gVSM4ViewProj( 2 ) ),
                        d3d9_dot( position, gVSM4ViewProj( 3 ) ) );
#endif

#ifdef BILLBOARD_TEXCOORD
    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#endif
#ifdef BILLBOARD_VERTEX_COLOR
    out_Color0 = clamp( in_Color0, vec4( 0.0 ), vec4( 1.0 ) );
#else
    out_Color0 = vec4( 1.0 );
#endif
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
