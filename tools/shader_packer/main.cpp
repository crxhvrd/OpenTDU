#include "core/arg_parser.h"
#include "core/assert.h"
#include "core/logger.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

static constexpr uint64_t kInvalidMetadataOffset = 0xffffffffffffffffull;

// ShaderTableHeaderEntry (render/shaders/shader_binary_cache.h): the pack is a
// shader table whose entries share the bytecode of identical permutations.
struct PackHeaderEntry {
    uint64_t Hashcode;
    uint64_t Offset;
    uint64_t Size;
    uint64_t MetadataOffset;
};
static_assert( sizeof( PackHeaderEntry ) == 32, "on-disk layout" );

static char gpListPath[OTDU_MAX_PATH];
static CmdLineArg CmdLineArgList( "list", []( const char *pArg ) {
    strncpy( gpListPath, pArg, OTDU_MAX_PATH - 1 );
} );

static char gpInputPath[OTDU_MAX_PATH];
static CmdLineArg CmdLineArgInput( "input", []( const char *pArg ) {
    strncpy( gpInputPath, pArg, OTDU_MAX_PATH - 1 );
} );

static char gpOutputPath[OTDU_MAX_PATH];
static CmdLineArg CmdLineArgOutput( "output", []( const char *pArg ) {
    strncpy( gpOutputPath, pArg, OTDU_MAX_PATH - 1 );
} );

static char gpUnpackPath[OTDU_MAX_PATH];
static CmdLineArg CmdLineArgUnpack( "unpack", []( const char *pArg ) {
    strncpy( gpUnpackPath, pArg, OTDU_MAX_PATH - 1 );
} );

[[noreturn]] static void PrintUsage() {
    OTDU_LOG_ALWAYS(
        "USAGE: opentdu_shaderPacker -list <list> -input <dir> -output <pack>\n"
        "       opentdu_shaderPacker -unpack <pack> -list <list> -output "
        "<dir>\n"
        "\n"
        "Packs the shaders rebuilt from source into a single shader table.\n"
        "\n"
        "-list:   One '<HASHCODE> <module>' line per shader table entry; the\n"
        "         module is the compiled permutation ('<id>.vert.spv').\n"
        "-input:  Folder holding the compiled permutations.\n"
        "-output: Pack to write (or, with -unpack, the folder receiving one\n"
        "         '<HASHCODE>.<vert|frag>.spv' per entry, as\n"
        "         tools/shader_lifter/verify.py expects).\n"
        "-unpack: Pack to extract.\n" );
    exit( 1 );
}

struct ListEntry {
    uint64_t Hashcode;
    std::string Module;
};

static bool ReadList( const char *pPath, std::vector<ListEntry> &outEntries ) {
    std::ifstream listStream( pPath );
    if ( !listStream.is_open() ) {
        OTDU_LOG_ALWAYS( "Cannot open '%s'\n", pPath );
        return false;
    }

    std::unordered_set<uint64_t> hashcodes;
    std::string line;
    while ( std::getline( listStream, line ) ) {
        std::istringstream lineStream( line );
        std::string hashcode;
        ListEntry entry;
        if ( !( lineStream >> hashcode >> entry.Module ) ) {
            continue;
        }
        entry.Hashcode = std::stoull( hashcode, nullptr, 16 );
        // The same table entry can be listed twice (shared by two sections).
        if ( hashcodes.insert( entry.Hashcode ).second ) {
            outEntries.push_back( entry );
        }
    }
    return true;
}

static bool ReadFile( const std::filesystem::path &path,
                      std::vector<char> &outContent ) {
    std::ifstream stream( path, std::ios_base::binary );
    if ( !stream.is_open() ) {
        return false;
    }
    outContent.assign( std::istreambuf_iterator<char>( stream ), {} );
    return true;
}

static int Pack( const std::vector<ListEntry> &entries ) {
    const uint64_t headerSize =
        sizeof( uint64_t ) + entries.size() * sizeof( PackHeaderEntry );

    std::vector<PackHeaderEntry> header;
    std::vector<char> blobs;
    std::unordered_map<std::string, PackHeaderEntry> modules;
    for ( const ListEntry &entry : entries ) {
        auto it = modules.find( entry.Module );
        if ( it == modules.end() ) {
            std::vector<char> content;
            const std::filesystem::path modulePath =
                std::filesystem::path( gpInputPath ) / entry.Module;
            if ( !ReadFile( modulePath, content ) || content.empty() ) {
                OTDU_LOG_ALWAYS( "Cannot read '%s'\n",
                                 modulePath.string().c_str() );
                return 1;
            }

            PackHeaderEntry blob = {};
            blob.Offset = headerSize + blobs.size();
            blob.Size = content.size();
            blob.MetadataOffset = kInvalidMetadataOffset;
            blobs.insert( blobs.end(), content.begin(), content.end() );
            it = modules.insert( std::make_pair( entry.Module, blob ) ).first;
        }

        PackHeaderEntry headerEntry = it->second;
        headerEntry.Hashcode = entry.Hashcode;
        header.push_back( headerEntry );
    }

    std::ofstream packStream( gpOutputPath, std::ios_base::binary );
    if ( !packStream.is_open() ) {
        OTDU_LOG_ALWAYS( "Cannot write '%s'\n", gpOutputPath );
        return 1;
    }
    const uint64_t numEntries = header.size();
    packStream.write( (const char *)&numEntries, sizeof( uint64_t ) );
    packStream.write( (const char *)header.data(),
                      header.size() * sizeof( PackHeaderEntry ) );
    packStream.write( blobs.data(), blobs.size() );

    OTDU_LOG_ALWAYS( "Packed %llu shaders (%zu modules, %zu bytes) into '%s'\n",
                     (unsigned long long)numEntries, modules.size(),
                     blobs.size(), gpOutputPath );
    return 0;
}

static int Unpack( const std::vector<ListEntry> &entries ) {
    std::vector<char> pack;
    if ( !ReadFile( gpUnpackPath, pack ) || pack.size() < sizeof( uint64_t ) ) {
        OTDU_LOG_ALWAYS( "Cannot read '%s'\n", gpUnpackPath );
        return 1;
    }

    uint64_t numEntries = 0;
    memcpy( &numEntries, pack.data(), sizeof( uint64_t ) );
    if ( sizeof( uint64_t ) + numEntries * sizeof( PackHeaderEntry ) >
         pack.size() ) {
        OTDU_LOG_ALWAYS( "'%s' is corrupt\n", gpUnpackPath );
        return 1;
    }

    // The stage only lives in the module name of the list.
    std::unordered_map<uint64_t, const char *> extensions;
    for ( const ListEntry &entry : entries ) {
        extensions[entry.Hashcode] =
            ( entry.Module.find( ".vert." ) != std::string::npos )
                ? ".vert.spv"
                : ".frag.spv";
    }

    std::error_code errorCode;
    std::filesystem::create_directories( gpOutputPath, errorCode );

    const PackHeaderEntry *pHeader =
        (const PackHeaderEntry *)( pack.data() + sizeof( uint64_t ) );
    for ( uint64_t i = 0; i < numEntries; i++ ) {
        const PackHeaderEntry &entry = pHeader[i];
        auto it = extensions.find( entry.Hashcode );
        if ( it == extensions.end() ||
             entry.Offset + entry.Size > pack.size() ) {
            OTDU_LOG_ALWAYS( "Skipping entry %016llX\n",
                             (unsigned long long)entry.Hashcode );
            continue;
        }

        char filename[32];
        snprintf( filename, sizeof( filename ), "%016llX%s",
                  (unsigned long long)entry.Hashcode, it->second );
        std::ofstream moduleStream( std::filesystem::path( gpOutputPath ) /
                                        filename,
                                    std::ios_base::binary );
        moduleStream.write( pack.data() + entry.Offset, entry.Size );
    }

    OTDU_LOG_ALWAYS( "Unpacked %llu shaders into '%s'\n",
                     (unsigned long long)numEntries, gpOutputPath );
    return 0;
}

int main( int argc, char *argv[] ) {
    memset( gpListPath, 0, sizeof( gpListPath ) );
    memset( gpInputPath, 0, sizeof( gpInputPath ) );
    memset( gpOutputPath, 0, sizeof( gpOutputPath ) );
    memset( gpUnpackPath, 0, sizeof( gpUnpackPath ) );

    GetArgParserRegister().parseCmdLineArgs( (const char **)argv, argc );

    const bool bUnpack = *gpUnpackPath != '\0';
    if ( *gpListPath == '\0' || *gpOutputPath == '\0' ||
         ( !bUnpack && *gpInputPath == '\0' ) ) {
        PrintUsage();
    }

    std::vector<ListEntry> entries;
    if ( !ReadList( gpListPath, entries ) ) {
        return 1;
    }

    return bUnpack ? Unpack( entries ) : Pack( entries );
}
