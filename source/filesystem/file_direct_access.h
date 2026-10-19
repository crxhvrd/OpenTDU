#pragma once

#include "file_handle.h"
#include "file_map.h"

struct FileDirectAccess {
    std::string NormalizedFilename;
    std::string AbsoluteFilename;
    std::string FolderPath;
    std::string ContainerFolderPath;
    std::wstring FolderName;

    FileFindInfos FindInfos;

    std::ifstream Stream;
    std::ofstream StreamWrite;
    uint64_t CurrentHashcode;
    void *pContext;
    void *pHandle;
    uint32_t StoredOnHDD;

    uint8_t bNeedName : 1;
    uint8_t bIsOpen : 1;

    FileDirectAccess();

    bool enumerateContext( const uint64_t profileHashcode,
                           const eContextSource source,
                           const char *pDeviceFilter,
                           const char *pContextSuffix,
                           const uint32_t maxNumItems,
                           EnumerateContextItem *pOutItems,
                           uint32_t *pOutNumItems );
    bool openContext( const uint64_t hashcode,
                      const char *pFilename,
                      const eContextSource source );
    bool loadContentIntoMemory( const char *pFilename,
                                int32_t sizeToRead,
                                int32_t offset,
                                void **ppOutContent,
                                uint32_t *pContentSize );
    bool loadzCompressFileInMem( const char *pFilename,
                                 int32_t sizeToRead,
                                 int32_t offset,
                                 void **ppOutContent,
                                 uint32_t *pContentSize );
    bool openFile( const char *pFilename,
                   const bool bReadAccess,
                   const bool bWriteAccess );
    bool fileExists( const char *pFilename );

    uint32_t getFileSize();
    bool seek( const int32_t offset, const bool bFromEnd = false );
    uint32_t tell();
    uint32_t readFromFile( void *pDest, const uint32_t size );
    uint32_t writeToFile( const void *pSource, const uint32_t size );

    bool copyContextFrom( const FileDirectAccess &other );
    bool findFirstFile( const char *pPattern, std::string &outFilename );
    bool findNextFile( std::string &outFilename );
    void findClose();

    bool deleteFileInContext( const char *pFilename );

    bool close();
    bool reset();

private:
    std::vector<std::string> FindResults;
    size_t FindIndex;
};
