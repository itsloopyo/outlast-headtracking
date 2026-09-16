// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "MinHook.h"

namespace OutlastHeadTracking {

// Create then enable, the two-step every hook in this mod goes through. Returns the
// first status that was not MH_OK so each caller keeps its own wording and its own
// severity: the camera and scene-view hooks failing means there is no head tracking
// this session, while the field-of-view hook failing costs only the override.
//
// A failed enable is undone here rather than at each call site: create may have
// succeeded on its own, leaving the entry and its trampoline allocated, and a caller
// that only nulls its own pointers strands them and leaves MinHook holding the target.
// The trampoline written to *original is freed with it, so callers must null that too.
inline MH_STATUS CreateAndEnableHook(void* target, void* detour, void** original) {
    const MH_STATUS created = MH_CreateHook(target, detour, original);
    if (created != MH_OK) {
        return created;
    }
    const MH_STATUS enabled = MH_EnableHook(target);
    if (enabled != MH_OK) {
        MH_RemoveHook(target);
    }
    return enabled;
}

}  // namespace OutlastHeadTracking
