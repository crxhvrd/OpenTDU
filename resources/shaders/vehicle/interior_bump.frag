#version 450
#extension GL_GOOGLE_include_directive : require

// Bump mapped vehicle interior (VehicleInterior): one light, normal map (s2).
//
// Permutations:
//   INTERIOR_OBJECT_SPACE  object space light (Texcoord3) and UVs (Texcoord5);
//                          else tangent space light (Texcoord4, normalized)
//                          and UVs (Texcoord3)
//   INTERIOR_DETAIL        detail map (s1) blended over the base color
//   INTERIOR_SHADOW        baked shadow map (s4)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gPSLightAmbient c.f[0]
#define gPSLightDiffuse c.f[1]
#define gPSBaseColor c.f[15]

#ifdef INTERIOR_DETAIL
D3D9_DECLARE_SAMPLER_2D( 1 ) // detail map
#endif
D3D9_DECLARE_SAMPLER_2D( 2 ) // normal map
#ifdef INTERIOR_SHADOW
D3D9_DECLARE_SAMPLER_2D( 4 ) // baked shadow map
#endif

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
#ifdef INTERIOR_OBJECT_SPACE
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
#else
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
#endif
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS ) in vec4 in_ViewBasisX;      // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS + 1 ) in vec4 in_ViewBasisY;  // OTDU
layout( location = OTDU_SLOT_VIEW_BASIS + 2 ) in vec4 in_ViewBasisZ;  // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
#ifdef INTERIOR_OBJECT_SPACE
    vec2 uv = d3d9_ps_texcoord( in_Texcoord5 ).xy;
    vec3 lightDirection = d3d9_ps_texcoord( in_Texcoord3 ).xyz;
#else
    vec2 uv = d3d9_ps_texcoord( in_Texcoord3 ).xy;
    vec3 lightDirection = normalize( d3d9_ps_texcoord( in_Texcoord4 ).xyz );
#endif

    vec3 normal = tex2D_s2( uv ).xyz + vec3( -0.5 );
    normal = normalize( normal + normal );
    float diffuse = max( dot( normal, -lightDirection ), 0.0 );

    // OTDU_ADDITION: night lamps join the sun light, occluded by the vertex
    // color and the baked shadow map like it.
    vec3 lamps = tdu_lamp_pixel_mapped( in_ViewPosition, in_ViewBasisX,
                                        in_ViewBasisY, in_ViewBasisZ, normal );
    vec3 light =
        ( gPSLightDiffuse.xyz * diffuse + gPSLightAmbient.xyz + lamps ) *
        in_Color0.xyz;
#ifdef INTERIOR_DETAIL
    vec4 detail = tex2D_s1( uv );
    vec3 color = light * d3d9_lerp( gPSBaseColor.xyz, detail.xyz, detail.www );
#else
    vec3 color = light * gPSBaseColor.xyz;
#endif
#ifdef INTERIOR_SHADOW
    color = color * tex2D_s4( uv ).xyz;
#endif

    oC0 = vec4( color, 1.0 );
    d3d9_ps_alpha_test( oC0 );
}
