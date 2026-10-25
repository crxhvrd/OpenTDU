#version 450
#extension GL_GOOGLE_include_directive : require

// Low detail skinned characters (avatar table NPCs): vertex lit (Color0),
// UV set 0 (Texcoord1), vertex fog (Texcoord2.x).

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"
#include "tdu_avatar.glsl"

#define gWorldViewProj( i ) c.f[8 + ( i )]
#define gVSWorldView( i ) c.f[20 + ( i )]
#define gVSLightDir c.f[93]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
layout( location = D3D9_IN_BLENDINDICES0 ) in vec4 in_BlendIndices0;
layout( location = D3D9_IN_BLENDWEIGHT0 ) in vec4 in_BlendWeight0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;

layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) out vec4 out_ViewNormal;     // OTDU

void main() {
    ivec4 bones = tdu_skin_bones( in_BlendIndices0 );
    vec4 weights = tdu_skin_weights( in_BlendWeight0 );
    vec4 position = tdu_skin_position( in_Position0, bones, weights );
    vec3 normal = tdu_skin_normal( in_Normal0.xyz, bones, weights );
    gl_Position = vec4( d3d9_dot( position, gWorldViewProj( 0 ) ),
                        d3d9_dot( position, gWorldViewProj( 1 ) ),
                        d3d9_dot( position, gWorldViewProj( 2 ) ),
                        d3d9_dot( position, gWorldViewProj( 3 ) ) );

    vec3 viewPosition = vec3( d3d9_dot( position, gVSWorldView( 0 ) ),
                              d3d9_dot( position, gVSWorldView( 1 ) ),
                              d3d9_dot( position, gVSWorldView( 2 ) ) );
    float viewDistance =
        1.0 / inversesqrt( abs( d3d9_dot( viewPosition, viewPosition ) ) );
    out_Texcoord2 = vec4( tdu_vertex_fog( viewDistance ), 0.0, 0.0, 0.0 );

    float diffuse = max( d3d9_dot( normal, -gVSLightDir.xyz ), 0.0 );
    out_Color0 = vec4( gVSLightDiffuse.xyz * diffuse + gVSLightAmbient.xyz,
                       gVSLightDiffuse.w );
    out_Texcoord1 = vec4( in_Texcoord0.xy, 0.0, 0.0 );

    // OTDU_ADDITION: night lamps.
    tdu_avatar_lamp_varyings( position, normal, 1.0, out_ViewPosition,
                              out_ViewNormal );

    out_Color0 = clamp( out_Color0, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
