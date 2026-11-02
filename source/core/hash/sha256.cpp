#include "shared.h"
#include "sha256.h"

#include <random>

static constexpr uint32_t kRoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

static inline uint32_t RotateRight( const uint32_t value, const uint32_t n ) {
    return ( value >> n ) | ( value << ( 32 - n ) );
}

static void ProcessBlock( uint32_t state[8], const uint8_t *pBlock ) {
    uint32_t w[64];
    for ( int32_t i = 0; i < 16; i++ ) {
        w[i] = ( uint32_t( pBlock[i * 4] ) << 24 ) |
               ( uint32_t( pBlock[i * 4 + 1] ) << 16 ) |
               ( uint32_t( pBlock[i * 4 + 2] ) << 8 ) |
               uint32_t( pBlock[i * 4 + 3] );
    }
    for ( int32_t i = 16; i < 64; i++ ) {
        const uint32_t s0 = RotateRight( w[i - 15], 7 ) ^
                            RotateRight( w[i - 15], 18 ) ^ ( w[i - 15] >> 3 );
        const uint32_t s1 = RotateRight( w[i - 2], 17 ) ^
                            RotateRight( w[i - 2], 19 ) ^ ( w[i - 2] >> 10 );
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for ( int32_t i = 0; i < 64; i++ ) {
        const uint32_t s1 =
            RotateRight( e, 6 ) ^ RotateRight( e, 11 ) ^ RotateRight( e, 25 );
        const uint32_t ch = ( e & f ) ^ ( ~e & g );
        const uint32_t t1 = h + s1 + ch + kRoundConstants[i] + w[i];
        const uint32_t s0 =
            RotateRight( a, 2 ) ^ RotateRight( a, 13 ) ^ RotateRight( a, 22 );
        const uint32_t maj = ( a & b ) ^ ( a & c ) ^ ( b & c );
        const uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

SHA256Digest_t SHA256( const void *pData, const size_t size ) {
    uint32_t state[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };

    const uint8_t *pBytes = static_cast<const uint8_t *>( pData );
    size_t offset = 0;
    for ( ; offset + 64 <= size; offset += 64 ) {
        ProcessBlock( state, pBytes + offset );
    }

    uint8_t tail[128] = {};
    const size_t remaining = size - offset;
    memcpy( tail, pBytes + offset, remaining );
    tail[remaining] = 0x80;
    const size_t tailSize = ( remaining < 56 ) ? 64 : 128;
    const uint64_t bitLength = uint64_t( size ) * 8;
    for ( int32_t i = 0; i < 8; i++ ) {
        tail[tailSize - 1 - i] = uint8_t( bitLength >> ( i * 8 ) );
    }
    ProcessBlock( state, tail );
    if ( tailSize == 128 ) {
        ProcessBlock( state, tail + 64 );
    }

    SHA256Digest_t digest;
    for ( int32_t i = 0; i < 8; i++ ) {
        digest[i * 4] = uint8_t( state[i] >> 24 );
        digest[i * 4 + 1] = uint8_t( state[i] >> 16 );
        digest[i * 4 + 2] = uint8_t( state[i] >> 8 );
        digest[i * 4 + 3] = uint8_t( state[i] );
    }
    return digest;
}

std::string ToHexString( const uint8_t *pData, const size_t size ) {
    static constexpr const char *kDigits = "0123456789abcdef";
    std::string hex( size * 2, '0' );
    for ( size_t i = 0; i < size; i++ ) {
        hex[i * 2] = kDigits[pData[i] >> 4];
        hex[i * 2 + 1] = kDigits[pData[i] & 0xf];
    }
    return hex;
}

std::string HashPassword( const std::string &password,
                          const std::string &saltHex ) {
    static constexpr int32_t kIterations = 20000;

    std::string input = saltHex + ":" + password;
    SHA256Digest_t digest = SHA256( input.data(), input.size() );
    for ( int32_t i = 1; i < kIterations; i++ ) {
        std::string round( reinterpret_cast<const char *>( digest.data() ),
                           digest.size() );
        round += saltHex;
        digest = SHA256( round.data(), round.size() );
    }
    return ToHexString( digest.data(), digest.size() );
}

std::string GenerateSaltHex() {
    std::random_device device;
    uint8_t salt[16];
    for ( uint8_t &byte : salt ) {
        byte = uint8_t( device() );
    }
    return ToHexString( salt, sizeof( salt ) );
}
