#include "asset_extractor.h"

#include "shared.h"

#include "core/arg_parser.h"
#include "core/assert.h"
#include "core/logger.h"

#include "system/system_shared.h"

#include <cstring>
#include <filesystem>

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

static char gpPathToExecutable[OTDU_MAX_PATH]; // Path to TestDriveUnlimited.exe
                                               // (default: working dir)
static CmdLineArg
    CmdLineArgsExecutablePath( "executable", []( const char *pArg ) {
        strcpy( gpPathToExecutable, pArg );
    } );

static char
    gpOutputPath[OTDU_MAX_PATH]; // (Base) Output path for asset extraction
static CmdLineArg CmdLineArgsOutputPath( "asset_output_path",
                                         []( const char *pArg ) {
                                             strcpy( gpOutputPath, pArg );
                                         } );

static bool gbExtractShaders =
    false; // If true, extract the retail shader tables (default: false)
static CmdLineArg CmdLineArgsExtractShaders( "extract_shaders",
                                             []( const char *pArg ) {
                                                 gbExtractShaders = true;
                                             } );

static bool gbForceExtraction =
    false; // If true, ignore previously extracted assets
static CmdLineArg CmdLineArgsForce( "force", []( const char *pArg ) {
    gbForceExtraction = true;
} );

[[noreturn]] static void PrintUsage() {
    OTDU_LOG_ALWAYS(
        "USAGE: opentdu_assetExtractor [-executable <pathToExe> "
        "-asset_output_path <pathToOutput> -extract_shaders -force]\n"
        "\n"
        "-executable:            Path to your TestDriveUnlimited.exe "
        "executable (optional) (default: executable in current working "
        "directory)\n"
        "-asset_output_path:     Path used to write extracted assets "
        "(optional) (default: current working directory)\n"
        "-extract_shaders:       Extract the retail shader tables (shader "
        "lifter tools only) (optional) (default: false)\n"
        "-force:                 Force asset extraction and ignore previously "
        "extracted assets (optional) (default: false)\n" );
    exit( 1 );
}

static void SanitizePath( char *pPath ) {
    char *pIt = pPath;
    while ( pIt && *pIt != '\0' ) {
        if ( *pIt == '\\' ) {
            *pIt = '/';
        }
        pIt++;
    }
}

int main( int argc, char *argv[] ) {
    // Parse cmdline args
    memset( gpPathToExecutable, 0, sizeof( char ) * OTDU_MAX_PATH );
    memset( gpOutputPath, 0, sizeof( char ) * OTDU_MAX_PATH );

    GetArgParserRegister().parseCmdLineArgs( (const char **)argv, argc );

    if ( *gpPathToExecutable == '\0' ) {
        std::filesystem::path folderFS =
            std::filesystem::path( "TestDriveUnlimited.exe" );
        if ( std::filesystem::exists( folderFS ) ) {
            strcpy( gpPathToExecutable, "TestDriveUnlimited.exe" );
            OTDU_LOG_ALWAYS( "No executable path provided; will use executable "
                             "in the current working directory\n" );
        } else {
            OTDU_LOG_ALWAYS( "Missing cmdline arg 'executable'!\n" );
            PrintUsage();
        }
    }

    if ( *gpOutputPath == '\0' ) {
        TestDrive::GetWorkingDir( gpOutputPath, OTDU_MAX_PATH );
        OTDU_LOG_ALWAYS(
            "No output path provided ('asset_output_path' cmdline arg not "
            "found); will use current working directory\n" );
    }

    SanitizePath( gpPathToExecutable );
    SanitizePath( gpOutputPath );

    AssetExtractor::ExtractionParams params;
    params.pExecutablePath = gpPathToExecutable;
    params.pOutputPath = gpOutputPath;
    params.bForceExtraction = gbForceExtraction;
    params.bExtractShaders = gbExtractShaders;

    AssetExtractor::ExtractionResult result = AssetExtractor::Run( params );
    const bool bMoviesBuilt =
        AssetExtractor::BuildGeneratedMovies( gpOutputPath );
    const bool bFarImpostorsBuilt =
        AssetExtractor::BuildFarImpostors( gpOutputPath );
    return ( result.bSuccess && bMoviesBuilt && bFarImpostorsBuilt ) ? 0 : 1;
}
