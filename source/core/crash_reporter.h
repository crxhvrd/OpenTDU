#pragma once

namespace CrashReporter {
#ifdef OTDU_CRASH_REPORTER
    void ProcessPendingReports();
    void Install( const char *pLogFilePath,
                  const char **pCmdLineArgs,
                  const int32_t argCount );
#else
    inline void ProcessPendingReports() {}
    inline void Install( const char *pLogFilePath,
                         const char **pCmdLineArgs,
                         const int32_t argCount ) {}
#endif
} // namespace CrashReporter
