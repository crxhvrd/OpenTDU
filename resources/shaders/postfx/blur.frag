#version 450
#extension GL_GOOGLE_include_directive : require

// Weighted blur over the taps of blur.vert. blur_params: center weight (x),
// then the weight of the taps 1, 2, 3 steps away (y, z, w); blur_params2.x:
// 4 steps away.
//
// Permutations: see blur.vert (BLUR_BOX: plain average).

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define blur_params D3D9_C( 3 )
#define blur_params2 D3D9_C( 4 )

D3D9_DECLARE_SAMPLER_2D( 0 )
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
#if !defined( BLUR_LINEAR ) || BLUR_LINEAR >= 3
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
#endif
#if defined( BLUR_CROSS ) || defined( BLUR_GRID ) ||                           \
    ( defined( BLUR_LINEAR ) && BLUR_LINEAR >= 4 )
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
#endif
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 t0 = d3d9_ps_texcoord( in_Texcoord0 );
    vec4 t1 = d3d9_ps_texcoord( in_Texcoord1 );
    vec4 t2 = d3d9_ps_texcoord( in_Texcoord2 );
#if !defined( BLUR_LINEAR ) || BLUR_LINEAR >= 3
    vec4 t3 = d3d9_ps_texcoord( in_Texcoord3 );
#endif
#if defined( BLUR_CROSS ) || defined( BLUR_GRID ) ||                           \
    ( defined( BLUR_LINEAR ) && BLUR_LINEAR >= 4 )
    vec4 t4 = d3d9_ps_texcoord( in_Texcoord4 );
#endif

    vec4 color;
#if defined( BLUR_LINEAR ) && BLUR_LINEAR == 2
    color = tex2D_s0( t0.zw ) * blur_params.yyyy;
    color = blur_params.zzzz * tex2D_s0( t0.xy ) + color;
    color = blur_params.xxxx * tex2D_s0( t2.xy ) + color;
    color = blur_params.yyyy * tex2D_s0( t1.xy ) + color;
    color = blur_params.zzzz * tex2D_s0( t1.zw ) + color;
#elif defined( BLUR_LINEAR ) && BLUR_LINEAR == 3
    color = tex2D_s0( t0.zw ) * blur_params.zzzz;
    color = blur_params.wwww * tex2D_s0( t0.xy ) + color;
    color = blur_params.yyyy * tex2D_s0( t1.xy ) + color;
    color = blur_params.xxxx * tex2D_s0( t3.xy ) + color;
    color = blur_params.yyyy * tex2D_s0( t1.zw ) + color;
    color = blur_params.zzzz * tex2D_s0( t2.xy ) + color;
    color = blur_params.wwww * tex2D_s0( t2.zw ) + color;
#elif defined( BLUR_LINEAR ) && BLUR_LINEAR == 4
    color = tex2D_s0( t0.zw ) * blur_params.wwww;
    color = blur_params2.xxxx * tex2D_s0( t0.xy ) + color;
    color = blur_params.zzzz * tex2D_s0( t1.xy ) + color;
    color = blur_params.yyyy * tex2D_s0( t1.zw ) + color;
    color = blur_params.xxxx * tex2D_s0( t4.xy ) + color;
    color = blur_params.yyyy * tex2D_s0( t2.xy ) + color;
    color = blur_params.zzzz * tex2D_s0( t2.zw ) + color;
    color = blur_params.wwww * tex2D_s0( t3.xy ) + color;
    color = blur_params2.xxxx * tex2D_s0( t3.zw ) + color;
#elif defined( BLUR_CROSS )
    color = tex2D_s0( t1.xy ) * blur_params.yyyy;
    color = blur_params.yyyy * tex2D_s0( t0.xy ) + color;
    color = blur_params.yyyy * tex2D_s0( t2.xy ) + color;
    color = blur_params.yyyy * tex2D_s0( t3.xy ) + color;
    color = blur_params.xxxx * tex2D_s0( t4.xy ) + color;
#elif defined( BLUR_BOX )
    color = tex2D_s0( t0.xy ) + tex2D_s0( t1.xy );
    color = tex2D_s0( t2.xy ) + color;
    color = tex2D_s0( t3.xy ) + color;
    color *= vec4( 0.25 );
#elif defined( BLUR_GRID )
    color = tex2D_s0( t0.zw ) * blur_params.yyyy;
    color = blur_params.yyyy * tex2D_s0( t0.xy ) + color;
    color = blur_params.yyyy * tex2D_s0( t1.xy ) + color;
    color = blur_params.yyyy * tex2D_s0( t1.zw ) + color;
    color = blur_params.zzzz * tex2D_s0( t2.xy ) + color;
    color = blur_params.zzzz * tex2D_s0( t2.zw ) + color;
    color = blur_params.zzzz * tex2D_s0( t3.xy ) + color;
    color = blur_params.zzzz * tex2D_s0( t3.zw ) + color;
    color = blur_params.xxxx * tex2D_s0( t4.xy ) + color;
#endif
    oC0 = color;
    d3d9_ps_alpha_test( oC0 );
}
