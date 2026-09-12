#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout( set = 0, binding = 0 ) uniform texture2D uBackbuffer;
layout( set = 0, binding = 1, std140 ) uniform GammaRampBuffer {
    vec4 uGammaRamp[256];
};

layout( location = 0 ) out vec4 oColor;

void main() {
    vec4 color = texelFetch( uBackbuffer, ivec2( gl_FragCoord.xy ), 0 );

    vec3 position = clamp( color.rgb, 0.0, 1.0 ) * 255.0;
    ivec3 index = min( ivec3( position ), ivec3( 254 ) );
    vec3 weight = position - vec3( index );

    vec3 low = vec3( uGammaRamp[index.r].r, uGammaRamp[index.g].g,
                     uGammaRamp[index.b].b );
    vec3 high = vec3( uGammaRamp[index.r + 1].r, uGammaRamp[index.g + 1].g,
                      uGammaRamp[index.b + 1].b );

    oColor = vec4( mix( low, high, weight ), color.a );
}
