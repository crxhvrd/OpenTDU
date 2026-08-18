#pragma once

template <typename T> T Read( const uint8_t *pData, uint32_t offset = 0 ) {
    T value;
    std::memcpy( &value, pData + offset, sizeof( T ) );
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
