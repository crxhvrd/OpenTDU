#pragma once

struct FileMapEntry {
    uint32_t CRC = 0;
    uint32_t SizeWithPadding = 0;
    uint32_t Offset = 0;
    uint32_t Size = 0;
    uint32_t Version = 0;
    uint32_t EndiannessProbe = 0;
    uint8_t bCompressed : 1;

    FileMapEntry() : bCompressed( false ) {}

    int32_t deserialize( const void *pMemory );
};

enum class eContextSource {
    CS_None = 0,
    CS_Publisher = 1,
    CS_Marketplace = 2,
    CS_Savegame = 4
};

struct EnumerateContextItem {
    uint8_t bIsValid : 1;

    uint32_t StoredOnHDD;
    eContextSource Source;

    std::wstring DisplayName;
    std::string Filename;
};

struct FileMap {
    uint32_t pCRCLUT[256] = {};
    std::string FilenameBigFile;

    void *pMapAllocation = nullptr;

    void *pMapStart = nullptr;
    uint32_t MapSize = 0;

    EnumerateContextItem Context;
    FileMapEntry Entry;

    std::string CurrentFile;
    std::string HeaderToSkip;
    std::string TempFilename;

    uint8_t bUseBigFile : 1;
    uint8_t bNeedName : 1;
    uint8_t bUnknown : 1;

    FileMap() : bUseBigFile( false ), bNeedName( false ), bUnknown( false ) {}

    bool initFromMem( void *pMemory, const uint32_t mapFileSize );
    void setHeaderToSkip( const char *pHeaderToSkip );
    bool setBigFileName( const char *pFilename );
    void releaseMap();

    void initializeCRCLUT();
    bool findEntryInSet( const char *pFilename, FileMapEntry **ppOutEntry );
    FileMapEntry *findEntry( const char *pFilename );
    FileMapEntry *findEntry( const uint32_t crc );
    void normalizeFilename( const char *pFilename );
    uint32_t hashFilename( const char *pFilename );
    uint32_t crc32( const char *pString, size_t length );
    bool startsWithHeaderToSkip( const char *pFilename ) const;
    std::string &resolveName( const char *pFilename, const bool bCompressed );
};
