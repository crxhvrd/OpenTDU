#version 450
#extension GL_GOOGLE_include_directive : require

// Ocean normal map passes (retail ocean table): the 4 neighbour taps of the
// height map, gVSMaterialParam.xy apart.
//
// Permutations:
//   OCEAN_NORMAL_PROJECTED  render target quad over the projected grid: also
//                           the world positions of the neighbours,
//                           interpolated between the grid corners
//                           (gVSCorner, homogeneous)

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"

#define gVSM4WorldViewProj( i ) D3D9_CR( 8, i )
#define gVSMaterialParam D3D9_C( 88 )
#define gVSCorner( i ) D3D9_CR( 125, i )

#ifdef OCEAN_NORMAL_PROJECTED
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#else
layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_IN_TEXCOORD( 0 ) ) in vec4 in_Texcoord0;
#endif
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
#ifdef OCEAN_NORMAL_PROJECTED
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;

vec4 gridPosition( vec2 uv ) {
    vec4 corner = d3d9_lerp(
        uv.x * ( -gVSCorner( 0 ) + gVSCorner( 2 ) ) + gVSCorner( 0 ),
        uv.x * ( -gVSCorner( 1 ) + gVSCorner( 3 ) ) + gVSCorner( 1 ), uv.y );
    return vec4( corner.xyz * ( 1.0 / corner.w ), 0.0 );
}
#endif

void main() {
    vec2 uv = in_Texcoord0.xy;
#ifdef OCEAN_NORMAL_PROJECTED
    gl_Position = vec4( ( 2.0 * uv.x + -1.0 ) + -gVSMaterialParam.x,
                        ( 2.0 * -uv.y + gVSMaterialParam.y ) + 1.0, 0.0, 1.0 );
    vec2 low = uv + -gVSMaterialParam.xy;
    vec2 high = uv + gVSMaterialParam.xy;
    out_Texcoord0 = vec4( uv, 0.0, 0.0 );
    out_Texcoord1 = gridPosition( vec2( high.x, uv.y ) );
    out_Texcoord2 = gridPosition( vec2( uv.x, high.y ) );
    out_Texcoord3 = gridPosition( vec2( low.x, uv.y ) );
    out_Texcoord4 = gridPosition( vec2( uv.x, low.y ) );
#else
    gl_Position = vec4( d3d9_dot( in_Position0, gVSM4WorldViewProj( 0 ) ),
                        d3d9_dot( in_Position0, gVSM4WorldViewProj( 1 ) ),
                        d3d9_dot( in_Position0, gVSM4WorldViewProj( 2 ) ),
                        d3d9_dot( in_Position0, gVSM4WorldViewProj( 3 ) ) );
    out_Texcoord0 = vec4( uv.x + -gVSMaterialParam.x, uv.y, 0.0, 0.0 );
    out_Texcoord1 = vec4( uv.x + gVSMaterialParam.x, uv.y, 0.0, 0.0 );
    out_Texcoord2 = vec4( uv.x, uv.y + -gVSMaterialParam.y, 0.0, 0.0 );
    out_Texcoord3 = vec4( uv.x, uv.y + gVSMaterialParam.y, 0.0, 0.0 );
#endif
    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
