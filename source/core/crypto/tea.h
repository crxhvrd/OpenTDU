#pragma once

static constexpr uint32_t kTEAKeysDefault[4] = {
    0x64EA432C, 0xF8A35B24, 0x18ECD81, 0x8326BEAC }; // DAT_00f99f28

static void TestDriveTEADecrypt( uint32_t *pOutV,
                                 const uint32_t *pInV,
                                 const uint32_t k[4] ) {
    uint32_t sum = 0xc6ef3720;

    uint32_t v0 = pInV[0];
    uint32_t v1 = pInV[1];

    for ( int32_t i = 0; i < 32; i++ ) {
        v1 = v1 - ( ( v0 >> 5 ^ v0 << 4 ) + v0 ^ k[sum >> 0xb & 3] + sum );
        sum += 0x61c88647;
        v0 = v0 - ( ( v1 >> 5 ^ v1 * 0x10 ) + v1 ^ k[sum & 3] + sum );
    }

    pOutV[0] = v0;
    pOutV[1] = v1;
}

static void TestDriveTEAEncrypt( uint32_t *pOutV,
                                 const uint32_t *pInV,
                                 const uint32_t k[4] ) {
    uint32_t sum = 0;

    uint32_t v0 = pInV[0];
    uint32_t v1 = pInV[1];

    for ( int32_t i = 0; i < 32; i++ ) {
        v0 = v0 + ( ( v1 >> 5 ^ v1 << 4 ) + v1 ^ k[sum & 3] + sum );
        sum += 0x9e3779b9;
        v1 = v1 + ( ( v0 >> 5 ^ v0 * 0x10 ) + v0 ^ k[sum >> 0xb & 3] + sum );
    }

    pOutV[0] = v0;
    pOutV[1] = v1;
}

inline void TestDriveTEADecrypt( uint32_t *pOutV, const uint32_t *pInV ) {
    TestDriveTEADecrypt( pOutV, pInV, kTEAKeysDefault );
}

inline void TestDriveTEAEncrypt( uint32_t *pOutV, const uint32_t *pInV ) {
    TestDriveTEAEncrypt( pOutV, pInV, kTEAKeysDefault );
}

inline uint32_t TestDriveTEAReadDword( const uint32_t pBlock[2] ) {
    uint32_t pPlainText[2] = {};
    TestDriveTEADecrypt( pPlainText, pBlock );
    return pPlainText[0];
}

inline void TestDriveTEAWriteDword( uint32_t pBlock[2], const uint32_t value ) {
    uint32_t pPlainText[2] = { value, 0u };
    TestDriveTEAEncrypt( pBlock, pPlainText );
}

inline float TestDriveTEAReadFloat( const uint32_t pBlock[2] ) {
    const uint32_t asDword = TestDriveTEAReadDword( pBlock );

    float asFloat = 0.0f;
    memcpy( &asFloat, &asDword, sizeof( float ) );
    return asFloat;
}

inline void TestDriveTEAWriteFloat( uint32_t pBlock[2], const float value ) {
    uint32_t asDword = 0u;
    memcpy( &asDword, &value, sizeof( uint32_t ) );

    TestDriveTEAWriteDword( pBlock, asDword );
}

inline uint64_t TestDriveTEAReadQword( const uint32_t pBlock[2] ) {
    uint32_t pPlainText[2] = {};
    TestDriveTEADecrypt( pPlainText, pBlock );
    return uint64_t( pPlainText[0] ) | ( uint64_t( pPlainText[1] ) << 32ull );
}

inline void TestDriveTEAWriteQword( uint32_t pBlock[2], const uint64_t value ) {
    uint32_t pPlainText[2] = { uint32_t( value ), uint32_t( value >> 32ull ) };
    TestDriveTEAEncrypt( pBlock, pPlainText );
}

inline void TestDriveTEADecryptBuffer( void *pBuffer, const size_t size ) {
    // FUN_006de080
    uint32_t *pBlock = (uint32_t *)pBuffer;

    for ( size_t numBlocks = size >> 3; numBlocks != 0; numBlocks-- ) {
        uint32_t pPlainText[2] = {};
        TestDriveTEADecrypt( pPlainText, pBlock );

        pBlock[0] = pPlainText[0];
        pBlock[1] = pPlainText[1];
        pBlock += 2;
    }
}

inline void TestDriveTEAEncryptBuffer( void *pBuffer, const size_t size ) {
    uint32_t *pBlock = (uint32_t *)pBuffer;

    for ( size_t numBlocks = size >> 3; numBlocks != 0; numBlocks-- ) {
        uint32_t pCipherText[2] = {};
        TestDriveTEAEncrypt( pCipherText, pBlock );

        pBlock[0] = pCipherText[0];
        pBlock[1] = pCipherText[1];
        pBlock += 2;
    }
}
