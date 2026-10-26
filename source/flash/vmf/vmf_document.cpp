#include "shared.h"
#include "vmf_document.h"

#include "core/binary_helpers.h"
#include "core/hash/crc32.h"

static constexpr uint32_t kSpriteHeaderSize = 0x14;
static constexpr uint32_t kTagHeaderSize = 8;
static constexpr uint32_t kHeaderCRCSize = 0x44;

template <typename T> static void Append( std::vector<uint8_t> &out, T value ) {
    const size_t offset = out.size();
    out.resize( offset + sizeof( T ) );
    std::memcpy( out.data() + offset, &value, sizeof( T ) );
}

template <typename... Args>
static std::string Format( const char *pFormat, Args... args ) {
    char buffer[256];
    snprintf( buffer, sizeof( buffer ), pFormat, args... );
    return buffer;
}

bool VMFTag::getCharacterIndex( uint32_t &outIndex ) const {
    switch ( TagId ) {
    case 0x02: // DefineShape
    case 0x07: // DefineButton
    case 0x0a: // DefineFont
    case 0x0b: // DefineText
    case 0x0e: // DefineSound
    case 0x14: // DefineBitsLossless
    case 0x25: // DefineEditText
    case 0x2e: // DefineMorphShape
    case 0x40: // Material reference
        if ( Payload.size() < sizeof( uint16_t ) ) {
            return false;
        }
        outIndex = Read<uint16_t>( Payload.data() );
        return true;
    case kVMFTagDefineSprite:
        outIndex = CharacterIndex;
        return true;
    default:
        return false;
    }
}

static void ComputeNumCharacters( const VMFTag &sprite, uint32_t &maxIndex ) {
    for ( const VMFTag &child : sprite.Children ) {
        uint32_t index = 0;
        if ( child.getCharacterIndex( index ) ) {
            maxIndex = Max( maxIndex, index + 1 );
        }
        if ( child.isSprite() ) {
            ComputeNumCharacters( child, maxIndex );
        }
    }
}

static void WriteSprite( std::vector<uint8_t> &out,
                         const VMFTag &sprite,
                         const bool bRoot ) {
    const size_t spriteOffset = out.size();
    const uint16_t numFrames = sprite.getNumFrames();
    const uint32_t tagSize =
        kSpriteHeaderSize + ( numFrames + 1 ) * sizeof( uint32_t );

    Append<uint32_t>( out, sprite.TagId );
    Append<uint32_t>( out, tagSize );
    Append<uint32_t>( out, sprite.CharacterIndex );
    Append<uint16_t>( out, numFrames );
    Append<uint16_t>( out, sprite.NumDepths );
    out.resize( spriteOffset + tagSize ); // span + frame table, patched below

    std::vector<uint32_t> frameOffsets;
    frameOffsets.reserve( numFrames + 1 );
    frameOffsets.push_back( tagSize );

    for ( const VMFTag &child : sprite.Children ) {
        if ( child.isSprite() ) {
            WriteSprite( out, child, false );
            continue;
        }

        Append<uint32_t>( out, child.TagId );
        Append<uint32_t>( out,
                          uint32_t( kTagHeaderSize + child.Payload.size() ) );
        out.insert( out.end(), child.Payload.begin(), child.Payload.end() );

        if ( child.TagId == kVMFTagShowFrame ) {
            frameOffsets.push_back( uint32_t( out.size() - spriteOffset ) );
        }
    }

    Append<uint32_t>( out, kVMFTagEnd );
    Append<uint32_t>( out, bRoot ? 0u : kTagHeaderSize );

    uint8_t *pSprite = out.data() + spriteOffset;
    Write<uint32_t>( pSprite, 0x10, uint32_t( out.size() - spriteOffset ) );
    for ( size_t i = 0; i < frameOffsets.size(); i++ ) {
        Write<uint32_t>( pSprite, uint32_t( kSpriteHeaderSize + i * 4 ),
                         frameOffsets[i] );
    }
}

bool VMFPlacement::decode( const std::vector<uint8_t> &payload ) {
    size_t cursor = 0;
    auto read = [&]( void *pOut, size_t size ) {
        if ( cursor + size > payload.size() ) {
            return false;
        }
        std::memcpy( pOut, payload.data() + cursor, size );
        cursor += size;
        return true;
    };
    auto align = [&]() { cursor = Align( cursor, size_t( 4 ) ); };

    if ( !read( &Depth, 2 ) || !read( &Flags, 2 ) ) {
        return false;
    }
    if ( ( Flags & PF_HasCharacter ) != 0 && !read( &CharacterIndex, 2 ) ) {
        return false;
    }
    if ( ( Flags & PF_HasMatrix ) != 0 ) {
        align();
        if ( ( ( Flags & PF_HasScale ) != 0 && !read( Scale, 8 ) ) ||
             ( ( Flags & PF_HasRotateSkew ) != 0 && !read( RotateSkew, 8 ) ) ||
             ( ( Flags & PF_HasTranslate ) != 0 && !read( Translate, 8 ) ) ) {
            return false;
        }
    }
    if ( ( Flags & PF_HasColorTransform ) != 0 ) {
        align();
        if ( !read( ColorTransform, sizeof( ColorTransform ) ) ) {
            return false;
        }
    }
    if ( ( Flags & PF_HasRatio ) != 0 && !read( &Ratio, 2 ) ) {
        return false;
    }
    if ( ( Flags & PF_HasClipDepth ) != 0 && !read( &ClipDepth, 2 ) ) {
        return false;
    }
    if ( ( Flags & PF_HasName ) != 0 ) {
        const char *pName = (const char *)payload.data() + cursor;
        const size_t length = strnlen( pName, payload.size() - cursor );
        if ( cursor + length >= payload.size() ) {
            return false;
        }
        Name.assign( pName, length );
        cursor += length + 1;
    }

    Trailing.assign( payload.begin() + cursor, payload.end() );
    return true;
}

std::vector<uint8_t> VMFPlacement::encode() const {
    std::vector<uint8_t> out;
    auto align = [&]() { out.resize( Align( out.size(), size_t( 4 ) ) ); };

    Append<uint16_t>( out, Depth );
    Append<uint16_t>( out, Flags );
    if ( ( Flags & PF_HasCharacter ) != 0 ) {
        Append<uint16_t>( out, CharacterIndex );
    }
    if ( ( Flags & PF_HasMatrix ) != 0 ) {
        align();
        if ( ( Flags & PF_HasScale ) != 0 ) {
            Append<float>( out, Scale[0] );
            Append<float>( out, Scale[1] );
        }
        if ( ( Flags & PF_HasRotateSkew ) != 0 ) {
            Append<float>( out, RotateSkew[0] );
            Append<float>( out, RotateSkew[1] );
        }
        if ( ( Flags & PF_HasTranslate ) != 0 ) {
            Append<float>( out, Translate[0] );
            Append<float>( out, Translate[1] );
        }
    }
    if ( ( Flags & PF_HasColorTransform ) != 0 ) {
        align();
        for ( uint16_t term : ColorTransform ) {
            Append<uint16_t>( out, term );
        }
    }
    if ( ( Flags & PF_HasRatio ) != 0 ) {
        Append<uint16_t>( out, Ratio );
    }
    if ( ( Flags & PF_HasClipDepth ) != 0 ) {
        Append<uint16_t>( out, ClipDepth );
    }
    if ( ( Flags & PF_HasName ) != 0 ) {
        out.insert( out.end(), Name.begin(), Name.end() );
        out.push_back( 0 );
    }

    if ( Trailing.empty() ) {
        align();
    } else {
        out.insert( out.end(), Trailing.begin(), Trailing.end() );
    }
    return out;
}

uint16_t VMFTag::getNumFrames() const {
    return uint16_t( std::count_if( Children.begin(), Children.end(),
                                    []( const VMFTag &child ) {
                                        return child.TagId == kVMFTagShowFrame;
                                    } ) );
}

bool VMFDocument::parse( const uint8_t *pData, size_t size ) {
    error.clear();
    warnings.clear();
    Root = VMFTag();
    AtlasEntries.clear();
    Trailer.clear();

    if ( size < kVMFHeaderSize + kSpriteHeaderSize ) {
        error = "file too small";
        return false;
    }
    if ( Read<uint32_t>( pData ) != kVMFMagic ) {
        error = "bad magic";
        return false;
    }

    const uint32_t headerCRC = TestDriveCRC32Unfolded( pData, kHeaderCRCSize );
    if ( Read<uint32_t>( pData, kHeaderCRCSize ) != headerCRC ) {
        warnings.push_back( Format( "header CRC %08x, computed %08x",
                                    Read<uint32_t>( pData, kHeaderCRCSize ),
                                    headerCRC ) );
    }

    HeaderPadding = Read<uint8_t>( pData, 0x04 );
    VersionMajor = Read<uint8_t>( pData, 0x05 );
    VersionMinor = Read<uint16_t>( pData, 0x06 );
    ExporterVersion = Read<uint32_t>( pData, 0x08 );
    std::memcpy( AuthorName.data(), pData + 0x0c, AuthorName.size() );
    std::memcpy( FrameSize, pData + 0x2c, sizeof( FrameSize ) );
    FrameRate = Read<uint16_t>( pData, 0x3c );
    NumCharacters = Read<uint16_t>( pData, 0x3e );
    NumDisplayList = Read<uint16_t>( pData, 0x40 );
    NumSprites = Read<uint16_t>( pData, 0x42 );

    size_t cursor = 0;
    if ( !parseSprite( pData, size, kVMFHeaderSize, true, Root, cursor ) ) {
        return false;
    }

    if ( cursor + 8 > size ||
         Read<uint32_t>( pData, cursor ) != kVMFXIXTMagic ) {
        error = Format( "missing XIXT block at 0x%zx", cursor );
        return false;
    }

    const uint32_t numEntries = Read<uint32_t>( pData, uint32_t( cursor + 4 ) );
    cursor += 8;
    if ( cursor + size_t( numEntries ) * sizeof( VMFAtlasEntry ) > size ) {
        error = Format( "XIXT block truncated (%u entries)", numEntries );
        return false;
    }

    AtlasEntries.resize( numEntries );
    std::memcpy( AtlasEntries.data(), pData + cursor,
                 numEntries * sizeof( VMFAtlasEntry ) );
    cursor += numEntries * sizeof( VMFAtlasEntry );

    Trailer.assign( pData + cursor, pData + size );
    if ( !Trailer.empty() ) {
        warnings.push_back( Format( "%zu trailing bytes past the XIXT block",
                                    Trailer.size() ) );
    }

    return true;
}

bool VMFDocument::parseSprite( const uint8_t *pData,
                               size_t size,
                               size_t offset,
                               bool bRoot,
                               VMFTag &outSprite,
                               size_t &outEnd ) {
    if ( offset + kSpriteHeaderSize > size ) {
        error = Format( "sprite at 0x%zx is truncated", offset );
        return false;
    }

    const uint8_t *pSprite = pData + offset;
    const uint32_t tagSize = Read<uint32_t>( pSprite, 4 );
    if ( tagSize < kSpriteHeaderSize + 4 ||
         ( tagSize - kSpriteHeaderSize ) % 4 != 0 || offset + tagSize > size ) {
        error = Format( "sprite at 0x%zx has a bad header size 0x%x", offset,
                        tagSize );
        return false;
    }

    outSprite.TagId = Read<uint32_t>( pSprite );
    outSprite.CharacterIndex = Read<uint32_t>( pSprite, 0x08 );
    outSprite.NumDepths = Read<uint16_t>( pSprite, 0x0e );

    std::vector<uint32_t> frameOffsets = { tagSize };
    size_t cursor = offset + tagSize;
    while ( true ) {
        if ( cursor + kTagHeaderSize > size ) {
            error = Format( "sprite at 0x%zx runs past the end of the file",
                            offset );
            return false;
        }

        const uint32_t tagId = Read<uint32_t>( pData, uint32_t( cursor ) );
        const uint32_t childSize =
            Read<uint32_t>( pData, uint32_t( cursor + 4 ) );

        if ( tagId == kVMFTagEnd ) {
            const uint32_t expectedSize = bRoot ? 0u : kTagHeaderSize;
            if ( childSize != expectedSize ) {
                warnings.push_back( Format( "End tag at 0x%zx has size %u",
                                            cursor, childSize ) );
            }
            cursor += kTagHeaderSize;
            break;
        }

        if ( tagId == kVMFTagDefineSprite ) {
            VMFTag child;
            if ( !parseSprite( pData, size, cursor, false, child, cursor ) ) {
                return false;
            }
            outSprite.Children.push_back( std::move( child ) );
            continue;
        }

        if ( childSize < kTagHeaderSize || cursor + childSize > size ) {
            error = Format( "tag 0x%x at 0x%zx has a bad size 0x%x", tagId,
                            cursor, childSize );
            return false;
        }

        VMFTag child;
        child.TagId = tagId;
        child.Payload.assign( pData + cursor + kTagHeaderSize,
                              pData + cursor + childSize );
        outSprite.Children.push_back( std::move( child ) );
        cursor += childSize;

        if ( tagId == kVMFTagShowFrame ) {
            frameOffsets.push_back( uint32_t( cursor - offset ) );
        }
    }

    outEnd = cursor;

    const uint16_t numFrames = Read<uint16_t>( pSprite, 0x0c );
    if ( numFrames + 1u != frameOffsets.size() ) {
        warnings.push_back( Format( "sprite at 0x%zx stores %u frames, has %zu",
                                    offset, numFrames,
                                    frameOffsets.size() - 1 ) );
    }

    const uint32_t span = Read<uint32_t>( pSprite, 0x10 );
    if ( span != cursor - offset ) {
        warnings.push_back(
            Format( "sprite at 0x%zx stores span 0x%x, has 0x%zx", offset, span,
                    cursor - offset ) );
    }

    const size_t numStoredOffsets = ( tagSize - kSpriteHeaderSize ) / 4;
    if ( numStoredOffsets != frameOffsets.size() ) {
        warnings.push_back(
            Format( "sprite at 0x%zx stores %zu frame offsets, has %zu", offset,
                    numStoredOffsets, frameOffsets.size() ) );
    } else {
        for ( size_t i = 0; i < numStoredOffsets; i++ ) {
            const uint32_t stored = Read<uint32_t>(
                pSprite, uint32_t( kSpriteHeaderSize + i * 4 ) );
            if ( stored != frameOffsets[i] ) {
                warnings.push_back( Format(
                    "sprite at 0x%zx frame %zu stores 0x%x, starts at 0x%x",
                    offset, i + 1, stored, frameOffsets[i] ) );
                break;
            }
        }
    }

    return true;
}

std::vector<uint8_t> VMFDocument::serialize() const {
    std::vector<uint8_t> out;

    Append<uint32_t>( out, kVMFMagic );
    Append<uint8_t>( out, HeaderPadding );
    Append<uint8_t>( out, VersionMajor );
    Append<uint16_t>( out, VersionMinor );
    Append<uint32_t>( out, ExporterVersion );
    out.insert( out.end(), AuthorName.begin(), AuthorName.end() );
    for ( float value : FrameSize ) {
        Append<float>( out, value );
    }
    Append<uint16_t>( out, FrameRate );
    Append<uint16_t>( out, NumCharacters );
    Append<uint16_t>( out, NumDisplayList );
    Append<uint16_t>( out, NumSprites );
    Append<uint32_t>( out, TestDriveCRC32Unfolded( out.data(), out.size() ) );

    WriteSprite( out, Root, true );

    Append<uint32_t>( out, kVMFXIXTMagic );
    Append<uint32_t>( out, uint32_t( AtlasEntries.size() ) );
    const uint8_t *pEntries = (const uint8_t *)AtlasEntries.data();
    out.insert( out.end(), pEntries,
                pEntries + AtlasEntries.size() * sizeof( VMFAtlasEntry ) );
    out.insert( out.end(), Trailer.begin(), Trailer.end() );

    return out;
}

uint16_t VMFDocument::computeNumCharacters() const {
    uint32_t maxIndex = 0;
    ComputeNumCharacters( Root, maxIndex );
    return uint16_t( maxIndex );
}
