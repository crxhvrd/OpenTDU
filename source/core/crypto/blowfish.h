#pragma once

// edNet Blowfish (ECB). Standard cipher, but the retail code loads each 8-byte
// block as two native little-endian dwords (no byte swap), so the ciphertext
// differs from the big-endian test vectors for the same key/plaintext.
class BlowFishCrypto {
public:
    static constexpr uint32_t kBlockSize = 8;

public:
    BlowFishCrypto();

    void setKey( const uint8_t *pKey, const int32_t keyLength );

    void encryptBlock( uint32_t &left, uint32_t &right ) const;
    void decryptBlock( uint32_t &left, uint32_t &right ) const;

    // Returns the encrypted size (input size rounded up to kBlockSize).
    // pOut must hold that many bytes; the tail of the last block is zero
    // padded. pIn and pOut may alias.
    uint32_t
    encrypt( const uint8_t *pIn, uint8_t *pOut, const uint32_t size ) const;

    // Like retail, size is rounded up to kBlockSize: both buffers must hold
    // GetEncryptedSize( size ) bytes. pIn and pOut may alias.
    void
    decrypt( const uint8_t *pIn, uint8_t *pOut, const uint32_t size ) const;

    static uint32_t GetEncryptedSize( const uint32_t size ) {
        return ( size + kBlockSize - 1 ) & ~( kBlockSize - 1 );
    }

private:
    uint32_t P[18];
    uint32_t S[4][256];

private:
    inline uint32_t f( const uint32_t x ) const {
        return ( ( S[0][x >> 24] + S[1][( x >> 16 ) & 0xff] ) ^
                 S[2][( x >> 8 ) & 0xff] ) +
               S[3][x & 0xff];
    }
};
