; Down-level Win32 import overrides for the OTDU_TARGET_WIN7 build (x86).
; See win7_thunks_x64.asm for the rationale; the only difference here is the stdcall
; decoration on both the imported name and our own implementations (@<argument bytes>).

.386
.MODEL FLAT

EXTERN _CopyFile2Downlevel@12 : PROC
EXTERN _CreateFile2Downlevel@20 : PROC
EXTERN _GetSystemTimePreciseAsFileTimeDownlevel@4 : PROC

.DATA

PUBLIC __imp__CopyFile2@12
PUBLIC __imp__CreateFile2@20
PUBLIC __imp__GetSystemTimePreciseAsFileTime@4

__imp__CopyFile2@12                         DD _CopyFile2Downlevel@12
__imp__CreateFile2@20                       DD _CreateFile2Downlevel@20
__imp__GetSystemTimePreciseAsFileTime@4     DD _GetSystemTimePreciseAsFileTimeDownlevel@4

END
