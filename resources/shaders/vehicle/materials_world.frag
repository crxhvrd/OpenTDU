#version 450
#extension GL_GOOGLE_include_directive : require

// Vehicle materials with an object space normal map (table
// vehicle_materials_worldspace): per pixel sun light, specular and
// reflection from the map normal (s2).
//
// Permutations:
//   WORLD_MATERIAL_MAP       material map (s0)
//   WORLD_MATERIAL_UV_ZW     the material map uses Texcoord0.zw (UV set 0)
//   WORLD_DETAIL             detail map (s1) blended over the material
//   WORLD_GLOSS_REFLECTION   the gloss map (s3) masks the reflection
//   WORLD_GLOSS_SPECULAR     the gloss map (s3) masks the specular
//   WORLD_REFLECTION_CUBE    environment cube map (s4), world space
//   WORLD_REFLECTION_SPHERE  environment sphere map (s4), view space
//   WORLD_SHADOW             baked shadow map (s5)
//   WORLD_SPECULAR           specular of light 0
//   WORLD_SPECULAR1          specular of light 1 (half the exponent)
//   WORLD_TANGENT_SPACE      tangent space map
//   (vehicle_materials_tangentspace):
//                            normalized light vector; the reflection vector
//                            still goes through gPSWorld, as in retail

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gPSLightAmbient c.f[0]
#define gPSLightDiffuse c.f[1]
#define gPSLightSpecular c.f[2]
#define gPSLightSpecularParam c.f[3]
#define gPSLightSpecular1 c.f[7]
#define gPSWorld( i ) c.f[8 + ( i )]
#define gPSWorldView( i ) c.f[8 + ( i )]

#if defined( WORLD_REFLECTION_CUBE ) || defined( WORLD_REFLECTION_SPHERE )
#define WORLD_REFLECTION
#endif
#if defined( WORLD_GLOSS_REFLECTION ) || defined( WORLD_GLOSS_SPECULAR )
#define WORLD_GLOSS
#endif
#if defined( WORLD_REFLECTION ) || defined( WORLD_SPECULAR )
#define WORLD_VIEW
#endif

#ifdef WORLD_MATERIAL_MAP
D3D9_DECLARE_SAMPLER_2D( 0 ) // material map
#endif
#ifdef WORLD_DETAIL
D3D9_DECLARE_SAMPLER_2D( 1 ) // detail map
#endif
D3D9_DECLARE_SAMPLER_2D( 2 ) // object space normal map
#ifdef WORLD_GLOSS
D3D9_DECLARE_SAMPLER_2D( 3 ) // gloss map
#endif
#ifdef WORLD_REFLECTION_CUBE
D3D9_DECLARE_SAMPLER_CUBE( 4 ) // reflection
#elif defined( WORLD_REFLECTION_SPHERE )
D3D9_DECLARE_SAMPLER_2D( 4 ) // reflection
#endif
#ifdef WORLD_SHADOW
D3D9_DECLARE_SAMPLER_2D( 5 ) // baked shadow map
#endif

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#ifdef WORLD_SPECULAR1
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
#endif
#ifdef WORLD_VIEW
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
#endif
#ifdef WORLD_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
#endif
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS ) in vec4 in_ViewBasisX;      // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS + 1 ) in vec4 in_ViewBasisY;  // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS + 2 ) in vec4 in_ViewBasisZ;  // OTDU
layout( location = 0 ) out vec4 oC0;

// Specular: (view . light reflected about the normal) ^ exponent.
float specularTerm( vec3 lightDirection,
                    vec3 normal,
                    vec3 toCamera,
                    float exponent ) {
    float lightDotNormal = dot( normal, lightDirection );
    vec3 reflected =
        normal * -( lightDotNormal + lightDotNormal ) + lightDirection;
    return pow( abs( max( dot( toCamera, reflected ), 0.0 ) ), exponent );
}

void main() {
    vec4 uv = d3d9_ps_texcoord( in_Texcoord0 );
    float fog = d3d9_ps_texcoord( in_Texcoord1 ).x;
#ifdef WORLD_TANGENT_SPACE
    vec3 lightDirection = normalize( d3d9_ps_texcoord( in_Texcoord2 ).xyz );
#else
    vec3 lightDirection = d3d9_ps_texcoord( in_Texcoord2 ).xyz;
#endif

    vec3 mapNormal = tex2D_s2( uv.xy ).xyz + vec3( -0.5 );
    mapNormal = mapNormal + mapNormal;
    vec3 normal = normalize( mapNormal );
    float diffuse = max( dot( normal, -lightDirection ), 0.0 );

    // OTDU_ADDITION: night lamps join the sun light (map normal).
    vec3 lamps = vec3( 0.0 );
    if ( tdu_lamps_enabled() ) {
        lamps = tdu_lamp_pixel_mapped( in_ViewPosition, in_ViewBasisX,
                                       in_ViewBasisY, in_ViewBasisZ, normal );
    }
    vec3 light =
        ( gPSLightDiffuse.xyz * diffuse + gPSLightAmbient.xyz + lamps ) *
        in_Color0.xyz;
#ifdef WORLD_SHADOW
    light = light * tex2D_s5( uv.xy ).xyz;
#endif

#ifdef WORLD_MATERIAL_MAP
#ifdef WORLD_MATERIAL_UV_ZW
    vec3 base = tex2D_s0( uv.zw ).xyz;
#else
    vec3 base = tex2D_s0( uv.xy ).xyz;
#endif
#ifdef WORLD_DETAIL
    vec4 detail = tex2D_s1( uv.xy );
    base = d3d9_lerp( base, detail.xyz, detail.www );
#endif
    vec3 color = light * base;
#elif defined( WORLD_DETAIL )
    vec4 detail = tex2D_s1( uv.xy );
    vec3 color = light * ( detail.xyz * detail.www + ( 1.0 + -detail.w ) );
#else
    vec3 color = light;
#endif

#ifdef WORLD_GLOSS
    float gloss = tex2D_s3( uv.xy ).x;
#endif
#ifdef WORLD_VIEW
    vec3 toCamera4 = d3d9_ps_texcoord( in_Texcoord4 ).xyz;
    vec3 toCamera = normalize( toCamera4 );
#endif
#ifdef WORLD_SPECULAR
    vec3 specular =
        gPSLightSpecular.xyz * specularTerm( lightDirection, normal, toCamera,
                                             gPSLightSpecularParam.x );
#ifdef WORLD_SPECULAR1
    specular = gPSLightSpecular1.xyz *
                   specularTerm( d3d9_ps_texcoord( in_Texcoord3 ).xyz, normal,
                                 toCamera, gPSLightSpecularParam.x * 0.5 ) +
               specular;
#endif
#ifdef WORLD_GLOSS_SPECULAR
    specular = specular * gloss;
#endif
    color = color + specular;
#endif

#ifdef WORLD_REFLECTION
    vec4 reflectionColorPower = d3d9_ps_texcoord( in_Texcoord5 );
#ifdef WORLD_REFLECTION_CUBE
    float mirrorDotNormal = dot( normal, -toCamera );
    vec3 reflection =
        normal * -( mirrorDotNormal + mirrorDotNormal ) + -toCamera;
    vec3 environment =
        texCUBE_s4( vec3( dot( reflection, gPSWorld( 2 ).xyz ),
                          dot( reflection, gPSWorld( 1 ).xyz ),
                          dot( reflection, gPSWorld( 0 ).xyz ) ) )
            .xyz;
#else
    float viewDotNormal = dot( normal, toCamera );
    vec3 reflection = normal * -( viewDotNormal + viewDotNormal ) + toCamera;
    reflection = normalize( reflection );
    vec3 environment =
        tex2D_s4( vec2( dot( reflection, gPSWorldView( 0 ).xyz ),
                        dot( reflection, gPSWorldView( 1 ).xyz ) ) *
                      0.5 +
                  0.5 )
            .xyz;
#endif
    vec3 reflected =
        exp2( log2( abs( environment ) ) * reflectionColorPower.www ) *
        reflectionColorPower.xyz;
#ifdef WORLD_GLOSS_REFLECTION
    reflected = reflected * gloss;
#endif
    color = reflected + color;
#endif

    oC0 = vec4( tdu_apply_fog( color, fog ), 1.0 );
    d3d9_ps_alpha_test( oC0 );
}
