#include "shared.h"
#include "file_map.h"

#include "core/binary_helpers.h"

int32_t FileMapEntry::deserialize( const void *pMemory ) {
    // FUN_009a6800
    OTDU_ASSERT( pMemory );

    EndiannessProbe = 0x1020304;

    const uint8_t *pMemoryAsByte = (const uint8_t *)pMemory;

    uint32_t crc = ReadUint32( pMemoryAsByte + 0x00 );
    uint32_t size = ReadUint32( pMemoryAsByte + 0x04 );
    uint32_t offset = ReadUint32( pMemoryAsByte + 0x08 );
    uint32_t uncompressedSize = ReadUint32( pMemoryAsByte + 0x0c );
    uint32_t version = ReadUint32( pMemoryAsByte + 0x10 );

    bCompressed = ( pMemoryAsByte[0x14] != 0 );

    if ( *(const uint8_t *)&EndiannessProbe != 1 ) {
        crc = ReverseEndianness32( crc );
        size = ReverseEndianness32( size );
        offset = ReverseEndianness32( offset );
        uncompressedSize = ReverseEndianness32( uncompressedSize );
        version = ReverseEndianness32( version );
    }

    CRC = crc;
    SizeWithPadding = size;
    Offset = offset;
    Size = uncompressedSize;
    Version = version;

    return 0x18;
}

bool FileMap::initFromMem( void *pMemory, const uint32_t mapFileSize ) {
    // FUN_009a6500
    releaseMap();

    if ( pMemory == nullptr || mapFileSize <= 5 ) {
        return false;
    }

    const char *pMagic = (const char *)pMemory;
    if ( pMagic[0] != 'M' || pMagic[1] != 'A' || pMagic[2] != 'P' ) {
        return false;
    }

    if ( pMagic[3] == '3' ) {
        bUnknown = true;
    } else if ( pMagic[3] != '4' ) {
        return false;
    }

    if ( pMagic[4] != '\0' ) {
        return false;
    }

    pMapAllocation = pMemory;
    pMapStart = (uint8_t *)pMemory + 5;
    MapSize = mapFileSize - 5;

    return true;
}

void FileMap::setHeaderToSkip( const char *pHeaderToSkip ) {
    // FUN_009a6770
    HeaderToSkip.clear();
    if ( pHeaderToSkip != nullptr ) {
        HeaderToSkip = pHeaderToSkip;
    }
}

bool FileMap::setBigFileName( const char *pFilename ) {
    // FUN_009a3310
    if ( pFilename == nullptr || strlen( pFilename ) > 0x103 ) {
        return false;
    }

    FilenameBigFile = pFilename;
    bUseBigFile = true;

    return true;
}

void FileMap::releaseMap() {
    if ( pMapAllocation != nullptr ) {
        TestDrive::FreeAligned( pMapAllocation );
        pMapAllocation = nullptr;
    }

    pMapStart = nullptr;
    MapSize = 0;
    bUnknown = false;
    bUseBigFile = false;
    FilenameBigFile.clear();
}

void FileMap::initializeCRCLUT() {
    uint32_t uVar1 = 0;
    uint32_t uVar2 = 0;

    do {
        uint32_t uVar1 = uVar2 >> 1;
        if ( ( uVar2 & 1 ) != 0 ) {
            uVar1 = uVar1 ^ 0xedb88320;
        }
        if ( ( uVar1 & 1 ) == 0 ) {
            uVar1 = uVar1 >> 1;
        } else {
            uVar1 = uVar1 >> 1 ^ 0xedb88320;
        }
        if ( ( uVar1 & 1 ) == 0 ) {
            uVar1 = uVar1 >> 1;
        } else {
            uVar1 = uVar1 >> 1 ^ 0xedb88320;
        }
        if ( ( uVar1 & 1 ) == 0 ) {
            uVar1 = uVar1 >> 1;
        } else {
            uVar1 = uVar1 >> 1 ^ 0xedb88320;
        }
        if ( ( uVar1 & 1 ) == 0 ) {
            uVar1 = uVar1 >> 1;
        } else {
            uVar1 = uVar1 >> 1 ^ 0xedb88320;
        }
        if ( ( uVar1 & 1 ) == 0 ) {
            uVar1 = uVar1 >> 1;
        } else {
            uVar1 = uVar1 >> 1 ^ 0xedb88320;
        }
        if ( ( uVar1 & 1 ) == 0 ) {
            uVar1 = uVar1 >> 1;
        } else {
            uVar1 = uVar1 >> 1 ^ 0xedb88320;
        }
        if ( ( uVar1 & 1 ) == 0 ) {
            uVar1 = uVar1 >> 1;
        } else {
            uVar1 = uVar1 >> 1 ^ 0xedb88320;
        }
        pCRCLUT[uVar2] = uVar1;
        uVar2 = uVar2 + 1;
    } while ( uVar2 < 256 );
}

bool FileMap::findEntryInSet( const char *pFilename,
                              FileMapEntry **ppOutEntry ) {
    std::string local_104 = pFilename;
    local_104 += ".zc";

    if ( MapSize == 0 ) {
        *ppOutEntry = nullptr;
        return false;
    }

    FileMapEntry *pFoundEntry = findEntry( local_104.c_str() );
    if ( pFoundEntry == nullptr ) {
        pFoundEntry = findEntry( pFilename );
        if ( pFoundEntry == nullptr ) {
            *ppOutEntry = nullptr;
            return false;
        }
    }

    *ppOutEntry = pFoundEntry;
    return true;
}

bool FileMap::startsWithHeaderToSkip( const char *pFilename ) const {
    return HeaderToSkip.length() > 0 &&
           StringNCmpI( pFilename, HeaderToSkip.c_str(),
                        HeaderToSkip.length() ) == 0;
}

FileMapEntry *FileMap::findEntry( const char *pFilename ) {
    // FUN_009a6bb0
    if ( startsWithHeaderToSkip( pFilename ) ) {
        uint32_t hashcode = hashFilename( pFilename + HeaderToSkip.length() );
        return findEntry( hashcode );
    }

    uint32_t hashcode = hashFilename( pFilename );
    return findEntry( hashcode );
}

FileMapEntry *FileMap::findEntry( const uint32_t crc ) {
    // FUN_009a6b10
    if ( pMapStart == nullptr ) {
        return nullptr;
    }

    if ( bUnknown ) {
        int8_t *pMapIterator = (int8_t *)pMapStart;
        for ( uint32_t i = 0; i < MapSize;
              i += 0x18 /* sizeof(SerializedFilemapEntry)*/ ) {
            Entry.deserialize( pMapIterator + i );
            if ( Entry.CRC == crc ) {
                return &Entry;
            }
        }

        return nullptr;
    }

    int32_t iVar6 = 0;
    int32_t iVar4 = MapSize / 0x18 - 1;
    if ( -1 < iVar4 ) {
        int8_t *pMapIt = (int8_t *)pMapStart;
        do {
            int32_t iVar5 = ( iVar4 - iVar6 ) / 2 + iVar6;
            Entry.deserialize( pMapIt + iVar5 * 0x18 );

            if ( crc == Entry.CRC ) {
                return &Entry;
            }

            if ( crc < Entry.CRC ) {
                iVar4 = iVar5 + -1;
            } else {
                iVar6 = iVar5 + 1;
            }
        } while ( iVar6 <= iVar4 );
    }

    return nullptr;
}

void FileMap::normalizeFilename( const char *pFilename ) {
    CurrentFile.clear();

    std::string normalizedName = pFilename;
    std::replace( normalizedName.begin(), normalizedName.end(), '\\', '/' );

    std::transform( normalizedName.begin(), normalizedName.end(),
                    normalizedName.begin(), tolower );

    CurrentFile = normalizedName;
}

uint32_t FileMap::hashFilename( const char *pFilename ) {
    normalizeFilename( pFilename );
    return crc32( CurrentFile.c_str(), CurrentFile.length() );
}

uint32_t FileMap::crc32( const char *param_1, size_t param_2 ) {
    uint32_t uVar1 = 0xffffffff;
    if ( 0 < param_2 ) {
        do {
            uVar1 = uVar1 >> 8 ^ pCRCLUT[( *param_1 ^ uVar1 ) & 0xff];
            param_1 = (char *)( param_1 + 1 );
            param_2 = param_2 + -1;
        } while ( param_2 != 0 );
    }

    return ~uVar1;
}

std::string &FileMap::resolveName( const char *pFilename,
                                   const bool bCompressed ) {
    TempFilename.clear();

    std::string filename = pFilename;
    if ( startsWithHeaderToSkip( pFilename ) ) {
        filename = filename.substr( HeaderToSkip.length() );
    }

    std::string extension = bCompressed ? ".zc" : "";
    TempFilename += HeaderToSkip;
    TempFilename += filename;
    TempFilename += extension;

    return TempFilename;
}
