#include "shared.h"
#include "virtual_file_system.h"
#include "file_path_register.h"

VirtualFileSystemRegister gVirtualFileSystemRegister = {};

void PathSplit( char *param_1,
                char *param_2,
                char *param_3,
                char *param_4,
                char *param_5 ) {
    char cVar1;
    char *pcVar2;
    char *pcVar3;

    if ( ( *param_5 != '\0' ) && ( *param_5 == '<' ) ) {
        do {
            cVar1 = *param_5;
            if ( param_1 != (char *)0x0 ) {
                *param_1 = cVar1;
                param_1 = param_1 + 1;
            }
        } while ( ( cVar1 != '\0' ) &&
                  ( param_5 = param_5 + 1, cVar1 != '>' ) );
    }
    if ( param_1 != (char *)0x0 ) {
        *param_1 = '\0';
    }
    if ( ( *param_5 != '\0' ) &&
         ( ( pcVar2 = strchr( param_5, '/' ), pcVar2 != (char *)0x0 ) ||
           ( pcVar2 = strchr( param_5, ':' ), pcVar2 != (char *)0x0 ) ) ) {
        do {
            cVar1 = *param_5;
            if ( param_2 != (char *)0x0 ) {
                *param_2 = cVar1;
                param_2 = param_2 + 1;
            }
        } while ( ( cVar1 != '\0' ) &&
                  ( param_5 = param_5 + 1, cVar1 != ':' ) );
    }
    if ( param_2 != (char *)0x0 ) {
        *param_2 = '\0';
    }
    if ( ( *param_5 != '\0' ) &&
         ( ( pcVar2 = strchr( param_5, '/' ), pcVar2 != (char *)0x0 ) ||
           ( pcVar2 = strchr( param_5, '\\' ), pcVar2 != (char *)0x0 ) ) ) {
        pcVar2 = param_5;
        do {
            cVar1 = *pcVar2;
            pcVar2 = pcVar2 + 1;
        } while ( cVar1 != '\0' );
        pcVar3 = param_5 + ( (size_t)pcVar2 - (size_t)( param_5 + 1 ) );
        cVar1 = param_5[(size_t)pcVar2 - (size_t)( param_5 + 1 )];
        while ( cVar1 != '\\' ) {
            pcVar2 = pcVar3 + -1;
            pcVar3 = pcVar3 + -1;
            cVar1 = *pcVar2;
        }
        for ( ; ( param_5 != pcVar3 + 1 && ( *param_5 != '\0' ) );
              param_5 = param_5 + 1 ) {
            if ( param_3 != (char *)0x0 ) {
                *param_3 = *param_5;
                param_3 = param_3 + 1;
            }
        }
    }
    if ( param_3 != (char *)0x0 ) {
        *param_3 = '\0';
    }
    cVar1 = *param_5;
    while ( cVar1 != '\0' ) {
        if ( param_4 != (char *)0x0 ) {
            *param_4 = cVar1;
            param_4 = param_4 + 1;
        }
        if ( cVar1 != '\0' ) {
            param_5 = param_5 + 1;
        }
        cVar1 = *param_5;
    }
    if ( param_4 != (char *)0x0 ) {
        *param_4 = '\0';
    }
}

#ifdef OTDU_UNIX
inline bool IsCharEqualInsensitive(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](char ac, char bc) {
        return std::tolower(static_cast<unsigned char>(ac)) == std::tolower(static_cast<unsigned char>(bc));
    });
}

std::filesystem::path ResolvePathInsensitive(const std::filesystem::path& targetPath) {
    std::filesystem::path currentResolved = targetPath.root_path();

    for (const auto& component : targetPath.relative_path()) {
        std::string targetComponentStr = component.string();
        bool bMatchFound = false;

        if (!std::filesystem::exists(currentResolved) || !std::filesystem::is_directory(currentResolved)) {
            return std::filesystem::path{};
        }

        // Scan the directory for a case-insensitive match
        for (const auto& entry : std::filesystem::directory_iterator(currentResolved)) {
            std::string entryName = entry.path().filename().string();
            if (IsCharEqualInsensitive(entryName, targetComponentStr)) {
                currentResolved /= entry.path().filename();
                bMatchFound = true;
                break;
            }
        }

        if (!bMatchFound) {
            return std::filesystem::path{};
        }
    }

    return currentResolved;
}
#endif

inline bool FileExists( FileIterator *pIterator, const std::string& completeResourcePath ) {
     if ( std::filesystem::exists( completeResourcePath ) ) {
        OTDU_LOG_DEBUG( "File '%s' exists!\n", completeResourcePath.c_str() );

        std::filesystem::file_status status =
            std::filesystem::status( completeResourcePath );
        pIterator->ItInfos.bIsFolder = std::filesystem::is_directory( status );
        pIterator->ItInfos.Filename = completeResourcePath;
        pIterator->ItInfos.Size =
            pIterator->ItInfos.bIsFolder
                ? 0
                : static_cast<uint32_t>(
                      std::filesystem::file_size( completeResourcePath ) );

        // TODO: AFAIK those are useless so we can skip all this crap for now...
        // pIterator->ItInfos.CreationDate =
        // std::filesystem::last_write_time(completeResourcePath);
        // pIterator->ItInfos.ModificationDate =
        // std::filesystem::last_write_time(completeResourcePath);

        return true;
    }
    return false;
}

VirtualFileSystem::VirtualFileSystem()
    : pFirstDeviceHandle( nullptr ),
      flagCaps( 0x2F ),
      name( "<PC>" ),
      defaultUnit( "" ) {}

VirtualFileSystem::~VirtualFileSystem() {}

void VirtualFileSystem::initialize() {
    // TODO: Support multiple type of VFS? (TDU1 only support HDD)
    char pWorkingDirectory[512];

#ifdef OTDU_WIN32
    GetCurrentDirectoryA( 512, pWorkingDirectory );
#elif defined( OTDU_UNIX )
    getcwd( pWorkingDirectory, 512 * sizeof( char ) );
#endif

    setDefaultPath( pWorkingDirectory );

    std::string path = name;
    path += pWorkingDirectory;
    path += "\\";

    gFilePathRegister.registerPath( path );
}

static bool MatchesWildcard( const char *pName, const char *pPattern ) {
    if ( *pPattern == '\0' ) {
        return *pName == '\0';
    }

    if ( *pPattern == '*' ) {
        for ( const char *pSuffix = pName;; pSuffix++ ) {
            if ( MatchesWildcard( pSuffix, pPattern + 1 ) ) {
                return true;
            }
            if ( *pSuffix == '\0' ) {
                return false;
            }
        }
    }

    if ( *pName == '\0' ) {
        return false;
    }

    if ( *pPattern != '?' && tolower( (unsigned char)*pName ) !=
                                 tolower( (unsigned char)*pPattern ) ) {
        return false;
    }

    return MatchesWildcard( pName + 1, pPattern + 1 );
}

bool VirtualFileSystem::findFile( FileIterator *pIterator,
                                  const FileIterator::eOpenMode mode ) {
    // FUN_0060b360
    if ( mode == FileIterator::eOpenMode::FIOM_Close ) {
        pIterator->Matches.clear();
        pIterator->ItHandle = 0;
        return true;
    }

    if ( mode == FileIterator::eOpenMode::FIOM_FindNext ) {
        if ( pIterator->ItHandle < 0 ||
             pIterator->ItHandle >= (int32_t)pIterator->Matches.size() ) {
            return false;
        }
        return FileExists( pIterator,
                           pIterator->Matches[pIterator->ItHandle++] );
    }

    pIterator->Matches.clear();
    pIterator->ItHandle = 0;

    // YOLO: Check for direct access (we're on PC and don't really need to check
    // disk or other kind of external devices)
    std::string completeResourcePath = pIterator->SearchDirectory;
    completeResourcePath += "/";
    completeResourcePath += pIterator->SearchPattern;
    if ( FileExists( pIterator, completeResourcePath ) ) {
        return true;
    }

#ifdef OTDU_UNIX
    // TDU1 filename casing is very inconsistent (codebase, banks and assets). 
    // To avoid patchins assets we do another insensitive lookup on Linux.
    auto insensitiveFilepath = ResolvePathInsensitive(completeResourcePath);
    if (!insensitiveFilepath.empty()) {
        OTDU_LOG_WARN( "Filepath '%s' casing is incorrect! Found closest match '%s'\n", 
            completeResourcePath.c_str(),insensitiveFilepath.string().c_str() );

        completeResourcePath = insensitiveFilepath.string();

        if ( FileExists( pIterator, completeResourcePath ) ) {
            return true;
        }
    }
#endif

    if ( pIterator->SearchPattern.find_first_of( "*?" ) == std::string::npos ) {
        return false;
    }

    const std::filesystem::path searchPath( completeResourcePath );
    std::filesystem::path searchFolder = searchPath.parent_path();
    std::string namePattern = searchPath.filename().string();
    if ( namePattern == "*.*" ) {
        namePattern = "*";
    }

    std::error_code errorCode;
#ifdef OTDU_UNIX
    if ( !std::filesystem::is_directory( searchFolder, errorCode ) ) {
        searchFolder = ResolvePathInsensitive( searchFolder );
    }
#endif
    if ( searchFolder.empty() ||
         !std::filesystem::is_directory( searchFolder, errorCode ) ) {
        return false;
    }

    for ( const auto &entry :
          std::filesystem::directory_iterator( searchFolder, errorCode ) ) {
        const std::string entryName = entry.path().filename().string();
        if ( MatchesWildcard( entryName.c_str(), namePattern.c_str() ) ) {
            pIterator->Matches.push_back( entry.path().string() );
        }
    }

    if ( pIterator->Matches.empty() ) {
        return false;
    }

    std::sort( pIterator->Matches.begin(), pIterator->Matches.end() );
    pIterator->ItHandle = 1;
    return FileExists( pIterator, pIterator->Matches[0] );
}

void VirtualFileSystem::setDefaultPath( const char *param_1 ) {
    char pDefaultUnit[512];
    char local_200[512];
    strcpy( local_200, param_1 );

    PathSplit( nullptr, pDefaultUnit, nullptr, nullptr, local_200 );

    defaultUnit = param_1;

    OTDU_LOG_DEBUG( "Default unit set to '%s'\n", defaultUnit.c_str() );
}

bool VirtualFileSystem::getDefaultPath( std::string &param_2 ) {
    param_2 = defaultUnit;
    return !defaultUnit.empty();

    // char cVar1;
    // char *pcVar2;
    // char *pcVar3;

    // pcVar3 = this->pDefaultUnit;
    // pcVar2 = pcVar3;
    // do {
    //     cVar1 = *pcVar2;
    //     pcVar2[(size_t)param_2 - (size_t)pcVar3] = cVar1;
    //     pcVar2 = pcVar2 + 1;
    // } while (cVar1 != '\0');
    // do {
    //     cVar1 = *pcVar3;
    //     pcVar3 = pcVar3 + 1;
    // } while (cVar1 != '\0');
    // return (size_t)pcVar3 - (size_t)(this->pDefaultUnit + 1);
}

VirtualFileSystemRegister::VirtualFileSystemRegister() {}

VirtualFileSystemRegister::~VirtualFileSystemRegister() {}

VirtualFileSystem *VirtualFileSystemRegister::find( const char *param_1 ) {
    for ( VirtualFileSystem *pVFS : vfsList ) {
        if ( pVFS->getName() == param_1 ) {
            return pVFS;
        }
    }

    OTDU_LOG_WARN( "Failed to find VFS with root '%s'", param_1 );
    return nullptr;
}

VirtualFileSystem *VirtualFileSystemRegister::get( std::string &param_2,
                                                   std::string &param_3,
                                                   const bool param_4 ) {
    if ( param_3.empty() ) {
        param_3 = "";
    }

    size_t iVar7 = 0;
    if ( param_3[0] == '<' ) {
        bool bVar1 = false;
        for ( uint32_t i = 1; i < param_3.length(); i++ ) {
            char cVar2 = param_3[i];
            if ( cVar2 == '>' ) {
                bVar1 = true;
                break;
            }

            char cVar3 = cVar2;
            if ( ( '`' < cVar2 ) && ( cVar2 < '{' ) ) {
                cVar3 = cVar2 + -0x20;
            }
            param_2 += cVar3;
            iVar7++;
        }

        if ( !bVar1 ) {
            param_2.clear();
            return nullptr;
        }
    } else {
        iVar7 = gFilePathRegister.getFiler( param_2.c_str() );
    }

    VirtualFileSystem *peVar4 = findVFS( param_2 );
    if ( peVar4 != nullptr ) {
        if ( param_3[0] == '|' ) {
            uint32_t i = 0;
            for ( i = 0; i < param_3.length(); i++ ) {
                char cVar2 = param_3[i];

                if ( ( '`' < cVar2 ) && ( cVar2 < '{' ) ) {
                    cVar2 = cVar2 + -0x20;
                }

                param_2 += cVar2;
                iVar7++;

                if ( param_3[i + 1] == '|' || param_3[i + 1] == '\0' ) {
                    break;
                }
            }

            if ( i < param_3.length() ) {
                return nullptr;
            }
        } else {
            int32_t iVar5 = 0;
            auto pcVar8 = param_3.find( ':' );
            if ( pcVar8 == std::string::npos ) {
                iVar5 = peVar4->getDefaultPath( param_2 );
                iVar7 += iVar5;
            } else {
                for ( size_t i = 1; i < param_3.length(); i++ ) {
                    char cVar3 = param_3[i];
                    if ( ( '`' < cVar3 ) && ( cVar3 < '{' ) ) {
                        cVar3 = cVar3 + -0x20;
                    }

                    param_2[iVar7] = cVar3;
                    iVar7++;
                    if ( cVar3 == ':' )
                        break;
                }
            }
            char local_200[512];
            local_200[0] = '\0';
            char *pcVar9 = nullptr;

            size_t pcVar7 = 0;
            if ( !param_4 ) {
                auto pcVar7 = param_3.find( '/' );
                if ( pcVar7 == std::string::npos ) {
                    pcVar7 = param_3.find( '\\' );
                }
            }
        }

        return peVar4;
    }

    param_2.clear();
    return nullptr;
}

VirtualFileSystem *VirtualFileSystemRegister::findVFS( std::string &param_2 ) {
    for ( VirtualFileSystem *vfs : vfsList ) {
        if ( vfs->getName() == param_2 ||
             vfs->getName() == "<PC>" ) { // TODO: FIXME:
            return vfs;
        }
    }
    return nullptr;
}

void VirtualFileSystemRegister::registerVFS( VirtualFileSystem *pVfs ) {
    vfsList.push_back( pVfs );
}
