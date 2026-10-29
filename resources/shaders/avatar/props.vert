#version 450
#extension GL_GOOGLE_include_directive : require

// Avatar props (avatar table): object space normal (Texcoord0), view
// vector (Texcoord1), light direction (Texcoord2), UV sets 0 / 1
// (Texcoord3), reflection (Texcoord4) and the ambient / diffuse / specular
// light colors (Texcoord5..7), lit per pixel.
//
// Permutations:
//   AVATAR_SKINNED       4 bone skinning
//   AVATAR_VERTEX_COLOR  forwards Color0
//   AVATAR_UV0 / UV1     forwards UV set 0 / 1
//   AVATAR_UV0_GLOSS     UV set 0 goes to the gloss map slot (Texcoord3.zw)
//   AVATAR_VIEW          view vector
//   AVATAR_ENVMAP        world space reflection vector (cube map)
//   AVATAR_SPHEREMAP     view space reflection, sphere map coordinates
//   AVATAR_CAR_LIGHTING  light colors attenuated by gVSAttenuationFactor
//                        below the car interior height range
//                        (gVSCarIntegratorFactor.xy)

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"
#include "tdu_avatar.glsl"

#define gWorldViewProj( i ) D3D9_CR( 8, i )
#define gVSWorld( i ) D3D9_CR( 16, i )
#define gWorldView( i ) D3D9_CR( 20, i )
#define gVSLightDir D3D9_C( 93 )
#define gVSCamPosInObj D3D9_C( 119 )

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_NORMAL0 ) in vec4 in_Normal0;
#ifdef AVATAR_SKINNED
layout( location = D3D9_IN_BLENDINDICES0 ) in vec4 in_BlendIndices0;
layout( location = D3D9_IN_BLENDWEIGHT0 ) in vec4 in_BlendWeight0;
#endif
#ifdef AVATAR_VERTEX_COLOR
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0;
#endif
#ifdef AVATAR_UV0
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#endif
#ifdef AVATAR_UV1
layout( location = D3D9_IN_TEXCOORD( 1 ) ) in vec4 in_Texcoord1;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
#ifdef AVATAR_VIEW
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
#endif
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
#ifdef AVATAR_UV0
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
#endif
#if defined( AVATAR_ENVMAP ) || defined( AVATAR_SPHEREMAP )
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
#endif
layout( location = D3D9_SLOT_TEXCOORD5 ) out vec4 out_Texcoord5;
layout( location = D3D9_SLOT_TEXCOORD6 ) out vec4 out_Texcoord6;
layout( location = D3D9_SLOT_TEXCOORD7 ) out vec4 out_Texcoord7;

layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) out vec4 out_ViewNormal;     // OTDU

void main() {
#ifdef AVATAR_SKINNED
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

    out_Texcoord0 = vec4( normal, 0.0 );
    out_Texcoord2 = vec4( -gVSLightDir.xyz, 0.0 );
#if defined( AVATAR_VIEW ) || defined( AVATAR_ENVMAP ) ||                      \
    defined( AVATAR_SPHEREMAP )
    vec3 toCamera = -position.xyz + gVSCamPosInObj.xyz;
#endif
#ifdef AVATAR_VIEW
    out_Texcoord1 = vec4( toCamera, 0.0 );
#endif
#if defined( AVATAR_ENVMAP )
    float cosView = d3d9_dot( -toCamera, normal );
    vec3 reflected =
        d3d9_normalize( normal * -( cosView + cosView ) + -toCamera );
    out_Texcoord4 = vec4( d3d9_dot( reflected, gVSWorld( 0 ).xyz ),
                          d3d9_dot( reflected, gVSWorld( 1 ).xyz ),
                          d3d9_dot( reflected, gVSWorld( 2 ).xyz ), 0.0 );
#elif defined( AVATAR_SPHEREMAP )
    float cosView = d3d9_dot( toCamera, normal );
    vec3 reflected =
        d3d9_normalize( normal * -( cosView + cosView ) + toCamera );
    out_Texcoord4 = vec4(
        d3d9_dot( reflected, gWorldView( 0 ).xyz ) * 0.5 + 0.5,
        d3d9_dot( reflected, gWorldView( 1 ).xyz ) * 0.5 + 0.5, 0.0, 0.0 );
#endif
#if defined( AVATAR_UV1 )
    out_Texcoord3 = vec4( in_Texcoord0.xy, in_Texcoord1.xy );
#elif defined( AVATAR_UV0_GLOSS )
    out_Texcoord3 = in_Texcoord0.xxxy * vec4( 0.0, 0.0, 1.0, 1.0 );
#elif defined( AVATAR_UV0 )
    out_Texcoord3 = in_Texcoord0.xyxx * vec4( 1.0, 1.0, 0.0, 0.0 );
#endif

#ifdef AVATAR_CAR_LIGHTING
    float inside = tdu_avatar_car_factor( position.y );
    out_Texcoord5 =
        vec4( tdu_avatar_car_light( gVSLightAmbient.xyz, gVSAttenuationFactor.x,
                                    inside ),
              gVSLightAmbient.w );
    out_Texcoord6 =
        vec4( tdu_avatar_car_light( gVSLightDiffuse.xyz, gVSAttenuationFactor.y,
                                    inside ),
              gVSLightDiffuse.w );
    out_Texcoord7 =
        vec4( tdu_avatar_car_light( gVSLightSpecular.xyz,
                                    gVSAttenuationFactor.z, inside ),
              gVSLightSpecular.w );
#if defined( AVATAR_ENVMAP ) || defined( AVATAR_SPHEREMAP )
    out_Texcoord4.w = gVSAttenuationFactor.w;
#endif
#else
    out_Texcoord5 = gVSLightAmbient;
    out_Texcoord6 = gVSLightDiffuse;
    out_Texcoord7 = gVSLightSpecular;
#endif

    // OTDU_ADDITION: night lamps, attenuated like the diffuse light in the car.
#ifdef AVATAR_CAR_LIGHTING
    float lampTint =
        tdu_avatar_car_light( vec3( 1.0 ), gVSAttenuationFactor.y, inside ).x;
#else
    float lampTint = 1.0;
#endif
    tdu_avatar_lamp_varyings( position, normal, lampTint, out_ViewPosition,
                              out_ViewNormal );

#ifdef AVATAR_VERTEX_COLOR
    out_Color0 = clamp( in_Color0, vec4( 0.0 ), vec4( 1.0 ) );
#else
    out_Color0 = vec4( 1.0 );
#endif
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
