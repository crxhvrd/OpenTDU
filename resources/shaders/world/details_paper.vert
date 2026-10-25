#version 450
#extension GL_GOOGLE_include_directive : require

// Ground litter (details table): the sector shadow map coordinates
// (Texcoord1.xy, sectors are 768 m wide) and a fade out with the distance
// (Texcoord1.z), vertex fog (Texcoord2.x).

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gVSColorContrast c.f[1]
#define gVSWorldViewProject( i ) c.f[8 + ( i )]
#define gVSWorld( i ) c.f[16 + ( i )]
#define gVSWorldView( i ) c.f[20 + ( i )]
#define gVSSectorOffset c.f[121]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;

void main() {
    const float kSectorSize = 768.0;
    const float kInvSectorSize = 0.0013020834;

    gl_Position = vec4( d3d9_dot( in_Position0, gVSWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 3 ) ) );

    vec2 sectorPosition =
        vec2( d3d9_dot( in_Position0, gVSWorld( 0 ) ) + -gVSSectorOffset.x,
              d3d9_dot( in_Position0, gVSWorld( 2 ) ) + -gVSSectorOffset.z );

    vec4 viewPosition = vec4( d3d9_dot( in_Position0, gVSWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 2 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 3 ) ) );
    // The fade distance includes w.
    float fadeDistance =
        1.0 / inversesqrt( abs( d3d9_dot( viewPosition, viewPosition ) ) );
    float viewDistance =
        1.0 /
        inversesqrt( abs( d3d9_dot( viewPosition.xyz, viewPosition.xyz ) ) );
    float fade = max( -fadeDistance + kSectorSize, 0.0 ) * gVSColorContrast.w;

    out_Texcoord1 =
        vec4( sectorPosition * kInvSectorSize, fade * kInvSectorSize, 0.0 );
    out_Texcoord2 = vec4( tdu_vertex_fog( viewDistance ), 0.0, 0.0, 0.0 );
    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
    out_Color0 = clamp( in_Color0, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
