#version 450
#extension GL_GOOGLE_include_directive : require

// Vertex lit vehicle paint (table vehicle_paint).
//
// Permutations:
//   PAINT_RAMP               ramp colors (Color1) under the color map
//   PAINT_SPECULAR           vertex specular (Texcoord0), never masked
//   PAINT_COLOR_MAP          color map (s0, UV set 0)
//   PAINT_DETAIL_COLOR       the color map is a decal tinted by gPSDetailColor
//                            (alpha = coverage) over the ramp
//   PAINT_DECAL              decal map (s1, UV set 1) over the paint
//   PAINT_GLOSS              gloss map (s2) masking the reflection
//   PAINT_REFLECTION_CUBE    environment cube map (s3)
//   PAINT_REFLECTION_SPHERE  environment sphere map (s3)
//   PAINT_CAR_LIGHTS         car light mask (s4) and light map (s5), UV set 1
//                            when there is one
//   PAINT_UV1                UV set 1 (Texcoord4)
//   PAINT_ALPHA_COLOR        alpha from the color map
//   PAINT_ALPHA_GLOSS        alpha from the gloss map

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"
#include "tdu_vehicle.glsl"

#define gRampCarLights c.f[14]
#define gPSDetailColor c.f[19]

#if defined( PAINT_REFLECTION_CUBE ) || defined( PAINT_REFLECTION_SPHERE )
#define PAINT_REFLECTION
#endif

#ifdef PAINT_COLOR_MAP
D3D9_DECLARE_SAMPLER_2D( 0 ) // color map
#endif
#ifdef PAINT_DECAL
D3D9_DECLARE_SAMPLER_2D( 1 ) // decal map
#endif
#ifdef PAINT_GLOSS
D3D9_DECLARE_SAMPLER_2D( 2 ) // gloss map
#endif
#ifdef PAINT_REFLECTION_CUBE
D3D9_DECLARE_SAMPLER_CUBE( 3 ) // reflection
#elif defined( PAINT_REFLECTION_SPHERE )
D3D9_DECLARE_SAMPLER_2D( 3 ) // reflection
#endif
#ifdef PAINT_CAR_LIGHTS
D3D9_DECLARE_SAMPLER_2D( 4 ) // car light mask
D3D9_DECLARE_SAMPLER_2D( 5 ) // car light colors
#endif

#ifdef PAINT_RAMP
layout( location = D3D9_SLOT_COLOR1 ) centroid in vec4 in_Color1;
#endif
#ifdef PAINT_SPECULAR
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#endif
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
#ifdef PAINT_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
#endif
#ifdef PAINT_UV1
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
#endif
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 uvFog = d3d9_ps_texcoord( in_Texcoord1 );
    vec4 sunLight = d3d9_ps_texcoord( in_Texcoord5 );
#ifdef PAINT_UV1
    vec2 uv1 = d3d9_ps_texcoord( in_Texcoord4 ).xy;
#else
    vec2 uv1 = uvFog.xy;
#endif

#ifdef PAINT_RAMP
    vec3 ramp = in_Color1.xyz;
#endif
#ifdef PAINT_COLOR_MAP
    vec4 colorMap = tex2D_s0( uvFog.xy );
#ifdef PAINT_DETAIL_COLOR
    float coverage = gPSDetailColor.w * colorMap.w;
#ifdef PAINT_RAMP
    vec3 base = gPSDetailColor.xyz * colorMap.xyz * coverage +
                ( -gPSDetailColor.w * colorMap.w + 1.0 ) * ramp;
#else
    vec3 base = gPSDetailColor.xyz * colorMap.xyz * coverage +
                ( -gPSDetailColor.w * colorMap.w + 1.0 );
#endif
#elif defined( PAINT_RAMP )
    vec3 base = ramp * colorMap.xyz;
#else
    vec3 base = colorMap.xyz;
#endif
#elif defined( PAINT_RAMP )
    vec3 base = ramp;
#else
    vec3 base = vec3( 1.0 );
#endif
#ifdef PAINT_DECAL
    vec4 decal = tex2D_s1( uv1 );
    base = d3d9_lerp( base, decal.xyz, decal.www );
#endif

    // OTDU_ADDITION: night lamps join the vertex light.
    vec3 light =
        sunLight.xyz + tdu_lamp_pixel( in_ViewPosition, in_ViewNormal );
#ifdef PAINT_CAR_LIGHTS
    TDUCarLights carLights =
        tdu_car_lights( tex2D_s4( uv1 ), tex2D_s5( uv1 ), gRampCarLights );
    vec3 color = carLights.mask * ( light * base ) + carLights.emissive;
#else
    vec3 color = light * base;
#endif

#ifdef PAINT_GLOSS
    float gloss = tex2D_s2( uvFog.xy ).x;
#endif
#ifdef PAINT_REFLECTION
    vec4 fresnelPower = d3d9_ps_texcoord( in_Texcoord3 );
#ifdef PAINT_REFLECTION_CUBE
    vec3 environment = texCUBE_s3( d3d9_ps_texcoord( in_Texcoord2 ).xyz ).xyz;
#else
    vec3 environment = tex2D_s3( d3d9_ps_texcoord( in_Texcoord2 ).xy ).xyz;
#endif
    vec3 reflected = exp2( log2( abs( environment ) ) * fresnelPower.www ) *
                     fresnelPower.xyz;
#ifdef PAINT_GLOSS
    reflected = reflected * gloss;
#endif
    color = reflected + color;
#endif
#ifdef PAINT_SPECULAR
    color = color + d3d9_ps_texcoord( in_Texcoord0 ).xyz;
#endif

#if defined( PAINT_ALPHA_GLOSS )
    float alpha = sunLight.w * gloss;
#elif defined( PAINT_ALPHA_COLOR )
    float alpha = sunLight.w * colorMap.w;
#else
    float alpha = sunLight.w;
#endif
    oC0 = vec4( tdu_apply_fog( color, uvFog.z ), alpha );
    d3d9_ps_alpha_test( oC0 );
}
