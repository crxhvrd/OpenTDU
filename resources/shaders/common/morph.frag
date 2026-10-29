#version 450
#extension GL_GOOGLE_include_directive : require

// Two-texture morph (retail UVA shader table), used by the sky dome to blend
// the current and next day period gradients.

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"
#include "tdu_common.glsl"

#define gMorphTexCoef D3D9_C( 14 )

D3D9_DECLARE_SAMPLER_2D( 0 ) // morph target
D3D9_DECLARE_SAMPLER_2D( 1 ) // morph source

layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
layout( location = 0 ) out vec4 oC0;

// OTDU_ADDITION: procedural night star field over the dome (world space,
// +y up). Each cell of a cube face grid holds at most one star.
uvec3 pcg3d( uvec3 v ) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

vec3 starField( vec3 direction ) {
    const float kCellsPerFace = 160.0;
    const float kStarDensity = 0.05;
    const float kStarRadius = 0.11; // in cells

    vec3 axis = abs( direction );
    vec2 faceUV;
    uint face;
    if ( axis.x >= axis.y && axis.x >= axis.z ) {
        faceUV = direction.yz / axis.x;
        face = ( direction.x > 0.0 ) ? 0u : 1u;
    } else if ( axis.y >= axis.z ) {
        faceUV = direction.xz / axis.y;
        face = ( direction.y > 0.0 ) ? 2u : 3u;
    } else {
        faceUV = direction.xy / axis.z;
        face = ( direction.z > 0.0 ) ? 4u : 5u;
    }

    vec2 grid = ( faceUV * 0.5 + 0.5 ) * kCellsPerFace;
    uvec3 hash = pcg3d( uvec3( uvec2( floor( grid ) ), face ) );
    vec3 random = vec3( hash ) * ( 1.0 / 4294967295.0 );
    if ( random.x > kStarDensity ) {
        return vec3( 0.0 );
    }

    vec2 starPosition = 0.2 + 0.6 * random.yz;
    vec2 offset = ( fract( grid ) - starPosition ) * ( 1.0 / kStarRadius );
    float shape = exp( -dot( offset, offset ) );

    // Few bright stars, many faint ones; slightly blue or warm.
    float brightness = pow( random.x * ( 1.0 / kStarDensity ), 4.0 );
    vec3 tint = mix( vec3( 0.85, 0.9, 1.0 ), vec3( 1.0, 0.92, 0.8 ),
                     fract( random.y * 7.0 ) );
    float horizon = smoothstep( 0.02, 0.3, direction.y );
    return tint * ( shape * brightness * horizon );
}

float valueNoise( vec2 p ) {
    vec2 cell = floor( p );
    vec2 f = fract( p );
    f = f * f * ( 3.0 - 2.0 * f );
    uvec2 c = uvec2( ivec2( cell ) + 1024 );
    float a = float( pcg3d( uvec3( c, 7u ) ).x ) * ( 1.0 / 4294967295.0 );
    float b = float( pcg3d( uvec3( c + uvec2( 1, 0 ), 7u ) ).x ) *
              ( 1.0 / 4294967295.0 );
    float d = float( pcg3d( uvec3( c + uvec2( 0, 1 ), 7u ) ).x ) *
              ( 1.0 / 4294967295.0 );
    float e = float( pcg3d( uvec3( c + uvec2( 1, 1 ), 7u ) ).x ) *
              ( 1.0 / 4294967295.0 );
    return mix( mix( a, b, f.x ), mix( d, e, f.x ), f.y );
}

// OTDU_ADDITION: procedural full moon (rgb, coverage) around the moon
// direction: limb darkening, darker maria, a faint glow.
vec4 moonDisc( vec3 direction, vec3 moonDirection ) {
    const float kMoonRadius = 0.025; // radians
    const float kGlowRadius = 0.12;

    vec3 tangent = normalize( cross( moonDirection, vec3( 0.0, 1.0, 0.0 ) ) );
    vec3 bitangent = cross( tangent, moonDirection );
    vec2 disc = vec2( dot( direction, tangent ), dot( direction, bitangent ) ) *
                ( 1.0 / kMoonRadius );
    float facing = dot( direction, moonDirection );
    float radius = length( disc );
    if ( facing <= 0.0 ) {
        return vec4( 0.0 );
    }

    float glow =
        exp( -( 1.0 - facing ) * ( 1.0 / ( kGlowRadius * kGlowRadius ) ) ) *
        0.08;
    if ( radius >= 1.0 ) {
        return vec4( vec3( 0.55, 0.6, 0.7 ) * glow, 0.0 );
    }

    float limb = sqrt( 1.0 - radius * radius );
    float maria = valueNoise( disc * 2.2 + 3.0 ) * 0.6 +
                  valueNoise( disc * 5.0 + 11.0 ) * 0.3 +
                  valueNoise( disc * 11.0 + 23.0 ) * 0.1;
    float albedo = mix( 1.0, 0.55, smoothstep( 0.45, 0.7, maria ) );
    float edge = 1.0 - smoothstep( 0.9, 1.0, radius );
    vec3 surface = vec3( 0.93, 0.92, 0.88 ) * albedo * ( 0.55 + 0.45 * limb );
    return vec4( surface, edge );
}

void main() {
    vec4 emissive = d3d9_ps_texcoord( in_Texcoord0 );
    vec4 uv = d3d9_ps_texcoord( in_Texcoord1 );
    vec4 fog = d3d9_ps_texcoord( in_Texcoord2 );

    vec4 color =
        d3d9_lerp( tex2D_s1( uv.xy ), tex2D_s0( uv.xy ), gMorphTexCoef.x );
    color *= emissive;

    if ( tdu_night_params.z > 0.0 || tdu_night_params.w > 0.0 ) {
        vec3 direction = normalize( fog.yzw );
        color.rgb += tdu_night_params.z * starField( direction );
        if ( tdu_night_params.w > 0.0 ) {
            vec4 moon = moonDisc( direction, tdu_moon_direction.xyz );
            color.rgb =
                mix( color.rgb, moon.rgb * tdu_night_params.w, moon.a ) +
                ( 1.0 - moon.a ) * moon.rgb * tdu_night_params.w;
        }
    }

    oC0 = vec4( tdu_apply_fog( color.rgb, fog.x ), color.a );
    d3d9_ps_alpha_test( oC0 );
}
