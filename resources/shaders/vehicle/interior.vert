#version 450
#extension GL_GOOGLE_include_directive : require

// Vehicle interior (VehicleInterior): no fog, two lights, noise UVs for the
// brushed specular.
//
// Layouts:
//   (default)                tangent frame + object space light directions,
//                            vertex diffuse in Color0.w (noise PS without
//                            normal map); INTERIOR_UV1 adds UV set 1
//   INTERIOR_OBJECT_SPACE    no normal: object space normal map PS (UVs in
//                            Texcoord5, lights in Texcoord3 / 4)
//   INTERIOR_TANGENT_LIGHTS  view and light vectors in tangent space (bump PS)

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gVSWorldViewProject( i ) c.f[8 + ( i )]
#define gVSWorldView( i ) c.f[20 + ( i )]
#define gVSLightDir c.f[93]
#define gVSLightDir1 c.f[101]
#define gVSCamPosInObj c.f[119]
#define gVSNoiseScale c.f[98]

layout( location = 0 ) in vec4 in_Position0;
layout( location = 1 ) in vec4 in_Color0;
#ifdef INTERIOR_OBJECT_SPACE
layout( location = 2 ) in vec4 in_Binormal0;
layout( location = 3 ) in vec4 in_Tangent0;
layout( location = 4 ) in vec4 in_Texcoord0;
layout( location = 5 ) in vec4 in_Texcoord1;
#else
layout( location = 2 ) in vec4 in_Normal0;
layout( location = 3 ) in vec4 in_Binormal0;
layout( location = 4 ) in vec4 in_Tangent0;
layout( location = 5 ) in vec4 in_Texcoord0;
#if defined( INTERIOR_UV1 ) || defined( INTERIOR_TANGENT_LIGHTS )
layout( location = 6 ) in vec4 in_Texcoord1;
#endif
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
#ifndef INTERIOR_TANGENT_LIGHTS
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
#endif
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
layout( location = D3D9_SLOT_TEXCOORD5 ) out vec4 out_Texcoord5;
// OTDU_ADDITION: night lamps. View position, then the view space normal
// (default layout, in X) or the normal map axes (object / tangent space).
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

    vec3 toCamera = gVSCamPosInObj.xyz + -in_Position0.xyz;
    vec2 noiseUV = gVSNoiseScale.x * in_Texcoord0.xy;

#if defined( INTERIOR_OBJECT_SPACE )
    out_Color0 = in_Color0;
    out_Texcoord0 = vec4( in_Binormal0.xyz, 0.0 );
    out_Texcoord1 = vec4( in_Tangent0.xyz, 0.0 );
    out_Texcoord2 = vec4( toCamera, 0.0 );
    out_Texcoord3 = vec4( gVSLightDir.xyz, 0.0 );
    out_Texcoord4 = vec4( gVSLightDir1.xyz, 0.0 );
    out_Texcoord5 = vec4( in_Texcoord1.xy, noiseUV );
#elif defined( INTERIOR_TANGENT_LIGHTS )
    vec3 tangent = in_Tangent0.xyz;
    vec3 binormal = in_Binormal0.xyz;
    vec3 normal = in_Normal0.xyz;
    out_Color0 = in_Color0;
    out_Texcoord2 =
        vec4( d3d9_dot( toCamera, tangent ), d3d9_dot( toCamera, binormal ),
              d3d9_dot( toCamera, normal ), 0.0 );
    out_Texcoord3 = vec4( in_Texcoord1.xy, noiseUV );
    out_Texcoord4 = vec4( d3d9_dot( gVSLightDir.xyz, tangent ),
                          d3d9_dot( gVSLightDir.xyz, binormal ),
                          d3d9_dot( gVSLightDir.xyz, normal ), 0.0 );
    out_Texcoord5 = vec4( d3d9_dot( gVSLightDir1.xyz, tangent ),
                          d3d9_dot( gVSLightDir1.xyz, binormal ),
                          d3d9_dot( gVSLightDir1.xyz, normal ), 0.0 );
#else
    out_Color0 =
        vec4( in_Color0.xyz,
              max( d3d9_dot( in_Normal0.xyz, -gVSLightDir.xyz ), 0.0 ) );
    out_Texcoord0 = vec4( in_Tangent0.xyz, 0.0 );
    out_Texcoord1 = vec4( in_Binormal0.xyz, 0.0 );
    out_Texcoord2 = vec4( toCamera, 0.0 );
#ifdef INTERIOR_UV1
    out_Texcoord3 = vec4( in_Texcoord1.xy, noiseUV );
#else
    out_Texcoord3 = vec4( 0.0, 0.0, noiseUV );
#endif
    out_Texcoord4 = vec4( gVSLightDir.xyz, 0.0 );
    out_Texcoord5 = vec4( gVSLightDir1.xyz, 0.0 );
#endif

    // OTDU_ADDITION: night lamps (the rebuilt interior shaders always get the
    // world view matrix, see RenderContext::registerShader).
    bool bLamps = tdu_lamps_enabled();
    out_ViewPosition =
        bLamps ? vec4( d3d9_dot( in_Position0, gVSWorldView( 0 ) ),
                       d3d9_dot( in_Position0, gVSWorldView( 1 ) ),
                       d3d9_dot( in_Position0, gVSWorldView( 2 ) ), 1.0 )
               : vec4( 0.0 );
#if defined( INTERIOR_OBJECT_SPACE )
    out_ViewBasisX = bLamps ? toView( vec3( 1.0, 0.0, 0.0 ) ) : vec4( 0.0 );
    out_ViewBasisY = bLamps ? toView( vec3( 0.0, 1.0, 0.0 ) ) : vec4( 0.0 );
    out_ViewBasisZ = bLamps ? toView( vec3( 0.0, 0.0, 1.0 ) ) : vec4( 0.0 );
#elif defined( INTERIOR_TANGENT_LIGHTS )
    out_ViewBasisX = bLamps ? toView( tangent ) : vec4( 0.0 );
    out_ViewBasisY = bLamps ? toView( binormal ) : vec4( 0.0 );
    out_ViewBasisZ = bLamps ? toView( normal ) : vec4( 0.0 );
#else
    out_ViewBasisX = bLamps ? toView( in_Normal0.xyz ) : vec4( 0.0 );
    out_ViewBasisY = vec4( 0.0 );
    out_ViewBasisZ = vec4( 0.0 );
#endif

    out_Color0 = clamp( out_Color0, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
