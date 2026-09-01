#pragma once

class GlobalLogger {
public:
    static bool bLogTransientToFile;
    static bool bVerbose;

    static bool OpenLogFile( const char *pFilePath );
    static void CloseLogFile();
    static void Write( const char *pFormat, ... );
    static void WriteTransient( const char *pFormat, ... );
    static void WriteVerbose( const char *pFormat, ... );
};

#if OTDU_MSVC
#define OTDU_STRINGIFY_EXPAND( x ) #x
#define OTDU_STRINGIFY( x ) OTDU_STRINGIFY_EXPAND( x )
#else
#define OTDU_STRINGIFY( x )
#endif

#ifdef OTDU_DEVBUILD
#ifndef OTDU_FILENAME
#define OTDU_FILENAME __FILE__
#endif

#define OTDU_LOG_INFO( format, ... )                                           \
    GlobalLogger::Write( "[LOG]   %s:%u >> " format, OTDU_FILENAME, __LINE__,  \
                         ##__VA_ARGS__ )
#define OTDU_LOG_WARN( format, ... )                                           \
    GlobalLogger::Write( "[WARN]  %s:%u >> " format, OTDU_FILENAME, __LINE__,  \
                         ##__VA_ARGS__ )
#define OTDU_LOG_ERROR( format, ... )                                          \
    GlobalLogger::Write( "[ERROR] %s:%u >> " format, OTDU_FILENAME, __LINE__,  \
                         ##__VA_ARGS__ )
#define OTDU_LOG_ERROR_TRANSIENT( format, ... )                                \
    GlobalLogger::WriteTransient( "[ERROR] %s:%u >> " format, OTDU_FILENAME,   \
                                  __LINE__, ##__VA_ARGS__ )
#define OTDU_LOG_DEBUG( format, ... )                                          \
    GlobalLogger::WriteVerbose( "[DEBUG] %s:%u >> " format, OTDU_FILENAME, __LINE__,  \
                         ##__VA_ARGS__ )
#else
#define OTDU_LOG_INFO( format, ... )
#define OTDU_LOG_WARN( format, ... )
#define OTDU_LOG_ERROR( format, ... )
#define OTDU_LOG_ERROR_TRANSIENT( format, ... )
#define OTDU_LOG_DEBUG( format, ... )
#endif

#define OTDU_LOG_ALWAYS( format, ... )                                         \
    GlobalLogger::Write( format, ##__VA_ARGS__ )
