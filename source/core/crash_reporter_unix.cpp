#include "shared.h"
#include "crash_reporter.h"

#if defined( OTDU_CRASH_REPORTER ) && defined( OTDU_UNIX )
#include "crash_reporter_internal.h"

#include <atomic>
#include <csignal>
#include <dlfcn.h>
#include <exception>
#include <execinfo.h>
#include <sys/stat.h>

#if defined( OTDU_MACOS )
#include <mach-o/dyld.h>
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif

namespace CrashReporter {
    namespace {
        // Everything below runs inside a signal handler: only async-signal-safe
        // calls (open/write/read/mkdir/backtrace_symbols_fd) and no heap
        // allocation.
        constexpr int32_t kMaxBacktraceDepth = 128;
        constexpr size_t kAltStackSize = 128 * 1024;
        constexpr int kHandledSignals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE,
                                            SIGABRT };

        alignas( 16 ) uint8_t gAltStack[kAltStackSize];
        std::atomic<bool> gbHandlingCrash( false );
        pthread_t gReportingThread = {};
        char gUncaughtExceptionMessage[512];
        uintptr_t gImageLoadAddress = 0;
        std::terminate_handler gpPreviousTerminateHandler = nullptr;

        class FdWriter {
        public:
            explicit FdWriter( const int fd ) : fd( fd ) {}

            FdWriter &str( const char *pString ) {
                if ( pString != nullptr ) {
                    const ssize_t ignored =
                        write( fd, pString, strlen( pString ) );
                    (void)ignored;
                }
                return *this;
            }

            FdWriter &dec( uint64_t value ) {
                char buffer[24];
                char *pIt = buffer + sizeof( buffer );
                *--pIt = '\0';
                do {
                    *--pIt = static_cast<char>( '0' + ( value % 10 ) );
                    value /= 10;
                } while ( value != 0 );
                return str( pIt );
            }

            FdWriter &hex( uint64_t value ) {
                static constexpr const char *kHexDigits = "0123456789abcdef";
                char buffer[19];
                char *pIt = buffer + sizeof( buffer );
                *--pIt = '\0';
                do {
                    *--pIt = kHexDigits[value & 0xf];
                    value >>= 4;
                } while ( value != 0 );
                *--pIt = 'x';
                *--pIt = '0';
                return str( pIt );
            }

        private:
            int fd;
        };

        class PathBuilder {
        public:
            PathBuilder &append( const char *pString ) {
                while ( *pString != '\0' && length < sizeof( buffer ) - 1 ) {
                    buffer[length++] = *pString++;
                }
                buffer[length] = '\0';
                return *this;
            }

            PathBuilder &appendPadded( const int value,
                                       const int32_t numDigits ) {
                char digits[8];
                int remaining = value;
                for ( int32_t i = numDigits - 1; i >= 0; i-- ) {
                    digits[i] = static_cast<char>( '0' + ( remaining % 10 ) );
                    remaining /= 10;
                }
                digits[numDigits] = '\0';
                return append( digits );
            }

            const char *c_str() const { return buffer; }

        private:
            char buffer[OTDU_MAX_PATH] = {};
            size_t length = 0;
        };

        const char *GetSignalName( const int signalNumber ) {
            switch ( signalNumber ) {
            case SIGSEGV:
                return "SIGSEGV";
            case SIGBUS:
                return "SIGBUS";
            case SIGILL:
                return "SIGILL";
            case SIGFPE:
                return "SIGFPE";
            case SIGABRT:
                return "SIGABRT";
            default:
                return "unknown signal";
            }
        }

        uintptr_t GetFaultingPC( const void *pUserContext ) {
            if ( pUserContext == nullptr ) {
                return 0;
            }
            const ucontext_t *pContext =
                static_cast<const ucontext_t *>( pUserContext );
#if defined( OTDU_MACOS ) && defined( __aarch64__ )
            return static_cast<uintptr_t>( pContext->uc_mcontext->__ss.__pc );
#elif defined( OTDU_MACOS ) && defined( __x86_64__ )
            return static_cast<uintptr_t>( pContext->uc_mcontext->__ss.__rip );
#elif defined( __x86_64__ )
            return static_cast<uintptr_t>(
                pContext->uc_mcontext.gregs[REG_RIP] );
#elif defined( __aarch64__ )
            return static_cast<uintptr_t>( pContext->uc_mcontext.pc );
#else
            return 0;
#endif
        }

        void CopyFile( const char *pSourcePath, const char *pDestinationPath ) {
            const int sourceFd = open( pSourcePath, O_RDONLY );
            if ( sourceFd < 0 ) {
                return;
            }

            const int destinationFd =
                open( pDestinationPath, O_WRONLY | O_CREAT | O_TRUNC, 0644 );
            if ( destinationFd >= 0 ) {
                char buffer[4096];
                ssize_t numRead = 0;
                while ( ( numRead = read( sourceFd, buffer,
                                          sizeof( buffer ) ) ) > 0 ) {
                    if ( write( destinationFd, buffer,
                                static_cast<size_t>( numRead ) ) != numRead ) {
                        break;
                    }
                }
                close( destinationFd );
            }
            close( sourceFd );
        }

        void WriteReport( const int signalNumber,
                          const siginfo_t *pSignalInfo,
                          const void *pUserContext ) {
            const char *pCrashFolder = gContext.CrashFolder.c_str();
            mkdir( pCrashFolder, 0755 );

            PathBuilder reportFolder;
            reportFolder.append( pCrashFolder ).append( "/crash_" );

            time_t rawTime = time( nullptr );
            struct tm localTime = {};
            if ( localtime_r( &rawTime, &localTime ) != nullptr ) {
                reportFolder.appendPadded( localTime.tm_year + 1900, 4 )
                    .appendPadded( localTime.tm_mon + 1, 2 )
                    .appendPadded( localTime.tm_mday, 2 )
                    .append( "_" )
                    .appendPadded( localTime.tm_hour, 2 )
                    .appendPadded( localTime.tm_min, 2 )
                    .appendPadded( localTime.tm_sec, 2 );
            }
            mkdir( reportFolder.c_str(), 0755 );

            PathBuilder reportPath;
            reportPath.append( reportFolder.c_str() ).append( "/report.txt" );

            const int fd =
                open( reportPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644 );
            if ( fd >= 0 ) {
                FdWriter report( fd );
                report
                    .str( "OpenTDU " OTDU_COMMIT_HASH )
#if defined( OTDU_MACOS )
                    .str( " (macOS" )
#else
                    .str( " (Linux" )
#endif
                    .str( OTDU_64_BITS ? " 64 bits)\n" : " 32 bits)\n" );
                report.str( "Command line: " )
                    .str( gContext.CommandLine.c_str() )
                    .str( "\n\n" );

                report.str( "Signal: " )
                    .str( GetSignalName( signalNumber ) )
                    .str( " (" )
                    .dec( static_cast<uint64_t>( signalNumber ) )
                    .str( ")\n" );
                if ( pSignalInfo != nullptr ) {
                    report.str( "Code: " )
                        .dec( static_cast<uint64_t>( pSignalInfo->si_code ) )
                        .str( "\nFault address: " )
                        .hex( reinterpret_cast<uintptr_t>(
                            pSignalInfo->si_addr ) )
                        .str( "\n" );
                }
                report.str( "PC: " )
                    .hex( GetFaultingPC( pUserContext ) )
                    .str( "\n" );
                report.str( "Image load address: " )
                    .hex( gImageLoadAddress )
                    .str( "\n" );

                if ( gUncaughtExceptionMessage[0] != '\0' ) {
                    report.str( "Uncaught exception: " )
                        .str( gUncaughtExceptionMessage )
                        .str( "\n" );
                }

                report.str( "\nBacktrace:\n" );
                void *pFrames[kMaxBacktraceDepth];
                const int numFrames = backtrace( pFrames, kMaxBacktraceDepth );
                backtrace_symbols_fd( pFrames, numFrames, fd );
                close( fd );
            }

            if ( !gContext.LogFilePath.empty() ) {
                PathBuilder logCopyPath;
                logCopyPath.append( reportFolder.c_str() )
                    .append( "/opentdu.log" );
                CopyFile( gContext.LogFilePath.c_str(), logCopyPath.c_str() );
            }
        }

        void OnFatalSignal( int signalNumber,
                            siginfo_t *pSignalInfo,
                            void *pContext ) {
            if ( !gbHandlingCrash.exchange( true ) ) {
                gReportingThread = pthread_self();
                WriteReport( signalNumber, pSignalInfo, pContext );
            } else if ( !pthread_equal( gReportingThread, pthread_self() ) ) {
                // Another thread is writing the report; give it a chance to
                // finish.
                sleep( 10 );
            }

            // Let the default action run (core dump/OS crash reporter).
            signal( signalNumber, SIG_DFL );
            raise( signalNumber );
        }

        void OnTerminate() {
            if ( std::exception_ptr pException = std::current_exception() ) {
                try {
                    std::rethrow_exception( pException );
                } catch ( const std::exception &exception ) {
                    strncpy( gUncaughtExceptionMessage, exception.what(),
                             sizeof( gUncaughtExceptionMessage ) - 1 );
                } catch ( ... ) {
                    strncpy( gUncaughtExceptionMessage,
                             "unknown exception type",
                             sizeof( gUncaughtExceptionMessage ) - 1 );
                }
            }

            if ( gpPreviousTerminateHandler != nullptr ) {
                gpPreviousTerminateHandler();
            }
            abort();
        }

        uintptr_t RetrieveImageLoadAddress() {
#if defined( OTDU_MACOS )
            return reinterpret_cast<uintptr_t>( _dyld_get_image_header( 0 ) );
#else
            Dl_info info = {};
            if ( dladdr( reinterpret_cast<void *>( &RetrieveImageLoadAddress ),
                         &info ) != 0 ) {
                return reinterpret_cast<uintptr_t>( info.dli_fbase );
            }
            return 0;
#endif
        }
    } // namespace

    void InstallPlatformHandlers() {
        gImageLoadAddress = RetrieveImageLoadAddress();

        // backtrace() may lazily load libgcc on first use, which is not safe
        // from a signal handler.
        void *pWarmupFrame = nullptr;
        backtrace( &pWarmupFrame, 1 );

        // Only covers stack overflows on the main thread (alternate stacks are
        // per thread); other threads still get the OS crash report.
        stack_t altStack = {};
        altStack.ss_sp = gAltStack;
        altStack.ss_size = kAltStackSize;
        sigaltstack( &altStack, nullptr );

        struct sigaction action = {};
        action.sa_sigaction = OnFatalSignal;
        action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset( &action.sa_mask );
        for ( const int signalNumber : kHandledSignals ) {
            sigaction( signalNumber, &action, nullptr );
        }

        gpPreviousTerminateHandler = std::set_terminate( OnTerminate );
    }
} // namespace CrashReporter
#endif
