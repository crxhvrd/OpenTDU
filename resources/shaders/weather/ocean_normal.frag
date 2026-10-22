#version 450
#extension GL_GOOGLE_include_directive : require

// Ocean normal map passes (retail ocean table): the normal from the height
// map (s0) neighbours (heights scaled by gPSMaterialParam), packed in
// [0, 1]. The operand orders are retail's.
//
// Permutations:
//   OCEAN_NORMAL_PROJECTED  neighbours sampled around Texcoord0
//                           (gPSMaterialParam.xy apart), positioned on the
//                           projected grid (Texcoord1..4)

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

#define gPSMaterialParam c.f[4]

D3D9_DECLARE_SAMPLER_2D( 0 )
layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
layout( location = D3D9_SLOT_TEXCOORD3 ) in vec4 in_Texcoord3;
#ifdef OCEAN_NORMAL_PROJECTED
layout( location = D3D9_SLOT_TEXCOORD4 ) in vec4 in_Texcoord4;
#endif
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 t0 = d3d9_ps_texcoord( in_Texcoord0 );
    vec4 t1 = d3d9_ps_texcoord( in_Texcoord1 );
    vec4 t2 = d3d9_ps_texcoord( in_Texcoord2 );
    vec4 t3 = d3d9_ps_texcoord( in_Texcoord3 );
#ifdef OCEAN_NORMAL_PROJECTED
    vec4 t4 = d3d9_ps_texcoord( in_Texcoord4 );
    float scale = gPSMaterialParam.z;
    float rightHeight =
        tex2D_s0( vec2( t0.x + gPSMaterialParam.x, t0.y ) ).x * scale;
    float leftHeight =
        tex2D_s0( vec2( t0.x + -gPSMaterialParam.x, t0.y ) ).x * scale;
    float downHeight =
        tex2D_s0( vec2( t0.x, t0.y + gPSMaterialParam.y ) ).x * scale;
    float upHeight =
        tex2D_s0( vec2( t0.x, t0.y + -gPSMaterialParam.y ) ).x * scale;
    vec3 tangentX = vec3( t1.zx, rightHeight ) + -vec3( t3.zx, leftHeight );
    vec3 tangentY = vec3( downHeight, t2.zx ) + -vec3( upHeight, t4.zx );
    vec3 normal = tangentX.zxy * tangentY.yzx + -( tangentX * tangentY );
    oC0 = vec4( normalize( -normal ) * vec3( 0.5 ) + vec3( 0.5 ), 1.0 );
#else
    vec4 heights = vec4( tex2D_s0( t0.xy ).x, tex2D_s0( t1.xy ).x,
                         tex2D_s0( t2.xy ).x, tex2D_s0( t3.xy ).x ) *
                   gPSMaterialParam.xxxx;
    vec3 tangentX = vec3( heights.y, t1.yx ) + -vec3( heights.x, t0.yx );
    vec3 tangentY = vec3( t3.yx, heights.w ) + -vec3( t2.yx, heights.z );
    vec3 normal = tangentX.yzx * tangentY.zxy + -( tangentX * tangentY );
    float lengthSquared =
        normal.y * normal.y + ( normal.z * normal.z + normal.x * normal.x );
    normal *= inversesqrt( abs( lengthSquared ) );
    oC0 = vec4( normal * vec3( 0.5 ) + vec3( 0.5 ), 0.0 );
#endif
    d3d9_ps_alpha_test( oC0 );
}
