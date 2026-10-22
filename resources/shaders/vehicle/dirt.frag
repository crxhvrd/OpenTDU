#version 450
#extension GL_GOOGLE_include_directive : require

// Vehicle dirt layer (table vehicle_dirt): the dirt map (s1) covers the base
// by the sun facing amount.
//
// Permutations:
//   DIRT_RAMP        the base blends the ramp colors and the color map
//   DIRT_OPAQUE      alpha 1 (else the dirt adds to the color map alpha)
//   DIRT_REFLECTION  environment cube map (s3) with the vertex fresnel

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

D3D9_DECLARE_SAMPLER_2D( 0 ) // color map
D3D9_DECLARE_SAMPLER_2D( 1 ) // dirt map
#ifdef DIRT_REFLECTION
D3D9_DECLARE_SAMPLER_CUBE( 3 ) // reflection
#endif

layout( location = D3D9_SLOT_COLOR0 ) centroid in vec4 in_Color0;
#ifdef DIRT_RAMP
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
#endif
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
#ifdef DIRT_REFLECTION
layout( location = D3D9_SLOT_TEXCOORD5 ) in vec4 in_Texcoord5;
layout( location = D3D9_SLOT_TEXCOORD6 ) in vec4 in_Texcoord6;
#endif
layout( location = OTDU_SLOT_VIEW_POSITION ) in vec4 in_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) in vec4 in_ViewNormal;     // OTDU
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 uvParams = d3d9_ps_texcoord( in_Texcoord2 );
    vec4 fogDirt = d3d9_ps_texcoord( in_Texcoord4 );

    vec4 dirt = tex2D_s1( d3d9_ps_texcoord( in_Texcoord3 ).xy );
    vec4 albedo = tex2D_s0( uvParams.xy );
    float bare = 1.0 + -albedo.w;
    float coverage = fogDirt.y * dirt.x * in_Color0.w;
#ifdef DIRT_RAMP
    vec3 base =
        bare * d3d9_ps_texcoord( in_Texcoord0 ).xyz + albedo.xyz * albedo.w;
#else
    vec3 base = albedo.xyz * albedo.w + bare;
#endif
    vec3 surface =
        ( coverage * bare ) * dirt.xyz + ( coverage * -bare + 1.0 ) * base;

    // OTDU_ADDITION: night lamps join the vertex light.
    vec3 color = surface * ( in_Color0.xyz +
                             tdu_lamp_pixel( in_ViewPosition, in_ViewNormal ) );
#ifdef DIRT_REFLECTION
    vec4 colorFresnel = d3d9_ps_texcoord( in_Texcoord6 );
    color = uvParams.z *
                texCUBE_s3( d3d9_ps_texcoord( in_Texcoord5 ).xyz ).xyz *
                colorFresnel.xyz * colorFresnel.w +
            color;
#endif

#ifdef DIRT_OPAQUE
    float alpha = 1.0;
#else
    float alpha = min( ( coverage * bare ) * fogDirt.z + albedo.w, 1.0 );
#endif
    oC0 = vec4( tdu_apply_fog( color, fogDirt.x ), alpha );
    d3d9_ps_alpha_test( oC0 );
}
