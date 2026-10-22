#version 450
#extension GL_GOOGLE_include_directive : require

// Vertex lit vehicle materials (table vehicle_materials).
//
// Permutations:
//   MATERIALS_SPECULAR           vertex specular (Texcoord0)
//   MATERIALS_MATERIAL_MAP       material map (s0, Texcoord1)
//   MATERIALS_DETAIL             detail map (s1) blended over the material
//   MATERIALS_GLOSS_REFLECTION   the gloss map (s2) masks the reflection
//   MATERIALS_GLOSS_SPECULAR     the gloss map (s2) masks the specular
//   MATERIALS_REFLECTION_CUBE    environment cube map (s3)
//   MATERIALS_REFLECTION_SPHERE  environment sphere map (s3)
//   MATERIALS_SHADOW             baked shadow map (s4)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#if defined( MATERIALS_REFLECTION_CUBE ) ||                                    \
    defined( MATERIALS_REFLECTION_SPHERE )
#define MATERIALS_REFLECTION
#endif
#if defined( MATERIALS_GLOSS_REFLECTION ) || defined( MATERIALS_GLOSS_SPECULAR )
#define MATERIALS_GLOSS
#endif

#ifdef MATERIALS_MATERIAL_MAP
D3D9_DECLARE_SAMPLER_2D( 0 ) // material map
#endif
#ifdef MATERIALS_DETAIL
D3D9_DECLARE_SAMPLER_2D( 1 ) // detail map
#endif
#ifdef MATERIALS_GLOSS
D3D9_DECLARE_SAMPLER_2D( 2 ) // gloss map
#endif
#ifdef MATERIALS_REFLECTION_CUBE
D3D9_DECLARE_SAMPLER_CUBE( 3 ) // reflection
#elif defined( MATERIALS_REFLECTION_SPHERE )
D3D9_DECLARE_SAMPLER_2D( 3 ) // reflection
#endif
#ifdef MATERIALS_SHADOW
D3D9_DECLARE_SAMPLER_2D( 4 ) // baked shadow map
#endif

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
#ifdef MATERIALS_SPECULAR
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#endif
#ifdef MATERIALS_MATERIAL_MAP
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#endif
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#ifdef MATERIALS_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
#endif
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 uvFog = d3d9_ps_texcoord( in_Texcoord2 );

#ifdef MATERIALS_MATERIAL_MAP
    vec3 base = tex2D_s0( d3d9_ps_texcoord( in_Texcoord1 ).xy ).xyz;
#ifdef MATERIALS_DETAIL
    vec4 detail = tex2D_s1( uvFog.xy );
    base = d3d9_lerp( base, detail.xyz, detail.www );
#endif
#elif defined( MATERIALS_DETAIL )
    vec4 detail = tex2D_s1( uvFog.xy );
    vec3 base = detail.xyz * detail.www + ( 1.0 + -detail.w );
#else
    vec3 base = vec3( 1.0 );
#endif

    // OTDU_ADDITION: night lamps join the vertex light.
    vec3 light =
        in_Color0.xyz + tdu_lamp_pixel( in_ViewPosition, in_ViewNormal );
    vec3 color = light * base;
#ifdef MATERIALS_SHADOW
    color = color * tex2D_s4( uvFog.xy ).xyz;
#endif

#ifdef MATERIALS_GLOSS
    float gloss = tex2D_s2( uvFog.xy ).x;
#endif
#ifdef MATERIALS_SPECULAR
    vec3 specular = d3d9_ps_texcoord( in_Texcoord0 ).xyz;
#ifdef MATERIALS_GLOSS_SPECULAR
    specular = specular * gloss;
#endif
    color = color + specular;
#endif

#ifdef MATERIALS_REFLECTION
    vec4 fresnelPower = d3d9_ps_texcoord( in_Texcoord4 );
#ifdef MATERIALS_REFLECTION_CUBE
    vec3 environment = texCUBE_s3( d3d9_ps_texcoord( in_Texcoord3 ).xyz ).xyz;
#else
    vec3 environment = tex2D_s3( d3d9_ps_texcoord( in_Texcoord3 ).xy ).xyz;
#endif
    vec3 reflected = exp2( log2( abs( environment ) ) * fresnelPower.www ) *
                     fresnelPower.xyz;
#ifdef MATERIALS_GLOSS_REFLECTION
    reflected = reflected * gloss;
#endif
    color = reflected + color;
#endif

    oC0 = vec4( tdu_apply_fog( color, uvFog.z ), 1.0 );
    d3d9_ps_alpha_test( oC0 );
}
