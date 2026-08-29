#pragma once

#include "core/binary_helpers.h"
#include "core/maths_helper.h"

static constexpr float kGaussLogScale = -2.0f;     // DAT_00f417d0
static constexpr float kGaussDeviateScale = 0.15f; // DAT_00f41af8
static constexpr float kGaussHalfToFull = 2.0f;    // DAT_00f416b0

// One step of the particle LCG (Numerical Recipes constants).
static constexpr inline uint32_t LCGNext( uint32_t state ) {
    // FUN_00458870
    return state * 0x19660d + 0x3c6ef35f;
}

// Float RNG
inline float RandFloat01( const uint32_t state ) {
    // The RNG builds a mantissa in [1, 2) then subtracts one.
    return BitsToFloat( ( state & 0x7fffff ) | 0x3f800000 ) - kOne;
}

// Float RNG + Seed Update
inline float RandFloat01( uint8_t *pRng ) {
    const uint32_t state = LCGNext( Read<uint32_t>( pRng, 0 ) );
    Write<uint32_t>( pRng, 0, state );

    return RandFloat01( state );
}

inline float
RandGaussian( uint32_t &state, const float center, const float spread ) {
    // FUN_008e23e0
    const uint32_t first = LCGNext( state );
    state = LCGNext( first );

    const float radius =
        std::sqrt( std::log( kOne - RandFloat01( first ) ) * kGaussLogScale );
    float normalized = kHalf + kGaussDeviateScale * radius *
                                   std::cos( RandFloat01( state ) * kTwoPI );
    if ( !( 0.0f <= normalized ) ) {
        normalized = 0.0f;
    } else if ( kOne < normalized ) {
        normalized = kOne;
    }

    return ( normalized * kGaussHalfToFull - kOne ) * spread + center;
}
