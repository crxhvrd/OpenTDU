#include "shared.h"
#include "file_direct_access.h"

#include "core/mutex.h"
#include "gs_file.h"
#include "player_data/user_profile.h"
#include "virtual_file_system.h"

#include "zlib.h"

static constexpr uint32_t kZlibChunkSize = 65535;
struct CompressedFileIntermediates {
    TestDriveMutex Mutex;
    Bytef pInput[kZlibChunkSize];
    uint32_t InputSize;
    Bytef pOutput[kZlibChunkSize];
    uLongf OutputSize;

    CompressedFileIntermediates() : Mutex(), InputSize( 0 ), OutputSize( 0 ) {
        memset( pInput, 0, sizeof( Bytef ) * kZlibChunkSize );
        memset( pOutput, 0, sizeof( Bytef ) * kZlibChunkSize );
    }

    int32_t Inflate( const Bytef *param_2, uint32_t param_3 ) {
        // FUN_0060ac70
        if ( 0xffff < param_3 ) {
            return -1;
        }
        OutputSize = -1;
        int32_t iVar1 = uncompress( pOutput, &OutputSize, param_2, param_3 );
        return ( iVar1 == Z_OK ) ? OutputSize : -1;
    }
};
static CompressedFileIntermediates DAT_00feaef8 = {}; // DAT_00feaef8
// Endianess of the app?
static int32_t INT_00f479a0 = -1; // DAT_00f479a0

static bool CreateFolders( const std::wstring &path ) {
    std::error_code ec;

    bool bSuccess = std::filesystem::create_directories( path, ec );
    if ( !bSuccess ) {
        OTDU_LOG_ERROR( "Failed to create directory '%S': %s\n", path.c_str(),
                        ec.message().c_str() );
    }

    return bSuccess;
}

static uint32_t GetZCMagic() {
    // FUN_0060ac20
    if ( INT_00f479a0 == -1 ) {
        INT_00f479a0 = 1;
    }

    return 0x2e5a4300;
}

static uint32_t ReverseBits( uint32_t param_1 ) {
    // FUN_0060ab90
    if ( INT_00f479a0 == -1 ) {
        INT_00f479a0 = 1;
    } else if ( INT_00f479a0 != 1 ) {
        return param_1;
    }

    return ( ( param_1 & 0xff00 ) | ( param_1 << 0x10 ) ) << 8 |
           ( ( param_1 & 0xff0000 ) | ( param_1 >> 0x10 ) ) >> 8;
}

uint16_t FUN_0060abe0( uint16_t param_1 ) {
    if ( INT_00f479a0 == -1 ) {
        INT_00f479a0 = 1;
        return param_1 >> 8 | param_1 << 8;
    }
    if ( INT_00f479a0 == 1 ) {
        return param_1 >> 8 | param_1 << 8;
    }
    return param_1;
}

FileDirectAccess::FileDirectAccess()
    : CurrentHashcode( 0 ),
      pContext( nullptr ),
      pHandle( nullptr ),
      StoredOnHDD( 0 ),
      bNeedName( false ),
      bIsOpen( false ) {}

void FUN_006078d0( std::string &param_2 ) {
    size_t pcVar2 = param_2.find( '>' );
    if ( pcVar2 == std::string::npos ) {
        return;
    }

    size_t iVar3 = param_2.length() - pcVar2;
    for ( size_t i = pcVar2; i < param_2.length(); i++ ) {
        param_2[iVar3] = param_2[i];
    }
}

bool FileDirectAccess::enumerateContext( const uint64_t profileHashcode,
                                         const eContextSource source,
                                         const char *pDeviceFilter,
                                         const char *pContextSuffix,
                                         const uint32_t maxNumItems,
                                         EnumerateContextItem *pOutItems,
                                         uint32_t *pOutNumItems ) {
    // FUN_00607ff0
    *pOutNumItems = 0u;

#ifndef OTDU_FILE_NO_CONTEXT_SUPPORT

    for ( uint32_t i = 0; i < maxNumItems; i++ ) {
        pOutItems[i] = EnumerateContextItem();
    }

    std::wstring saveFolder = gpFile->getSaveLocation( source );
    if ( saveFolder.empty() ) {
        return false;
    }

    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    AbsoluteFilename = converter.to_bytes( saveFolder );

    const std::string profileName =
        gUserProfileRegister.getProfileName( profileHashcode );
    if ( profileName.empty() ) {
        return false;
    }

    FolderName = saveFolder;
    FolderName += L"/";
    FolderName += converter.from_bytes( profileName );
    FolderName += L"/";

    std::error_code errorCode;
    if ( !std::filesystem::is_directory( FolderName, errorCode ) ) {
        OTDU_LOG_WARN(
            "Directory '%S' does not exist; skipping context enumeration...\n",
            FolderName.c_str() );
        return false;
    }

    const size_t deviceFilterLength =
        ( pDeviceFilter != nullptr ) ? strlen( pDeviceFilter ) : 0;
    const size_t contextSuffixLength =
        ( pContextSuffix != nullptr ) ? strlen( pContextSuffix ) : 0;

    uint32_t numItems = 0u;
    for ( const auto &entry :
          std::filesystem::directory_iterator( FolderName, errorCode ) ) {
        if ( !entry.is_directory() ) {
            continue;
        }

        const std::string folderName = entry.path().filename().string();
        if ( deviceFilterLength != 0 &&
             StringNCmpI( folderName.c_str(), pDeviceFilter,
                          deviceFilterLength ) != 0 ) {
            continue;
        }

        if ( contextSuffixLength != 0 ) {
            if ( folderName.length() < contextSuffixLength ||
                 StringNCmpI( folderName.c_str() + folderName.length() -
                                  contextSuffixLength,
                              pContextSuffix, contextSuffixLength ) != 0 ) {
                continue;
            }
        }

        OTDU_LOG_DEBUG( "\t-Found savegame context '%s'\n",
                        folderName.c_str() );

        EnumerateContextItem &item = pOutItems[numItems];
        item.bIsValid = true;
        item.StoredOnHDD = uint32_t( profileHashcode );
        item.Source = source;
        item.Filename = folderName;
        item.DisplayName = converter.from_bytes( folderName );

        numItems++;
        if ( numItems >= maxNumItems ) {
            break;
        }
    }

    *pOutNumItems = numItems;
    return true;
#else
    return false;
#endif
}

bool FileDirectAccess::openContext( const uint64_t hashcode,
                                    const char *pFilename,
                                    const eContextSource source ) {
    // FUN_00607e80
#ifndef OTDU_FILE_NO_CONTEXT_SUPPORT
    FolderPath.clear();
    ContainerFolderPath.clear();
    FolderName.clear();
    CurrentHashcode = 0ull;
    StoredOnHDD = 0;

    std::wstring saveFolder = gpFile->getSaveLocation( source );
    if ( saveFolder.empty() ) {
        return false;
    }

    CurrentHashcode = hashcode;
    bIsOpen = true;
    ContainerFolderPath = pFilename;

    const std::string profileName =
        gUserProfileRegister.getProfileName( CurrentHashcode );
    if ( profileName.empty() ) {
        bIsOpen = false;
        return false;
    }

    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    FolderPath = converter.to_bytes( saveFolder );

    FolderName = saveFolder;
    FolderName += L"/";
    FolderName += converter.from_bytes( profileName );
    FolderName += L"/";
    FolderName += converter.from_bytes( ContainerFolderPath );
    FolderName += L"/";

    CreateFolders( FolderName );

    return true;
#else
    return false;
#endif
}

bool FileDirectAccess::copyContextFrom( const FileDirectAccess &other ) {
    FolderPath = other.FolderPath;
    ContainerFolderPath = other.ContainerFolderPath;
    FolderName = other.FolderName;
    CurrentHashcode = other.CurrentHashcode;
    StoredOnHDD = other.StoredOnHDD;
    bNeedName = other.bNeedName;
    bIsOpen = other.bIsOpen;

    return bIsOpen;
}

bool FileDirectAccess::close() {
    Stream.close();
    StreamWrite.close();

    return true;
}

bool FileDirectAccess::reset() {
    if ( !bIsOpen ) {
        return false;
    }

    bIsOpen = false;
    FolderPath.clear();
    ContainerFolderPath.clear();
    StoredOnHDD = 0;
    return true;
}

bool FileDirectAccess::loadContentIntoMemory( const char *pFilename,
                                              int32_t sizeToRead,
                                              int32_t offset,
                                              void **ppOutContent,
                                              uint32_t *pContentSize ) {
    Stream =
        std::ifstream( pFilename, std::ifstream::in | std::ifstream::binary );
    if ( !Stream.is_open() || !Stream.good() ) {
        return false;
    }
    Stream.seekg( 0, std::ios::end );
    auto fileSize = Stream.tellg();
    Stream.seekg( 0, std::ios::beg );

    if ( offset > (int32_t)fileSize ) {
        close();
        return true;
    }

    Stream.seekg( offset );
    char *pOutContent = (char *)*ppOutContent;
    //    pOutContent += 4;

    Stream.read( (char *)pOutContent, sizeToRead );

    auto numBytesRead = Stream.gcount();

    if ( pContentSize != nullptr ) {
        *pContentSize = static_cast<uint32_t>( numBytesRead );
    }

    close();
    return ( numBytesRead != 0 );
}

bool FileDirectAccess::loadzCompressFileInMem( const char *pFilename,
                                               int32_t sizeToRead,
                                               int32_t offset,
                                               void **ppOutContent,
                                               uint32_t *pContentSize ) {
    // FUN_006087d0
    Stream =
        std::ifstream( pFilename, std::ifstream::in | std::ifstream::binary );
    if ( !Stream.is_open() || !Stream.good() ) {
        return false;
    }

    int8_t *puVar10 = (int8_t *)*ppOutContent;
    if ( offset == 0 ) {
        Stream.seekg( offset );

        uint32_t magic = 0x0;
        Stream.read( (char *)&magic, sizeof( uint32_t ) );

        if ( !Stream && Stream.gcount() == 0 ) {
            OTDU_LOG_ERROR(
                "Failed to read .zc magic from file (bad stream)\n" );
        } else {
            OTDU_ASSERT( Stream.gcount() == 4 );

            uint32_t uVar5 = ReverseBits( magic );
            uint32_t uVar6 = GetZCMagic();
            if ( uVar5 != uVar6 ) {
                OTDU_LOG_ERROR( "edFileDirectAccess::LoadFileInMem()=>  "
                                "INVALIDValid ZC Header Found Loop\n" );
                if ( pContentSize != nullptr ) {
                    *pContentSize = 0;
                }
                close();
                return false;
            }

            OTDU_LOG_INFO( "edFileDirectAccess::LoadFileInMem()=>  Valid ZC "
                           "Header Found Loop\n" );
            {
                ScopedMutexLock lock( &DAT_00feaef8.Mutex );

                uint32_t DVar3;
                Stream.read( (char *)&DVar3, sizeof( int32_t ) );

                if ( Stream.gcount() != 4 ) {
                    close();
                    return false;
                }

                int32_t DStack_4 = ReverseBits( DVar3 );
                int32_t param_5 = 0;
                do {
                    uint16_t uStack_c = 0;
                    Stream.read( (char *)&uStack_c, sizeof( uint16_t ) );
                    if ( Stream.gcount() != 0x2 ) {
                        break;
                    }

                    uint16_t uVar8 = FUN_0060abe0( uStack_c );
                    Stream.read( (char *)&DAT_00feaef8.pInput, uVar8 );

                    if ( Stream.gcount() != uVar8 ) {
                        OTDU_LOG_ERROR(
                            "edFileDirectAccess::LoadFileInMem()=>  anormal "
                            "End of ReadFile( Compress read) Loop\n" );
                        if ( pContentSize != nullptr ) {
                            *pContentSize = 0;
                        }
                        Stream.close();
                        return false;
                    }

                    int32_t uVar9 =
                        DAT_00feaef8.Inflate( DAT_00feaef8.pInput, uVar8 );
                    if ( uVar9 < 0 ) {
                        OTDU_LOG_ERROR(
                            "edFileDirectAccess::LoadFileInMem()=>  Error in  "
                            "ReadFile(Uncom pressToDst) Loop\n" );
                        if ( pContentSize != nullptr ) {
                            *pContentSize = 0;
                        }
                        Stream.close();
                        return false;
                    }

                    memcpy( puVar10, DAT_00feaef8.pOutput, uVar9 );

                    puVar10 += uVar9;
                    param_5 += uVar9;

                    if ( pContentSize != nullptr ) {
                        *pContentSize = param_5;
                    }
                } while ( param_5 != DStack_4 );
            }
            Stream.close();
            return true;
        }
    }

    Stream.close();
    return false;
}

bool FileDirectAccess::openFile( const char *pFilename,
                                 const bool bReadAccess,
                                 const bool bWriteAccess ) {
    // FUN_00607900
    OTDU_ASSERT( !Stream.is_open() && !StreamWrite.is_open() );

    // Once a context has been opened every filename is relative to it.
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    std::wstring fullPath = bIsOpen ? FolderName : std::wstring();
    fullPath += converter.from_bytes( pFilename );

    AbsoluteFilename = converter.to_bytes( fullPath );

    if ( bWriteAccess ) {
        StreamWrite = std::ofstream(
            AbsoluteFilename, std::ifstream::out | std::ifstream::binary );
        return StreamWrite.is_open() && StreamWrite.good();
    }

    if ( !bReadAccess ) {
        return false;
    }

    Stream = std::ifstream( AbsoluteFilename,
                            std::ifstream::in | std::ifstream::binary );
    return Stream.is_open() && Stream.good();
}

uint32_t FileDirectAccess::getFileSize() {
    // FUN_00607b60
    if ( !Stream.is_open() ) {
        return 0u;
    }

    const auto currentOffset = Stream.tellg();
    Stream.seekg( 0, std::ios::end );
    const auto fileSize = Stream.tellg();
    Stream.seekg( currentOffset );

    return uint32_t( fileSize );
}

uint32_t FileDirectAccess::writeToFile( const void *pSource,
                                        const uint32_t size ) {
    if ( !StreamWrite.is_open() || pSource == nullptr ) {
        return 0u;
    }

    StreamWrite.write( (const char *)pSource, size );
    return StreamWrite.good() ? size : 0u;
}

uint32_t FileDirectAccess::readFromFile( void *pDest, const uint32_t size ) {
    // FUN_00607ab0
    if ( !Stream.is_open() || pDest == nullptr ) {
        return 0u;
    }

    Stream.read( (char *)pDest, size );
    return uint32_t( Stream.gcount() );
}
