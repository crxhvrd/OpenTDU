#pragma once

class edStore {
public:
    inline uint16_t getStreamCRC() const { return CRC; }
    inline uint32_t getWrittenSize() const { return bufferSize; }

public:
    edStore();
    ~edStore();

    void bindExternalBuffer( void *pExtBuffer, uint32_t bufferLength );
    void clear();

    int32_t seek( const uint32_t offset );

    int32_t reserve_buffer( int32_t sizeToReserve );

    void writeByte( const uint8_t value );
    void writeUShort( const uint16_t value );
    void writeInteger( int32_t param_2 );
    void writeULongLong( const uint64_t value );
    void writeFloat( const float value );
    void writeDouble( const double value );
    void writeString( const char *pValue );
    void writeBuffer( const void *pSource, const uint32_t size );

    uint32_t readStringNoAlloc( char *param_2, uint32_t param_3 );
    uint32_t readBufferNoAlloc( void *pDest, uint32_t maxSize );
    int32_t readInteger();
    uint32_t readUInteger();
    uint32_t readULong();
    uint64_t readULongLong();
    float readFloat();
    double readDouble();
    int16_t readShort();
    uint16_t readUShort();
    uint8_t readByte();
    bool popObjectNoAlloc( edStore &outStore );

    int32_t compareStreamCRC( uint32_t param_2 );
    int32_t compareStreamCRC( const char *param_2 );

    int32_t getStatus() const;
    bool isWithinBounds();

private:
    uint32_t ID;
    uint32_t bufferSize;
    uint32_t bufferSizeWithPadding;
    uint32_t bufferOffset;
    uint32_t isExternalBuffer;
    uint16_t CRC;
    uint8_t state;
    void *pBuffer;

private:
    void prepareBuffer();

    void packAndWriteToBuffer( int32_t in, int32_t *pOut );
};