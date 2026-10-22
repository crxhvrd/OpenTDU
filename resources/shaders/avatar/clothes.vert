#version 450
#extension GL_GOOGLE_include_directive : require

// Avatar clothes (avatar table), vertex lit: diffuse + ambient light and
// the sun specular intensity (Texcoord0), UV set 0 (Texcoord1), the layer
// UV sets (Texcoord2..4), the detail UV set (Texcoord5), the specular color
// (Texcoord6) and the fog (Texcoord7.x).
//
// Permutations:
//   CLOTHES_SKINNED  4 bone skinning
//   CLOTHES_LAYER1   layer 1 from the next UV set, scaled by gVSUVScale.xy
//   CLOTHES_LAYER2   layer 2 from the next UV set
//   CLOTHES_LAYER3   layer 3 from the next UV set
//   CLOTHES_DETAIL   detail map: UV set 0 scaled by gVSUVScale.zw

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"
#include "tdu_avatar.glsl"

#define gWorldViewProj( i ) c.f[8 + ( i )]
#define gWorldView( i ) c.f[20 + ( i )]
#define gVSLightDir c.f[93]
#define gVSLightSpecularParam c.f[98]
#define gVSCamPosInObj c.f[119]
#define gVSUVScale c.f[124]

layout( location = 0 ) in vec4 in_Position0;
layout( location = 1 ) in vec4 in_Normal0;
#ifdef CLOTHES_SKINNED
layout( location = 2 ) in vec4 in_BlendIndices0;
layout( location = 3 ) in vec4 in_BlendWeight0;
#define UV_LOCATION 4
#else
#define UV_LOCATION 2
#endif
layout( location = UV_LOCATION ) in vec4 in_Texcoord0;
#ifdef CLOTHES_LAYER1
#define LAYER2_UV 2
layout( location = UV_LOCATION + 1 ) in vec4 in_Texcoord1;
#else
#define LAYER2_UV 1
#endif
#ifdef CLOTHES_LAYER2
#define LAYER3_UV ( LAYER2_UV + 1 )
layout( location = UV_LOCATION + LAYER2_UV ) in vec4 in_LayerUV2;
#else
#define LAYER3_UV LAYER2_UV
#endif
#ifdef CLOTHES_LAYER3
layout( location = UV_LOCATION + LAYER3_UV ) in vec4 in_LayerUV3;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
#ifdef CLOTHES_LAYER1
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
#endif
#ifdef CLOTHES_LAYER2
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
#endif
#ifdef CLOTHES_LAYER3
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
#endif
#ifdef CLOTHES_DETAIL
layout( location = D3D9_SLOT_TEXCOORD5 ) out vec4 out_Texcoord5;
#endif
layout( location = D3D9_SLOT_TEXCOORD6 ) out vec4 out_Texcoord6;
layout( location = D3D9_SLOT_TEXCOORD7 ) out vec4 out_Texcoord7;

layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) out vec4 out_ViewNormal;     // OTDU

void main() {
#ifdef CLOTHES_SKINNED
    ivec4 bones = tdu_skin_bones( in_BlendIndices0 );
    vec4 weights = tdu_skin_weights( in_BlendWeight0 );
    vec4 position = tdu_skin_position( in_Position0, bones, weights );
    vec3 normal = tdu_skin_normal( in_Normal0.xyz, bones, weights );
#else
    vec4 position = in_Position0;
    vec3 normal = in_Normal0.xyz;
#endif
    gl_Position = vec4( d3d9_dot( position, gWorldViewProj( 0 ) ),
                        d3d9_dot( position, gWorldViewProj( 1 ) ),
                        d3d9_dot( position, gWorldViewProj( 2 ) ),
                        d3d9_dot( position, gWorldViewProj( 3 ) ) );

    float inside = tdu_avatar_car_factor( position.y );
    vec3 ambient = tdu_avatar_car_light( gVSLightAmbient.xyz,
                                         gVSAttenuationFactor.x, inside );
    vec3 diffuse = tdu_avatar_car_light( gVSLightDiffuse.xyz,
                                         gVSAttenuationFactor.y, inside );
    vec3 specularColor = tdu_avatar_car_light( gVSLightSpecular.xyz,
                                               gVSAttenuationFactor.z, inside );

    float cosLight = d3d9_dot( normal, -gVSLightDir.xyz );
    float specularMask = min( max( cosLight * 4.0, 0.0 ), 1.0 );
    vec4 toCamera = -position + gVSCamPosInObj;
    vec3 view =
        toCamera.xyz * inversesqrt( abs( d3d9_dot( toCamera, toCamera ) ) );
    float cosReflected = d3d9_dot( gVSLightDir.xyz, normal );
    vec3 reflected =
        normal * -( cosReflected + cosReflected ) + gVSLightDir.xyz;
    float specular =
        specularMask * pow( abs( max( d3d9_dot( reflected, view ), 0.0 ) ),
                            gVSLightSpecularParam.y );
    out_Texcoord0 = vec4( diffuse * min( max( cosLight, 0.0 ), 1.0 ) + ambient,
                          specular * gVSLightSpecularParam.x );
    out_Texcoord6 = vec4( specularColor, gVSLightSpecular.w );

    vec3 viewPosition = vec3( d3d9_dot( position, gWorldView( 0 ) ),
                              d3d9_dot( position, gWorldView( 1 ) ),
                              d3d9_dot( position, gWorldView( 2 ) ) );
    float viewDistance =
        1.0 / inversesqrt( abs( d3d9_dot( viewPosition, viewPosition ) ) );
    out_Texcoord7 = vec4( tdu_vertex_fog( viewDistance ), 0.0, 0.0, 0.0 );

    // OTDU_ADDITION: night lamps, attenuated like the diffuse light in the car.
    tdu_avatar_lamp_varyings(
        position, normal,
        tdu_avatar_car_light( vec3( 1.0 ), gVSAttenuationFactor.y, inside ).x,
        out_ViewPosition, out_ViewNormal );

    out_Texcoord1 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
#ifdef CLOTHES_LAYER1
    out_Texcoord2 = vec4( in_Texcoord1.xy * gVSUVScale.xy, 0.0, 0.0 );
#endif
#ifdef CLOTHES_LAYER2
    out_Texcoord3 = vec4( in_LayerUV2.xy, 0.0, 0.0 );
#endif
#ifdef CLOTHES_LAYER3
    out_Texcoord4 = vec4( in_LayerUV3.xy, 0.0, 0.0 );
#endif
#ifdef CLOTHES_DETAIL
    out_Texcoord5 = vec4( in_Texcoord0.xy * gVSUVScale.zw, 0.0, 0.0 );
#endif
    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
