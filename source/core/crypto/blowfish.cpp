#include "shared.h"
#include "blowfish.h"

#include "blowfish_constants.inl"
#include "core/binary_helpers.h"

BlowFishCrypto::BlowFishCrypto() {
    // FUN_0062f470
    memcpy( P, kBlowfishInitP, sizeof( P ) );
    memcpy( S, kBlowfishInitS, sizeof( S ) );
}

void BlowFishCrypto::setKey( const uint8_t *pKey, const int32_t keyLength ) {
    // FUN_0062fc10
    memcpy( P, kBlowfishInitP, sizeof( P ) );
    memcpy( S, kBlowfishInitS, sizeof( S ) );

    int32_t keyIndex = 0;
    for ( int32_t i = 0; i < 18; i++ ) {
        uint32_t data = 0;
        for ( int32_t j = 0; j < 4; j++ ) {
            data = ( data << 8 ) | pKey[keyIndex];
            keyIndex = ( keyIndex + 1 ) % keyLength;
        }
        P[i] ^= data;
    }

    uint32_t left = 0;
    uint32_t right = 0;
    for ( int32_t i = 0; i < 18; i += 2 ) {
        encryptBlock( left, right );
        P[i] = left;
        P[i + 1] = right;
    }

    for ( int32_t box = 0; box < 4; box++ ) {
        for ( int32_t i = 0; i < 256; i += 2 ) {
            encryptBlock( left, right );
            S[box][i] = left;
            S[box][i + 1] = right;
        }
    }
}

void BlowFishCrypto::encryptBlock( uint32_t &left, uint32_t &right ) const {
    // FUN_0062f490
    uint32_t l = left;
    uint32_t r = right;
    for ( int32_t i = 0; i < 16; i += 2 ) {
        l ^= P[i];
        r ^= f( l );
        r ^= P[i + 1];
        l ^= f( r );
    }
    l ^= P[16];
    r ^= P[17];

    left = r;
    right = l;
}

void BlowFishCrypto::decryptBlock( uint32_t &left, uint32_t &right ) const {
    // FUN_0062f850
    uint32_t l = left;
    uint32_t r = right;
    for ( int32_t i = 17; i > 1; i -= 2 ) {
        l ^= P[i];
        r ^= f( l );
        r ^= P[i - 1];
        l ^= f( r );
    }
    l ^= P[1];
    r ^= P[0];

    left = r;
    right = l;
}

uint32_t BlowFishCrypto::encrypt( const uint8_t *pIn,
                                  uint8_t *pOut,
                                  const uint32_t size ) const {
    // FUN_00630040
    const uint32_t paddedSize = GetEncryptedSize( size );
    for ( uint32_t offset = 0; offset < paddedSize; offset += kBlockSize ) {
        uint8_t block[kBlockSize] = {};
        const uint32_t remaining = size - Min( size, offset );
        memcpy( block, pIn + offset, Min( remaining, kBlockSize ) );

        uint32_t left = ReadUint32LittleEndian( block );
        uint32_t right = ReadUint32LittleEndian( block + 4 );
        encryptBlock( left, right );
        WriteUint32LittleEndian( pOut + offset, left );
        WriteUint32LittleEndian( pOut + offset + 4, right );
    }
    return paddedSize;
}

void BlowFishCrypto::decrypt( const uint8_t *pIn,
                              uint8_t *pOut,
                              const uint32_t size ) const {
    // FUN_006301b0
    const uint32_t paddedSize = GetEncryptedSize( size );
    for ( uint32_t offset = 0; offset < paddedSize; offset += kBlockSize ) {
        uint32_t left = ReadUint32LittleEndian( pIn + offset );
        uint32_t right = ReadUint32LittleEndian( pIn + offset + 4 );
        decryptBlock( left, right );
        WriteUint32LittleEndian( pOut + offset, left );
        WriteUint32LittleEndian( pOut + offset + 4, right );
    }
}
