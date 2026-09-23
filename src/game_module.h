// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"

#include <cstdint>

namespace OutlastHeadTracking {

// Address range of the running game EXE. Every address the discovery tool
// reports is converted to a module RVA through this, because an RVA is what a
// build profile pins and what is the same on every run.
struct GameModule {
    uintptr_t base = 0;
    uintptr_t end  = 0;
    // Read by the same walk that produced the range, and carried rather than read
    // again: the two come from one another's header fields, so a second walk is a
    // second chance for them to disagree and a branch on a failure that has already
    // been handled.
    PeFingerprint fingerprint{};

    bool Contains(uintptr_t address) const { return address >= base && address < end; }
    uintptr_t Rva(uintptr_t address) const { return address - base; }
};

// Range of the host EXE, read from its PE headers. All-zero (and so containing
// nothing) if the headers cannot be read.
GameModule HostGameModule();

}  // namespace OutlastHeadTracking
