; Down-level Win32 import overrides for the OTDU_TARGET_WIN7 build (x64).
;
; The MSVC STL / CRT reference a handful of Windows 8+ entry points through their
; import-address-table symbol (__imp_<api>, emitted for any __declspec(dllimport)
; call). Defining that symbol *here*, in a regular object file, means the matching
; kernel32.lib member is never pulled into the link -- so no import descriptor is
; emitted for it -- and every dllimport call site ends up calling our down-level
; implementation in win7_thunks.cpp instead.
;
; This has to be assembly rather than C++ because the x86 flavour of these symbols
; carries stdcall decoration (__imp__CreateFile2@20), which is not a legal C
; identifier; the two flavours are kept symmetrical for readability.
;
; NB. /alternatename does NOT work here: it emits a weak external, which the linker
; only falls back to when the symbol is otherwise undefined -- and kernel32.lib
; defines it strongly, so the real Win8 import always won.

EXTERN CopyFile2Downlevel : PROC
EXTERN CreateFile2Downlevel : PROC
EXTERN GetSystemTimePreciseAsFileTimeDownlevel : PROC

.DATA

PUBLIC __imp_CopyFile2
PUBLIC __imp_CreateFile2
PUBLIC __imp_GetSystemTimePreciseAsFileTime

__imp_CopyFile2                         DQ CopyFile2Downlevel
__imp_CreateFile2                       DQ CreateFile2Downlevel
__imp_GetSystemTimePreciseAsFileTime    DQ GetSystemTimePreciseAsFileTimeDownlevel

END
