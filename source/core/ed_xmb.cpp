#include "shared.h"
#include "ed_xmb.h"

#include "binary_helpers.h"

static constexpr uint32_t kMagic = 0x46424d58;  // 'XMBF'
static constexpr float kXMBFloatDefault = 0.0f; // FLOAT_00f41660

// Added back when an unsigned dword reads negative
static constexpr float kXMBUInt32Bias = 4294967296.0f; // FLOAT_00f41670

edXMBNode::edXMBNode()
    : pStringPool( nullptr ),
      pNodeTable( nullptr ),
      pDataBase( nullptr ),
      pName( "Empty" ), // DAT_00faa5b0
      NodeIndex( 0 ),
      Type( XMBT_Node ),
      bIsArray( false ),
      ArrayCount( 0 ),
      pValue( nullptr ),
      CachedChildIndex( 0 ),
      CachedChildDataOffset( 0 ) {}

edXMBNode::edXMBNode( const void *pBuffer ) : edXMBNode() {
    // FUN_0045e5e0
    const uint8_t *pBytes = reinterpret_cast<const uint8_t *>( pBuffer );
    if ( pBytes == nullptr || pBytes[0] != 'X' || pBytes[1] != 'M' ||
         pBytes[2] != 'B' || pBytes[3] != 'F' ) {
        return;
    }

    const uint32_t stringPoolOffset = ReadUint32( pBytes + 0x08 );
    if ( ( stringPoolOffset & 0xff000000u ) != 0 ) {
        // Swap endianess if needed (using FUN_0045ed80). Not implemented since
        // this is presumably Xbox only
        OTDU_UNIMPLEMENTED;
        return;
    }

    pStringPool = reinterpret_cast<const char *>( pBytes + stringPoolOffset );
    pNodeTable = reinterpret_cast<const uint32_t *>(
        pBytes + ReadUint32( pBytes + 0x0c ) );
    pDataBase = pBytes + ReadUint32( pBytes + 0x10 );
    pName = pStringPool + ReadUint32( pBytes + 0x14 );
    NodeIndex = static_cast<int32_t>( ReadUint32( pBytes + 0x18 ) );
    Type = XMBT_Node;
    pValue = pDataBase;
}

const char *edXMBNode::getTypeName() const {
    if ( pNodeTable == nullptr ) {
        return pName;
    }
    return pStringPool + pNodeTable[NodeIndex + 1];
}

int32_t edXMBNode::getChildCount() const {
    if ( pNodeTable == nullptr ) {
        return 0;
    }
    return HeaderChildCount( getHeader( NodeIndex ) );
}

const void *edXMBNode::getValue() const {
    // FUN_0045ea70
    if ( pValue == nullptr || ( !bIsArray && Type != XMBT_String ) ) {
        return nullptr;
    }
    return pValue;
}

void edXMBNode::readEntry( int32_t index,
                           uint32_t &nameOffset,
                           uint32_t &entryHigh ) const {
    const uint32_t *pEntry = pNodeTable + NodeIndex + 2 + index * 2;
    nameOffset = pEntry[0];
    entryHigh = pEntry[1];
}

int32_t edXMBNode::getEntrySize( int32_t nodeIndex, bool bEntryIsArray ) const {
    if ( bEntryIsArray ) {
        return 8;
    }

    const uint32_t header = getHeader( nodeIndex );
    return HeaderType( header ) == XMBT_String ? 4 : HeaderDataSize( header );
}

int32_t edXMBNode::computeChildDataOffset( int32_t index ) const {
    // FUN_0045e710
    int32_t start = CachedChildIndex;
    int32_t dataOffset = 0;

    if ( start < index ) {
        dataOffset = CachedChildDataOffset;
    } else if ( start == index ) {
        return CachedChildDataOffset;
    } else {
        start = 0;
    }

    for ( int32_t i = start; i < index; i++ ) {
        uint32_t nameOffset = 0;
        uint32_t entryHigh = 0;
        readEntry( i, nameOffset, entryHigh );
        dataOffset += getEntrySize( EntryNodeIndex( entryHigh ),
                                    EntryIsArray( entryHigh ) );
    }

    CachedChildDataOffset = dataOffset;
    CachedChildIndex = index;
    return dataOffset;
}

void edXMBNode::setFromEntry( const edXMBNode &parent,
                              uint32_t nameOffset,
                              uint32_t entryHigh,
                              const uint8_t *pData ) {
    // FUN_0045ea90
    CachedChildIndex = 0;
    CachedChildDataOffset = 0;

    pStringPool = parent.pStringPool;
    pNodeTable = parent.pNodeTable;
    pDataBase = parent.pDataBase;
    pName = pStringPool + nameOffset;
    NodeIndex = EntryNodeIndex( entryHigh );
    Type = HeaderType( getHeader( NodeIndex ) );
    bIsArray = EntryIsArray( entryHigh );

    if ( bIsArray ) {
        ArrayCount = ReadInt32( pData );
        pValue = pDataBase + ReadInt32( pData + 4 );
        return;
    }

    ArrayCount = 0;
    pValue = pData;

    if ( Type == XMBT_String ) {
        const int32_t stringOffset = ReadInt32( pData );
        pValue = stringOffset != -1 ? pDataBase + stringOffset : nullptr;
        return;
    }

    if ( Type == XMBT_Reference ) {
        NodeIndex = ReadInt32( pData );
        pValue = pDataBase + ReadInt32( pData + 4 );
        Type = HeaderType( getHeader( NodeIndex ) );
        return;
    }

    if ( HeaderDataSize( getHeader( NodeIndex ) ) == 0 ) {
        pValue = nullptr;
    }
}

int32_t edXMBNode::findChildIndex( const char *pChildName ) const {
    // FUN_0045ec80
    if ( pNodeTable == nullptr || pChildName == nullptr ) {
        return -1;
    }

    const int32_t childCount = getChildCount();
    if ( childCount <= 0 ) {
        return -1;
    }

    const int32_t hint = CachedChildIndex;
    uint32_t nameOffset = 0;
    uint32_t entryHigh = 0;

    for ( int32_t i = hint == 0 ? 0 : hint + 1; i < childCount; i++ ) {
        readEntry( i, nameOffset, entryHigh );
        if ( strcmp( pStringPool + nameOffset, pChildName ) == 0 ) {
            return i;
        }
    }

    for ( int32_t i = 0; i <= hint && i < childCount; i++ ) {
        readEntry( i, nameOffset, entryHigh );
        if ( strcmp( pStringPool + nameOffset, pChildName ) == 0 ) {
            return i;
        }
    }

    return -1;
}

edXMBNode edXMBNode::getChild( int32_t index ) const {
    // FUN_0045e820
    edXMBNode result;
    if ( pValue == nullptr || index >= getChildCount() ) {
        return result;
    }

    if ( index < 0 ) {
        return result;
    }

    const int32_t dataOffset = computeChildDataOffset( index );

    uint32_t nameOffset = 0;
    uint32_t entryHigh = 0;
    readEntry( index, nameOffset, entryHigh );
    result.setFromEntry( *this, nameOffset, entryHigh, pValue + dataOffset );
    return result;
}

edXMBNode edXMBNode::findChild( const char *pChildName ) const {
    // FUN_0045e9b0
    if ( pValue != nullptr ) {
        const int32_t index = findChildIndex( pChildName );
        if ( index != -1 ) {
            return getChild( index );
        }
    }
    return edXMBNode();
}

edXMBNode edXMBNode::getElement( int32_t index ) const {
    // FUN_0045e680 + FUN_0045ebc0
    if ( pValue == nullptr || !bIsArray || index < 0 || index >= ArrayCount ) {
        return edXMBNode();
    }

    edXMBNode result = *this;
    result.bIsArray = false;
    result.ArrayCount = 0;

    if ( Type == XMBT_String ) {
        result.pValue = pDataBase + ReadInt32( pValue + index * 4 );
    } else {
        result.pValue =
            pValue + HeaderDataSize( getHeader( NodeIndex ) ) * index;
    }

    return result;
}

int32_t edXMBNode::asInt( int32_t defaultValue ) const {
    // FUN_0045f440
    if ( pValue == nullptr || bIsArray ) {
        return defaultValue;
    }

    switch ( Type ) {
    case XMBT_Bool:
    case XMBT_UInt8:
        return *pValue;
    case XMBT_Int8:
        return static_cast<int8_t>( *pValue );
    case XMBT_Int16:
        return ReadI16( pValue );
    case XMBT_UInt16:
        return ReadU16( pValue );
    case XMBT_Int32:
    case XMBT_Int64: // truncating, as the original does
    case XMBT_UInt32:
        return ReadInt32( pValue );
    case XMBT_UInt64:
        return static_cast<int32_t>( ReadInt64( pValue ) );
    case XMBT_Single:
        return static_cast<int32_t>( ReadFloat( pValue ) );
    case XMBT_Double:
        return static_cast<int32_t>( ReadDouble( pValue ) );
    default:
        return defaultValue;
    }
}

float edXMBNode::asFloat( float defaultValue ) const {
    // FUN_0045f500
    if ( pValue == nullptr || bIsArray ) {
        return defaultValue;
    }

    switch ( Type ) {
    case XMBT_Bool:
    case XMBT_UInt8:
        return static_cast<float>( *pValue );
    case XMBT_Int8:
        return static_cast<float>( static_cast<int8_t>( *pValue ) );
    case XMBT_Int16:
        return static_cast<float>( ReadI16( pValue ) );
    case XMBT_UInt16:
        return static_cast<float>( ReadU16( pValue ) );
    case XMBT_Int32:
        return static_cast<float>( ReadInt32( pValue ) );
    case XMBT_Int64:
        return static_cast<float>( ReadInt64( pValue ) );
    case XMBT_UInt32: {
        // Converted as signed then biased back, which is what the x87 path does
        // for want of an unsigned load.
        const int32_t value = ReadInt32( pValue );
        float asFloat = static_cast<float>( value );
        if ( value < 0 ) {
            asFloat += kXMBUInt32Bias;
        }
        return asFloat;
    }
    case XMBT_UInt64: {
        const int64_t value = ReadInt64( pValue );
        return static_cast<float>( static_cast<uint64_t>( value ) );
    }
    case XMBT_Single:
        return ReadFloat( pValue );
    case XMBT_Double:
        return static_cast<float>( ReadDouble( pValue ) );
    default:
        return kXMBFloatDefault;
    }
}

bool edXMBNode::asBool( bool defaultValue ) const {
    if ( pValue == nullptr || bIsArray ) {
        return defaultValue;
    }

    if ( Type == XMBT_Bool ) {
        return *pValue != 0;
    }
    return asInt( defaultValue ? 1 : 0 ) != 0;
}

const char *edXMBNode::asString( const char *pDefault ) const {
    if ( Type != XMBT_String || pValue == nullptr || bIsArray ) {
        return pDefault;
    }
    return reinterpret_cast<const char *>( pValue );
}

int32_t edXMBNode::readInt( const char *pChildName,
                            int32_t defaultValue ) const {
    // FUN_0040e9f0
    return findChild( pChildName ).asInt( defaultValue );
}

float edXMBNode::readFloat( const char *pChildName, float defaultValue ) const {
    // FUN_0040ea90
    return findChild( pChildName ).asFloat( defaultValue );
}

bool edXMBNode::readBool( const char *pChildName, bool defaultValue ) const {
    // FUN_0040eb40
    return findChild( pChildName ).asBool( defaultValue );
}

const char *edXMBNode::readString( const char *pChildName,
                                   const char *pDefault ) const {
    return findChild( pChildName ).asString( pDefault );
}
