// Down-level Win32 API thunks for the OTDU_TARGET_WIN7 build.
//
// Three Windows 8+ entry points are pulled into our binaries by code Microsoft compiled
// ahead of time, so *no* _WIN32_WINNT level on our own targets can remove them:
//
//   * CreateFile2                     -- MSVC STL <filesystem>, from exists/status/file_size
//                                        (via __std_fs_open_handle).
//   * CopyFile2                       -- same precompiled translation unit, even though
//                                        nothing here calls copy_file. Only actually reaches
//                                        the image in Debug: a Release link discards the
//                                        unreferenced __std_fs_copy_file COMDAT, and the
//                                        import with it. Thunked anyway, since a Debug or
//                                        /OPT:NOREF build would otherwise regress.
//   * GetSystemTimePreciseAsFileTime  -- MSVC <chrono> std::chrono::system_clock::now(),
//                                        header-inline in Jolt (x86 Jolt.dll only).
//
// A static import of any of them makes the whole module fail to load on Windows 7 SP1. We
// redirect each one to a Win7-available equivalent by defining its __imp_ IAT symbol
// ourselves, in win7_thunks_x86.asm / win7_thunks_x64.asm: because the symbol is then
// already defined, the linker never pulls the matching kernel32.lib member and no import
// descriptor is emitted for the Win8 entry point. The implementations below are what those
// IAT slots point at, so they must keep C linkage (the assembly names them directly).
//
// This TU is only added to the link when OTDU_TARGET_WIN7 is ON (see the top-level
// CMakeLists.txt), and is injected into every Windows binary we ship -- the executable, the
// three tools and the Jolt shared library -- since each imports a different subset.

#if defined( _WIN32 )

#include <windows.h>

// The Win8 extended-parameter structs are not declared at low _WIN32_WINNT levels; redeclare
// the (ABI-stable) fields we consume under private names so this TU builds at any SDK level.
struct Win7CopyFile2Params {
    DWORD dwSize;
    DWORD dwCopyFlags;
    BOOL *pfCancel;
    void *pProgressRoutine;
    void *pvCallbackContext;
};

struct Win7CreateFile2Params {
    DWORD                 dwSize;
    DWORD                 dwFileAttributes;
    DWORD                 dwFileFlags;
    DWORD                 dwSecurityQosFlags;
    LPSECURITY_ATTRIBUTES lpSecurityAttributes;
    HANDLE                hTemplateFile;
};

extern "C" {

// CopyFile2 (Win8) -> CopyFileExW (Win2000+).
HRESULT WINAPI CopyFile2Downlevel( PCWSTR pwszExistingFileName, PCWSTR pwszNewFileName,
                                   Win7CopyFile2Params *pExtendedParameters ) {
    DWORD dwFlags  = 0;
    BOOL *pfCancel = nullptr;
    if ( pExtendedParameters != nullptr ) {
        dwFlags  = pExtendedParameters->dwCopyFlags;
        pfCancel = pExtendedParameters->pfCancel;
    }
    // The STL never supplies a progress routine, so only flags + cancel flag are forwarded.
    const BOOL bSucceeded =
        CopyFileExW( pwszExistingFileName, pwszNewFileName, nullptr, nullptr, pfCancel, dwFlags );
    return bSucceeded ? S_OK : HRESULT_FROM_WIN32( GetLastError() );
}

// CreateFile2 (Win8) -> CreateFileW (Win2000+).
HANDLE WINAPI CreateFile2Downlevel( PCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                    DWORD dwCreationDisposition, Win7CreateFile2Params *pCreateExParams ) {
    DWORD                 dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL;
    LPSECURITY_ATTRIBUTES lpSecurityAttributes = nullptr;
    HANDLE                hTemplateFile        = nullptr;
    if ( pCreateExParams != nullptr ) {
        // CreateFileW folds attributes, flags and SQOS bits into a single dwFlagsAndAttributes.
        dwFlagsAndAttributes = pCreateExParams->dwFileAttributes | pCreateExParams->dwFileFlags |
                               pCreateExParams->dwSecurityQosFlags;
        lpSecurityAttributes = pCreateExParams->lpSecurityAttributes;
        hTemplateFile        = pCreateExParams->hTemplateFile;
    }
    return CreateFileW( lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                        dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile );
}

// GetSystemTimePreciseAsFileTime (Win8) -> GetSystemTimeAsFileTime (Win2000+, ~10-15ms resolution).
void WINAPI GetSystemTimePreciseAsFileTimeDownlevel( LPFILETIME lpSystemTimeAsFileTime ) {
    GetSystemTimeAsFileTime( lpSystemTimeAsFileTime );
}

} // extern "C"

#endif // _WIN32
