#version 450
#extension GL_GOOGLE_include_directive : require

// Avatar skin (avatar table): light direction and wrap (Texcoord0),
// skinned normal (Texcoord1), view vector and fog (Texcoord2), UV set 0
// and the detail UV set (Texcoord3), ambient / diffuse / specular light
// colors (Texcoord4..6), lit per pixel.
//
// Permutations:
//   SKIN_VERTEX_COLOR  forwards Color0

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"
#include "tdu_avatar.glsl"

#define gWorldViewProj( i ) c.f[8 + ( i )]
#define gWorldView( i ) c.f[20 + ( i )]
#define gVSLightDir c.f[93]
#define gVSCamPosInObj c.f[119]
#define gVSUVScale c.f[124]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
#ifdef SKIN_VERTEX_COLOR
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
#endif
layout( location = D3D9_IN_BLENDINDICES0 ) in vec4 in_BlendIndices0;
layout( location = D3D9_IN_BLENDWEIGHT0 ) in vec4 in_BlendWeight0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
layout( location = D3D9_SLOT_TEXCOORD5 ) out vec4 out_Texcoord5;
layout( location = D3D9_SLOT_TEXCOORD6 ) out vec4 out_Texcoord6;

layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) out vec4 out_ViewNormal;     // OTDU

void main() {
    ivec4 bones = tdu_skin_bones( in_BlendIndices0 );
    vec4 weights = tdu_skin_weights( in_BlendWeight0 );
    vec4 position = tdu_skin_position( in_Position0, bones, weights );
    gl_Position = vec4( d3d9_dot( position, gWorldViewProj( 0 ) ),
                        d3d9_dot( position, gWorldViewProj( 1 ) ),
                        d3d9_dot( position, gWorldViewProj( 2 ) ),
                        d3d9_dot( position, gWorldViewProj( 3 ) ) );
    vec3 normal = tdu_skin_normal( in_Normal0.xyz, bones, weights );
    out_Texcoord1 = vec4( normal, 0.0 );

    vec3 viewPosition = vec3( d3d9_dot( position, gWorldView( 0 ) ),
                              d3d9_dot( position, gWorldView( 1 ) ),
                              d3d9_dot( position, gWorldView( 2 ) ) );
    float viewDistance =
        1.0 / inversesqrt( abs( d3d9_dot( viewPosition, viewPosition ) ) );
    out_Texcoord2 = vec4( d3d9_normalize( -position.xyz + gVSCamPosInObj.xyz ),
                          tdu_vertex_fog( viewDistance ) );

    out_Texcoord0 = vec4( -gVSLightDir.xyz, gVSUVScale.z );
    out_Texcoord3 = vec4( in_Texcoord0.xy, in_Texcoord0.xy * gVSUVScale.xy );

    float inside = tdu_avatar_car_factor( position.y );
    out_Texcoord4 =
        vec4( tdu_avatar_car_light( gVSLightAmbient.xyz, gVSAttenuationFactor.x,
                                    inside ),
              gVSLightAmbient.w );
    out_Texcoord5 =
        vec4( tdu_avatar_car_light( gVSLightDiffuse.xyz, gVSAttenuationFactor.y,
                                    inside ),
              gVSLightDiffuse.w );
    out_Texcoord6 =
        vec4( tdu_avatar_car_light( gVSLightSpecular.xyz,
                                    gVSAttenuationFactor.z, inside ),
              gVSLightSpecular.w );

    // OTDU_ADDITION: night lamps, attenuated like the diffuse light in the car.
    tdu_avatar_lamp_varyings(
        position, normal,
        tdu_avatar_car_light( vec3( 1.0 ), gVSAttenuationFactor.y, inside ).x,
        out_ViewPosition, out_ViewNormal );

#ifdef SKIN_VERTEX_COLOR
    out_Color0 = clamp( in_Color0, vec4( 0.0 ), vec4( 1.0 ) );
#else
    out_Color0 = vec4( 1.0 );
#endif
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
