// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"
#include "ue3_types.h"

namespace OutlastHeadTracking {

void InitCameraTrace(std::uintptr_t base, const OffsetTable& offsets);
bool TraceCameraAim(void* controller, const UE3Vector& eye, const UE3Rotator& rotation,
                    UE3Vector& target, bool& hit);

}  // namespace OutlastHeadTracking
