#include "shared.h"
#include "logger.h"

#include "assert.h"
#include "time.h"

#include <cstdarg>
#include <iostream>

#if OTDU_MSVC
inline void OutputToDebugger( const char *pFormatedString ) {
    OutputDebugString( pFormatedString );
}
#else
inline void OutputToDebugger( const char *pFormatedString ) {
#ifndef OTDU_LOG_STDOUT // Redundant if we already redirect logs to stdout...
    printf( "%s", pFormatedString );
#endif
}
#endif

bool GlobalLogger::bLogTransientToFile = false;

static constexpr int32_t MaxLogSize = 8192;
static FILE *gpLogFile = nullptr;
static float gLogFileOpenTimeInSeconds = 0.0f;

static struct LogFileCloser {
    ~LogFileCloser() { GlobalLogger::CloseLogFile(); }
} gLogFileCloser;

static constexpr const char *GetPlatformName() {
#if defined( OTDU_MACOS )
    return "macOS";
#elif defined( OTDU_UNIX )
    return "Linux";
#elif defined( OTDU_WIN32 )
    return "Windows";
#else
    return "Unknown";
#endif
}

static void WriteToLogFile( const char *pFormatedString ) {
    if ( gpLogFile == nullptr ) {
        return;
    }

    fprintf( gpLogFile, "[%10.3f] %s",
             GetTimeSeconds() - gLogFileOpenTimeInSeconds, pFormatedString );
    fflush( gpLogFile );
}

bool GlobalLogger::OpenLogFile( const char *pFilePath ) {
    if ( gpLogFile != nullptr ) {
        return true;
    }

    std::error_code errorCode;
    std::filesystem::path logFilePath( pFilePath );
    if ( std::filesystem::exists( logFilePath, errorCode ) ) {
        std::filesystem::path backupFilePath( logFilePath );
        backupFilePath += ".old";

        std::filesystem::rename( logFilePath, backupFilePath, errorCode );
    }

    FILE *pLogFile = fopen( pFilePath, "w" );
    if ( pLogFile == nullptr ) {
        OTDU_LOG_ERROR( "Could not open log file '%s' for writing\n",
                        pFilePath );
        return false;
    }

    char pDate[64] = "unknown date";
    time_t rawTime = time( nullptr );
    struct tm *pLocalTime = localtime( &rawTime );
    if ( pLocalTime != nullptr ) {
        strftime( pDate, sizeof( pDate ), "%Y-%m-%d %H:%M:%S", pLocalTime );
    }

    fprintf( pLogFile,
             "OpenTDU " OTDU_COMMIT_HASH " (%s %u bits)\nSession started on "
             "%s\n\n",
             GetPlatformName(), OTDU_64_BITS ? 64u : 32u, pDate );

    gLogFileOpenTimeInSeconds = GetTimeSeconds();
    gpLogFile = pLogFile;

    return true;
}

void GlobalLogger::CloseLogFile() {
    if ( gpLogFile == nullptr ) {
        return;
    }

    FILE *pLogFile = gpLogFile;
    gpLogFile = nullptr;

    fclose( pLogFile );
}

static void
WriteImpl( const bool bWriteToDisk, const char *pFormat, va_list argList ) {
    thread_local char pBuffer[MaxLogSize];

    int32_t bufferOutputSize =
        vsnprintf( pBuffer, sizeof( pBuffer ), pFormat, argList );

    // Bad format/buffer overrun; early exit to avoid hard crash.
    if ( bufferOutputSize == -1 ) {
        OTDU_TRIGGER_BREAKPOINT;
        return;
    }

#ifdef OTDU_LOG_STDOUT
    printf( "%s", pBuffer );
#endif

    OutputToDebugger( pBuffer );

    if ( bWriteToDisk ) {
        WriteToLogFile( pBuffer );
    }

    OTDU_ASSERT( bufferOutputSize < static_cast<int32_t>( sizeof( pBuffer ) ) );
}

void GlobalLogger::Write( const char *pFormat, ... ) {
    va_list argList;
    va_start( argList, pFormat );

    WriteImpl( true, pFormat, argList );

    va_end( argList );
}

void GlobalLogger::WriteTransient( const char *pFormat, ... ) {
    va_list argList;
    va_start( argList, pFormat );

    WriteImpl( bLogTransientToFile, pFormat, argList );

    va_end( argList );
}
