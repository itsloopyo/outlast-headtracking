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
        nt.Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    out.timeDateStamp = nt.FileHeader.TimeDateStamp;
    out.sizeOfImage   = nt.OptionalHeader.SizeOfImage;
    out.checkSum      = nt.OptionalHeader.CheckSum;
    return true;
}

}  // namespace OutlastHeadTracking
