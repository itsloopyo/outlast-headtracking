// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "pe_headers.h"

#include "cameraunlock/memory/safe_memory.h"

#include <windows.h>

namespace OutlastHeadTracking {

bool ReadPeFingerprint(std::uintptr_t base, PeFingerprint& out) {
    IMAGE_DOS_HEADER dos{};
    if (!cameraunlock::memory::SafeRead(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    IMAGE_NT_HEADERS nt{};
    if (!cameraunlock::memory::SafeRead(base + dos.e_lfanew, nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE ||
        // IMAGE_NT_HEADERS is the 64-bit form here, so a 32-bit image's optional header
        // is a different shape and SizeOfImage and CheckSum would be read out of fields
        // that are not them. The two numbers that come back look ordinary, which is the
        // problem: the module range is then wrong by whatever those bytes happened to
        // say. The game is x64 (CMakeLists refuses a Win32 build), so this is the
        // boundary saying so rather than a case being handled.
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return false;
    }
    out.timeDateStamp = nt.FileHeader.TimeDateStamp;
    out.sizeOfImage   = nt.OptionalHeader.SizeOfImage;
    out.checkSum      = nt.OptionalHeader.CheckSum;
    return true;
}

}  // namespace OutlastHeadTracking
