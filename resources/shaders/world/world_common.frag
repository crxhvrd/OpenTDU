#version 450
#extension GL_GOOGLE_include_directive : require

// Normal mapped world geometry (World Common), lit in tangent space.
//
// Permutations:
//   WORLD_ENVMAP                  reflection from the cube environment map
//   WORLD_SPHEREMAP               reflection from a sphere map (gPSWorldView)
//   WORLD_GLOSS                   reflection scaled by the gloss map
//   WORLD_TANGENT_REFLECTION_FIX  OTDU_ADDITION: rebuilds the env map
//                                 reflection in object space from the tangent
//                                 basis (see PatchTangentReflectionPS)
//
// gPSMaterialParam = (reflection scale, reflection power, -, -)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gPSLightAmbient D3D9_C( 0 )
#define gPSLightDiffuse D3D9_C( 1 )
#define gPSMaterialParam D3D9_C( 4 )
#define gPSWorld( i ) D3D9_CR( 8, i )
#define gPSWorldView( i ) D3D9_CR( 8, i )

#if defined( WORLD_ENVMAP ) || defined( WORLD_SPHEREMAP )
#define WORLD_REFLECTION
#endif

D3D9_DECLARE_SAMPLER_2D( 0 ) // color map
D3D9_DECLARE_SAMPLER_2D( 1 ) // detail map
D3D9_DECLARE_SAMPLER_2D( 2 ) // normal map
#ifdef WORLD_GLOSS
D3D9_DECLARE_SAMPLER_2D( 3 ) // gloss map
#endif
#ifdef WORLD_ENVMAP
D3D9_DECLARE_SAMPLER_CUBE( 4 ) // reflection
#elif defined( WORLD_SPHEREMAP )
D3D9_DECLARE_SAMPLER_2D( 4 ) // reflection
#endif

layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#ifdef WORLD_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#endif
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
#if defined( WORLD_ENVMAP ) && defined( WORLD_TANGENT_REFLECTION_FIX )
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
layout( location = D3D9_SLOT_TEXCOORD6 ) in vec4 in_Texcoord6;
layout( location = D3D9_SLOT_TEXCOORD7 ) in vec4 in_Texcoord7;
#endif
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 colorUV = d3d9_ps_texcoord( in_Texcoord0 );
    vec4 detailUV = d3d9_ps_texcoord( in_Texcoord1 );
    vec3 lightDirection = d3d9_ps_texcoord( in_Texcoord3 ).xyz;
    float fog = d3d9_ps_texcoord( in_Texcoord4 ).x;

    vec3 normal = normalize( 2.0 * tex2D_s2( detailUV.xy ).xyz + vec3( -1.0 ) );
    float diffuse = max( dot( normal, -lightDirection ), 0.0 );
    vec3 light = gPSLightDiffuse.rgb * diffuse + gPSLightAmbient.rgb;
    // OTDU_ADDITION: night lamps (vertex normal, not the normal map).
    light += tdu_lamp_pixel( in_ViewPosition, in_ViewNormal );

    vec4 detail = tex2D_s1( detailUV.xy );
    vec3 color = tex2D_s0( colorUV.xy ).rgb * detail.rgb * light;

#ifdef WORLD_REFLECTION
    vec3 toCamera = normalize( d3d9_ps_texcoord( in_Texcoord2 ).xyz );
#ifdef WORLD_ENVMAP
    float facing = dot( -toCamera, normal );
    vec3 reflection = normal * -( facing + facing ) + -toCamera;
#ifdef WORLD_TANGENT_REFLECTION_FIX
    vec3 tangentReflection = reflection;
    reflection = d3d9_ps_texcoord( in_Texcoord5 ).xyz * tangentReflection.x;
    reflection =
        d3d9_ps_texcoord( in_Texcoord6 ).xyz * tangentReflection.y + reflection;
    reflection =
        d3d9_ps_texcoord( in_Texcoord7 ).xyz * tangentReflection.z + reflection;
#endif
    vec4 environment =
        texCUBE_s4( vec3( dot( reflection, gPSWorld( 2 ).xyz ),
                          dot( reflection, gPSWorld( 1 ).xyz ),
                          dot( reflection, gPSWorld( 0 ).xyz ) ) );
#else
    float facing = dot( toCamera, normal );
    vec3 reflection = normal * -( facing + facing ) + toCamera;
    vec2 sphereUV = vec2( dot( reflection, gPSWorldView( 0 ).xyz ),
                          dot( reflection, gPSWorldView( 1 ).xyz ) ) *
                        0.5 +
                    0.5;
    vec4 environment = tex2D_s4( sphereUV );
#endif
    vec3 reflected =
        exp2( log2( abs( environment.rgb ) ) * gPSMaterialParam.y );
#ifdef WORLD_GLOSS
    reflected *= tex2D_s3( detailUV.xy ).x * gPSMaterialParam.x;
#else
    reflected *= gPSMaterialParam.x;
#endif
    color += reflected;
#endif

    oC0 = vec4( tdu_apply_fog( color, fog ), detail.a );
    d3d9_ps_alpha_test( oC0 );
}
