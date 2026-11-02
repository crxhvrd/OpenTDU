#pragma once

#ifdef OTDU_CRASH_REPORTER
namespace CrashReporter {
    struct Context {
        std::filesystem::path CrashFolder;
        std::filesystem::path
            LogFilePath; // Empty if logging to disk is disabled
        std::string CommandLine;
    };

    extern Context gContext;

    // Platform specific; the handlers must only read gContext (it is fully
    // initialized before this call and never modified afterwards).
    void InstallPlatformHandlers();
} // namespace CrashReporter
#endif
