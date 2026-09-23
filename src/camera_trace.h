// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"
#include "ue3_types.h"

namespace OutlastHeadTracking {

// AActor::execTrace's mask for a zero-extent trace that includes actors. It is what the
// reticle wants - the mark belongs on whatever the shot would meet, a body included - and
// it is the only mask this build is known to use, so it is also what the lean clamp
// starts from rather than a narrower one nothing here has confirmed.
constexpr std::uint32_t kTraceAllFlags = 0x20BFu;

void InitCameraTrace(std::uintptr_t base, const OffsetTable& offsets);

// One line check between two world points, shared by the reticle and the lean clamp.
// False when the world or the player's pawn could not be read, in which case neither
// out-parameter is written: that is a failure to ask, not an answer of "nothing there".
// The pawn is the actor the check ignores, so the trace never stops on the player.
bool TraceWorldLine(void* controller, const UE3Vector& start, const UE3Vector& end,
                    std::uint32_t flags, UE3Vector& impact, bool& hit);
bool TraceCameraAim(void* controller, const UE3Vector& eye, const UE3Rotator& rotation,
                    UE3Vector& target, bool& hit);

}  // namespace OutlastHeadTracking
