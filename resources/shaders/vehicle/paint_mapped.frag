#version 450
#extension GL_GOOGLE_include_directive : require

// Normal mapped vehicle paint (tables vehicle_paint_normalmapped and
// vehicle_paint_normalmapped2): per pixel sun light, ramp colors and
// reflection from the map normal (s1).
//
// Permutations:
//   PAINTMAP_TANGENT_SPACE     tangent space map: normalized light vector
//   PAINTMAP_RAMP              per pixel ramp colors by facing
//   PAINTMAP_COLOR_DECAL       color map (s0) blended by its alpha
//   PAINTMAP_COLOR_MODULATE    color map (s0) multiplied
//   PAINTMAP_GLOSS             gloss map (s2) masking the reflection
//   PAINTMAP_REFLECTION_CUBE   environment cube map (s3), world space
//   PAINTMAP_REFLECTION_SPHERE environment sphere map (s3), view space
//   PAINTMAP_CAR_LIGHTS        car light mask (s4) and light map (s5)
//   PAINTMAP_SPECULAR          sun specular
//   PAINTMAP_ALPHA_COLOR       alpha from the color map
//   PAINTMAP_ALPHA_GLOSS       alpha from the gloss map

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"
#include "tdu_vehicle.glsl"

#define gPSLightAmbient c.f[0]
#define gPSLightDiffuse c.f[1]
#define gPSLightSpecular c.f[2]
#define gPSLightSpecularParam c.f[3]
#define gPSWorld( i ) c.f[8 + ( i )]
#define gPSWorldView( i ) c.f[8 + ( i )]
#define gRampCarLights c.f[14]
#define gPSRampColor1 c.f[15]
#define gPSRampColor2 c.f[16]
#define gPSRampColorPow c.f[17]

#if defined( PAINTMAP_REFLECTION_CUBE ) || defined( PAINTMAP_REFLECTION_SPHERE )
#define PAINTMAP_REFLECTION
#endif
#if defined( PAINTMAP_COLOR_DECAL ) || defined( PAINTMAP_COLOR_MODULATE )
#define PAINTMAP_COLOR_MAP
#endif
#if defined( PAINTMAP_RAMP ) || defined( PAINTMAP_REFLECTION ) ||              \
    defined( PAINTMAP_SPECULAR )
#define PAINTMAP_VIEW
#endif

#ifdef PAINTMAP_COLOR_MAP
D3D9_DECLARE_SAMPLER_2D( 0 ) // color map
#endif
D3D9_DECLARE_SAMPLER_2D( 1 ) // normal map
#ifdef PAINTMAP_GLOSS
D3D9_DECLARE_SAMPLER_2D( 2 ) // gloss map
#endif
#ifdef PAINTMAP_REFLECTION_CUBE
D3D9_DECLARE_SAMPLER_CUBE( 3 ) // reflection
#elif defined( PAINTMAP_REFLECTION_SPHERE )
D3D9_DECLARE_SAMPLER_2D( 3 ) // reflection
#endif
#ifdef PAINTMAP_CAR_LIGHTS
D3D9_DECLARE_SAMPLER_2D( 4 ) // car light mask
D3D9_DECLARE_SAMPLER_2D( 5 ) // car light colors
#endif

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#ifdef PAINTMAP_VIEW
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
#endif
#ifdef PAINTMAP_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
#endif
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS ) in vec4 in_ViewBasisX;      // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS + 1 ) in vec4 in_ViewBasisY;  // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS + 2 ) in vec4 in_ViewBasisZ;  // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec2 uv = d3d9_ps_texcoord( in_Texcoord0 ).xy;
#ifdef PAINTMAP_TANGENT_SPACE
    vec3 lightDirection = normalize( d3d9_ps_texcoord( in_Texcoord1 ).xyz );
#else
    vec3 lightDirection = d3d9_ps_texcoord( in_Texcoord1 ).xyz;
#endif
    float fog = d3d9_ps_texcoord( in_Texcoord2 ).x;

    vec3 mapNormal = tex2D_s1( uv ).xyz + vec3( -0.5 );
    vec3 normal = normalize( mapNormal + mapNormal );
#ifdef PAINTMAP_VIEW
    vec3 toCamera = normalize( d3d9_ps_texcoord( in_Texcoord3 ).xyz );
    float viewDotNormal = dot( normal, toCamera );
#endif
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

#ifdef PAINTMAP_RAMP
    float facing = pow( abs( max( viewDotNormal, 0.0 ) ), gPSRampColorPow.x );
    vec3 base =
        ( 1.0 + -facing ) * gPSRampColor2.xyz + gPSRampColor1.xyz * facing;
#else
    vec3 base = vec3( 1.0 );
#endif
#ifdef PAINTMAP_COLOR_MAP
    vec4 colorMap = tex2D_s0( uv );
#ifdef PAINTMAP_COLOR_DECAL
    base = d3d9_lerp( base, colorMap.xyz, colorMap.www );
#else
    base = base * colorMap.xyz;
#endif
#endif

#ifdef PAINTMAP_CAR_LIGHTS
    TDUCarLights carLights =
        tdu_car_lights( tex2D_s4( uv ), tex2D_s5( uv ), gRampCarLights );
    vec3 color = carLights.mask * ( light * base ) + carLights.emissive;
#else
    vec3 color = light * base;
#endif

#ifdef PAINTMAP_GLOSS
    float gloss = tex2D_s2( uv ).x;
#endif
#ifdef PAINTMAP_REFLECTION
    vec4 reflectionColorPower = d3d9_ps_texcoord( in_Texcoord4 );
#ifdef PAINTMAP_REFLECTION_CUBE
    float mirrorDotNormal = dot( normal, -toCamera );
    vec3 reflection =
        normal * -( mirrorDotNormal + mirrorDotNormal ) + -toCamera;
    vec3 environment =
        texCUBE_s3( vec3( dot( reflection, gPSWorld( 2 ).xyz ),
                          dot( reflection, gPSWorld( 1 ).xyz ),
                          dot( reflection, gPSWorld( 0 ).xyz ) ) )
            .xyz;
#else
    vec3 reflection =
        normalize( normal * -( viewDotNormal + viewDotNormal ) + toCamera );
    vec3 environment =
        tex2D_s3( vec2( dot( reflection, gPSWorldView( 0 ).xyz ),
                        dot( reflection, gPSWorldView( 1 ).xyz ) ) *
                      0.5 +
                  0.5 )
            .xyz;
#endif
    vec3 reflected =
        exp2( log2( abs( environment ) ) * reflectionColorPower.www ) *
        reflectionColorPower.xyz;
#ifdef PAINTMAP_GLOSS
    reflected = reflected * gloss;
#endif
    color = reflected + color;
#endif
#ifdef PAINTMAP_SPECULAR
    float lightDotNormal = dot( normal, lightDirection );
    vec3 lightReflection =
        ( normal + normal ) * -lightDotNormal + lightDirection;
    color = gPSLightSpecular.xyz *
                pow( abs( max( dot( toCamera, lightReflection ), 0.0 ) ),
                     gPSLightSpecularParam.x ) +
            color;
#endif

#if defined( PAINTMAP_ALPHA_GLOSS )
    float alpha = gloss;
#elif defined( PAINTMAP_ALPHA_COLOR )
    float alpha = colorMap.w;
#else
    float alpha = 1.0;
#endif
    oC0 = vec4( tdu_apply_fog( color, fog ), alpha );
    d3d9_ps_alpha_test( oC0 );
}
