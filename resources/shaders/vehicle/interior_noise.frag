#version 450
#extension GL_GOOGLE_include_directive : require

// Vehicle interior (VehicleInterior) with a brushed specular: the tangent is
// rotated by a noise map angle and lit by two lights (anisotropic highlight,
// the second light at half the exponent).
//
// Permutations:
//   INTERIOR_NORMAL_MAP  object space normal map (s2), tangent frame made
//                        orthogonal to it; gloss and shadow maps move to s3/s4
//   INTERIOR_DETAIL      detail map (s1) blended over the base color
//   INTERIOR_GLOSS       gloss map
//   INTERIOR_SHADOW      baked shadow map

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gPSLightAmbient c.f[0]
#define gPSLightDiffuse c.f[1]
#define gPSLightSpecular c.f[2]
#define gPSBaseColor c.f[15]
#define gPSGlossColor c.f[16]
#define gPSNoiseRate c.f[17]
#define gPSSpecularParam c.f[22]

D3D9_DECLARE_SAMPLER_2D( 0 ) // noise map
#ifdef INTERIOR_DETAIL
D3D9_DECLARE_SAMPLER_2D( 1 ) // detail map
#endif
#ifdef INTERIOR_NORMAL_MAP
D3D9_DECLARE_SAMPLER_2D( 2 ) // normal map
#define tex2D_gloss tex2D_s3
#define tex2D_shadow tex2D_s4
#ifdef INTERIOR_GLOSS
D3D9_DECLARE_SAMPLER_2D( 3 )
#endif
#ifdef INTERIOR_SHADOW
D3D9_DECLARE_SAMPLER_2D( 4 )
#endif
#else
#define tex2D_gloss tex2D_s2
#define tex2D_shadow tex2D_s3
#ifdef INTERIOR_GLOSS
D3D9_DECLARE_SAMPLER_2D( 2 )
#endif
#ifdef INTERIOR_SHADOW
D3D9_DECLARE_SAMPLER_2D( 3 )
#endif
#endif

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS ) in vec4 in_ViewBasisX;      // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS + 1 ) in vec4 in_ViewBasisY;  // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS + 2 ) in vec4 in_ViewBasisZ;  // OTDU
layout( location = 0 ) out vec4 oC0;

// sqrt( 1 - x * x ) as the retail rcp( rsq( ) ).
float sine( float cosine ) {
    return 1.0 / inversesqrt( abs( cosine * -cosine + 1.0 ) );
}

void main() {
    vec4 t0 = d3d9_ps_texcoord( in_Texcoord0 );
    vec4 t1 = d3d9_ps_texcoord( in_Texcoord1 );
    vec4 t2 = d3d9_ps_texcoord( in_Texcoord2 );
#ifdef INTERIOR_NORMAL_MAP
    vec4 uvNoise = d3d9_ps_texcoord( in_Texcoord5 );
    vec3 light0 = d3d9_ps_texcoord( in_Texcoord3 ).xyz;
    vec3 light1 = d3d9_ps_texcoord( in_Texcoord4 ).xyz;
#else
    vec4 uvNoise = d3d9_ps_texcoord( in_Texcoord3 );
    vec3 light0 = d3d9_ps_texcoord( in_Texcoord4 ).xyz;
    vec3 light1 = d3d9_ps_texcoord( in_Texcoord5 ).xyz;
#endif

    float angle =
        fract( tex2D_s0( uvNoise.zw ).x * gPSNoiseRate.x * 0.15915494 + 0.5 ) *
            6.2831855 +
        -3.1415927;
    vec2 cosSin = vec2( cos( angle ), sin( angle ) );

#ifdef INTERIOR_NORMAL_MAP
    vec3 normal = tex2D_s2( uvNoise.xy ).xyz + vec3( -0.5 );
    normal = normalize( normal + normal );
    vec3 binormal = normalize( normal * -dot( normal, t0.xyz ) + t0.xyz );
    vec3 tangent = normalize( normal * -dot( normal, t1.xyz ) + t1.xyz );
    vec3 brushed = cosSin.yyy * tangent + cosSin.xxx * binormal;
    float diffuse = max( dot( -light0, normal ), 0.0 );
#else
    vec3 brushed = cosSin.yyy * t0.xyz + cosSin.xxx * t1.xyz;
    float diffuse = in_Color0.w;
#endif

    // Kajiya-Kay style: cos and sin of the angles to the brushed direction.
    float viewCos = dot( normalize( t2.xyz ), brushed );
    float viewSin = sine( viewCos );
    float light0Cos = dot( -light0, brushed );
    float light1Cos = dot( -light1, brushed );
    float highlight0 =
        d3d9_saturate( -viewCos * light0Cos + viewSin * sine( light0Cos ) );
    float highlight1 =
        d3d9_saturate( -viewCos * light1Cos + viewSin * sine( light1Cos ) );
    float specular = pow( abs( highlight1 ), 0.5 * gPSSpecularParam.y ) +
                     pow( abs( highlight0 ), gPSSpecularParam.y );

    float ambientLuminance =
        dot( gPSLightAmbient.xyz, vec3( 0.299, 0.587, 0.114 ) );
    vec3 gloss = specular * gPSGlossColor.xyz * gPSSpecularParam.xxx *
                 gPSLightSpecular.xyz * ( diffuse + ambientLuminance );
#ifdef INTERIOR_GLOSS
    gloss = gloss * tex2D_gloss( uvNoise.xy ).xyz;
#endif

    // OTDU_ADDITION: night lamps join the sun light, occluded by the vertex
    // color and the baked shadow map like it.
#ifdef INTERIOR_NORMAL_MAP
    vec3 lamps = tdu_lamp_pixel_mapped( in_ViewPosition, in_ViewBasisX,
                                        in_ViewBasisY, in_ViewBasisZ, normal );
#else
    vec3 lamps = tdu_lamp_pixel( in_ViewPosition, in_ViewBasisX );
#endif
    vec3 light =
        ( gPSLightDiffuse.xyz * diffuse + gPSLightAmbient.xyz + lamps ) *
        in_Color0.xyz;
#ifdef INTERIOR_DETAIL
    vec4 detail = tex2D_s1( uvNoise.xy );
    vec3 base = d3d9_lerp( gPSBaseColor.xyz, detail.xyz, detail.www );
#else
    vec3 base = gPSBaseColor.xyz;
#endif
    vec3 color = light * base;
#ifdef INTERIOR_SHADOW
    color = color * tex2D_shadow( uvNoise.xy ).xyz + gloss;
#else
    color = color + gloss;
#endif

    oC0 = vec4( color, 1.0 );
    d3d9_ps_alpha_test( oC0 );
}
