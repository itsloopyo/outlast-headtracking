// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"

#include <cstdint>

namespace OutlastHeadTracking {

// Walks a loaded image's DOS and NT headers and reads the three fields that identify
// the build it was compiled from, plus the image size the module's address range is
// derived from. One implementation because two callers want the same walk: the module
// range (game_module.h) and the build fingerprint (build_registry.h), and a second copy
// of it is a second place for the signature and bounds checks to drift.
//
// False when the headers cannot be read or do not describe a PE image, in which case
// `out` is untouched. Reads are SEH-guarded, so a base that is not mapped fails rather
// than faulting.
bool ReadPeFingerprint(std::uintptr_t base, PeFingerprint& out);

}  // namespace OutlastHeadTracking
