#version 450
#extension GL_GOOGLE_include_directive : require

// Vertex lit materials, optionally skinned (Skinning table): the light goes
// through Color0, UV set 0 through Texcoord0, the fog through Texcoord1.
//
// Permutations:
//   SKIN_SKINNED        4 bone skinning (the fog uses the unskinned position)
//   SKIN_AMBIENT        gVSLightAmbient
//   SKIN_AMBIENT_ALPHA  the light alpha is the ambient alpha (else 1)
//   SKIN_DIFFUSE        sun diffuse added to the ambient
//   SKIN_VERTEX_COLOR   modulates the light by Color0
//   SKIN_TEXCOORD       forwards UV set 0
//   SKIN_UV_MATRIX0     UV set 0 goes through gUVMatrix0

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gWorldViewProject( i ) c.f[8 + ( i )]
#define gWorldView( i ) c.f[20 + ( i )]
#define gUVMatrix0( i ) c.f[84 + ( i )]
#define gVSLightDir c.f[93]
#define gVSLightAmbient c.f[94]
#define gVSLightDiffuse c.f[95]

layout( location = 0 ) in vec4 in_Position0;
#ifdef SKIN_DIFFUSE
layout( location = 1 ) in vec4 in_Normal0;
#define SKIN_LOCATION 2
#else
#define SKIN_LOCATION 1
#endif
#ifdef SKIN_SKINNED
layout( location = SKIN_LOCATION ) in vec4 in_BlendIndices0;
layout( location = SKIN_LOCATION + 1 ) in vec4 in_BlendWeight0;
#define COLOR_LOCATION ( SKIN_LOCATION + 2 )
#else
#define COLOR_LOCATION SKIN_LOCATION
#endif
#ifdef SKIN_VERTEX_COLOR
layout( location = COLOR_LOCATION ) in vec4 in_Color0;
#define TEXCOORD_LOCATION ( COLOR_LOCATION + 1 )
#else
#define TEXCOORD_LOCATION COLOR_LOCATION
#endif
#ifdef SKIN_TEXCOORD
layout( location = TEXCOORD_LOCATION ) in vec4 in_Texcoord0;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
#ifdef SKIN_TEXCOORD
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
#endif
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;

void main() {
#ifdef SKIN_SKINNED
    ivec4 bones = tdu_skin_bones( in_BlendIndices0 );
    vec4 weights = tdu_skin_weights( in_BlendWeight0 );
    vec4 position = tdu_skin_position( in_Position0, bones, weights );
#else
    vec4 position = in_Position0;
#endif
    gl_Position = vec4( d3d9_dot( position, gWorldViewProject( 0 ) ),
                        d3d9_dot( position, gWorldViewProject( 1 ) ),
                        d3d9_dot( position, gWorldViewProject( 2 ) ),
                        d3d9_dot( position, gWorldViewProject( 3 ) ) );

#ifdef SKIN_DIFFUSE
#ifdef SKIN_SKINNED
    vec3 normal = tdu_skin_normal( in_Normal0.xyz, bones, weights );
#else
    vec3 normal = in_Normal0.xyz;
#endif
    float diffuse = max( d3d9_dot( normal, -gVSLightDir.xyz ), 0.0 );
    vec4 light =
        vec4( gVSLightDiffuse.xyz * diffuse + gVSLightAmbient.xyz, 1.0 );
#elif defined( SKIN_AMBIENT )
    vec4 light = vec4( gVSLightAmbient.xyz, 1.0 );
#else
    vec4 light = vec4( 1.0 );
#endif
#ifdef SKIN_AMBIENT_ALPHA
    light.w = gVSLightAmbient.w;
#endif
#ifdef SKIN_VERTEX_COLOR
    out_Color0 = light * in_Color0;
#else
    out_Color0 = light;
#endif

#ifdef SKIN_TEXCOORD
#ifdef SKIN_UV_MATRIX0
    out_Texcoord0 =
        vec4( TDU_APPLY_UV_MATRIX( in_Texcoord0.xy, gUVMatrix0 ), 0.0, 0.0 );
#else
    out_Texcoord0 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#endif
#endif

    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gWorldView( 2 ) ) );
    float viewDistance =
        1.0 / inversesqrt( abs( d3d9_dot( viewPosition, viewPosition ) ) );
    out_Texcoord1 = vec4( tdu_vertex_fog( viewDistance ), 0.0, 0.0, 0.0 );

    out_Color0 = clamp( out_Color0, vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
