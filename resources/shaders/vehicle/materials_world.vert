#version 450
#extension GL_GOOGLE_include_directive : require

// Vehicle materials with an object space normal map (table
// vehicle_materials_worldspace): per pixel lighting, no vertex normal.
//
// Permutations:
//   WORLD_UV2         Texcoord0 = (UV set 1, UV set 0), else (UV set 0, 0, 0)
//   WORLD_LIGHT1      second light direction (Texcoord3)
//   WORLD_VIEW        object space view vector (Texcoord4)
//   WORLD_REFLECTION  reflection color and power (Texcoord5)
//   WORLD_TANGENT_SPACE  tangent space normal map (vehicle_materials_
//                        tangentspace): light / view vectors in tangent space

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gVSWorldViewProject( i ) c.f[8 + ( i )]
#define gVSWorldView( i ) c.f[20 + ( i )]
#define gVSMaterialParam c.f[88]
#define gVSLightDir0 c.f[93]
#define gVSLightDir1 c.f[101]
#define gVSCamPosInObj c.f[119]
#define gEnvMapCoeff c.f[239]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
#ifdef WORLD_TANGENT_SPACE
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
layout( location = D3D9_IN_BINORMAL0 ) in vec4 in_Binormal0;
layout( location = D3D9_IN_TANGENT0 ) in vec4 in_Tangent0;
#endif
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#ifdef WORLD_UV2
layout( location = D3D9_IN_TEXCOORD( 1 ) ) in vec4 in_Texcoord1;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
#ifdef WORLD_LIGHT1
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
#endif
#ifdef WORLD_VIEW
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
#endif
#ifdef WORLD_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD5 ) out vec4 out_Texcoord5;
#endif
layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS ) out vec4 out_ViewBasisX;      // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS + 1 ) out vec4 out_ViewBasisY;  // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS + 2 ) out vec4 out_ViewBasisZ;  // OTDU

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gVSWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 3 ) ) );

#ifdef WORLD_UV2
    out_Texcoord0 = vec4( in_Texcoord1.xy, in_Texcoord0.xy );
#else
    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#endif
    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gVSWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 2 ) ) );
    out_Texcoord1 =
        vec4( tdu_vertex_fog( length( viewPosition ) ), 0.0, 0.0, 0.0 );
#ifdef WORLD_TANGENT_SPACE
    vec3 tangent = in_Tangent0.xyz;
    vec3 binormal = in_Binormal0.xyz;
    vec3 normal = in_Normal0.xyz;
    out_Texcoord2 = vec4( d3d9_dot( gVSLightDir0.xyz, tangent ),
                          d3d9_dot( gVSLightDir0.xyz, binormal ),
                          d3d9_dot( gVSLightDir0.xyz, normal ), 0.0 );
#ifdef WORLD_VIEW
    vec3 toCamera = gVSCamPosInObj.xyz + -in_Position0.xyz;
    out_Texcoord4 =
        vec4( d3d9_dot( toCamera, tangent ), d3d9_dot( toCamera, binormal ),
              d3d9_dot( toCamera, normal ), 0.0 );
#endif
#else
    out_Texcoord2 = vec4( gVSLightDir0.xyz, 0.0 );
#ifdef WORLD_LIGHT1
    out_Texcoord3 = vec4( gVSLightDir1.xyz, 0.0 );
#endif
#ifdef WORLD_VIEW
    out_Texcoord4 = vec4( gVSCamPosInObj.xyz + -in_Position0.xyz, 0.0 );
#endif
#endif
#ifdef WORLD_REFLECTION
    out_Texcoord5 = vec4( gEnvMapCoeff.x * in_Color0.xyz * gVSMaterialParam.x,
                          gVSMaterialParam.y );
#endif

    // OTDU_ADDITION: night lamps; the normal map axes in view space (the PS
    // applies the vertex color).
    bool bLamps = tdu_lamps_enabled();
    out_ViewPosition = bLamps ? vec4( viewPosition, 1.0 ) : vec4( 0.0 );
#ifdef WORLD_TANGENT_SPACE
    out_ViewBasisX =
        bLamps ? vec4( d3d9_dot( tangent, gVSWorldView( 0 ).xyz ),
                       d3d9_dot( tangent, gVSWorldView( 1 ).xyz ),
                       d3d9_dot( tangent, gVSWorldView( 2 ).xyz ), 0.0 )
               : vec4( 0.0 );
    out_ViewBasisY =
        bLamps ? vec4( d3d9_dot( binormal, gVSWorldView( 0 ).xyz ),
                       d3d9_dot( binormal, gVSWorldView( 1 ).xyz ),
                       d3d9_dot( binormal, gVSWorldView( 2 ).xyz ), 0.0 )
               : vec4( 0.0 );
    out_ViewBasisZ =
        bLamps ? vec4( d3d9_dot( normal, gVSWorldView( 0 ).xyz ),
                       d3d9_dot( normal, gVSWorldView( 1 ).xyz ),
                       d3d9_dot( normal, gVSWorldView( 2 ).xyz ), 0.0 )
               : vec4( 0.0 );
#else
    out_ViewBasisX = bLamps ? vec4( gVSWorldView( 0 ).x, gVSWorldView( 1 ).x,
                                    gVSWorldView( 2 ).x, 0.0 )
                            : vec4( 0.0 );
    out_ViewBasisY = bLamps ? vec4( gVSWorldView( 0 ).y, gVSWorldView( 1 ).y,
                                    gVSWorldView( 2 ).y, 0.0 )
                            : vec4( 0.0 );
    out_ViewBasisZ = bLamps ? vec4( gVSWorldView( 0 ).z, gVSWorldView( 1 ).z,
                                    gVSWorldView( 2 ).z, 0.0 )
                            : vec4( 0.0 );
#endif

    out_Color0 = clamp( in_Color0, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
