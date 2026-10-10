#pragma once

template <typename T> T Read( const uint8_t *pData, uint32_t offset = 0 ) {
    T value;
    std::memcpy( (void *)&value, pData + offset, sizeof( T ) );
    return value;
}

template <typename T> void Write( uint8_t *pData, uint32_t offset, T value ) {
    std::memcpy( pData + offset, &value, sizeof( T ) );
}

static inline float ReadFloat( const uint8_t *pData ) {
    return Read<float>( pData );
}

static inline double ReadDouble( const uint8_t *pData ) {
    return Read<double>( pData );
}

static inline int16_t ReadI16( const uint8_t *pData ) {
    return Read<int16_t>( pData );
}

static inline uint16_t ReadU16( const uint8_t *pData ) {
    return Read<uint16_t>( pData );
}

static inline int32_t ReadInt32( const uint8_t *pData ) {
    return Read<int32_t>( pData );
}

static inline uint32_t ReadUint32( const uint8_t *pData ) {
    return Read<uint32_t>( pData );
}

static inline int64_t ReadInt64( const uint8_t *pData ) {
    return Read<int64_t>( pData );
}

// Reinterpret a bit pattern as a float
static inline float BitsToFloat( uint32_t bits ) {
    float value;
    std::memcpy( &value, &bits, sizeof( value ) );
    return value;
}

static inline constexpr uint32_t ColorByte( float channel ) {
    float scaled = channel * 255.0f;
    if ( scaled <= 0.0f ) {
        return 0;
    }
    if ( scaled >= 255.0f ) {
        return 255;
    }
    return static_cast<uint32_t>( scaled + 0.5f );
}

static inline uint32_t ReverseEndianness32( uint32_t value ) {
    return ( ( value & 0x000000ffu ) << 24 ) |
           ( ( value & 0x0000ff00u ) << 8 ) | ( ( value & 0x00ff0000u ) >> 8 ) |
           ( ( value & 0xff000000u ) >> 24 );
}

static inline uint16_t ReverseEndianness16( uint16_t value ) {
    return (uint16_t)( ( value >> 8 ) | ( value << 8 ) );
}

static inline int32_t FloatToInt( float value ) {
    // FUN_00674eb4
    return (int32_t)value;
}

static float UIntToFloat( const int32_t value ) {
    return static_cast<float>( static_cast<uint32_t>( value ) );
}
