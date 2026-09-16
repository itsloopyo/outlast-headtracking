// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

namespace OutlastHeadTracking {

// Address range of the running game EXE. Every address the discovery tool
// reports is converted to a module RVA through this, because an RVA is what
// pastes into Ghidra and what a build profile pins.
struct GameModule {
    uintptr_t base = 0;
    uintptr_t end  = 0;

    bool Contains(uintptr_t address) const { return address >= base && address < end; }
    uintptr_t Rva(uintptr_t address) const { return address - base; }
};

// Range of the host EXE, read from its PE headers. All-zero (and so containing
// nothing) if the headers cannot be read.
GameModule HostGameModule();

}  // namespace OutlastHeadTracking
