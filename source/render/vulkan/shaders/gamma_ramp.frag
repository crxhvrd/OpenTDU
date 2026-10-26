#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout( set = 0, binding = 0 ) uniform texture2D uBackbuffer;
layout( set = 0, binding = 1, std140 ) uniform GammaRampBuffer {
    vec4 uGammaRamp[256];
};

// Must match eOutputTransfer (gamma_ramp_pass.h)
const uint OT_SDR = 0u;
const uint OT_PQ = 1u;
const uint OT_ExtendedLinear = 2u;

layout( push_constant ) uniform OutputConstants {
    uint uOutputTransfer;
    uint uApplyRamp;
    float uOutputScale;
    uint uTestPattern;
    float uOutputPeak;
    float uHighlightStrength;
};

layout( location = 0 ) out vec4 oColor;

// HDR output: values above 1.0 (highlights) keep their excess over the ramp
vec3 ApplyGammaRamp( vec3 color ) {
    vec3 excess = max( color - vec3( 1.0 ), vec3( 0.0 ) );
    vec3 position = clamp( color, 0.0, 1.0 ) * 255.0;
    ivec3 index = min( ivec3( position ), ivec3( 254 ) );
    vec3 weight = position - vec3( index );

    vec3 low = vec3( uGammaRamp[index.r].r, uGammaRamp[index.g].g,
                     uGammaRamp[index.b].b );
    vec3 high = vec3( uGammaRamp[index.r + 1].r, uGammaRamp[index.g + 1].g,
                      uGammaRamp[index.b + 1].b );
    return mix( low, high, weight ) + excess;
}

// Identity up to the paper white (1.0), then an exponential shoulder with a
// unit slope at 1.0 that approaches the display peak. Per channel, so very
// bright colors bleach to white like the retail clip
vec3 RollOffHighlights( vec3 linear709 ) {
    float range = uOutputPeak - 1.0;
    vec3 excess =
        max( linear709 - vec3( 1.0 ), vec3( 0.0 ) ) * uHighlightStrength;
    vec3 rolled = ( range > 0.0 )
                      ? vec3( 1.0 ) +
                            range * ( vec3( 1.0 ) - exp( -excess / range ) )
                      : vec3( 1.0 );
    return mix( linear709, rolled, step( vec3( 1.0 ), linear709 ) );
}

// SMPTE ST 2084, input normalized to 10000 nits
vec3 EncodePQ( vec3 value ) {
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;

    vec3 ym1 = pow( clamp( value, 0.0, 1.0 ), vec3( m1 ) );
    return pow( ( c1 + c2 * ym1 ) / ( 1.0 + c3 * ym1 ), vec3( m2 ) );
}

// Linear values relative to the SDR white, top right corner: greys, primaries
// and a ramp from 1/8 to 16 (one stop per patch width)
bool GetTestPatternColor( out vec3 linear709 ) {
    const float kPatchSize = 64.0;
    const float kPatchStride = 72.0;
    const float kGreys[7] = float[]( 0.0, 0.18, 0.5, 1.0, 2.0, 4.0, 8.0 );
    const vec3 kPrimaries[7] =
        vec3[]( vec3( 1.0, 0.0, 0.0 ), vec3( 0.0, 1.0, 0.0 ),
                vec3( 0.0, 0.0, 1.0 ), vec3( 4.0, 0.0, 0.0 ),
                vec3( 0.0, 4.0, 0.0 ), vec3( 0.0, 0.0, 4.0 ),
                vec3( 4.0, 4.0, 0.0 ) );

    vec2 size = vec2( textureSize( uBackbuffer, 0 ) );
    vec2 origin = vec2( size.x - 7.0 * kPatchStride - 24.0, 24.0 );
    vec2 position = gl_FragCoord.xy - origin;
    if ( position.x < 0.0 || position.y < 0.0 ||
         position.x >= 7.0 * kPatchStride || position.y >= 3.0 * kPatchStride ) {
        return false;
    }

    int row = int( position.y / kPatchStride );
    if ( row == 2 ) {
        if ( position.y - 2.0 * kPatchStride >= kPatchSize ) {
            return false;
        }
        float stops = position.x / kPatchStride - 3.0;
        linear709 = vec3( exp2( stops ) );
        return true;
    }

    int column = int( position.x / kPatchStride );
    if ( mod( position.x, kPatchStride ) >= kPatchSize ||
         mod( position.y, kPatchStride ) >= kPatchSize ) {
        return false;
    }
    linear709 = ( row == 0 ) ? vec3( kGreys[column] ) : kPrimaries[column];
    return true;
}

void main() {
    vec4 color = texelFetch( uBackbuffer, ivec2( gl_FragCoord.xy ), 0 );
    vec3 rgb = ( uApplyRamp != 0u ) ? ApplyGammaRamp( color.rgb ) : color.rgb;

    vec3 patternColor;
    bool bPattern = ( uTestPattern != 0u ) && GetTestPatternColor( patternColor );
    if ( bPattern && uOutputTransfer == OT_SDR ) {
        rgb = pow( clamp( patternColor, 0.0, 1.0 ), vec3( 1.0 / 2.2 ) );
    }

    if ( uOutputTransfer != OT_SDR ) {
        // The game targets gamma 2.2 displays (not the sRGB piecewise curve)
        // Above 1.0 the backbuffer holds the composition's highlights
        vec3 linear709 = bPattern ? patternColor
                                  : pow( max( rgb, vec3( 0.0 ) ), vec3( 2.2 ) );
        linear709 = RollOffHighlights( linear709 );

        if ( uOutputTransfer == OT_PQ ) {
            const mat3 kRec709ToRec2020 =
                mat3( 0.6274040, 0.0690970, 0.0163916, 0.3292820, 0.9195400,
                      0.0880132, 0.0433136, 0.0113612, 0.8955950 );
            rgb = EncodePQ( ( kRec709ToRec2020 * linear709 ) * uOutputScale );
        } else {
            rgb = linear709 * uOutputScale;
        }
    }

    oColor = vec4( rgb, color.a );
}
