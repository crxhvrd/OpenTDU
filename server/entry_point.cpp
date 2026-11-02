#include "shared.h"

#include "core/arg_parser.h"
#include "tdu_server.h"

#include <atomic>
#include <chrono>
#include <csignal>

bool gBreakOnAssertFailure = false;
bool gDisableAssert = false;

static ServerConfig gServerConfig;

static CmdLineArg CmdLineArgPort( "port", []( const char *pArg ) {
    gServerConfig.ProxyPort = uint16_t( atoi( pArg ) );
} );

static CmdLineArg CmdLineArgBind( "bind", []( const char *pArg ) {
    gServerConfig.BindAddress = pArg;
} );

static CmdLineArg CmdLineArgPublicAddress( "public_address",
                                           []( const char *pArg ) {
                                               gServerConfig.PublicAddress =
                                                   pArg;
                                           } );

static CmdLineArg CmdLineArgVerbose( "verbose", []( const char *pArg ) {
    gServerConfig.bVerbose = true;
} );

static CmdLineArg CmdLineArgDatabase( "db", []( const char *pArg ) {
    gServerConfig.DatabasePath = pArg;
} );

static CmdLineArg CmdLineArgNoAutoRegister( "no_auto_register",
                                            []( const char *pArg ) {
                                                gServerConfig.bAutoRegister =
                                                    false;
                                            } );

static CmdLineArg CmdLineArgNetConfig( "netconfig", []( const char *pArg ) {
    gServerConfig.NetConfigPath = pArg;
} );

static CmdLineArg CmdLineArgDebugRPCs( "debug_rpcs", []( const char *pArg ) {
    gServerConfig.bDebugRPCs = true;
} );

static CmdLineArg CmdLineArgLoss( "sim_loss", []( const char *pArg ) {
    gServerConfig.SimulatedLoss = float( atof( pArg ) );
} );

static CmdLineArg CmdLineArgDuplication( "sim_dup", []( const char *pArg ) {
    gServerConfig.SimulatedDuplication = float( atof( pArg ) );
} );

static std::atomic<bool> gbRunning( true );

static void OnSignal( int ) { gbRunning = false; }

int main( int argc, char *argv[] ) {
    GetArgParserRegister().parseCmdLineArgs( (const char **)argv, argc );

    std::signal( SIGINT, OnSignal );
    std::signal( SIGTERM, OnSignal );

    Server server( gServerConfig );
    if ( !server.initialize() ) {
        OTDU_LOG_ALWAYS( "Failed to start the server\n" );
        return 1;
    }

    OTDU_LOG_ALWAYS( "OpenTDU server listening on %s:%u\n",
                     gServerConfig.BindAddress.c_str(),
                     gServerConfig.ProxyPort );

    while ( gbRunning ) {
        if ( !server.tick() ) {
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
    }

    server.terminate();
    OTDU_LOG_ALWAYS( "Server stopped\n" );
    return 0;
}
