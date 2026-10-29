#version 450
#extension GL_GOOGLE_include_directive : require

// Instanced world props (PMI): the instance matrix rows come with the vertex.
//
// Permutations:
//   PMI_TEXTURED      UV set 0 (the matrix rows follow it)
//   PMI_VERTEX_COLOR  per vertex color, on top of the instance color

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gProj( i ) D3D9_CR( 12, i )
#define gView( i ) D3D9_CR( 24, i )
#define gVSLightDir D3D9_C( 93 )
#define gVSLightAmbient D3D9_C( 94 )
#define gVSLightDiffuse D3D9_C( 95 )
#define gVSLODDist D3D9_C( 118 )

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
#ifdef PMI_VERTEX_COLOR
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_VertexColor;
#define INSTANCE_COLOR 1
#else
#define INSTANCE_COLOR 0
#endif
#ifdef PMI_TEXTURED
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#define INSTANCE_UV 1
#else
#define INSTANCE_UV 0
#endif
layout( location = D3D9_IN_TEXCOORD( INSTANCE_UV + 0 ) ) in vec4 in_InstanceRow0;
layout( location = D3D9_IN_TEXCOORD( INSTANCE_UV + 1 ) ) in vec4 in_InstanceRow1;
layout( location = D3D9_IN_TEXCOORD( INSTANCE_UV + 2 ) ) in vec4 in_InstanceRow2;
layout( location = D3D9_IN_COLOR( INSTANCE_COLOR ) ) in vec4 in_InstanceColor;

layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
#ifdef PMI_TEXTURED
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
#endif
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) out vec4 out_ViewNormal;     // OTDU

void main() {
    vec4 worldPosition = vec4( d3d9_dot( in_Position0, in_InstanceRow0 ),
                               d3d9_dot( in_Position0, in_InstanceRow1 ),
                               d3d9_dot( in_Position0, in_InstanceRow2 ), 1.0 );
    vec4 viewPosition = vec4( d3d9_dot( worldPosition, gView( 0 ) ),
                              d3d9_dot( worldPosition, gView( 1 ) ),
                              d3d9_dot( worldPosition, gView( 2 ) ),
                              d3d9_dot( worldPosition, gView( 3 ) ) );
    gl_Position = vec4( d3d9_dot( viewPosition, gProj( 0 ) ),
                        d3d9_dot( viewPosition, gProj( 1 ) ),
                        d3d9_dot( viewPosition, gProj( 2 ) ),
                        d3d9_dot( viewPosition, gProj( 3 ) ) );

    vec3 normal = d3d9_normalize(
        vec3( d3d9_dot( in_Normal0.xyz, in_InstanceRow0.xyz ),
              d3d9_dot( in_Normal0.xyz, in_InstanceRow1.xyz ),
              d3d9_dot( in_Normal0.xyz, in_InstanceRow2.xyz ) ) );
    float diffuse = max( d3d9_dot( normal, -gVSLightDir.xyz ), 0.0 );
    vec4 light = vec4( gVSLightDiffuse.rgb * diffuse + gVSLightAmbient.rgb,
                       gVSLightAmbient.a );
#ifdef PMI_VERTEX_COLOR
    light.rgb *= in_InstanceColor.rgb;
    light.a = in_InstanceColor.a * gVSLightAmbient.a;
    light *= in_VertexColor;
#else
    light *= in_InstanceColor;
#endif

#ifdef PMI_TEXTURED
    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#endif
    out_Texcoord1 =
        vec4( tdu_vertex_fog( length( viewPosition.xyz ) ), 0.0, 0.0, 0.0 );
    out_Texcoord2 = vec4( gVSLODDist.w * gVSLODDist.y, 0.0, 0.0, 0.0 );

    // OTDU_ADDITION: night lamps; the instance (and vertex) color tints them.
#ifdef PMI_VERTEX_COLOR
    vec3 tint = in_InstanceColor.rgb * in_VertexColor.rgb;
#else
    vec3 tint = in_InstanceColor.rgb;
#endif
    bool bLamps = tdu_lamps_enabled();
    out_ViewPosition = bLamps ? vec4( viewPosition.xyz,
                                      dot( tint, vec3( 0.299, 0.587, 0.114 ) ) )
                              : vec4( 0.0 );
    out_ViewNormal = bLamps ? vec4( d3d9_dot( normal, gView( 0 ).xyz ),
                                    d3d9_dot( normal, gView( 1 ).xyz ),
                                    d3d9_dot( normal, gView( 2 ).xyz ), 0.0 )
                            : vec4( 0.0 );

    out_Color0 = clamp( light, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
