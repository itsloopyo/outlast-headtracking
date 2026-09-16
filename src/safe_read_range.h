// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "cameraunlock/memory/safe_memory.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace OutlastHeadTracking {

// SEH-guarded bulk read of a range core's typed SafeRead cannot serve: the two probes
// read a window whose size is only known at run time, or whose address is a guess the
// walk has not proven yet. Kept free of C++ objects so __try is legal, and filtered to
// access violations only so a breakpoint or a C++ exception travelling through still
// reaches whoever owns it.
inline bool SafeReadRange(std::uintptr_t addr, void* out, std::size_t bytes) {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(addr), bytes);
        return true;
    } __except (cameraunlock::memory::AccessViolationFilter(GetExceptionCode())) {
        return false;
    }
}

}  // namespace OutlastHeadTracking
