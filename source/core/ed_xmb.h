#pragma once

#include <cstdint>

enum eXMBType : int32_t {
    XMBT_Node = 0, // compound; dataSize covers all children
    XMBT_Bool = 1,
    XMBT_Int8 = 2,
    XMBT_Int16 = 3,
    XMBT_Int32 = 4,
    XMBT_Int64 = 5,
    XMBT_UInt8 = 6,
    XMBT_UInt16 = 7,
    XMBT_UInt32 = 8,
    XMBT_UInt64 = 9,
    XMBT_Single = 10,
    XMBT_Double = 11,
    XMBT_String = 12,    // data is a dword offset into the data region
    XMBT_Reference = 13, // data retargets the cursor at another node index
};

struct edXMBNode {
public:
    const char *pStringPool;
    const uint32_t *pNodeTable;
    const uint8_t *pDataBase;
    const char *pName;
    int32_t NodeIndex;
    eXMBType Type;
    bool bIsArray;
    int32_t ArrayCount;
    const uint8_t *pValue;
    mutable int32_t CachedChildIndex;
    mutable int32_t CachedChildDataOffset;

public:
    inline bool isValid() const { return pValue != nullptr; }
    inline const char *getName() const { return pName; }
    inline eXMBType getType() const { return Type; }
    inline bool isArray() const { return bIsArray; }
    inline int32_t getArrayCount() const { return ArrayCount; }

public:
    edXMBNode();
    explicit edXMBNode( const void *pBuffer );

    const char *getTypeName() const;
    int32_t getChildCount() const;
    const void *getValue() const;

    int32_t findChildIndex( const char *pChildName ) const;
    edXMBNode getChild( int32_t index ) const;
    edXMBNode findChild( const char *pChildName ) const;
    edXMBNode getElement( int32_t index ) const;

    int32_t asInt( int32_t defaultValue = 0 ) const;
    float asFloat( float defaultValue = 0.0f ) const;
    bool asBool( bool defaultValue = false ) const;
    const char *asString( const char *pDefault = nullptr ) const;

    int32_t readInt( const char *pChildName, int32_t defaultValue = 0 ) const;
    float readFloat( const char *pChildName, float defaultValue = 0.0f ) const;
    bool readBool( const char *pChildName, bool defaultValue = false ) const;
    const char *readString( const char *pChildName,
                            const char *pDefault = nullptr ) const;

private:
    static inline eXMBType HeaderType( uint32_t header ) {
        return static_cast<eXMBType>( header & 0xf );
    }
    static inline int32_t HeaderChildCount( uint32_t header ) {
        return static_cast<int32_t>( ( header >> 4 ) & 0x7ff );
    }
    static inline int32_t HeaderDataSize( uint32_t header ) {
        return static_cast<int32_t>( ( header >> 15 ) & 0x1ffff );
    }

    static inline int32_t EntryNodeIndex( uint32_t entryHigh ) {
        return static_cast<int32_t>( entryHigh & 0x3fffffff );
    }
    static inline bool EntryIsArray( uint32_t entryHigh ) {
        return ( entryHigh & 0x80000000u ) != 0;
    }

private:
    inline uint32_t getHeader( int32_t nodeIndex ) const {
        return pNodeTable[nodeIndex];
    }

private:
    void
    readEntry( int32_t index, uint32_t &nameOffset, uint32_t &entryHigh ) const;
    int32_t getEntrySize( int32_t nodeIndex, bool bEntryIsArray ) const;
    int32_t computeChildDataOffset( int32_t index ) const;
    void setFromEntry( const edXMBNode &parent,
                       uint32_t nameOffset,
                       uint32_t entryHigh,
                       const uint8_t *pData );
};
