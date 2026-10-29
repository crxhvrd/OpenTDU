#ifndef TDU_POSTFX_GLSL
#define TDU_POSTFX_GLSL

// Fullscreen post process passes (retail PostFX shader table).

#define TDU_LUMINANCE vec3( 0.299, 0.587, 0.114 )

#ifdef D3D9_VERTEX_SHADER
#define gPostFXScreenDim D3D9_C( 56 )

// Clip space quad shifted by half a pixel (D3D9 texel centers).
vec4 tdu_postfx_position( vec4 position ) {
    return vec4( -( 1.0 / gPostFXScreenDim.x ) + position.x,
                 ( 1.0 / gPostFXScreenDim.y ) + position.y, position.zw );
}

vec2 tdu_postfx_uv( vec4 position ) {
    return position.xy * vec2( 0.5, -0.5 ) + vec2( 0.5 );
}
#endif

#endif
