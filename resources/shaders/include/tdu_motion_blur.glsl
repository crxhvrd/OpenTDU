#ifndef TDU_MOTION_BLUR_GLSL
#define TDU_MOTION_BLUR_GLSL

// OTDU_ADDITION: motion blur (render/postfx/effects/postfx_motion_blur.cpp).
//
// Everything is CAMERA-RELATIVE (world position minus this frame's camera
// position, built in double precision on the CPU) so the reconstruction keeps
// its precision at Oahu's 40+ km coordinates.
//
// Matrices are stored as ROWS: clip = vec4( dot( p, row0 ), ..., dot( p, row3 ) ).

#define MB_INV_VP( i ) D3D9_CR( 0, i )  // ndc (x, y, depth, 1) -> relative world
#define MB_PREV_VP( i ) D3D9_CR( 4, i ) // relative world -> previous clip
#define MB_VP( i ) D3D9_CR( 8, i )      // relative world -> current clip
#define MB_SCREEN D3D9_C( 12 )   // width, height, 1 / width, 1 / height
#define MB_PARAMS D3D9_C( 13 )   // mode, vehicles, wheels, debug view
#define MB_PARAMS2 D3D9_C( 14 )  // blur radius share, max radius (px), -, -
#define MB_CAM_FWD D3D9_C( 15 )  // camera forward (unit), -

#define MB_MODE_WORLD 0
#define MB_MODE_PLAYER 1

// Data texture (RGBA32F, MB_DATA_WIDTH texels a row), linear texel index:
//   vehicle i (i < MB_MAX_VEHICLES), 7 texels from i * 7:
//     0..2  relative world -> vehicle space (rows)
//     3..5  vehicle space -> PREVIOUS relative world (rows)
//     6     half extents xyz (vehicle space, centred on the origin), -
//   wheel j, 3 texels from MB_WHEEL_BASE + j * 3:
//     0     hub (relative world) xyz, tyre radius (m)
//     1     axle (unit, relative world) xyz, half width (m)
//     2     arc swept during the shutter (rad, signed), -, -, -
#define MB_DATA_WIDTH 256
#define MB_MAX_VEHICLES 32
#define MB_MAX_WHEELS 128
#define MB_WHEEL_BASE ( MB_MAX_VEHICLES * 7 )

vec3 mb_transform_row3( vec4 r0, vec4 r1, vec4 r2, vec3 p ) {
    vec4 h = vec4( p, 1.0 );
    return vec3( dot( h, r0 ), dot( h, r1 ), dot( h, r2 ) );
}

vec2 mb_clip_to_uv( vec4 clip ) {
    vec2 ndc = clip.xy / clip.w;
    return vec2( ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5 );
}

vec4 mb_project( vec3 p ) {
    vec4 h = vec4( p, 1.0 );
    return vec4( dot( h, MB_VP( 0 ) ), dot( h, MB_VP( 1 ) ),
                 dot( h, MB_VP( 2 ) ), dot( h, MB_VP( 3 ) ) );
}

// Relative world position of a pixel from its depth buffer value.
vec3 mb_unproject( vec2 uv, float depth ) {
    vec4 ndc = vec4( uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0 );
    vec4 h = vec4( dot( ndc, MB_INV_VP( 0 ) ), dot( ndc, MB_INV_VP( 1 ) ),
                   dot( ndc, MB_INV_VP( 2 ) ), dot( ndc, MB_INV_VP( 3 ) ) );
    return h.xyz / h.w;
}

// Relative world position of a pixel from its view depth (distance along the
// camera forward axis), as stored by the velocity pass.
vec3 mb_unproject_view_depth( vec2 uv, float viewDepth ) {
    vec3 farPoint = mb_unproject( uv, 1.0 );
    float along = dot( farPoint, MB_CAM_FWD.xyz );
    return farPoint * ( viewDepth / max( along, 1e-4 ) );
}

#endif
