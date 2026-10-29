#version 450
#extension GL_GOOGLE_include_directive : require

// Normal mapped vehicle paint (tables vehicle_paint_normalmapped: object
// space map, vehicle_paint_normalmapped2: tangent space map).
//
// Permutations:
//   PAINTMAP_TANGENT_SPACE  light / view vectors in tangent space
//   PAINTMAP_VIEW           view vector (Texcoord3)
//   PAINTMAP_REFLECTION     reflection color and power (Texcoord4)

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gVSWorldViewProject( i ) D3D9_CR( 8, i )
#define gVSWorldView( i ) D3D9_CR( 20, i )
#define gVSMaterialParam D3D9_C( 88 )
#define gVSLightDir D3D9_C( 93 )
#define gVSCamPosInObj D3D9_C( 119 )
#define gEnvMapCoeff D3D9_C( 239 )

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
#ifdef PAINTMAP_TANGENT_SPACE
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
layout( location = D3D9_IN_BINORMAL0 ) in vec4 in_Binormal0;
layout( location = D3D9_IN_TANGENT0 ) in vec4 in_Tangent0;
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#else
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
#ifdef PAINTMAP_VIEW
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
#endif
#ifdef PAINTMAP_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
#endif
layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS ) out vec4 out_ViewBasisX;      // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS + 1 ) out vec4 out_ViewBasisY;  // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS + 2 ) out vec4 out_ViewBasisZ;  // OTDU

vec4 toView( vec3 v ) {
    return vec4( d3d9_dot( v, gVSWorldView( 0 ).xyz ),
                 d3d9_dot( v, gVSWorldView( 1 ).xyz ),
                 d3d9_dot( v, gVSWorldView( 2 ).xyz ), 0.0 );
}

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gVSWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 3 ) ) );

    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gVSWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 2 ) ) );
    out_Texcoord2 =
        vec4( tdu_vertex_fog( length( viewPosition ) ), 0.0, 0.0, 0.0 );

#ifdef PAINTMAP_TANGENT_SPACE
    vec3 tangent = in_Tangent0.xyz;
    vec3 binormal = in_Binormal0.xyz;
    vec3 normal = in_Normal0.xyz;
    out_Texcoord1 = vec4( d3d9_dot( gVSLightDir.xyz, tangent ),
                          d3d9_dot( gVSLightDir.xyz, binormal ),
                          d3d9_dot( gVSLightDir.xyz, normal ), 0.0 );
#ifdef PAINTMAP_VIEW
    vec3 toCamera = gVSCamPosInObj.xyz + -in_Position0.xyz;
    out_Texcoord3 =
        vec4( d3d9_dot( toCamera, tangent ), d3d9_dot( toCamera, binormal ),
              d3d9_dot( toCamera, normal ), 0.0 );
#endif
#else
    out_Texcoord1 = vec4( gVSLightDir.xyz, 0.0 );
#ifdef PAINTMAP_VIEW
    out_Texcoord3 = vec4( gVSCamPosInObj.xyz + -in_Position0.xyz, 0.0 );
#endif
#endif
#ifdef PAINTMAP_REFLECTION
    out_Texcoord4 = vec4( gEnvMapCoeff.x * in_Color0.xyz * gVSMaterialParam.x,
                          gVSMaterialParam.y );
#endif

    // OTDU_ADDITION: night lamps; the normal map axes in view space (the PS
    // applies the vertex color).
    bool bLamps = tdu_lamps_enabled();
    out_ViewPosition = bLamps ? vec4( viewPosition, 1.0 ) : vec4( 0.0 );
#ifdef PAINTMAP_TANGENT_SPACE
    out_ViewBasisX = bLamps ? toView( tangent ) : vec4( 0.0 );
    out_ViewBasisY = bLamps ? toView( binormal ) : vec4( 0.0 );
    out_ViewBasisZ = bLamps ? toView( normal ) : vec4( 0.0 );
#else
    out_ViewBasisX = bLamps ? toView( vec3( 1.0, 0.0, 0.0 ) ) : vec4( 0.0 );
    out_ViewBasisY = bLamps ? toView( vec3( 0.0, 1.0, 0.0 ) ) : vec4( 0.0 );
    out_ViewBasisZ = bLamps ? toView( vec3( 0.0, 0.0, 1.0 ) ) : vec4( 0.0 );
#endif

    out_Color0 = clamp( in_Color0, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
