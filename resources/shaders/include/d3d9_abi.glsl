// D3D9 shader ABI for lifted retail shaders.
//
// Lifted shaders are drop-in replacements for the SPIR-V that dxvk's DXSO
// compiler produces from the retail bytecode: the runtime keeps using the
// metadata (bindings, ISGN, push constant size) extracted from the dxvk
// module, so every resource, push constant, spec constant and interface
// location declared here MUST match what dxvk emits.
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

#ifdef D3D9_VERTEX_SHADER

layout( set = 0, binding = 0, std140 ) uniform D3D9VSConstants {
    ivec4 i[1];
    vec4 f[256];
}
c;

layout( set = 0, binding = 3, std140 ) uniform D3D9ClipPlanes {
    vec4 clip_planes[6];
}
clip_info;

out float gl_ClipDistance[6];

// Runs after the shader body (dxvk emitLinkerOutputSetup + emitVsClipping).
void d3d9_vs_epilogue() {
    gl_PointSize = clamp( render_state.point_size, render_state.point_size_min,
                          render_state.point_size_max );

    uint clipPlaneCount =
        bitfieldExtract( ( D3D9_SPEC_ENABLED != 0u ) ? D3D9_SPEC_DWORD5 : 6u,
                         21, 3 );
    for ( uint i = 0u; i < 6u; i++ ) {
        gl_ClipDistance[i] = ( i < clipPlaneCount )
                                 ? dot( gl_Position, clip_info.clip_planes[i] )
                                 : 0.0;
    }
}

#endif // D3D9_VERTEX_SHADER

#ifdef D3D9_PIXEL_SHADER

layout( set = 0, binding = 10, std140 ) uniform D3D9PSConstants {
    ivec4 i[1];
    vec4 f[224];
}
c;

layout( set = 1, binding = 0 ) uniform sampler d3d9_sampler_heap[];

#define D3D9_SPEC_DWORD( n ) \
    ( ( D3D9_SPEC_ENABLED != 0u ) ? D3D9_SPEC_DWORD##n : spec_state.dword##n )

#define D3D9_SAMPLER( n )                                                    \
    d3d9_sampler_heap[bitfieldExtract( render_state.sampler_idx[( n ) >> 1], \
                                       ( ( n ) & 1 ) * 16, 16 )]

#define D3D9_TEXTURE_BINDING( n ) ( 13 + ( n ) )

// Declares texture register sN and its tex2D_sN / texCUBE_sN / tex3D_sN
// helper.
#define D3D9_DECLARE_SAMPLER_2D( n )                                           \
    layout( set = 0, binding = D3D9_TEXTURE_BINDING( n ) ) uniform texture2D \
        t##n##_2d;                                                             \
    vec4 tex2D_s##n( vec2 uv ) {                                               \
        return texture( sampler2D( t##n##_2d, D3D9_SAMPLER( n ) ), uv );       \
    }                                                                          \
    vec4 tex2Dbias_s##n( vec2 uv, float bias ) {                               \
        return texture( sampler2D( t##n##_2d, D3D9_SAMPLER( n ) ), uv, bias ); \
    }

#define D3D9_DECLARE_SAMPLER_CUBE( n )                                          \
    layout( set = 0, binding = D3D9_TEXTURE_BINDING( n ) ) uniform textureCube \
        t##n##_cube;                                                            \
    vec4 texCUBE_s##n( vec3 uvw ) {                                             \
        return texture( samplerCube( t##n##_cube, D3D9_SAMPLER( n ) ), uvw );   \
    }                                                                           \
    vec4 texCUBEbias_s##n( vec3 uvw, float bias ) {                             \
        return texture( samplerCube( t##n##_cube, D3D9_SAMPLER( n ) ), uvw,     \
                        bias );                                                 \
    }

#define D3D9_DECLARE_SAMPLER_3D( n )                                         \
    layout( set = 0, binding = D3D9_TEXTURE_BINDING( n ) ) uniform texture3D \
        t##n##_3d;                                                           \
    vec4 tex3D_s##n( vec3 uvw ) {                                            \
        return texture( sampler3D( t##n##_3d, D3D9_SAMPLER( n ) ), uvw );    \
    }                                                                        \
    vec4 tex3Dbias_s##n( vec3 uvw, float bias ) {                            \
        return texture( sampler3D( t##n##_3d, D3D9_SAMPLER( n ) ), uvw,      \
                        bias );                                              \
    }

// Texture coordinate inputs are replaced by gl_PointCoord when point sprites
// are enabled (SpecPointMode bit 1).
vec4 d3d9_ps_texcoord( vec4 texcoord ) {
    bool bPointSprite =
        bitfieldExtract( bitfieldExtract( D3D9_SPEC_DWORD( 1 ), 24, 2 ), 1, 1 ) ==
        1u;
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
        alphaRef = float( ( render_state.alpha_ref << alphaPrecision ) |
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

#endif // D3D9_ABI_GLSL
