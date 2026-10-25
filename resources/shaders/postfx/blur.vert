#version 450
#extension GL_GOOGLE_include_directive : require

// Separable / box blur tap coordinates (uv_offset: one tap step).
//
// Permutations:
//   BLUR_LINEAR=N  2N + 1 taps along uv_offset: -N..-1, +1..+N, then the
//                  center, two per texcoord
//   BLUR_CROSS     center, up, left, down, center
//   BLUR_BOX       4 diagonal taps
//   BLUR_GRID      3x3 grid: left / right, up / down, the corners, center

#define D3D9_VERTEX_SHADER
#include "d3d9_abi.glsl"
#include "tdu_postfx.glsl"

#define uv_offset c.f[58]

layout( location = D3D9_IN_POSITION0 ) in vec4 in_Position0;
layout( location = D3D9_SLOT_COLOR0 ) out vec4 out_Color0;
layout( location = D3D9_SLOT_COLOR1 ) out vec4 out_Color1;
layout( location = D3D9_SLOT_TEXCOORD0 ) out vec4 out_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) out vec4 out_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) out vec4 out_Texcoord2;
#if !defined( BLUR_LINEAR ) || BLUR_LINEAR >= 3
layout( location = D3D9_SLOT_TEXCOORD3 ) out vec4 out_Texcoord3;
#endif
#if defined( BLUR_CROSS ) || defined( BLUR_GRID ) ||                           \
    ( defined( BLUR_LINEAR ) && BLUR_LINEAR >= 4 )
layout( location = D3D9_SLOT_TEXCOORD4 ) out vec4 out_Texcoord4;
#endif

vec2 tap( vec2 uv, int k ) {
    if ( k == 1 ) {
        return uv + uv_offset.xy;
    }
    if ( k == -1 ) {
        return uv + -uv_offset.xy;
    }
    return uv_offset.xy * float( k ) + uv;
}

void main() {
    gl_Position = tdu_postfx_position( in_Position0 );
    vec2 uv = tdu_postfx_uv( in_Position0 );

#if defined( BLUR_LINEAR ) && BLUR_LINEAR == 2
    out_Texcoord0 = vec4( tap( uv, -2 ), tap( uv, -1 ) );
    out_Texcoord1 = vec4( tap( uv, 1 ), tap( uv, 2 ) );
    out_Texcoord2 = vec4( uv, 0.0, 0.0 );
#elif defined( BLUR_LINEAR ) && BLUR_LINEAR == 3
    out_Texcoord0 = vec4( tap( uv, -3 ), tap( uv, -2 ) );
    out_Texcoord1 = vec4( tap( uv, -1 ), tap( uv, 1 ) );
    out_Texcoord2 = vec4( tap( uv, 2 ), tap( uv, 3 ) );
    out_Texcoord3 = vec4( uv, 0.0, 0.0 );
#elif defined( BLUR_LINEAR ) && BLUR_LINEAR == 4
    out_Texcoord0 = vec4( tap( uv, -4 ), tap( uv, -3 ) );
    out_Texcoord1 = vec4( tap( uv, -2 ), tap( uv, -1 ) );
    out_Texcoord2 = vec4( tap( uv, 1 ), tap( uv, 2 ) );
    out_Texcoord3 = vec4( tap( uv, 3 ), tap( uv, 4 ) );
    out_Texcoord4 = vec4( uv, 0.0, 0.0 );
#elif defined( BLUR_CROSS )
    out_Texcoord0 = vec4( uv, 0.0, 0.0 );
    out_Texcoord1 = vec4( uv.x, uv.y + -uv_offset.y, 0.0, 0.0 );
    out_Texcoord2 = vec4( uv.x + -uv_offset.x, uv.y, 0.0, 0.0 );
    out_Texcoord3 = vec4( uv.x, uv.y + uv_offset.y, 0.0, 0.0 );
    out_Texcoord4 = vec4( uv, 0.0, 0.0 );
#elif defined( BLUR_BOX )
    out_Texcoord0 = vec4( uv + uv_offset.xy, 0.0, 0.0 );
    out_Texcoord1 = vec4( uv_offset.xy * vec2( 1.0, -1.0 ) + uv, 0.0, 0.0 );
    out_Texcoord2 = vec4( uv + -uv_offset.xy, 0.0, 0.0 );
    out_Texcoord3 = vec4( uv_offset.xy * vec2( -1.0, 1.0 ) + uv, 0.0, 0.0 );
#elif defined( BLUR_GRID )
    float left = uv.x + -uv_offset.x;
    float right = uv.x + uv_offset.x;
    float up = uv.y + -uv_offset.y;
    float down = uv.y + uv_offset.y;
    out_Texcoord0 = vec4( right, uv.y, left, uv.y );
    out_Texcoord1 = vec4( uv.x, up, uv.x, down );
    out_Texcoord2 = vec4( right, up, left, up );
    out_Texcoord3 = vec4( right, down, left, down );
    out_Texcoord4 = vec4( uv, 0.0, 0.0 );
#endif

    out_Color0 = vec4( 1.0 );
    out_Color1 = vec4( 0.0 );
    d3d9_vs_epilogue();
}
