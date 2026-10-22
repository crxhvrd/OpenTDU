#version 450
#extension GL_GOOGLE_include_directive : require

// Vertex lit world geometry shadowed by the sector shadow map (Shading).
//
// Permutations:
//   SHADING_SKINNED           4 bone skinning (gVSWorldMatrixArray)
//   SHADING_VERTEX_COLOR      vertex color
//   SHADING_INSTANCE_COLOR    instance brightness (gVSInstanceColor.x)
//   SHADING_DETAIL            detail / gloss map coordinates (UV set 1)
//   SHADING_PIXEL_SPECULAR    lighting vectors for the per pixel specular
//   SHADING_VERTEX_SPECULAR   per vertex specular

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gVSColorContrast c.f[1]
#define gVSWorldViewProject( i ) c.f[8 + ( i )]
#define gVSWorld( i ) c.f[16 + ( i )]
#define gVSWorldView( i ) c.f[20 + ( i )]
#define gVSLightDir c.f[93]
#define gVSLightSpecularParam c.f[98]
#define gVSLODDist c.f[118]
#define gVSCamPosInObj c.f[119]
#define gVSSectorOffset c.f[121]
#define gVSInstanceColor c.f[238]

const float kSectorSize = 768.0;

layout( location = 0 ) in vec4 in_Position0;
layout( location = 1 ) in vec4 in_Normal0;
#ifdef SHADING_SKINNED
layout( location = 2 ) in vec4 in_BlendIndices0;
layout( location = 3 ) in vec4 in_BlendWeight0;
#define NEXT_LOCATION 4
#else
#define NEXT_LOCATION 2
#endif
#ifdef SHADING_VERTEX_COLOR
layout( location = NEXT_LOCATION ) in vec4 in_Color0;
#define UV_LOCATION ( NEXT_LOCATION + 1 )
#else
#define UV_LOCATION NEXT_LOCATION
#endif
layout( location = UV_LOCATION ) in vec4 in_Texcoord0;
#ifdef SHADING_DETAIL
layout( location = UV_LOCATION + 1 ) in vec4 in_Texcoord1;
#endif

layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
#ifdef SHADING_DETAIL
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
#endif
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
#if defined( SHADING_PIXEL_SPECULAR ) || defined( SHADING_VERTEX_SPECULAR )
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
#endif
#ifdef SHADING_PIXEL_SPECULAR
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
layout( location = D3D9_SLOT_TEXCOORD5 ) out vec4 out_Texcoord5;
#endif
layout( location = D3D9_SLOT_TEXCOORD7 ) out vec4 out_Texcoord7;
layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) out vec4 out_ViewNormal;     // OTDU

void main() {
#ifdef SHADING_SKINNED
    ivec4 bones = tdu_skin_bones( in_BlendIndices0 );
    vec4 weights = tdu_skin_weights( in_BlendWeight0 );
    vec4 position = tdu_skin_position( in_Position0, bones, weights );
    vec3 normal = tdu_skin_normal( in_Normal0.xyz, bones, weights );
#else
    vec4 position = in_Position0;
    vec3 normal = in_Normal0.xyz;
#endif

    gl_Position = vec4( d3d9_dot( position, gVSWorldViewProject( 0 ) ),
                        d3d9_dot( position, gVSWorldViewProject( 1 ) ),
                        d3d9_dot( position, gVSWorldViewProject( 2 ) ),
                        d3d9_dot( position, gVSWorldViewProject( 3 ) ) );

    float diffuse = max( d3d9_dot( normal, -gVSLightDir.xyz ), 0.0 );

    // The fog distance uses the unskinned position.
    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gVSWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 2 ) ) );
    float viewDistance = length( viewPosition );

    vec2 sectorUV =
        vec2( d3d9_dot( position, gVSWorld( 0 ) ) + -gVSSectorOffset.x,
              d3d9_dot( position, gVSWorld( 2 ) ) + -gVSSectorOffset.z );
    float shadowFade = max( -viewDistance + kSectorSize, 0.0 ) *
                       gVSColorContrast.w * ( 1.0 / kSectorSize );
    out_Texcoord2 =
        vec4( sectorUV * ( 1.0 / kSectorSize ), shadowFade, gVSLODDist.y );
    out_Texcoord7 = vec4( tdu_vertex_fog( viewDistance ), 0.0, 0.0, 0.0 );

    // OTDU_ADDITION: night lamps, at the skinned position (bone segments of
    // guard rails are far from their unskinned position).
    bool bLamps = tdu_lamps_enabled();
#ifdef SHADING_SKINNED
    vec3 lampPosition = vec3( d3d9_dot( position, gVSWorldView( 0 ) ),
                              d3d9_dot( position, gVSWorldView( 1 ) ),
                              d3d9_dot( position, gVSWorldView( 2 ) ) );
#else
    vec3 lampPosition = viewPosition;
#endif
    out_ViewPosition = bLamps ? vec4( lampPosition, 1.0 ) : vec4( 0.0 );
    out_ViewNormal =
        bLamps ? vec4( d3d9_dot( normal, gVSWorldView( 0 ).xyz ),
                       d3d9_dot( normal, gVSWorldView( 1 ).xyz ),
                       d3d9_dot( normal, gVSWorldView( 2 ).xyz ), 0.0 )
               : vec4( 0.0 );

#ifdef SHADING_INSTANCE_COLOR
    out_Texcoord0 =
        vec4( in_Texcoord0.xy, 1.0 / ( 0.001 + gVSInstanceColor.x ), 0.0 );
#else
    out_Texcoord0 = vec4( in_Texcoord0.xy, 1.0, 0.0 );
#endif
#ifdef SHADING_DETAIL
    out_Texcoord1 = vec4( in_Texcoord1.xy, 0.0, 0.0 );
#endif

#ifdef SHADING_PIXEL_SPECULAR
    // The normal is not skinned here.
    out_Texcoord3 = vec4( in_Normal0.xyz, 0.0 );
    out_Texcoord4 = vec4( gVSLightDir.xyz, 0.0 );
    out_Texcoord5 = vec4( gVSCamPosInObj.xyz - position.xyz, 0.0 );
#endif
#ifdef SHADING_VERTEX_SPECULAR
    vec4 toCamera = gVSCamPosInObj - position;
    vec3 viewDirection =
        toCamera.xyz * inversesqrt( abs( d3d9_dot( toCamera, toCamera ) ) );
    float lightDotNormal = d3d9_dot( gVSLightDir.xyz, normal );
    vec3 lightReflection =
        normal * -( lightDotNormal + lightDotNormal ) + gVSLightDir.xyz;
    float specular =
        pow( abs( max( d3d9_dot( lightReflection, viewDirection ), 0.0 ) ),
             gVSLightSpecularParam.x );
    out_Texcoord3 = vec4( specular * ( 1.0 + -gVSLODDist.y ),
                          gVSLightSpecularParam.y, 0.0, 0.0 );
#endif

#ifdef SHADING_VERTEX_COLOR
    vec3 color = in_Color0.rgb;
#else
    vec3 color = vec3( 1.0 );
#endif
    out_Color0 = clamp( vec4( color, diffuse ), vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
