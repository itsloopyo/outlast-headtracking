// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "game_module.h"

#include "pe_headers.h"

#include <windows.h>

namespace OutlastHeadTracking {

GameModule HostGameModule() {
    HMODULE module = GetModuleHandleW(nullptr);
    if (!module) return {};

    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    PeFingerprint headers{};
    if (!ReadPeFingerprint(base, headers)) return {};

    GameModule range;
    range.base = base;
    range.end  = base + headers.sizeOfImage;
    range.fingerprint = headers;
    return range;
}

}  // namespace OutlastHeadTracking
