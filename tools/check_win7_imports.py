#!/usr/bin/env python3
"""Fail the build if any shipped Windows binary imports an entry point that does not
exist on Windows 7 SP1.

Such an import is not a soft failure: the loader refuses to start the process with
"The procedure entry point <name> could not be located in the dynamic link library
KERNEL32.dll", before any of our code runs. The OTDU_TARGET_WIN7 build is supposed to
route those calls to down-level equivalents (source/core/win7_thunks.*), and this check
is what proves it actually happened -- the previous /alternatename attempt silently did
nothing for two releases.

Reads the PE import tables directly so it runs anywhere (no dumpbin / MSVC env needed).

Usage: python tools/check_win7_imports.py [directory (default: build/bin)]
"""

import os
import struct
import sys

# Entry points that are absent from Windows 7 SP1 and that the MSVC toolchain is known to
# reference on its own. The first three are the ones we thunk; the rest are here so a
# toolchain or dependency upgrade that introduces one gets caught rather than shipped.
WIN8_PLUS_IMPORTS = {
    "CopyFile2": "Win8, MSVC STL <filesystem>",
    "CreateFile2": "Win8, MSVC STL <filesystem> (__std_fs_open_handle)",
    "GetSystemTimePreciseAsFileTime": "Win8, MSVC <chrono> system_clock::now",
    "GetTempPath2A": "Win10 20H1, MSVC STL <filesystem>",
    "GetTempPath2W": "Win10 20H1, MSVC STL <filesystem>",
    "GetFileInformationByName": "Win11",
    "SetThreadDescription": "Win10 1607",
    "GetThreadDescription": "Win10 1607",
    "WaitOnAddress": "Win8",
    "WakeByAddressAll": "Win8",
    "WakeByAddressSingle": "Win8",
    "DiscardVirtualMemory": "Win8.1",
    "OfferVirtualMemory": "Win8.1",
    "ReclaimVirtualMemory": "Win8.1",
    "GetOverlappedResultEx": "Win8",
    "SetProcessMitigationPolicy": "Win8",
    "GetProcessMitigationPolicy": "Win8",
    "PathCchCombineEx": "Win8, Pathcch.h",
    "PathCchCanonicalizeEx": "Win8, Pathcch.h",
    "PathCchAppendEx": "Win8, Pathcch.h",
    "PathCchRemoveFileSpec": "Win8, Pathcch.h",
    "PathAllocCanonicalize": "Win8, Pathcch.h",
    "PathAllocCombine": "Win8, Pathcch.h",
}


class PEFile:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        if d[:2] != b"MZ":
            raise ValueError("not a PE image")
        peOffset = struct.unpack_from("<I", d, 0x3C)[0]
        if d[peOffset:peOffset + 4] != b"PE\0\0":
            raise ValueError("not a PE image")

        self.machine, sectionCount = struct.unpack_from("<HH", d, peOffset + 4)
        optionalHeaderSize = struct.unpack_from("<H", d, peOffset + 20)[0]
        optionalHeader = peOffset + 24
        self.bIs64Bits = struct.unpack_from("<H", d, optionalHeader)[0] == 0x20B
        self.osVersion = struct.unpack_from("<HH", d, optionalHeader + 40)
        self.subsystemVersion = struct.unpack_from("<HH", d, optionalHeader + 48)

        self.dataDirectory = optionalHeader + (112 if self.bIs64Bits else 96)
        self.sections = []
        sectionTable = optionalHeader + optionalHeaderSize
        for i in range(sectionCount):
            entry = sectionTable + i * 40
            virtualSize, virtualAddress, rawSize, rawOffset = struct.unpack_from("<IIII", d, entry + 8)
            self.sections.append((virtualAddress, max(virtualSize, rawSize), rawOffset))

    def offsetFromRVA(self, rva):
        for virtualAddress, size, rawOffset in self.sections:
            if virtualAddress <= rva < virtualAddress + size:
                return rawOffset + (rva - virtualAddress)
        return None

    def readString(self, offset):
        end = self.data.index(b"\0", offset)
        return self.data[offset:end].decode("ascii", "replace")

    def readThunkNames(self, thunkRVA):
        names = []
        offset = self.offsetFromRVA(thunkRVA)
        if offset is None:
            return names
        entrySize = 8 if self.bIs64Bits else 4
        ordinalFlag = 1 << (63 if self.bIs64Bits else 31)
        while True:
            value = struct.unpack_from("<Q" if self.bIs64Bits else "<I", self.data, offset)[0]
            if value == 0:
                break
            if not value & ordinalFlag:  # ordinal imports carry no name
                nameOffset = self.offsetFromRVA(value & 0x7FFFFFFF)
                if nameOffset is not None:
                    names.append(self.readString(nameOffset + 2))  # skip the hint
            offset += entrySize
        return names

    def getImports(self):
        """-> [(dll, symbol, kind)] for both the regular and the delay-load tables."""
        imports = []
        for directoryIndex, entrySize, kind in ((1, 20, "import"), (13, 32, "delay-load")):
            directoryRVA = struct.unpack_from("<II", self.data, self.dataDirectory + directoryIndex * 8)[0]
            if not directoryRVA:
                continue
            offset = self.offsetFromRVA(directoryRVA)
            if offset is None:
                continue
            while True:
                descriptor = self.data[offset:offset + entrySize]
                if len(descriptor) < entrySize or not any(descriptor):
                    break
                fields = struct.unpack_from("<IIIII", self.data, offset)
                if kind == "import":
                    lookupRVA, nameRVA, firstThunkRVA = fields[0], fields[3], fields[4]
                else:
                    nameRVA, firstThunkRVA, lookupRVA = fields[1], fields[3], fields[4]
                nameOffset = self.offsetFromRVA(nameRVA)
                dll = self.readString(nameOffset).lower() if nameOffset is not None else "?"
                for symbol in self.readThunkNames(lookupRVA or firstThunkRVA):
                    imports.append((dll, symbol, kind))
                offset += entrySize
        return imports


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join("build", "bin")
    if not os.path.isdir(root):
        print("ERROR: '%s' is not a directory" % root)
        return 2

    binaries = []
    for directory, _, files in os.walk(root):
        for name in sorted(files):
            if os.path.splitext(name)[1].lower() in (".exe", ".dll"):
                binaries.append(os.path.join(directory, name))

    if not binaries:
        print("ERROR: no .exe/.dll found under '%s' -- wrong path, or the build produced nothing" % root)
        return 2

    failures = []
    for path in binaries:
        try:
            image = PEFile(path)
        except (ValueError, IndexError, struct.error) as e:
            print("ERROR: %s: could not parse (%s)" % (path, e))
            return 2

        offenders = [(dll, symbol, kind) for dll, symbol, kind in image.getImports()
                     if symbol in WIN8_PLUS_IMPORTS]
        print("%-44s %-4s subsystem %d.%02d  %s"
              % (os.path.relpath(path, root),
                 "x64" if image.bIs64Bits else "x86",
                 image.subsystemVersion[0], image.subsystemVersion[1],
                 "FAIL" if offenders else "ok"))
        for dll, symbol, kind in offenders:
            print("      %s!%s [%s] -- %s" % (dll, symbol, kind, WIN8_PLUS_IMPORTS[symbol]))
            failures.append((path, dll, symbol))

    if failures:
        print("\n%d import(s) unavailable on Windows 7 SP1 in %d binary/binaries."
              % (len(failures), len({path for path, _, _ in failures})))
        print("Each one makes the module fail to load. Add a down-level thunk in "
              "source/core/win7_thunks.* and make sure the object reaches this target's link.")
        return 1

    print("\nAll %d binaries are loadable on Windows 7 SP1." % len(binaries))
    return 0


if __name__ == "__main__":
    sys.exit(main())
