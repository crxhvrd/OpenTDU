#version 450
#extension GL_GOOGLE_include_directive : require

// Roads. UV sets are consumed in order: color, marks, marks 2, detail.
//
// Permutations:
//   ROAD_MARKS    marks map coordinates (missing marks sample outside at 1)
//   ROAD_MARKS2   second marks map coordinates
//   ROAD_DETAIL   detail / gloss map coordinates
//   ROAD_SHADOW   sector shadow map coordinates and fade
//   ROAD_NORMAL   per vertex normal: lighting vectors for the pixel shader

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gColorContrast c.f[1]
#define gTDU_DiffuseFixedSun c.f[4]
#define gWorldViewProject( i ) c.f[8 + ( i )]
#define gWorldView( i ) c.f[20 + ( i )]
#define gVSLightDir c.f[93]
#define gVSCamPosInObj c.f[119]

const float kSectorSize = 768.0;

layout( location = 0 ) in vec4 in_Position0;
#ifdef ROAD_NORMAL
layout( location = 1 ) in vec4 in_Normal0;
#define UV_LOCATION( n ) ( 2 + ( n ) )
#else
#define UV_LOCATION( n ) ( 1 + ( n ) )
#endif
layout( location = UV_LOCATION( 0 ) ) in vec4 in_TexcoordColor;
#ifdef ROAD_MARKS
layout( location = UV_LOCATION( 1 ) ) in vec4 in_TexcoordMarks;
#define MARKS2_UV 2
#else
#define MARKS2_UV 1
#endif
#ifdef ROAD_MARKS2
layout( location = UV_LOCATION( MARKS2_UV ) ) in vec4 in_TexcoordMarks2;
#define DETAIL_UV ( MARKS2_UV + 1 )
#else
#define DETAIL_UV MARKS2_UV
#endif
#ifdef ROAD_DETAIL
layout( location = UV_LOCATION( DETAIL_UV ) ) in vec4 in_TexcoordDetail;
#endif

layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
#ifdef ROAD_DETAIL
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
#endif
#ifdef ROAD_SHADOW
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
#endif
#ifdef ROAD_NORMAL
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
layout( location = D3D9_SLOT_TEXCOORD5 ) out vec4 out_Texcoord5;
layout( location = D3D9_SLOT_TEXCOORD6 ) out vec4 out_Texcoord6;
layout( location = D3D9_SLOT_TEXCOORD7 ) out vec4 out_Texcoord7;
#endif

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 3 ) ) );

    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gWorldView( 2 ) ) );
    float viewDistance = length( viewPosition );

    // z: mip bias of the marks maps
    out_Texcoord0 = vec4( in_TexcoordColor.xy, gTDU_DiffuseFixedSun.w,
                          tdu_vertex_fog( viewDistance ) );

    vec4 marksUV = vec4( 1.0 );
#ifdef ROAD_MARKS
    marksUV.xy = in_TexcoordMarks.xy;
#endif
#ifdef ROAD_MARKS2
    marksUV.zw = in_TexcoordMarks2.xy;
#endif
    out_Texcoord1 = marksUV;
#ifdef ROAD_DETAIL
    out_Texcoord2 = vec4( in_TexcoordDetail.xy, 0.0, 0.0 );
#endif

#ifdef ROAD_SHADOW
    float shadowFade = max( -viewDistance + kSectorSize, 0.0 ) *
                       gColorContrast.w * ( 1.0 / kSectorSize );
    out_Texcoord3 =
        vec4( in_Position0.xz * ( 1.0 / kSectorSize ), shadowFade, 0.0 );
#endif

#ifdef ROAD_NORMAL
    out_Texcoord4 = vec4( in_Normal0.xyz, 0.0 );
    out_Texcoord5 = vec4( gVSLightDir.xyz, 0.0 );
    out_Texcoord6 = vec4( gVSCamPosInObj.xyz - in_Position0.xyz, 0.0 );
    out_Texcoord7 = vec4( gTDU_DiffuseFixedSun.xyz, 0.0 );
#endif

    // OTDU_ADDITION: night lamps.
    out_ViewPosition =
        tdu_lamps_enabled() ? vec4( viewPosition, 1.0 ) : vec4( 0.0 );

    out_Color0 = clamp( gColorContrast, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
