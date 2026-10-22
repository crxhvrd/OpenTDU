#version 450
#extension GL_GOOGLE_include_directive : require

// Avatar eyes (avatar table): the lighting lookup coordinates from the
// normal in head space, the sun diffuse term and the fog (Texcoord0), iris
// UV set (offset by gVSUVOffset) and UV set 1 (Texcoord1), view space
// reflection as sphere map coordinates (Texcoord2), and the ambient /
// diffuse / specular light (Texcoord4..6).
//
// Permutations:
//   EYE_ENVMAP     world space reflection vector (Texcoord3, cube map)
//   EYE_SPHEREMAP  Texcoord3 repeats the sphere map coordinates

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"
#include "tdu_avatar.glsl"

#define gWorldViewProj( i ) c.f[8 + ( i )]
#define gVSWorld( i ) c.f[16 + ( i )]
#define gWorldView( i ) c.f[20 + ( i )]
#define gVSLightDir c.f[93]
#define gVSCamPosInObj c.f[119]
#define gVSUVOffset c.f[124]
#define gVSHeadUp c.f[244]
#define gVSHeadRight c.f[245]

layout( location = 0 ) in vec4 in_Position0;
layout( location = 1 ) in vec4 in_Normal0;
layout( location = 2 ) in vec4 in_BlendIndices0;
layout( location = 3 ) in vec4 in_BlendWeight0;
layout( location = 4 ) in vec4 in_Texcoord0;
layout( location = 5 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
#if defined( EYE_ENVMAP ) || defined( EYE_SPHEREMAP )
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
#endif
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
layout( location = D3D9_SLOT_TEXCOORD5 ) out vec4 out_Texcoord5;
layout( location = D3D9_SLOT_TEXCOORD6 ) out vec4 out_Texcoord6;

layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU
layout( location = OTDU_SLOT_VIEW_NORMAL ) out vec4 out_ViewNormal;     // OTDU

void main() {
    ivec4 bones = tdu_skin_bones( in_BlendIndices0 );
    vec4 weights = tdu_skin_weights( in_BlendWeight0 );
    vec4 position = tdu_skin_position( in_Position0, bones, weights );
    vec3 normal = tdu_skin_normal( in_Normal0.xyz, bones, weights );
    gl_Position = vec4( d3d9_dot( position, gWorldViewProj( 0 ) ),
                        d3d9_dot( position, gWorldViewProj( 1 ) ),
                        d3d9_dot( position, gWorldViewProj( 2 ) ),
                        d3d9_dot( position, gWorldViewProj( 3 ) ) );

    float inside = tdu_avatar_car_factor( position.y );
    out_Texcoord4 =
        vec4( tdu_avatar_car_light( gVSLightAmbient.xyz, gVSAttenuationFactor.x,
                                    inside ),
              gVSLightAmbient.w );
    float diffuse = max( d3d9_dot( normal, -gVSLightDir.xyz ), 0.0 );
    out_Texcoord5 =
        diffuse * vec4( tdu_avatar_car_light( gVSLightDiffuse.xyz,
                                              gVSAttenuationFactor.y, inside ),
                        gVSLightDiffuse.w );
    out_Texcoord6 =
        vec4( tdu_avatar_car_light( gVSLightSpecular.xyz,
                                    gVSAttenuationFactor.z, inside ),
              gVSLightSpecular.w );

    vec3 viewPosition = vec3( d3d9_dot( position, gWorldView( 0 ) ),
                              d3d9_dot( position, gWorldView( 1 ) ),
                              d3d9_dot( position, gWorldView( 2 ) ) );
    float viewDistance =
        1.0 / inversesqrt( abs( d3d9_dot( viewPosition, viewPosition ) ) );
    vec2 headNormal = vec2( d3d9_dot( normal, gVSHeadRight.xyz ),
                            d3d9_dot( normal, gVSHeadUp.xyz ) );
    out_Texcoord0 = vec4( headNormal * vec2( 0.5, -0.5 ) + vec2( 0.5 ), diffuse,
                          tdu_vertex_fog( viewDistance ) );
    out_Texcoord1 = vec4( in_Texcoord0.xy + gVSUVOffset.xy, in_Texcoord1.xy );

    vec3 toCamera = -position.xyz + gVSCamPosInObj.xyz;
    float cosView = d3d9_dot( toCamera, normal );
    vec3 reflected =
        d3d9_normalize( normal * -( cosView + cosView ) + toCamera );
    vec3 sphereUV = vec3( vec2( d3d9_dot( reflected, gWorldView( 0 ).xyz ),
                                d3d9_dot( reflected, gWorldView( 1 ).xyz ) ) *
                                  vec2( 0.5 ) +
                              vec2( 0.5 ),
                          0.0 );
    out_Texcoord2 = vec4( sphereUV, 0.0 );
#if defined( EYE_ENVMAP )
    float cosViewWorld = d3d9_dot( -toCamera, normal );
    vec3 reflectedWorld =
        d3d9_normalize( normal * -( cosViewWorld + cosViewWorld ) + -toCamera );
    out_Texcoord3 = vec4( d3d9_dot( reflectedWorld, gVSWorld( 0 ).xyz ),
                          d3d9_dot( reflectedWorld, gVSWorld( 1 ).xyz ),
                          d3d9_dot( reflectedWorld, gVSWorld( 2 ).xyz ), 0.0 );
#elif defined( EYE_SPHEREMAP )
    out_Texcoord3 = vec4( sphereUV, 0.0 );
#endif

    // OTDU_ADDITION: night lamps, attenuated like the diffuse light in the car.
    tdu_avatar_lamp_varyings(
        position, normal,
        tdu_avatar_car_light( vec3( 1.0 ), gVSAttenuationFactor.y, inside ).x,
        out_ViewPosition, out_ViewNormal );

    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
