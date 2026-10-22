#version 450
#extension GL_GOOGLE_include_directive : require

// Generic vertex lit materials (Lighting table).
//
// Permutations:
//   LIGHTING_AMBIENT       adds gVSLightAmbient (alpha = ambient alpha)
//   LIGHTING_DIFFUSE       sun diffuse, unnormalized normal; without it the
//                          ambient is added to a diffuse of 1
//   LIGHTING_VERTEX_COLOR  modulates the lighting by Color0
//   LIGHTING_TEXCOORD      forwards UV set 0
//   LIGHTING_UV_MATRIX0    UV set 0 goes through gUVMatrix0
//   LIGHTING_FOG           vertex fog
//   LIGHTING_LOD_FADE      Texcoord3.x = LOD fade (gVSLODDist.y * w)
//   LIGHTING_FADE_ALPHA    Texcoord3.x = gVSFadeAlpha.x
//
// OTDU_ADDITION: unlit variants (no ambient, no diffuse: particles, shore
// foam) are scaled by the night unlit scale (1 = retail), emissive signs by
// the emissive scale.

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gVSFadeAlpha c.f[5]
#define gWorldViewProject( i ) c.f[8 + ( i )]
#define gWorldView( i ) c.f[20 + ( i )]
#define gUVMatrix0( i ) c.f[84 + ( i )]
#define gVSLightDir c.f[93]
#define gVSLightAmbient c.f[94]
#define gVSLightDiffuse c.f[95]
#define gVSLODDist c.f[118]

#if defined( LIGHTING_LOD_FADE ) || defined( LIGHTING_FADE_ALPHA )
#define LIGHTING_FADE
#endif

layout( location = 0 ) in vec4 in_Position0;
#ifdef LIGHTING_VERTEX_COLOR
layout( location = 1 ) in vec4 in_Color0;
#define NORMAL_LOCATION 2
#else
#define NORMAL_LOCATION 1
#endif
#ifdef LIGHTING_DIFFUSE
layout( location = NORMAL_LOCATION ) in vec4 in_Normal0;
#define TEXCOORD_LOCATION ( NORMAL_LOCATION + 1 )
#else
#define TEXCOORD_LOCATION NORMAL_LOCATION
#endif
#ifdef LIGHTING_TEXCOORD
layout( location = TEXCOORD_LOCATION ) in vec4 in_Texcoord0;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
#ifdef LIGHTING_TEXCOORD
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
#endif
#ifdef LIGHTING_FOG
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
#endif
#ifdef LIGHTING_FADE
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
#endif

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 3 ) ) );

#ifdef LIGHTING_DIFFUSE
    float diffuse = max( d3d9_dot( in_Normal0.xyz, -gVSLightDir.xyz ), 0.0 );
#ifdef LIGHTING_AMBIENT
    vec4 light = vec4( gVSLightDiffuse.rgb * diffuse + gVSLightAmbient.rgb,
                       gVSLightAmbient.a );
#else
    vec4 light = vec4( diffuse * gVSLightDiffuse.rgb, gVSLightDiffuse.a );
#endif
#elif defined( LIGHTING_AMBIENT )
    vec4 light = gVSLightAmbient + vec4( 1.0, 1.0, 1.0, 0.0 );
#endif

#if defined( LIGHTING_DIFFUSE ) || defined( LIGHTING_AMBIENT )
#ifdef LIGHTING_VERTEX_COLOR
    out_Texcoord0 = light * in_Color0;
#else
    out_Texcoord0 = light;
#endif
#elif defined( LIGHTING_VERTEX_COLOR )
    out_Texcoord0 =
        vec4( in_Color0.rgb * tdu_unlit_scale(), in_Color0.a ); // OTDU
#else
    out_Texcoord0 = vec4( vec3( tdu_unlit_scale() ), 1.0 ); // OTDU
#endif

#ifdef LIGHTING_TEXCOORD
#ifdef LIGHTING_UV_MATRIX0
    out_Texcoord1 =
        vec4( TDU_APPLY_UV_MATRIX( in_Texcoord0.xy, gUVMatrix0 ), 0.0, 0.0 );
#else
    out_Texcoord1 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#endif
#endif

#ifdef LIGHTING_FOG
    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gWorldView( 2 ) ) );
    float viewDistance =
        1.0 / inversesqrt( abs( d3d9_dot( viewPosition, viewPosition ) ) );
    out_Texcoord2 = vec4( tdu_vertex_fog( viewDistance ), 0.0, 0.0, 0.0 );
#endif

#ifdef LIGHTING_LOD_FADE
    out_Texcoord3 = vec4( gVSLODDist.y * gVSLODDist.w, 0.0, 0.0, 0.0 );
#elif defined( LIGHTING_FADE_ALPHA )
    out_Texcoord3 = vec4( gVSFadeAlpha.x, 0.0, 0.0, 0.0 );
#endif

    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
