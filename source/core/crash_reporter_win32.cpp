#include "shared.h"
#include "crash_reporter.h"

#if defined( OTDU_CRASH_REPORTER ) && defined( OTDU_WIN32 )
#include "crash_reporter_internal.h"

#include <csignal>
#include <dbghelp.h>
#include <exception>

namespace CrashReporter {
    namespace {
        // The report is written by a watchdog thread created at startup: the
        // crashing thread might be out of stack (stack overflow) or hold locks,
        // so it only hands its exception pointers over and waits.
        constexpr DWORD kCrashReporterException = 0xE0544455; // 'TDU'
        constexpr DWORD kWatchdogTimeoutMs = 120000;
        constexpr int32_t kMaxBacktraceDepth = 128;

        HANDLE gRequestEvent = nullptr;
        HANDLE gDoneEvent = nullptr;
        volatile LONG gbHandlingCrash = 0;
        EXCEPTION_POINTERS *gpExceptionPointers = nullptr;
        DWORD gCrashingThreadId = 0;
        const char *gpCrashReason = nullptr;
        char gUncaughtExceptionMessage[512];

        void WriteMinidump( const wchar_t *pDumpPath ) {
            HANDLE dumpFile =
                CreateFileW( pDumpPath, GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
            if ( dumpFile == INVALID_HANDLE_VALUE ) {
                return;
            }

            MINIDUMP_EXCEPTION_INFORMATION exceptionInfo = {};
            exceptionInfo.ThreadId = gCrashingThreadId;
            exceptionInfo.ExceptionPointers = gpExceptionPointers;
            exceptionInfo.ClientPointers = FALSE;

            const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
                MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory |
                MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules );

            MiniDumpWriteDump( GetCurrentProcess(), GetCurrentProcessId(),
                               dumpFile, dumpType, &exceptionInfo, nullptr,
                               nullptr );
            CloseHandle( dumpFile );
        }

        void WriteModuleOffset( FILE *pReport, const DWORD64 address ) {
            HANDLE process = GetCurrentProcess();
            const DWORD64 moduleBase = SymGetModuleBase64( process, address );

            char modulePath[MAX_PATH] = "???";
            if ( moduleBase != 0 ) {
                GetModuleFileNameA( reinterpret_cast<HMODULE>(
                                        static_cast<uintptr_t>( moduleBase ) ),
                                    modulePath, MAX_PATH );
            }
            const char *pModuleName = strrchr( modulePath, '\\' );
            pModuleName =
                ( pModuleName != nullptr ) ? pModuleName + 1 : modulePath;

            fprintf( pReport, "%s+0x%llx", pModuleName,
                     static_cast<unsigned long long>( address - moduleBase ) );
        }

        void WriteBacktrace( FILE *pReport ) {
            HANDLE process = GetCurrentProcess();
            HANDLE thread =
                OpenThread( THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION |
                                THREAD_SUSPEND_RESUME,
                            FALSE, gCrashingThreadId );
            if ( thread == nullptr ) {
                fprintf( pReport, "Could not open the crashing thread\n" );
                return;
            }

            CONTEXT context = *gpExceptionPointers->ContextRecord;
            STACKFRAME64 frame = {};
            frame.AddrPC.Mode = AddrModeFlat;
            frame.AddrFrame.Mode = AddrModeFlat;
            frame.AddrStack.Mode = AddrModeFlat;
#if defined( _M_X64 )
            const DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
            frame.AddrPC.Offset = context.Rip;
            frame.AddrFrame.Offset = context.Rbp;
            frame.AddrStack.Offset = context.Rsp;
#elif defined( _M_IX86 )
            const DWORD machineType = IMAGE_FILE_MACHINE_I386;
            frame.AddrPC.Offset = context.Eip;
            frame.AddrFrame.Offset = context.Ebp;
            frame.AddrStack.Offset = context.Esp;
#elif defined( _M_ARM64 )
            const DWORD machineType = IMAGE_FILE_MACHINE_ARM64;
            frame.AddrPC.Offset = context.Pc;
            frame.AddrFrame.Offset = context.Fp;
            frame.AddrStack.Offset = context.Sp;
#else
#error "Unsupported architecture"
#endif

            alignas(
                SYMBOL_INFO ) char symbolBuffer[sizeof( SYMBOL_INFO ) + 256];
            SYMBOL_INFO *pSymbol =
                reinterpret_cast<SYMBOL_INFO *>( symbolBuffer );

            for ( int32_t frameIdx = 0; frameIdx < kMaxBacktraceDepth;
                  frameIdx++ ) {
                if ( !StackWalk64( machineType, process, thread, &frame,
                                   &context, nullptr, SymFunctionTableAccess64,
                                   SymGetModuleBase64, nullptr ) ||
                     frame.AddrPC.Offset == 0 ) {
                    break;
                }

                const DWORD64 address = frame.AddrPC.Offset;
                fprintf( pReport, "#%02d ", frameIdx );
                WriteModuleOffset( pReport, address );

                memset( symbolBuffer, 0, sizeof( symbolBuffer ) );
                pSymbol->SizeOfStruct = sizeof( SYMBOL_INFO );
                pSymbol->MaxNameLen = 255;
                DWORD64 symbolDisplacement = 0;
                if ( SymFromAddr( process, address, &symbolDisplacement,
                                  pSymbol ) ) {
                    fprintf(
                        pReport, "  %s+0x%llx", pSymbol->Name,
                        static_cast<unsigned long long>( symbolDisplacement ) );
                }

                IMAGEHLP_LINE64 line = {};
                line.SizeOfStruct = sizeof( IMAGEHLP_LINE64 );
                DWORD lineDisplacement = 0;
                if ( SymGetLineFromAddr64( process, address, &lineDisplacement,
                                           &line ) ) {
                    fprintf( pReport, "  (%s:%lu)", line.FileName,
                             line.LineNumber );
                }
                fprintf( pReport, "\n" );
            }

            CloseHandle( thread );
        }

        void WriteReport( const wchar_t *pReportPath ) {
            FILE *pReport = _wfopen( pReportPath, L"w" );
            if ( pReport == nullptr ) {
                return;
            }

            fprintf( pReport,
                     "OpenTDU " OTDU_COMMIT_HASH " (Windows %u bits)\n",
                     OTDU_64_BITS ? 64u : 32u );
            fprintf( pReport, "Command line: %s\n\n",
                     gContext.CommandLine.c_str() );

            const EXCEPTION_RECORD *pRecord =
                gpExceptionPointers->ExceptionRecord;
            if ( pRecord->ExceptionCode == kCrashReporterException ) {
                fprintf( pReport, "Reason: %s\n",
                         gpCrashReason != nullptr ? gpCrashReason : "unknown" );
            } else {
                fprintf( pReport, "Exception: 0x%08lx\n",
                         pRecord->ExceptionCode );
                if ( pRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
                     pRecord->NumberParameters >= 2 ) {
                    const ULONG_PTR accessType =
                        pRecord->ExceptionInformation[0];
                    fprintf( pReport, "Access violation: %s 0x%llx\n",
                             accessType == 0   ? "reading"
                             : accessType == 8 ? "executing"
                                               : "writing",
                             static_cast<unsigned long long>(
                                 pRecord->ExceptionInformation[1] ) );
                }
            }

            if ( gUncaughtExceptionMessage[0] != '\0' ) {
                fprintf( pReport, "Uncaught exception: %s\n",
                         gUncaughtExceptionMessage );
            }

            fprintf( pReport, "Address: " );
            WriteModuleOffset( pReport, reinterpret_cast<DWORD64>(
                                            pRecord->ExceptionAddress ) );
            fprintf( pReport, "\n\nBacktrace:\n" );
            WriteBacktrace( pReport );
            fclose( pReport );
        }

        void HandleCrashRequest() {
            // Symbols are looked up next to the executable (users can drop the
            // PDB from the release symbols archive there).
            wchar_t executablePath[MAX_PATH] = {};
            GetModuleFileNameW( nullptr, executablePath, MAX_PATH );
            wchar_t *pLastSeparator = wcsrchr( executablePath, L'\\' );
            if ( pLastSeparator != nullptr ) {
                *pLastSeparator = L'\0';
            }

            char symbolSearchPath[MAX_PATH * 4] = {};
            WideCharToMultiByte( CP_ACP, 0, executablePath, -1,
                                 symbolSearchPath,
                                 static_cast<int>( sizeof( symbolSearchPath ) ),
                                 nullptr, nullptr );

            SymSetOptions( SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS |
                           SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS |
                           SYMOPT_NO_PROMPTS );
            SymInitialize( GetCurrentProcess(), symbolSearchPath, TRUE );

            SYSTEMTIME localTime = {};
            GetLocalTime( &localTime );

            const wchar_t *pCrashFolder = gContext.CrashFolder.c_str();
            CreateDirectoryW( pCrashFolder, nullptr );

            wchar_t reportFolder[MAX_PATH * 2] = {};
            swprintf( reportFolder, MAX_PATH * 2,
                      L"%ls\\crash_%04u%02u%02u_%02u%02u%02u", pCrashFolder,
                      localTime.wYear, localTime.wMonth, localTime.wDay,
                      localTime.wHour, localTime.wMinute, localTime.wSecond );
            CreateDirectoryW( reportFolder, nullptr );

            // Dump first: it is the most valuable artifact and the least likely
            // to be affected by a corrupted heap.
            wchar_t filePath[MAX_PATH * 2] = {};
            swprintf( filePath, MAX_PATH * 2, L"%ls\\opentdu.dmp",
                      reportFolder );
            WriteMinidump( filePath );

            swprintf( filePath, MAX_PATH * 2, L"%ls\\report.txt",
                      reportFolder );
            WriteReport( filePath );

            if ( !gContext.LogFilePath.empty() ) {
                swprintf( filePath, MAX_PATH * 2, L"%ls\\opentdu.log",
                          reportFolder );
                CopyFileW( gContext.LogFilePath.c_str(), filePath, FALSE );
            }

            SymCleanup( GetCurrentProcess() );
        }

        DWORD WINAPI WatchdogThread( LPVOID ) {
            WaitForSingleObject( gRequestEvent, INFINITE );
            HandleCrashRequest();
            SetEvent( gDoneEvent );
            return 0;
        }

        LONG WINAPI
        OnUnhandledException( EXCEPTION_POINTERS *pExceptionPointers ) {
            if ( InterlockedCompareExchange( &gbHandlingCrash, 1, 0 ) != 0 ) {
                // Another thread is already reporting; wait for it to terminate
                // us.
                Sleep( kWatchdogTimeoutMs );
                return EXCEPTION_EXECUTE_HANDLER;
            }

            gpExceptionPointers = pExceptionPointers;
            gCrashingThreadId = GetCurrentThreadId();
            SetEvent( gRequestEvent );
            WaitForSingleObject( gDoneEvent, kWatchdogTimeoutMs );

            TerminateProcess(
                GetCurrentProcess(),
                pExceptionPointers->ExceptionRecord->ExceptionCode );
            return EXCEPTION_EXECUTE_HANDLER;
        }

        // Used for failures that do not raise an SEH exception (abort, pure
        // virtual calls, ...): raises one so the regular path gets a context to
        // walk.
        void ReportAndTerminate( const char *pReason ) {
            gpCrashReason = pReason;
            __try {
                RaiseException( kCrashReporterException,
                                EXCEPTION_NONCONTINUABLE, 0, nullptr );
            } __except ( OnUnhandledException( GetExceptionInformation() ) ) {
            }
            TerminateProcess( GetCurrentProcess(), 3 );
        }

        void OnAbortSignal( int ) { ReportAndTerminate( "abort()" ); }

        void OnPureVirtualCall() {
            ReportAndTerminate( "pure virtual function call" );
        }

        void OnInvalidParameter( const wchar_t *,
                                 const wchar_t *,
                                 const wchar_t *,
                                 unsigned int,
                                 uintptr_t ) {
            ReportAndTerminate( "invalid CRT parameter" );
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
            ReportAndTerminate( "std::terminate()" );
        }
    } // namespace

    void InstallPlatformHandlers() {
        gRequestEvent = CreateEventW( nullptr, FALSE, FALSE, nullptr );
        gDoneEvent = CreateEventW( nullptr, FALSE, FALSE, nullptr );
        if ( gRequestEvent == nullptr || gDoneEvent == nullptr ) {
            return;
        }

        HANDLE watchdog =
            CreateThread( nullptr, 0, WatchdogThread, nullptr, 0, nullptr );
        if ( watchdog == nullptr ) {
            return;
        }
        CloseHandle( watchdog );

        // Leaves room for the filter to run after a stack overflow on the main
        // thread.
        ULONG stackGuarantee = 64 * 1024;
        SetThreadStackGuarantee( &stackGuarantee );

        SetUnhandledExceptionFilter( OnUnhandledException );
        _set_abort_behavior( 0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT );
        signal( SIGABRT, OnAbortSignal );
        _set_purecall_handler( OnPureVirtualCall );
        _set_invalid_parameter_handler( OnInvalidParameter );
        std::set_terminate( OnTerminate );
    }
} // namespace CrashReporter
#endif
