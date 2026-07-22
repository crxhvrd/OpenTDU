// Down-level Win32 API thunks for the OTDU_TARGET_WIN7 build.
//
// Three Windows 8+ entry points are pulled into our binaries by code Microsoft compiled
// ahead of time, so *no* _WIN32_WINNT level on our own targets can remove them:
//
//   * CopyFile2 / CreateFile2         -- MSVC STL <filesystem>. CreateFile2 comes from
//                                        std::filesystem::exists/status/file_size (via
//                                        __std_fs_open_handle); CopyFile2 is dragged in
//                                        from the same precompiled translation unit even
//                                        though nothing here calls copy_file.
//   * GetSystemTimePreciseAsFileTime  -- MSVC <chrono> std::chrono::system_clock::now()
//                                        (header-inline in a dependency, e.g. Jolt).
//
// A static import of any of them makes the whole module fail to load on Windows 7 SP1. We
// redirect each import to a Win7-available equivalent by overriding its __imp_ IAT pointer
// at link time (/alternatename), so the real (missing) entry point is never bound.
//
// This TU is only added to the link when OTDU_TARGET_WIN7 is ON (see the top-level
// CMakeLists.txt), and is injected into BOTH the opentdu executable and the Jolt shared
// library, since each imports a different subset of the three.

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

// CopyFile2 (Win8) -> CopyFileExW (Win2000+).
static HRESULT WINAPI CopyFile2Downlevel( PCWSTR pwszExistingFileName, PCWSTR pwszNewFileName,
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
static HANDLE WINAPI CreateFile2Downlevel( PCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
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
static void WINAPI GetSystemTimePreciseAsFileTimeDownlevel( LPFILETIME lpSystemTimeAsFileTime ) {
    GetSystemTimeAsFileTime( lpSystemTimeAsFileTime );
}

// Replacement IAT slots. A dllimport call site reads __imp_<api> and calls through it; aliasing
// each __imp_ symbol (below) to one of these pointers routes the call to our down-level version
// and stops the real import from ever being resolved against kernel32. The initializers are
// link-time constants, so these are populated at load time -- no CRT-init ordering hazard.
extern "C" {
HRESULT( WINAPI *Win7_CopyFile2_imp )( PCWSTR, PCWSTR, Win7CopyFile2Params * ) = &CopyFile2Downlevel;
HANDLE( WINAPI *Win7_CreateFile2_imp )( PCWSTR, DWORD, DWORD, DWORD, Win7CreateFile2Params * ) =
    &CreateFile2Downlevel;
void( WINAPI *Win7_GetSystemTimePreciseAsFileTime_imp )( LPFILETIME ) =
    &GetSystemTimePreciseAsFileTimeDownlevel;
}

#if defined( _WIN64 )
#pragma comment( linker, "/alternatename:__imp_CopyFile2=Win7_CopyFile2_imp" )
#pragma comment( linker, "/alternatename:__imp_CreateFile2=Win7_CreateFile2_imp" )
#pragma comment( linker, "/alternatename:__imp_GetSystemTimePreciseAsFileTime=Win7_GetSystemTimePreciseAsFileTime_imp" )
#else
#pragma comment( linker, "/alternatename:__imp__CopyFile2@12=_Win7_CopyFile2_imp" )
#pragma comment( linker, "/alternatename:__imp__CreateFile2@20=_Win7_CreateFile2_imp" )
#pragma comment( linker, "/alternatename:__imp__GetSystemTimePreciseAsFileTime@4=_Win7_GetSystemTimePreciseAsFileTime_imp" )
#endif

#endif // _WIN32
