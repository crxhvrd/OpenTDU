#version 450
#extension GL_GOOGLE_include_directive : require

// Terrain shadow map rendering pixel shader.

#define D3D9_PIXEL_SHADER
#include "d3d9_abi.glsl"

D3D9_DECLARE_SAMPLER_2D( 0 )
D3D9_DECLARE_SAMPLER_2D( 1 )

layout( location = D3D9_SLOT_TEXCOORD0 ) in vec4 in_Texcoord0;
layout( location = D3D9_SLOT_TEXCOORD1 ) in vec4 in_Texcoord1;
layout( location = D3D9_SLOT_TEXCOORD2 ) in vec4 in_Texcoord2;
layout( location = 0 ) out vec4 oC0;

void main() {
    vec4 uv = d3d9_ps_texcoord( in_Texcoord0 );
    vec4 positionUV = d3d9_ps_texcoord( in_Texcoord1 );
    vec4 shadowContrast = d3d9_ps_texcoord( in_Texcoord2 );

    float shadow = pow( abs( shadowContrast.x - tex2D_s0( uv.xy ).w + 1.0 ),
                        shadowContrast.y );
    shadow = max( shadow, shadowContrast.z );
    shadow = min( tex2D_s1( positionUV.xy ).x, shadow );

    oC0 = vec4( vec3( shadow ), 1.0 );
    d3d9_ps_alpha_test( oC0 );
}
