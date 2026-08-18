#pragma once

#include "core/binary_helpers.h"
#include "core/maths_helper.h"

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
