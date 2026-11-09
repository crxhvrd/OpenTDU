#version 450
#extension GL_GOOGLE_include_directive : require

// OTDU_ADDITION: motion blur, pass 1 of 2 (postfx_motion_blur.cpp).
//
// Per pixel: where the surface was during the previous frame. The world is
// static (camera motion only); a pixel inside one of the listed vehicles'
// boxes moves with that vehicle instead (its own previous transform), so
// traffic driving along with the camera stays sharp and parked cars blur
// like the world around them.
//
// Output (RGBA16F):
//   xy  blur radius vector in pixels (half of the motion over the shutter)
//   z   view depth (metres along the camera forward axis)
//   w   wheel index + 1 (0 = not a wheel); +0.5 for a vehicle pixel
//
// In MB_MODE_PLAYER (after the player's car is drawn) only the listed
// vehicle (the player's car) gets a velocity; everything else was blurred by
// the world pass already and is marked still.

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_motion_blur.glsl"

// depth (main depth buffer, depth aspect); multisampled under MSAA: sample 0
#ifdef MB_DEPTH_MSAA
layout( set = 0, binding = D3D9_TEXTURE_BINDING( 1 ) ) uniform texture2DMS t1_2dms;
float mb_depth( ivec2 pixel ) {
    return texelFetch( sampler2DMS( t1_2dms, D3D9_SAMPLER( 1 ) ), pixel, 0 ).x;
}
#else
D3D9_DECLARE_SAMPLER_2D( 1 )
float mb_depth( ivec2 pixel ) {
    return texelFetch( sampler2D( t1_2d, D3D9_SAMPLER( 1 ) ), pixel, 0 ).x;
}
#endif
D3D9_DECLARE_SAMPLER_2D( 2 ) // vehicle / wheel data

// The previous camera turned to this frame's orientation (its movement and
// zoom only); MB_PARAMS2.z = share of the camera's rotation kept. Kept here,
// not in tdu_motion_blur.glsl: an include edit rebuilds every shader.
#define MB_PREV_VP_STILL( i ) D3D9_CR( 16, i )

layout( location = 0 ) out vec4 oC0;

vec4 mb_data( int index ) {
    return texelFetch( sampler2D( t2_2d, D3D9_SAMPLER( 2 ) ),
                       ivec2( index % MB_DATA_WIDTH, index / MB_DATA_WIDTH ),
                       0 );
}

void main() {
    ivec2 pixel = ivec2( gl_FragCoord.xy );
    vec2 uv = ( vec2( pixel ) + 0.5 ) * MB_SCREEN.zw;
    float depth = mb_depth( pixel );

    vec3 position = mb_unproject( uv, depth );
    float viewDepth = dot( position, MB_CAM_FWD.xyz );

    int mode = int( MB_PARAMS.x + 0.5 );
    int numVehicles = min( int( MB_PARAMS.y + 0.5 ), MB_MAX_VEHICLES );
    int numWheels = min( int( MB_PARAMS.z + 0.5 ), MB_MAX_WHEELS );

    // Wheels first: a tyre moves with its car even where it pokes out of the
    // car's box (the box stops just above the road). The spin itself is drawn
    // by the geometry (render/wheel_blur.cpp), not here.
    float wheelId = 0.0;
    int wheelVehicle = -1;
    for ( int j = 0; j < numWheels; j++ ) {
        int base = MB_WHEEL_BASE + j * 3;
        vec4 hub = mb_data( base );
        vec4 axle = mb_data( base + 1 );
        vec3 offset = position - hub.xyz;
        float along = dot( offset, axle.xyz );
        vec3 radial = offset - along * axle.xyz;
        if ( abs( along ) <= axle.w && dot( radial, radial ) <= hub.w * hub.w ) {
            wheelId = float( j + 1 );
            wheelVehicle = int( floor( mb_data( base + 2 ).y + 0.5 ) );
            break;
        }
    }

    vec3 previous = position;
    bool bVehicle = false;
    bool bFollowed = false; // the player's car: true motion, no damping
    for ( int i = 0; i < numVehicles; i++ ) {
        int base = i * 7;
        vec3 local = mb_transform_row3( mb_data( base ), mb_data( base + 1 ),
                                        mb_data( base + 2 ), position );
        vec4 extents = mb_data( base + 6 );
        if ( i == wheelVehicle ||
             all( lessThanEqual( abs( local ), extents.xyz ) ) ) {
            previous = mb_transform_row3( mb_data( base + 3 ),
                                          mb_data( base + 4 ),
                                          mb_data( base + 5 ), local );
            bVehicle = true;
            bFollowed = extents.w > 0.5;
            break;
        }
    }

    vec2 blur = vec2( 0.0 );
    bool bMoves = ( mode == MB_MODE_WORLD ) || bVehicle;
    vec4 previousClip = vec4( previous, 1.0 );
    previousClip = vec4( dot( previousClip, MB_PREV_VP( 0 ) ),
                         dot( previousClip, MB_PREV_VP( 1 ) ),
                         dot( previousClip, MB_PREV_VP( 2 ) ),
                         dot( previousClip, MB_PREV_VP( 3 ) ) );
    vec4 stillClip = vec4( previous, 1.0 );
    stillClip = vec4( dot( stillClip, MB_PREV_VP_STILL( 0 ) ),
                      dot( stillClip, MB_PREV_VP_STILL( 1 ) ),
                      dot( stillClip, MB_PREV_VP_STILL( 2 ) ),
                      dot( stillClip, MB_PREV_VP_STILL( 3 ) ) );
    if ( bMoves && previousClip.w > 1e-3 ) {
        // Only a share of the camera's rotation, except on the followed car
        vec2 previousUV = mb_clip_to_uv( previousClip );
        if ( !bFollowed && stillClip.w > 1e-3 ) {
            previousUV = mix( mb_clip_to_uv( stillClip ), previousUV,
                              MB_PARAMS2.z );
        }
        vec2 motion = ( uv - previousUV ) * MB_SCREEN.xy;
        blur = motion * MB_PARAMS2.x;
        float radius = length( blur );
        if ( radius > MB_PARAMS2.y ) {
            blur *= MB_PARAMS2.y / radius;
        }
    }

    oC0 = vec4( blur, viewDepth, wheelId + ( bVehicle ? 0.5 : 0.0 ) );
}
