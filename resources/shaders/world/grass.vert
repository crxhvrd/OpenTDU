#version 450
#extension GL_GOOGLE_include_directive : require

// Tall grass: camera facing blades swaying in the wind, shaded with the
// sector color and baked shadow maps.
//
// gVSParam     = (alpha fade start, alpha fade length, wind phase, wind
//                 amplitude)
// gVSScaleDist = (scale fade start, scale fade length, scale, width)
// gVSSectorOffset.xz = sector origin, .y = atlas column width, .w = height

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gColorContrast D3D9_C( 1 )
#define gWorldViewProject( i ) D3D9_CR( 8, i )
#define gWorld( i ) D3D9_CR( 16, i )
#define gWorldView( i ) D3D9_CR( 20, i )
#define gVSM4InvView( i ) D3D9_CR( 48, i )
#define gVSParam D3D9_C( 114 )
#define gVSScaleDist D3D9_C( 115 )
#define gVSSectorOffset D3D9_C( 121 )

const float kPi = 3.1415927;
const float kSectorSize = 768.0;

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_COLOR( 0 ) ) in vec4 in_Color0; // xy: blade corner, z: atlas column
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0; // x: atlas u, y: blade height
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = OTDU_SLOT_VIEW_POSITION ) out vec4 out_ViewPosition; // OTDU

void main() {
    float groundHeight = in_Position0.y + gVSSectorOffset.w;
    vec4 position =
        vec4( in_Position0.x, groundHeight + in_Texcoord0.y, in_Position0.zw );
    vec4 viewPosition = vec4( d3d9_dot( position, gWorldView( 0 ) ),
                              d3d9_dot( position, gWorldView( 1 ) ),
                              d3d9_dot( position, gWorldView( 2 ) ),
                              d3d9_dot( position, gWorldView( 3 ) ) );
    float viewDistance =
        1.0 / inversesqrt( abs( d3d9_dot( viewPosition, viewPosition ) ) );

    // Blades shrink to (1 + scale) in the distance.
    float scaleFade = ( -( viewDistance + -gVSScaleDist.x ) + gVSScaleDist.y ) *
                      ( 1.0 / gVSScaleDist.y );
    scaleFade = -min( max( scaleFade, 0.0 ), 1.0 ) + 1.0;
    float size = gVSScaleDist.z * scaleFade + 1.0;

    float windPhase = ( -in_Position0.z + in_Position0.x ) + gVSParam.z;
    windPhase =
        fract( windPhase * ( 1.0 / ( 2.0 * kPi ) ) + 0.5 ) * ( 2.0 * kPi ) +
        -kPi;

    vec2 corner = 2.0 * in_Color0.xy + vec2( -1.0 );
    vec2 cornerOffset = size * vec2( groundHeight * gVSScaleDist.w * corner.x,
                                     groundHeight * corner.y );
    float sideOffset =
        sin( windPhase ) * in_Color0.y * gVSParam.w + cornerOffset.x;

    position.xyz += vec3( sideOffset * gVSM4InvView( 0 ).x,
                          sideOffset * gVSM4InvView( 1 ).x + cornerOffset.y,
                          sideOffset * gVSM4InvView( 2 ).x );
    position.w = in_Position0.w;
    gl_Position = vec4( d3d9_dot( position, gWorldViewProject( 0 ) ),
                        d3d9_dot( position, gWorldViewProject( 1 ) ),
                        d3d9_dot( position, gWorldViewProject( 2 ) ),
                        d3d9_dot( position, gWorldViewProject( 3 ) ) );

    vec2 sectorUV = vec2( d3d9_dot( position, gWorld( 0 ) ),
                          d3d9_dot( position, gWorld( 2 ) ) ) -
                    gVSSectorOffset.xz;
    float shadowFade = max( -viewDistance + kSectorSize, 0.0 ) *
                       gColorContrast.w * ( 1.0 / kSectorSize );
    float alphaFade =
        ( -( viewDistance + -gVSParam.x ) + gVSParam.y ) * ( 1.0 / gVSParam.y );

    out_Texcoord0 = vec4(
        ( in_Color0.z + in_Color0.x ) * gVSSectorOffset.y + in_Texcoord0.x,
        max( -in_Color0.y + 1.0, 0.004 ), tdu_vertex_fog( viewDistance ), 0.0 );
    out_Texcoord1 = vec4( sectorUV * ( 1.0 / kSectorSize ), shadowFade, 0.0 );

    // OTDU_ADDITION: night lamps.
    out_ViewPosition =
        tdu_lamps_enabled() ? vec4( viewPosition.xyz, 1.0 ) : vec4( 0.0 );

    out_Color0 =
        clamp( vec4( gColorContrast.rgb, min( max( alphaFade, 0.0 ), 1.0 ) ),
               vec4( 0.0 ), vec4( 1.0 ) );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
