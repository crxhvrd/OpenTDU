#version 450
#extension GL_GOOGLE_include_directive : require

// Two-texture morph (retail UVA shader table), used by the sky dome to blend
// the current and next day period gradients.

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gWorldViewProject( i ) D3D9_CR( 8, i )
#define gWorldView( i ) D3D9_CR( 20, i )
#define gVSMaterialEmissive D3D9_C( 241 )

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;

void main() {
    gl_Position = vec4( d3d9_dot( in_Position0, gWorldViewProject( 0 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 1 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 2 ) ),
                        d3d9_dot( in_Position0, gWorldViewProject( 3 ) ) );

    vec3 viewPosition = vec3( d3d9_dot( in_Position0, gWorldView( 0 ) ),
                              d3d9_dot( in_Position0, gWorldView( 1 ) ),
                              d3d9_dot( in_Position0, gWorldView( 2 ) ) );
    float fog = tdu_vertex_fog( length( viewPosition ) );

    // The emissive color goes through a texture coordinate, so it is not
    // clamped like a vertex color.
    out_Texcoord0 = gVSMaterialEmissive;
    out_Texcoord1 = vec4( in_Texcoord0.xy, 0.0, 0.0 );
    // OTDU_ADDITION: the world space view direction drives the night stars
    // and moon (zero, as retail, while both brightnesses are 0). The dome is
    // flattened, so its object space position is not a direction.
    bool bNightSky = tdu_night_params.z > 0.0 || tdu_night_params.w > 0.0;
    vec3 worldDirection =
        vec3( d3d9_dot( viewPosition, tdu_cam_to_world( 0 ).xyz ),
              d3d9_dot( viewPosition, tdu_cam_to_world( 1 ).xyz ),
              d3d9_dot( viewPosition, tdu_cam_to_world( 2 ).xyz ) );
    out_Texcoord2 = vec4( fog, bNightSky ? worldDirection : vec3( 0.0 ) );

    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
