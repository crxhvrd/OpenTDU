// D3D9 shader ABI for lifted retail shaders.
//
// The resources, push constants, spec constants and interface locations
// declared here keep the layout dxvk's DXSO compiler emitted for the retail
// bytecode (the render device binds them that way), except the float constant
// registers (see tdu_constants.glsl). The runtime reflects the
// bindings from the SPIR-V module and derives each vertex input semantic from
// its location (D3D9_IN_*).
//
// Define D3D9_VERTEX_SHADER or D3D9_PIXEL_SHADER before including this file.
//
// dxvk features the port never enables are left out:
//   - depth compare samplers (RenderDevice::setSamplerState always clears
//     SpecSamplerDepthMode)
//   - fetch4 / null sampler / PS 1.x projection spec bits (never written)
//   - fixed function fog (compiled out of dxvk's DXSO compiler)

#ifndef D3D9_ABI_GLSL
#define D3D9_ABI_GLSL

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_demote_to_helper_invocation : require
#extension GL_EXT_spirv_intrinsics : require

// D3D9 saturate (_sat modifier, fog output): dxvk emits NClamp, which maps
// NaN to 0 where GLSL clamp() is undefined.
spirv_instruction( set = "GLSL.std.450",
                   id = 81 ) float d3d9_nclamp( float x, float lo, float hi );
spirv_instruction( set = "GLSL.std.450", id = 81 ) vec2
    d3d9_nclamp( vec2 x, vec2 lo, vec2 hi );
spirv_instruction( set = "GLSL.std.450", id = 81 ) vec3
    d3d9_nclamp( vec3 x, vec3 lo, vec3 hi );
spirv_instruction( set = "GLSL.std.450", id = 81 ) vec4
    d3d9_nclamp( vec4 x, vec4 lo, vec4 hi );

// D3D9 lrp (and the matching fog mad), in dxvk's operation order: unlike
// mix(), the result keeps the cancellation of (y - x) when x is large.
float d3d9_lerp( float x, float y, float a ) { return a * ( y - x ) + x; }
vec2 d3d9_lerp( vec2 x, vec2 y, vec2 a ) { return a * ( y - x ) + x; }
vec3 d3d9_lerp( vec3 x, vec3 y, vec3 a ) { return a * ( y - x ) + x; }
vec4 d3d9_lerp( vec4 x, vec4 y, vec4 a ) { return a * ( y - x ) + x; }
vec2 d3d9_lerp( vec2 x, vec2 y, float a ) { return a * ( y - x ) + x; }
vec3 d3d9_lerp( vec3 x, vec3 y, float a ) { return a * ( y - x ) + x; }
vec4 d3d9_lerp( vec4 x, vec4 y, float a ) { return a * ( y - x ) + x; }

// D3D9 dp2/dp3/dp4, in dxvk's operation order (one multiply per component,
// summed left to right). dot() leaves the order to the driver: positions must
// be bit identical to the dxvk modules still drawing the same geometry in
// other passes (shadow receivers), or their depth test fails in patches.
float d3d9_dot( vec2 a, vec2 b ) { return a.x * b.x + a.y * b.y; }
float d3d9_dot( vec3 a, vec3 b ) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float d3d9_dot( vec4 a, vec4 b ) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

// D3D9 nrm and crs, in dxvk's operation order (see d3d9_dot).
vec3 d3d9_normalize( vec3 v ) { return v * inversesqrt( d3d9_dot( v, v ) ); }
vec3 d3d9_cross( vec3 a, vec3 b ) { return a.yzx * b.zxy - a.zxy * b.yzx; }

float d3d9_saturate( float x ) { return d3d9_nclamp( x, 0.0, 1.0 ); }
vec2 d3d9_saturate( vec2 x ) {
    return d3d9_nclamp( x, vec2( 0.0 ), vec2( 1.0 ) );
}
vec3 d3d9_saturate( vec3 x ) {
    return d3d9_nclamp( x, vec3( 0.0 ), vec3( 1.0 ) );
}
vec4 d3d9_saturate( vec4 x ) {
    return d3d9_nclamp( x, vec4( 0.0 ), vec4( 1.0 ) );
}

// Spec constant 12 tells whether dxvk's spec constants are used (VS) or
// whether the spec state has to be read from the UBO (PS).
layout( constant_id = 12 ) const uint D3D9_SPEC_ENABLED = 0u;
layout( constant_id = 1 ) const uint D3D9_SPEC_DWORD1 = 0u;
layout( constant_id = 2 ) const uint D3D9_SPEC_DWORD2 = 0u;
layout( constant_id = 5 ) const uint D3D9_SPEC_DWORD5 = 0u;

layout( set = 0, binding = 31, std140 ) uniform D3D9SpecState {
    uint dword0;
    uint dword1;
    uint dword2;
    uint dword3;
    uint dword4;
    uint dword5;
    uint dword6;
    uint dword7;
    uint dword8;
    uint dword9;
    uint dword10;
    uint dword11;
    uint dword12;
    uint dword13;
    uint dword14;
}
spec_state;

layout( push_constant, std430 ) uniform D3D9RenderState {
    layout( offset = 0 ) vec3 fog_color;
    layout( offset = 12 ) float fog_scale;
    layout( offset = 16 ) float fog_end;
    layout( offset = 20 ) float fog_density;
    layout( offset = 24 ) uint alpha_ref;
    layout( offset = 28 ) float point_size;
    layout( offset = 32 ) float point_size_min;
    layout( offset = 36 ) float point_size_max;
    layout( offset = 40 ) float point_scale_a;
    layout( offset = 44 ) float point_scale_b;
    layout( offset = 48 ) float point_scale_c;
    layout( offset = 52 ) uint instanced_draw_index;
    // Two 16-bit sampler heap indices per dword (even stage = low bits).
    layout( offset = 64 ) uint sampler_idx[8];
}
render_state;

// Interface locations assigned by dxvk's RegisterLinkerSlot. The slots are
// handed out globally in first-use order while the asset extractor walks the
// master shader table, so they are stable as long as the table order is.
#define D3D9_SLOT_COLOR0 0
#define D3D9_SLOT_COLOR1 1
#define D3D9_SLOT_TEXCOORD1 2
#define D3D9_SLOT_TEXCOORD0 3
#define D3D9_SLOT_TEXCOORD2 4
#define D3D9_SLOT_FOG 5
#define D3D9_SLOT_TEXCOORD3 6
#define D3D9_SLOT_TEXCOORD5 7
#define D3D9_SLOT_TEXCOORD4 8
#define D3D9_SLOT_TEXCOORD6 9
#define D3D9_SLOT_TEXCOORD7 10
// OTDU_ADDITION: varyings of the OpenTDU extensions start past dxvk's slots.
#define OTDU_SLOT_VIEW_POSITION 11
#define OTDU_SLOT_VIEW_NORMAL 12
// Normal mapped shaders: view space basis (normal map axes) at 12..14.
#define OTDU_SLOT_VIEW_BASIS 12
// Buildings: object space position (lit window cells).
#define OTDU_SLOT_OBJECT_POSITION 13

// Vertex input locations: one fixed location per D3D9 vertex declaration
// semantic (D3DDECLUSAGE + usage index). The runtime maps a location back to
// its semantic to fetch the matching vertex layout element; keep in sync with
// GetVertexInputSemantic (render/shaders/shader_binary_cache.cpp).
#define D3D9_IN_POSITION0 0
#define D3D9_IN_BLENDWEIGHT0 1
#define D3D9_IN_BLENDINDICES0 2
#define D3D9_IN_NORMAL0 3
#define D3D9_IN_TANGENT0 4
#define D3D9_IN_BINORMAL0 5
#define D3D9_IN_COLOR( n ) ( 6 + ( n ) )    // COLOR0..1
#define D3D9_IN_TEXCOORD( n ) ( 8 + ( n ) ) // TEXCOORD0..7

#ifdef D3D9_VERTEX_SHADER

layout( set = 0, binding = 3, std140 ) uniform D3D9ClipPlanes {
    vec4 clip_planes[6];
}
clip_info;

out float gl_ClipDistance[6];

// Runs after the shader body (dxvk emitLinkerOutputSetup + emitVsClipping).
void d3d9_vs_epilogue() {
    gl_PointSize = clamp( render_state.point_size, render_state.point_size_min,
                          render_state.point_size_max );

    uint clipPlaneCount = bitfieldExtract(
        ( D3D9_SPEC_ENABLED != 0u ) ? D3D9_SPEC_DWORD5 : 6u, 21, 3 );
    for ( uint i = 0u; i < 6u; i++ ) {
        gl_ClipDistance[i] = ( i < clipPlaneCount )
                                 ? dot( gl_Position, clip_info.clip_planes[i] )
                                 : 0.0;
    }
}

#endif // D3D9_VERTEX_SHADER

#ifdef D3D9_PIXEL_SHADER

layout( set = 1, binding = 0 ) uniform sampler d3d9_sampler_heap[];

#define D3D9_SPEC_DWORD( n )                                                   \
    ( ( D3D9_SPEC_ENABLED != 0u ) ? D3D9_SPEC_DWORD##n : spec_state.dword##n )

#define D3D9_SAMPLER( n )                                                      \
    d3d9_sampler_heap[bitfieldExtract( render_state.sampler_idx[( n ) >> 1],   \
                                       ( ( n ) & 1 ) * 16, 16 )]

#define D3D9_TEXTURE_BINDING( n ) ( 13 + ( n ) )

// Declares texture register sN and its tex2D_sN / texCUBE_sN / tex3D_sN
// helper.
#define D3D9_DECLARE_SAMPLER_2D( n )                                           \
    layout( set = 0, binding = D3D9_TEXTURE_BINDING( n ) )                     \
        uniform texture2D t##n##_2d;                                           \
    vec4 tex2D_s##n( vec2 uv ) {                                               \
        return texture( sampler2D( t##n##_2d, D3D9_SAMPLER( n ) ), uv );       \
    }                                                                          \
    vec4 tex2Dbias_s##n( vec2 uv, float bias ) {                               \
        return texture( sampler2D( t##n##_2d, D3D9_SAMPLER( n ) ), uv, bias ); \
    }

#define D3D9_DECLARE_SAMPLER_CUBE( n )                                         \
    layout( set = 0, binding = D3D9_TEXTURE_BINDING( n ) )                     \
        uniform textureCube t##n##_cube;                                       \
    vec4 texCUBE_s##n( vec3 uvw ) {                                            \
        return texture( samplerCube( t##n##_cube, D3D9_SAMPLER( n ) ), uvw );  \
    }                                                                          \
    vec4 texCUBEbias_s##n( vec3 uvw, float bias ) {                            \
        return texture( samplerCube( t##n##_cube, D3D9_SAMPLER( n ) ), uvw,    \
                        bias );                                                \
    }

#define D3D9_DECLARE_SAMPLER_3D( n )                                           \
    layout( set = 0, binding = D3D9_TEXTURE_BINDING( n ) )                     \
        uniform texture3D t##n##_3d;                                           \
    vec4 tex3D_s##n( vec3 uvw ) {                                              \
        return texture( sampler3D( t##n##_3d, D3D9_SAMPLER( n ) ), uvw );      \
    }                                                                          \
    vec4 tex3Dbias_s##n( vec3 uvw, float bias ) {                              \
        return texture( sampler3D( t##n##_3d, D3D9_SAMPLER( n ) ), uvw,        \
                        bias );                                                \
    }

// Texture coordinate inputs are replaced by gl_PointCoord when point sprites
// are enabled (SpecPointMode bit 1).
vec4 d3d9_ps_texcoord( vec4 texcoord ) {
    bool bPointSprite =
        bitfieldExtract( bitfieldExtract( D3D9_SPEC_DWORD( 1 ), 24, 2 ), 1,
                         1 ) == 1u;
    return bPointSprite ? vec4( gl_PointCoord, 0.0, 0.0 ) : texcoord;
}

// Fixed function alpha test (dxvk _DoFixedFunctionAlphaTest).
void d3d9_ps_alpha_test( vec4 color ) {
    uint alphaFunc = bitfieldExtract( D3D9_SPEC_DWORD( 1 ), 21, 3 );
    if ( alphaFunc == 7u ) {
        return;
    }

    uint alphaPrecision = bitfieldExtract( D3D9_SPEC_DWORD( 2 ), 27, 4 );
    float alphaRef;
    float alpha;
    if ( alphaPrecision <= 8u ) {
        alphaRef =
            float( ( render_state.alpha_ref << alphaPrecision ) |
                   ( render_state.alpha_ref >> ( 8u - alphaPrecision ) ) );
        alpha = roundEven( color.w * float( ( 256u << alphaPrecision ) - 1u ) );
    } else {
        alphaRef = float( render_state.alpha_ref ) / 255.0;
        alpha = color.w;
    }

    bool bPass;
    switch ( alphaFunc ) {
    case 0u:
        bPass = false;
        break;
    case 1u:
        bPass = alpha < alphaRef;
        break;
    case 2u:
        bPass = alpha == alphaRef;
        break;
    case 3u:
        bPass = alpha <= alphaRef;
        break;
    case 4u:
        bPass = alpha > alphaRef;
        break;
    case 5u:
        bPass = alpha != alphaRef;
        break;
    case 6u:
        bPass = alpha >= alphaRef;
        break;
    default:
        bPass = true;
        break;
    }

    if ( !bPass ) {
        demote;
    }
}

#endif // D3D9_PIXEL_SHADER

// Float constant registers c0..c255 (VS) / c0..c223 (PS): access them
// through D3D9_C( n ) / D3D9_CR( k, i ). The blocks holding them are generated
// from constant_registers.txt (split by update frequency).
#include "tdu_constants.glsl"

#endif // D3D9_ABI_GLSL
