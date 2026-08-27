

#include "core/arg_parser.h"
#include "core/assert.h"
#include "core/binary_helpers.h"
#include "core/logger.h"

#include "filesystem/file_map.h"

#if OTDU_DEVBUILD
bool gBreakOnAssertFailure = true; // DAT_00f47931
static CmdLineArg CmdLineArgDbgBreakOnAssertFailure( "break_on_assert_failure",
                                                     []( const char *pArg ) {
                                                         gBreakOnAssertFailure =
                                                             true;
                                                     } );

bool gDisableAssert = false;
static CmdLineArg CmdLineArgDisableAssert( "disable_assert",
                                           []( const char *pArg ) {
                                               gDisableAssert = true;
                                           } );
#endif

static constexpr uint32_t kBigFileAlignment = 2048u;
static constexpr uint32_t kMapEntrySize = 0x18u;
static constexpr char kMapMagic[5] = { 'M', 'A', 'P', '4', '\0' };

static constexpr const char *kBigFileName = "CarPack.big";
static constexpr const char *kMapFileName = "CarPack.map";
static constexpr const char *kSidecarFileName = "files.txt";

static char gpInputPath[OTDU_MAX_PATH];
static CmdLineArg CmdLineArgInput( "input", []( const char *pArg ) {
    strncpy( gpInputPath, pArg, OTDU_MAX_PATH - 1 );
} );

static char gpOutputPath[OTDU_MAX_PATH];
static CmdLineArg CmdLineArgOutput( "output", []( const char *pArg ) {
    strncpy( gpOutputPath, pArg, OTDU_MAX_PATH - 1 );
} );

static uint32_t gPackVersion = 1u;
static CmdLineArg CmdLineArgVersion( "version", []( const char *pArg ) {
    gPackVersion = (uint32_t)strtoul( pArg, nullptr, 10 );
} );

[[noreturn]] static void PrintUsage() {
    OTDU_LOG_ALWAYS(
        "USAGE: opentdu_modPacker -input <contentDir> [-output <packDir>] "
        "[-version <n>]\n"
        "\n"
        "Packs a folder into the CarPack.big / CarPack.map pair a mod pack "
        "ships,\n"
        "plus a files.txt sidecar naming what is inside it.\n"
        "\n"
        "-input:   Folder whose tree mirrors the resource root, so that\n"
        "          <contentDir>/DataBase/DB.bnk overrides EURO/BNK/DataBase/"
        "DB.bnk.\n"
        "-output:  Where to write the pack. Defaults to the parent of -input,\n"
        "          which is the Mods/<pack>/Files layout.\n"
        "-version: Content revision stamped on every entry (default 1). The "
        "game\n"
        "          keeps the highest across mounted packs and only logs a hit "
        "for\n"
        "          a non-zero one, so leave this alone unless you mean it.\n" );
    exit( 1 );
}

struct PackEntry {
    std::string RelativePath;
    uint32_t CRC = 0u;
    uint32_t Offset = 0u;
    uint32_t Size = 0u;
    uint32_t SizeWithPadding = 0u;
};

static uint32_t RoundUpToAlignment( const uint32_t size ) {
    return ( size + kBigFileAlignment - 1u ) & ~( kBigFileAlignment - 1u );
}

static void WriteBigEndian32( std::vector<uint8_t> &output,
                              const uint32_t value ) {
    output.push_back( (uint8_t)( value >> 24 ) );
    output.push_back( (uint8_t)( value >> 16 ) );
    output.push_back( (uint8_t)( value >> 8 ) );
    output.push_back( (uint8_t)value );
}

static std::string MakeRelativePath( const std::filesystem::path &root,
                                     const std::filesystem::path &file ) {
    std::string relative =
        std::filesystem::relative( file, root ).generic_string();
    return relative;
}

static bool IsPackerOutput( const std::string &relativePath ) {
    return relativePath == kBigFileName || relativePath == kMapFileName ||
           relativePath == kSidecarFileName ||
           relativePath == "contentdesc.xml";
}

static bool CollectEntries( const std::filesystem::path &inputPath,
                            FileMap &hasher,
                            std::vector<PackEntry> &outEntries ) {
    std::error_code errorCode;
    std::filesystem::recursive_directory_iterator it( inputPath, errorCode );
    if ( errorCode ) {
        OTDU_LOG_ALWAYS( "Cannot read '%s': %s\n", inputPath.string().c_str(),
                         errorCode.message().c_str() );
        return false;
    }

    const std::filesystem::recursive_directory_iterator end;
    for ( ; it != end; it.increment( errorCode ) ) {
        if ( errorCode ) {
            OTDU_LOG_ALWAYS( "Cannot walk '%s': %s\n",
                             inputPath.string().c_str(),
                             errorCode.message().c_str() );
            return false;
        }

        if ( !it->is_regular_file( errorCode ) || errorCode ) {
            continue;
        }

        const std::string relativePath =
            MakeRelativePath( inputPath, it->path() );
        if ( IsPackerOutput( relativePath ) ) {
            OTDU_LOG_ALWAYS( "  skipping '%s' (packer output)\n",
                             relativePath.c_str() );
            continue;
        }

        const uintmax_t fileSize = it->file_size( errorCode );
        if ( errorCode ) {
            OTDU_LOG_ALWAYS( "Cannot size '%s': %s\n", relativePath.c_str(),
                             errorCode.message().c_str() );
            return false;
        }

        if ( fileSize > 0xffffffffull ) {
            OTDU_LOG_ALWAYS( "'%s' is larger than a map entry can describe\n",
                             relativePath.c_str() );
            return false;
        }

        PackEntry entry;
        entry.RelativePath = relativePath;
        entry.CRC = hasher.hashFilename( relativePath.c_str() );
        entry.Size = (uint32_t)fileSize;
        entry.SizeWithPadding = RoundUpToAlignment( entry.Size );

        outEntries.push_back( entry );
    }

    return true;
}

static bool SortAndCheckForCollisions( std::vector<PackEntry> &entries ) {
    std::sort( entries.begin(), entries.end(),
               []( const PackEntry &lhs, const PackEntry &rhs ) {
                   return lhs.CRC < rhs.CRC;
               } );

    bool bCollided = false;
    for ( size_t i = 1u; i < entries.size(); i++ ) {
        if ( entries[i].CRC != entries[i - 1u].CRC ) {
            continue;
        }

        bCollided = true;
        OTDU_LOG_ALWAYS( "'%s' and '%s' both hash to %08x -- the game could "
                         "only ever serve one of them\n",
                         entries[i - 1u].RelativePath.c_str(),
                         entries[i].RelativePath.c_str(), entries[i].CRC );
    }

    return !bCollided;
}

static bool WriteBigFile( const std::filesystem::path &inputPath,
                          const std::filesystem::path &bigFilePath,
                          std::vector<PackEntry> &entries ) {
    std::ofstream bigFile( bigFilePath, std::ios::binary | std::ios::trunc );
    if ( !bigFile.is_open() ) {
        OTDU_LOG_ALWAYS( "Cannot write '%s'\n", bigFilePath.string().c_str() );
        return false;
    }

    std::vector<char> padding( kBigFileAlignment, '\0' );
    uint32_t offset = 0u;

    for ( PackEntry &entry : entries ) {
        const std::filesystem::path sourcePath =
            inputPath / std::filesystem::path( entry.RelativePath );

        std::ifstream sourceFile( sourcePath, std::ios::binary );
        if ( !sourceFile.is_open() ) {
            OTDU_LOG_ALWAYS( "Cannot read '%s'\n",
                             sourcePath.string().c_str() );
            return false;
        }

        entry.Offset = offset;

        std::vector<char> content( entry.Size );
        if ( entry.Size != 0u ) {
            sourceFile.read( content.data(), (std::streamsize)entry.Size );
            if ( sourceFile.gcount() != (std::streamsize)entry.Size ) {
                OTDU_LOG_ALWAYS( "'%s' changed size while being packed\n",
                                 entry.RelativePath.c_str() );
                return false;
            }
            bigFile.write( content.data(), (std::streamsize)entry.Size );
        }

        const uint32_t padSize = entry.SizeWithPadding - entry.Size;
        if ( padSize != 0u ) {
            bigFile.write( padding.data(), (std::streamsize)padSize );
        }

        OTDU_LOG_ALWAYS( "  %-52s crc=%08x off=%-10u size=%u\n",
                         entry.RelativePath.c_str(), entry.CRC, entry.Offset,
                         entry.Size );

        offset += entry.SizeWithPadding;
    }

    return true;
}

static bool WriteMapFile( const std::filesystem::path &mapFilePath,
                          const std::vector<PackEntry> &entries,
                          std::vector<uint8_t> &outMapContent ) {
    outMapContent.clear();
    outMapContent.insert( outMapContent.end(), kMapMagic,
                          kMapMagic + sizeof( kMapMagic ) );

    for ( const PackEntry &entry : entries ) {
        WriteBigEndian32( outMapContent, entry.CRC );
        WriteBigEndian32( outMapContent, entry.SizeWithPadding );
        WriteBigEndian32( outMapContent, entry.Offset );
        WriteBigEndian32( outMapContent, entry.Size );
        WriteBigEndian32( outMapContent, gPackVersion );

        outMapContent.push_back( 0u );
        outMapContent.push_back( 0u );
        outMapContent.push_back( 0u );
        outMapContent.push_back( 0u );
    }

    std::ofstream mapFile( mapFilePath, std::ios::binary | std::ios::trunc );
    if ( !mapFile.is_open() ) {
        OTDU_LOG_ALWAYS( "Cannot write '%s'\n", mapFilePath.string().c_str() );
        return false;
    }

    mapFile.write( (const char *)outMapContent.data(),
                   (std::streamsize)outMapContent.size() );

    return true;
}

static bool WriteSidecar( const std::filesystem::path &sidecarPath,
                          const std::vector<PackEntry> &entries ) {
    std::ofstream sidecarFile( sidecarPath, std::ios::trunc );
    if ( !sidecarFile.is_open() ) {
        OTDU_LOG_ALWAYS( "Cannot write '%s'\n", sidecarPath.string().c_str() );
        return false;
    }

    for ( const PackEntry &entry : entries ) {
        sidecarFile << entry.RelativePath << "\n";
    }

    return true;
}

static bool VerifyPack( const std::vector<uint8_t> &mapContent,
                        const std::vector<PackEntry> &entries,
                        const uint32_t bigFileSize ) {
    void *pMapCopy = TestDrive::AllocAligned( mapContent.size() );
    if ( pMapCopy == nullptr ) {
        OTDU_LOG_ALWAYS( "Out of memory verifying the pack\n" );
        return false;
    }

    memcpy( pMapCopy, mapContent.data(), mapContent.size() );

    FileMap filemap;
    filemap.initializeCRCLUT();
    if ( !filemap.initFromMem( pMapCopy, (uint32_t)mapContent.size() ) ) {
        OTDU_LOG_ALWAYS( "The map that was just written does not load\n" );
        return false;
    }

    filemap.setHeaderToSkip( "EURO/BNK/" );

    bool bValid = true;
    for ( const PackEntry &entry : entries ) {
        const std::string fullPath = "EURO/BNK/" + entry.RelativePath;

        FileMapEntry *pFound = filemap.findEntry( fullPath.c_str() );
        if ( pFound == nullptr ) {
            OTDU_LOG_ALWAYS( "'%s' is in the pack but does not resolve\n",
                             entry.RelativePath.c_str() );
            bValid = false;
            continue;
        }

        if ( pFound->CRC != entry.CRC || pFound->Size != entry.Size ||
             pFound->Offset != entry.Offset ||
             pFound->Version != gPackVersion ) {
            OTDU_LOG_ALWAYS( "'%s' reads back wrong (crc %08x/%08x, size "
                             "%u/%u, offset %u/%u, version %u/%u)\n",
                             entry.RelativePath.c_str(), pFound->CRC, entry.CRC,
                             pFound->Size, entry.Size, pFound->Offset,
                             entry.Offset, pFound->Version, gPackVersion );
            bValid = false;
            continue;
        }

        const uint64_t readEnd =
            (uint64_t)pFound->Offset + RoundUpToAlignment( pFound->Size );
        if ( readEnd > bigFileSize ) {
            OTDU_LOG_ALWAYS( "'%s' would be read past the end of the bigfile "
                             "(%llu > %u)\n",
                             entry.RelativePath.c_str(),
                             (unsigned long long)readEnd, bigFileSize );
            bValid = false;
        }
    }

    return bValid;
}

int main( int argc, char *argv[] ) {
    memset( gpInputPath, 0, sizeof( gpInputPath ) );
    memset( gpOutputPath, 0, sizeof( gpOutputPath ) );

    GetArgParserRegister().parseCmdLineArgs( (const char **)argv, argc );

    if ( *gpInputPath == '\0' ) {
        OTDU_LOG_ALWAYS( "Missing cmdline arg 'input'!\n" );
        PrintUsage();
    }

    std::error_code errorCode;
    const std::filesystem::path inputPath =
        std::filesystem::path( gpInputPath ).lexically_normal();
    if ( !std::filesystem::is_directory( inputPath, errorCode ) ) {
        OTDU_LOG_ALWAYS( "'%s' is not a folder\n", inputPath.string().c_str() );
        return 1;
    }

    std::filesystem::path outputPath =
        ( *gpOutputPath != '\0' ) ? std::filesystem::path( gpOutputPath )
                                  : inputPath.parent_path();
    if ( outputPath.empty() ) {
        outputPath = ".";
    }

    std::filesystem::create_directories( outputPath, errorCode );
    if ( !std::filesystem::is_directory( outputPath, errorCode ) ) {
        OTDU_LOG_ALWAYS( "Cannot write into '%s'\n",
                         outputPath.string().c_str() );
        return 1;
    }

    OTDU_LOG_ALWAYS( "Packing '%s' -> '%s' (version %u)\n",
                     inputPath.string().c_str(), outputPath.string().c_str(),
                     gPackVersion );

    FileMap hasher;
    hasher.initializeCRCLUT();

    std::vector<PackEntry> entries;
    if ( !CollectEntries( inputPath, hasher, entries ) ) {
        return 1;
    }

    if ( entries.empty() ) {
        OTDU_LOG_ALWAYS( "'%s' holds no files to pack\n",
                         inputPath.string().c_str() );
        return 1;
    }

    if ( !SortAndCheckForCollisions( entries ) ) {
        return 1;
    }

    const std::filesystem::path bigFilePath = outputPath / kBigFileName;
    if ( !WriteBigFile( inputPath, bigFilePath, entries ) ) {
        return 1;
    }

    std::vector<uint8_t> mapContent;
    if ( !WriteMapFile( outputPath / kMapFileName, entries, mapContent ) ) {
        return 1;
    }

    if ( !WriteSidecar( outputPath / kSidecarFileName, entries ) ) {
        return 1;
    }

    const uint32_t bigFileSize =
        entries.back().Offset + entries.back().SizeWithPadding;
    if ( !VerifyPack( mapContent, entries, bigFileSize ) ) {
        OTDU_LOG_ALWAYS( "The pack did NOT verify; do not ship it.\n" );
        return 1;
    }

    OTDU_LOG_ALWAYS( "Packed and verified %zu file(s): %s is %u bytes, %s is "
                     "%zu bytes.\n",
                     entries.size(), kBigFileName, bigFileSize, kMapFileName,
                     mapContent.size() );

    return 0;
}
