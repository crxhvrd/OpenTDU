#include "shared.h"
#include "ed_store.h"

#include "core/hash/crc32.h"

static uint8_t gUseHalfPrecisionIntegers = 0xff; // DAT_00f56a70

static uint64_t SwapEightBytes( const uint64_t value ) {
    // FUN_00622c30 / FUN_00622d50
    return ( value >> 56 ) | ( ( value >> 40 ) & 0x000000000000ff00ull ) |
           ( ( value >> 24 ) & 0x0000000000ff0000ull ) |
           ( ( value >> 8 ) & 0x00000000ff000000ull ) |
           ( ( value << 8 ) & 0x000000ff00000000ull ) |
           ( ( value << 24 ) & 0x0000ff0000000000ull ) |
           ( ( value << 40 ) & 0x00ff000000000000ull ) | ( value << 56 );
}

edStore::edStore()
    : ID( 0 ),
      bufferSize( 0 ),
      bufferSizeWithPadding( 0 ),
      bufferOffset( 0 ),
      isExternalBuffer( 0 ),
      CRC( 0 ),
      state( 0 ),
      pBuffer( nullptr ) {
    // FUN_0062449
    TestDriveCRC32( nullptr, 0 ); // Init CRC32 LUT
    if ( gUseHalfPrecisionIntegers == 0xff ) {
        gUseHalfPrecisionIntegers = true;
    }
}

edStore::~edStore() {
    // FUN_00622ff0
    if ( isExternalBuffer == 0 && pBuffer != nullptr ) {
        TestDrive::Free( pBuffer );
    }
    pBuffer = nullptr;
}

void edStore::bindExternalBuffer( void *pExtBuffer, uint32_t bufferLength ) {
    // FUN_00623180
    clear();
    pBuffer = pExtBuffer;
    bufferSizeWithPadding = bufferLength;
    bufferSize = bufferLength;
    isExternalBuffer = 1;
    prepareBuffer();
}

void edStore::clear() {
    // FUN_00623020
    if ( isExternalBuffer == 0 ) {
        if ( pBuffer != nullptr ) {
            TestDrive::Free( pBuffer );
            pBuffer = nullptr;
        }
        bufferSizeWithPadding = 0;
    }
    bufferSize = 0;
    bufferOffset = 0;
    CRC = 0;
    state = 0;
}

int32_t edStore::reserve_buffer( int32_t sizeToReserve ) {
    // FUN_00623060
    if ( isExternalBuffer != 0 ) {
        if ( bufferSizeWithPadding <= bufferOffset + sizeToReserve ) {
            OTDU_LOG_WARN( "**WARNING** edStore::reserve_buffer  "
                           "m_external_buffer too small (%d bytes)\n",
                           this->bufferSizeWithPadding );
            state = 0xff;
            return -1;
        }
        bufferSize = bufferSize + sizeToReserve;
        return 0;
    }
    if ( pBuffer == nullptr ) {
        void *puVar1 = TestDrive::Alloc( sizeToReserve );
        pBuffer = puVar1;
        if ( puVar1 != nullptr ) {
            bufferSizeWithPadding = sizeToReserve;
            bufferSize = sizeToReserve;
            return 0;
        }
        bufferSizeWithPadding = 0;
        bufferSize = 0;
        OTDU_LOG_WARN(
            "**WARNING** edStore::reserve_buffer m_buffer == NULL\n" );
        state = 0xff;
        return -1;
    }

    uint32_t iVar2 = bufferSize + sizeToReserve;
    if ( bufferSizeWithPadding < iVar2 ) {
        void *puVar1 = TestDrive::Realloc(
            pBuffer, bufferSizeWithPadding + 0x200 + sizeToReserve );
        pBuffer = puVar1;
        if ( puVar1 == nullptr ) {
            bufferSizeWithPadding = 0;
            bufferSize = 0;
            OTDU_LOG_WARN(
                "**WARNING** edStore::reserve_buffer m_buffer == NULL\n" );
            state = 0xff;
            return -1;
        }
        iVar2 = bufferSize + sizeToReserve;
        bufferSizeWithPadding = iVar2 + 0x200;
    }
    bufferSize = iVar2;
    return 0;
}

int32_t edStore::seek( const uint32_t offset ) {
    // FUN_00623750
    bufferOffset = offset;
    return 0;
}

void edStore::writeInteger( int32_t param_2 ) {
    // FUN_006234b0
    int32_t iVar1 = 0;
    if ( state != 0xff ) {
        iVar1 = reserve_buffer( 4 );
        if ( iVar1 == 0 ) {
            packAndWriteToBuffer(
                param_2, (int32_t *)( (uint8_t *)pBuffer + bufferOffset ) );
            bufferOffset = bufferOffset + 4;
        }
    }
}

void edStore::writeByte( const uint8_t value ) {
    // FUN_00623240
    if ( state == 0xff || reserve_buffer( 1 ) != 0 ) {
        return;
    }

    *( (uint8_t *)pBuffer + bufferOffset ) = value;
    bufferOffset += 1;
}

void edStore::writeUShort( const uint16_t value ) {
    // FUN_00623270
    if ( state == 0xff || reserve_buffer( 2 ) != 0 ) {
        return;
    }

    uint16_t packedValue = value;
    if ( gUseHalfPrecisionIntegers == '\x01' ) {
        packedValue = uint16_t( ( packedValue >> 8 & 0xff ) |
                                ( packedValue & 0xff ) << 8 );
    }

    uint16_t *pDest = (uint16_t *)( (uint8_t *)pBuffer + bufferOffset );
    if ( pDest != nullptr ) {
        *pDest = packedValue;
    }

    bufferOffset += 2;
}

void edStore::writeULongLong( const uint64_t value ) {
    // FUN_006234f0
    if ( state == 0xff || reserve_buffer( 8 ) != 0 ) {
        return;
    }

    uint64_t packedValue = value;
    if ( gUseHalfPrecisionIntegers == '\x01' ) {
        packedValue = SwapEightBytes( packedValue );
    }

    uint64_t *pDest = (uint64_t *)( (uint8_t *)pBuffer + bufferOffset );
    if ( pDest != nullptr ) {
        *pDest = packedValue;
    }

    bufferOffset += 8;
}

void edStore::writeFloat( const float value ) {
    // FUN_00623540
    if ( state == 0xff || reserve_buffer( 4 ) != 0 ) {
        return;
    }

    uint32_t asDword = 0u;
    memcpy( &asDword, &value, sizeof( uint32_t ) );

    packAndWriteToBuffer( int32_t( asDword ),
                          (int32_t *)( (uint8_t *)pBuffer + bufferOffset ) );
    bufferOffset += 4;
}

void edStore::writeDouble( const double value ) {
    // FUN_00623580
    if ( state == 0xff || reserve_buffer( 8 ) != 0 ) {
        return;
    }

    uint64_t asQword = 0ull;
    memcpy( &asQword, &value, sizeof( uint64_t ) );

    if ( gUseHalfPrecisionIntegers == '\x01' ) {
        asQword = SwapEightBytes( asQword );
    }

    uint64_t *pDest = (uint64_t *)( (uint8_t *)pBuffer + bufferOffset );
    if ( pDest != nullptr ) {
        *pDest = asQword;
    }

    bufferOffset += 8;
}

void edStore::writeString( const char *pValue ) {
    // FUN_006232d0
    if ( state == 0xff ) {
        return;
    }

    const uint32_t length = uint32_t( strlen( pValue ) );
    writeUShort( uint16_t( length ) );

    if ( reserve_buffer( int32_t( length ) ) != 0 ) {
        return;
    }

    memcpy( (uint8_t *)pBuffer + bufferOffset, pValue, length );
    bufferOffset += length;
}

void edStore::writeBuffer( const void *pSource, const uint32_t size ) {
    // FUN_00623370
    if ( state == 0xff ) {
        return;
    }

    writeUShort( uint16_t( size ) );

    if ( reserve_buffer( int32_t( size ) ) != 0 ) {
        return;
    }

    if ( pBuffer != nullptr && pSource != nullptr && size != 0u ) {
        memcpy( (uint8_t *)pBuffer + bufferOffset, pSource, size );
    }

    bufferOffset += size;
}

void edStore::writeObject( edStore &source ) {
    // FUN_00623410
    if ( state == 0xff || reserve_buffer( 2 ) != 0 ) {
        return;
    }

    const uint32_t objectSize = source.bufferSize;

    uint16_t packedSize = uint16_t( objectSize );
    if ( gUseHalfPrecisionIntegers == '\x01' ) {
        packedSize =
            uint16_t( ( packedSize >> 8 & 0xff ) | ( packedSize & 0xff ) << 8 );
    }

    uint16_t *pDest = (uint16_t *)( (uint8_t *)pBuffer + bufferOffset );
    if ( pDest != nullptr ) {
        *pDest = packedSize;
    }
    bufferOffset += 2;

    if ( reserve_buffer( int32_t( objectSize ) ) != 0 ) {
        return;
    }

    if ( pBuffer != nullptr && source.pBuffer != nullptr && objectSize != 0u ) {
        memcpy( (uint8_t *)pBuffer + bufferOffset, source.pBuffer, objectSize );
    }

    bufferOffset += objectSize;
}

void edStore::updateCRC( const uint16_t crc ) {
    // FUN_006231b0
    CRC = crc;
    bufferOffset = 0;
    bufferSize = 0;
    state = 0;

    if ( isExternalBuffer == 0 && reserve_buffer( 4 ) != 0 ) {
        return;
    }

    uint16_t packedCRC = CRC;
    if ( gUseHalfPrecisionIntegers == '\x01' ) {
        packedCRC =
            uint16_t( ( packedCRC >> 8 & 0xff ) | ( packedCRC & 0xff ) << 8 );
    }

    uint16_t *pDest = (uint16_t *)( (uint8_t *)pBuffer + bufferOffset );
    if ( pDest != nullptr ) {
        *pDest = packedCRC;
    }

    bufferOffset += 2;
    bufferSize = bufferOffset;
}

int32_t edStore::seekToEnd() {
    // FUN_00623740
    bufferOffset = bufferSize;
    return 0;
}

uint32_t edStore::readStringNoAlloc( char *param_2, uint32_t param_3 ) {
    // FUN_00623e60
    if ( !isWithinBounds() ) {
        state = 0xff;
        OTDU_LOG_ERROR(
            "pop_string_noalloc()=>file .\\sources\\edStore.cpp line:1334\n" );
        return 0;
    }

    int16_t uVar2 = readShort();
    if ( uVar2 != 0 ) {
        if ( 0x7ffd < uVar2 ) {
            OTDU_LOG_WARN(
                "**WARNING** edStore::pop_string_noalloc too BIG = %lu\n",
                uVar2 );
        }
        if ( (int)uVar2 <= (int)param_3 ) {
            param_3 = uVar2;
        }
        strncpy( param_2, (char *)pBuffer + bufferOffset, param_3 );
        param_2[param_3] = '\0';
        bufferOffset += uVar2;
        return param_3;
    }

    memset( param_2, 0, uVar2 * sizeof( char ) );
    return 0;
}

uint32_t edStore::readBufferNoAlloc( void *pDest, uint32_t maxSize ) {
    // FUN_00623d80
    if ( !isWithinBounds() ) {
        state = 0xff;
        OTDU_LOG_ERROR(
            "pop_buffer_noalloc()=>file .\\sources\\edStore.cpp line:1310\n" );
        return 0;
    }

    const uint32_t bufferLength = static_cast<uint16_t>( readShort() );
    if ( bufferLength == 0 ) {
        return 0;
    }

    if ( 0x7ffd < bufferLength ) {
        OTDU_LOG_WARN(
            "**WARNING** edStore::pop_buffer_noalloc too BIG = %lu\n",
            bufferLength );
    }

    if ( bufferLength <= maxSize ) {
        maxSize = bufferLength;
    }

    if ( pDest != nullptr && pBuffer != nullptr && maxSize != 0 ) {
        memcpy( pDest, (uint8_t *)pBuffer + bufferOffset, maxSize );
    }

    bufferOffset += bufferLength;
    return maxSize;
}

int32_t edStore::readInteger() {
    // FUN_00623910
    if ( !isWithinBounds() ) {
        state = 0xff;
        OTDU_LOG_ERROR( "pop_int()=>file .\\sources\\edStore.cpp line:1091\n" );
        return 0;
    }

    uint32_t uVar2 = 0u;
    if ( (uint32_t *)( (uint8_t *)pBuffer + bufferOffset ) != nullptr ) {
        uVar2 = *(uint32_t *)( (uint8_t *)pBuffer + bufferOffset );
    }
    if ( gUseHalfPrecisionIntegers == '\x01' ) {
        uVar2 = ( ( uVar2 & 0xff0000 ) | uVar2 >> 0x10 ) >> 8 |
                ( ( uVar2 & 0xff00 ) | uVar2 << 0x10 ) << 8;
    }

    bufferOffset += 4;
    return int32_t( uVar2 );
}

uint32_t edStore::readUInteger() {
    // FUN_006239d0
    if ( !isWithinBounds() ) {
        state = 0xff;
        OTDU_LOG_ERROR(
            "pop_uint()=>file .\\sources\\edStore.cpp line:1109\n" );
        return 0;
    }

    uint32_t uVar2 = 0;
    if ( (uint32_t *)( (uint8_t *)pBuffer + bufferOffset ) != nullptr ) {
        uVar2 = *(uint32_t *)( (uint8_t *)pBuffer + bufferOffset );
    }
    if ( gUseHalfPrecisionIntegers == '\x01' ) {
        uVar2 = ( ( uVar2 & 0xff0000 ) | uVar2 >> 0x10 ) >> 8 |
                ( ( uVar2 & 0xff00 ) | uVar2 << 0x10 ) << 8;
    }

    bufferOffset += 4;
    return uVar2;
}

uint32_t edStore::readULong() {
    // FUN_006240d0
    if ( !isWithinBounds() ) {
        state = 0xff;
        OTDU_LOG_ERROR(
            "pop_ulong()=>file .\\sources\\edStore.cpp line:1405\n" );
        return 0;
    }

    uint32_t uVar2 = 0;
    if ( (uint32_t *)( (uint8_t *)pBuffer + bufferOffset ) != nullptr ) {
        uVar2 = *(uint32_t *)( (uint8_t *)pBuffer + bufferOffset );
    }
    if ( gUseHalfPrecisionIntegers == '\x01' ) {
        uVar2 = ( ( uVar2 & 0xff0000 ) | uVar2 >> 0x10 ) >> 8 |
                ( ( uVar2 & 0xff00 ) | uVar2 << 0x10 ) << 8;
    }

    bufferOffset += 4;
    return uVar2;
}

uint64_t edStore::readULongLong() {
    // FUN_00624150
    if ( !isWithinBounds() ) {
        state = 0xff;
        OTDU_LOG_ERROR(
            "pop_ulonglong()=>file .\\sources\\edStore.cpp line:1415\n" );
        return 0ull;
    }

    uint64_t uVar2 = 0ull;
    if ( (uint64_t *)( (uint8_t *)pBuffer + bufferOffset ) != nullptr ) {
        uVar2 = *(uint64_t *)( (uint8_t *)pBuffer + bufferOffset );
    }
    if ( gUseHalfPrecisionIntegers == '\x01' ) {
        uVar2 = SwapEightBytes( uVar2 );
    }

    bufferOffset += 8;
    return uVar2;
}

double edStore::readDouble() {
    // FUN_00624260
    if ( !isWithinBounds() ) {
        state = 0xff;
        OTDU_LOG_ERROR(
            "pop_double()=>file .\\sources\\edStore.cpp line:1446\n" );
        return 0.0;
    }

    uint64_t asQword = 0ull;
    if ( (uint64_t *)( (uint8_t *)pBuffer + bufferOffset ) != nullptr ) {
        asQword = *(uint64_t *)( (uint8_t *)pBuffer + bufferOffset );
    }
    if ( gUseHalfPrecisionIntegers == '\x01' ) {
        asQword = SwapEightBytes( asQword );
    }

    bufferOffset += 8;

    double value = 0.0;
    memcpy( &value, &asQword, sizeof( double ) );
    return value;
}

float edStore::readFloat() {
    // FUN_00624210
    if ( !isWithinBounds() ) {
        state = 0xff;
        OTDU_LOG_ERROR(
            "pop_float()=>file .\\sources\\edStore.cpp line:1436\n" );
        return 0.0f;
    }

    uint32_t asDword = 0;
    if ( (uint32_t *)( (uint8_t *)pBuffer + bufferOffset ) != nullptr ) {
        asDword = *(uint32_t *)( (uint8_t *)pBuffer + bufferOffset );
    }
    if ( gUseHalfPrecisionIntegers == '\x01' ) {
        asDword = ( ( asDword & 0xff0000 ) | asDword >> 0x10 ) >> 8 |
                  ( ( asDword & 0xff00 ) | asDword << 0x10 ) << 8;
    }

    bufferOffset += 4;

    float value = 0.0f;
    memcpy( &value, &asDword, sizeof( float ) );
    return value;
}

int16_t edStore::readShort() {
    int16_t uVar2 = 0;
    if ( (int16_t *)( (uint8_t *)pBuffer + bufferOffset ) != nullptr ) {
        uVar2 = *(int16_t *)( (uint8_t *)pBuffer + bufferOffset );
    }

    if ( gUseHalfPrecisionIntegers == '\x01' ) {
        uVar2 = (int16_t)( ( (uint32_t)uVar2 >> 8 & 0xff ) |
                           ( (uint32_t)uVar2 & 0xff ) << 8 );
    }

    bufferOffset += 2;
    return uVar2;
}

uint16_t edStore::readUShort() {
    // FUN_00623880
    if ( !isWithinBounds() ) {
        state = 0xff;
        OTDU_LOG_ERROR(
            "pop_ushort()=>file .\\sources\\edStore.cpp line:1082\n" );
        return 0;
    }

    return static_cast<uint16_t>( readShort() );
}

uint8_t edStore::readByte() {
    // FUN_00623990
    if ( !isWithinBounds() ) {
        state = 0xff;
        OTDU_LOG_ERROR(
            "pop_byte()=>file .\\sources\\edStore.cpp line:1100\n" );
        return 0;
    }

    uint8_t uVar2 = *( (uint8_t *)pBuffer + bufferOffset );
    bufferOffset += 1;
    return uVar2;
}

bool edStore::popObjectNoAlloc( edStore &outStore ) {
    // FUN_00623bc0
    if ( !isWithinBounds() ) {
        state = 0xff;
        OTDU_LOG_ERROR(
            "pop_object_noalloc()=>file .\\sources\\edStore.cpp line:1240\n" );
        return false;
    }

    const uint32_t objectSize = static_cast<uint16_t>( readShort() );
    if ( objectSize == 0 ) {
        return false;
    }

    if ( 0x7ffd < objectSize ) {
        OTDU_LOG_WARN(
            "**WARNING** edStore::pop_object_noalloc too BIG = %lu\n",
            objectSize );
    }

    if ( outStore.bufferSizeWithPadding < objectSize ) {
        bufferOffset += objectSize;
        return false;
    }

    if ( outStore.pBuffer != nullptr && pBuffer != nullptr ) {
        memcpy( outStore.pBuffer, (uint8_t *)pBuffer + bufferOffset,
                objectSize );
    }

    outStore.bufferSize = objectSize;
    outStore.prepareBuffer();

    bufferOffset += objectSize;
    return true;
}

int32_t edStore::getStatus() const {
    // FUN_006242d0
    return ( state != 0xff ) ? 0 : -1;
}

int32_t edStore::compareStreamCRC( uint32_t param_2 ) {
    // FUN_00623760
    state = 0;
    bufferOffset = 0;

    if ( !isWithinBounds() ) {
        state = 0xff;
        return -1;
    }

    // Read CRC from stream
    uint16_t peStack_4 = 0;
    if ( (int32_t *)( (uint8_t *)pBuffer + bufferOffset ) != nullptr ) {
        peStack_4 = *(uint16_t *)( (uint8_t *)pBuffer + bufferOffset );
    }
    if ( gUseHalfPrecisionIntegers == '\x01' ) {
        peStack_4 = (uint16_t)( ( (uint32_t)peStack_4 >> 8 ) & 0xff ) |
                    (uint16_t)( ( (uint32_t)peStack_4 & 0xff ) << 8 );
    }
    CRC = peStack_4;

    // Not super safe (but that's how the original game did it so it should be
    // fine)
    if ( peStack_4 != static_cast<uint16_t>( param_2 ) ) {
        state = 0xff;
        return -1;
    }

    bufferOffset += 2;
    return 0;
}

int32_t edStore::compareStreamCRC( const char *param_2 ) {
    uint32_t uVar4 = TestDriveCRC32( param_2 );
    return compareStreamCRC( static_cast<uint16_t>( uVar4 ) );
}

void edStore::prepareBuffer() {
    // FUN_006236e0
    bufferOffset = 0;
    state = 0;

    uint16_t *local_4 = (uint16_t *)this;
    if ( pBuffer != nullptr ) {
        local_4 = (uint16_t *)pBuffer;
    }

    bufferOffset = 2;

    if ( gUseHalfPrecisionIntegers == '\x01' ) {
        CRC = (uint16_t)( *local_4 ) >> 8 | (uint16_t)( *local_4 ) << 8;
    } else {
        CRC = (uint16_t)( *local_4 );
    }
}

void edStore::packAndWriteToBuffer( int32_t in, int32_t *pOut ) {
    // FUN_00622df0
    uint32_t packedValue = uint32_t( in );
    if ( gUseHalfPrecisionIntegers == '\x01' ) {
        packedValue =
            ( ( packedValue & 0xff0000 ) | packedValue >> 0x10 ) >> 8 |
            ( ( packedValue & 0xff00 ) | packedValue << 0x10 ) << 8;
    }
    if ( pOut != nullptr ) {
        *pOut = int32_t( packedValue );
    }
}

bool edStore::isWithinBounds() {
    // FUN_006242e0
    if ( state == 0xff ) {
        return false;
    }

    return bufferOffset < bufferSize;
}
