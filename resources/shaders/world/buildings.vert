#version 450
#extension GL_GOOGLE_include_directive : require

// Vertex lit buildings.
//
// Permutations:
//   BUILDING_UV_MATRIX0..1     UV set N goes through gUVMatrixN
//   BUILDING_DETAIL            detail UV set (the detail map fades out with the
//                              LOD distance)
//   BUILDING_ENVMAP            cube map reflection vector (gVSWorld)
//   BUILDING_SPHEREMAP         sphere map reflection UVs (gVSWorldView)
//   BUILDING_SPECULAR          vertex specular (gVSLightSpecular)
//   BUILDING_INSTANCE_COLOR    blends the lighting to the instance color with
//   the
//                              LOD distance
//   BUILDING_FADE              fades the alpha out over the second half of the
//                              LOD distance
//   BUILDING_LOD_SCALE         1, 2 or 4: how fast the instance color and the
//                              specular blend out with the LOD distance

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gVSWorldViewProject( i ) c.f[8 + ( i )]
#define gVSWorld( i ) c.f[16 + ( i )]
#define gVSWorldView( i ) c.f[20 + ( i )]
#define gUVMatrix1( i ) c.f[80 + ( i )]
#define gUVMatrix0( i ) c.f[84 + ( i )]
#define gVSLightDir c.f[93]
#define gVSLightAmbient c.f[94]
#define gVSLightDiffuse c.f[95]
#define gVSLightSpecular c.f[96]
#define gVSLightSpecularParam c.f[98]
#define gVSLODDist c.f[118]
#define gVSCamPosInObj c.f[119]
#define gVSInstanceColor c.f[238]
#define gVSLightAmbient1 c.f[247]

#ifndef BUILDING_LOD_SCALE
#define BUILDING_LOD_SCALE 1
#endif

#if defined( BUILDING_ENVMAP ) || defined( BUILDING_SPHEREMAP )
#define BUILDING_REFLECTION
#endif

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#ifdef BUILDING_DETAIL
layout( location = D3D9_IN_TEXCOORD( 1 ) ) in vec4 in_Texcoord1;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
#ifdef BUILDING_DETAIL
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
#endif
#ifdef BUILDING_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
#endif
#ifdef BUILDING_SPECULAR
layout( location = D3D9_SLOT_TEXCOORD5 ) out vec4 out_Texcoord5;
#endif
layout( location = D3D9_SLOT_TEXCOORD6 ) out vec4 out_Texcoord6;
layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) out vec4 out_ViewNormal;     // OTDU
// OTDU
layout( location = OTDU_SLOT_OBJECT_POSITION ) out vec4 out_ObjectPosition;

float lodBlend() {
#if BUILDING_LOD_SCALE == 1
    return 1.0 - gVSLODDist.y;
#else
    return 1.0 - min( gVSLODDist.y * float( BUILDING_LOD_SCALE ), 1.0 );
#endif
}

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gVSWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gVSWorldViewProject( 3 ) ) );

    vec3 normal = d3d9_normalize( in_Normal0.xyz );
    float diffuse = max( d3d9_dot( normal, -gVSLightDir.xyz ), 0.0 );
    vec3 light = gVSLightDiffuse.rgb * diffuse + gVSLightAmbient.rgb;
#ifdef BUILDING_INSTANCE_COLOR
    vec3 instanceLight =
        gVSLightAmbient1.rgb * gVSInstanceColor.rgb + gVSLightAmbient.rgb;
    light = d3d9_lerp( instanceLight, light, lodBlend() );
#endif
    out_Texcoord0 = vec4( light, 1.0 );

#ifdef BUILDING_UV_MATRIX0
    out_Texcoord1 =
        vec4( TDU_APPLY_UV_MATRIX( in_Texcoord0.xy, gUVMatrix0 ), 0.0, 0.0 );
#else
    out_Texcoord1 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#endif
#ifdef BUILDING_DETAIL
#ifdef BUILDING_UV_MATRIX1
    out_Texcoord2 = vec4( TDU_APPLY_UV_MATRIX( in_Texcoord1.xy, gUVMatrix1 ),
                          gVSLODDist.y, 0.0 );
#else
    out_Texcoord2 = vec4( in_Texcoord1.xy, gVSLODDist.y, 0.0 );
#endif
#endif

#if defined( BUILDING_REFLECTION ) || defined( BUILDING_SPECULAR )
    vec4 toCamera4 = gVSCamPosInObj - in_Position0;
    vec3 toCamera =
        toCamera4.xyz * inversesqrt( d3d9_dot( toCamera4, toCamera4 ) );
#endif
#ifdef BUILDING_ENVMAP
    float facing = d3d9_dot( -toCamera, normal );
    vec3 reflection =
        d3d9_normalize( normal * -( facing + facing ) + -toCamera );
    out_Texcoord4 =
        vec4( d3d9_dot( reflection, gVSWorld( 2 ).xyz ),
              d3d9_dot( reflection, gVSWorld( 1 ).xyz ),
              d3d9_dot( reflection, gVSWorld( 0 ).xyz ), 1.0 - gVSLODDist.y );
#elif defined( BUILDING_SPHEREMAP )
    float facing = d3d9_dot( toCamera, normal );
    vec3 reflection =
        d3d9_normalize( normal * -( facing + facing ) + toCamera );
    out_Texcoord4 =
        vec4( vec2( d3d9_dot( reflection, gVSWorldView( 0 ).xyz ),
                    d3d9_dot( reflection, gVSWorldView( 1 ).xyz ) ) *
                      0.5 +
                  0.5,
              gVSLODDist.y * 0.0, gVSLODDist.y * -1.0 + 1.0 );
#endif
#ifdef BUILDING_SPECULAR
    float lightFacing = d3d9_dot( gVSLightDir.xyz, normal );
    vec3 lightReflection =
        normal * -( lightFacing + lightFacing ) + gVSLightDir.xyz;
    float specular =
        pow( abs( max( d3d9_dot( lightReflection, toCamera ), 0.0 ) ),
             gVSLightSpecularParam.x );
    out_Texcoord5 = vec4( lodBlend() * ( specular * gVSLightSpecular.rgb ),
                          gVSLightSpecularParam.y );
#endif

    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gVSWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gVSWorldView( 2 ) ) );
    out_Texcoord6 =
        vec4( tdu_vertex_fog( length( viewPosition ) ), 0.0, 0.0, 0.0 );

    // OTDU_ADDITION: night lamps.
    bool bLamps = tdu_lamps_enabled();
    out_ViewPosition = bLamps ? vec4( viewPosition, 1.0 ) : vec4( 0.0 );
    out_ViewNormal =
        bLamps ? vec4( d3d9_dot( normal, gVSWorldView( 0 ).xyz ),
                       d3d9_dot( normal, gVSWorldView( 1 ).xyz ),
                       d3d9_dot( normal, gVSWorldView( 2 ).xyz ), 0.0 )
               : vec4( 0.0 );
    // OTDU_ADDITION: lit windows (cells per floor and the object space normal
    // height, see buildings.frag).
    out_ObjectPosition = vec4( in_Position0.xyz, normal.y );
#ifdef BUILDING_FADE
    out_Texcoord6.y = 1.0 - max( ( gVSLODDist.y - 0.5 ) * 2.0, 0.0 );
#endif

    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
