#version 450
#extension GL_GOOGLE_include_directive : require

// OTDU_ADDITION: motion blur, pass 2 of 2 (postfx_motion_blur.cpp).
//
// Moving pixels: a full resolution reconstruction filter along the pixel's
// own blur vector (McGuire et al. 2012, without the tile pass): a sample in
// FRONT counts where its own blur reaches this pixel, a sample BEHIND counts
// where this pixel's blur reaches it, two blurry samples at the same depth
// share. A sharp foreground (traffic driving along) keeps its edge, the
// background never streaks over it.
//
// Wheels spin in their own geometry (render/wheel_blur.cpp); here they only
// move with their car.

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_motion_blur.glsl"

D3D9_DECLARE_SAMPLER_2D( 0 ) // scene (copy, linear filtering)
D3D9_DECLARE_SAMPLER_2D( 1 ) // velocity pass output (point)
D3D9_DECLARE_SAMPLER_2D( 2 ) // vehicle / wheel data

layout( location = 0 ) out vec4 oC0;

#define MB_MAX_SAMPLES_A_SIDE 24

vec4 mb_data( int index ) {
    return texelFetch( sampler2D( t2_2d, D3D9_SAMPLER( 2 ) ),
                       ivec2( index % MB_DATA_WIDTH, index / MB_DATA_WIDTH ),
                       0 );
}

vec4 mb_velocity( vec2 pixelPos ) {
    ivec2 p = clamp( ivec2( pixelPos ), ivec2( 0 ),
                     ivec2( MB_SCREEN.xy ) - ivec2( 1 ) );
    return texelFetch( sampler2D( t1_2d, D3D9_SAMPLER( 1 ) ), p, 0 );
}

vec3 mb_scene( vec2 pixelPos ) {
    return tex2D_s0( pixelPos * MB_SCREEN.zw ).rgb;
}

float mb_soft_depth( float zA, float zB ) {
    // 1 when A is in front of (or level with) B
    float extent = max( 0.1, zB * 0.02 );
    return clamp( 1.0 - ( zA - zB ) / extent, 0.0, 1.0 );
}

float mb_cone( float distance, float radius ) {
    return clamp( 1.0 - distance / max( radius, 1e-3 ), 0.0, 1.0 );
}

float mb_cylinder( float distance, float radius ) {
    return 1.0 - smoothstep( 0.95 * radius, 1.05 * radius, distance );
}

vec3 mb_linear_blur( vec2 pixelPos, vec4 centre, vec3 centreColour ) {
    vec2 blur = centre.xy;
    float radius = length( blur );
    if ( radius < 0.5 ) {
        return centreColour;
    }

    int samples = clamp( int( ceil( radius / 1.5 ) ), 1, MB_MAX_SAMPLES_A_SIDE );
    // Stagger the two sides by a per-pixel half step against banding
    float jitter = fract( 52.9829189 * fract( dot( pixelPos, vec2( 0.06711056, 0.00583715 ) ) ) ) - 0.5;

    float weightSum = 1.0 / radius;
    vec3 colourSum = centreColour * weightSum;
    for ( int i = 1; i <= samples; i++ ) {
        float t = ( float( i ) + jitter * 0.5 ) / float( samples );
        float distance = t * radius;
        for ( int side = 0; side < 2; side++ ) {
            vec2 offset = blur * ( side == 0 ? t : -t );
            vec4 sample_ = mb_velocity( pixelPos + offset );
            float sampleRadius = length( sample_.xy );

            float front = mb_soft_depth( sample_.z, centre.z );
            float back = mb_soft_depth( centre.z, sample_.z );
            float weight = front * mb_cone( distance, sampleRadius ) +
                           back * mb_cone( distance, radius ) +
                           mb_cylinder( distance, sampleRadius ) *
                               mb_cylinder( distance, radius ) * 2.0;

            weightSum += weight;
            colourSum += mb_scene( pixelPos + offset ) * weight;
        }
    }
    return colourSum / weightSum;
}

void main() {
    vec2 pixelPos = floor( gl_FragCoord.xy ) + 0.5;
    vec4 centreScene = texelFetch( sampler2D( t0_2d, D3D9_SAMPLER( 0 ) ),
                                   ivec2( gl_FragCoord.xy ), 0 );
    vec4 centre = mb_velocity( pixelPos );

    int debugView = int( MB_PARAMS.w + 0.5 );
    if ( debugView == 1 ) {
        // Blur vectors: hue = direction, brightness = length (16 px = full)
        vec2 b = centre.xy / 16.0;
        oC0 = vec4( clamp( 0.5 + 0.5 * b.x, 0.0, 1.0 ),
                    clamp( 0.5 + 0.5 * b.y, 0.0, 1.0 ),
                    clamp( length( b ), 0.0, 1.0 ), 1.0 );
        return;
    }
    if ( debugView == 2 ) {
        // Vehicles blue, wheels red
        float tag = centre.w;
        oC0 = vec4( tag >= 1.0 ? 1.0 : 0.0, 0.0, fract( tag ) > 0.25 ? 1.0 : 0.0,
                    1.0 ) * 0.5 + vec4( centreScene.rgb * 0.5, 0.5 );
        return;
    }

    vec3 colour = mb_linear_blur( pixelPos, centre, centreScene.rgb );
    oC0 = vec4( colour, centreScene.a );
}
