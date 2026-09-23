// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "cameraunlock/camera/lean_clamp.h"

namespace OutlastHeadTracking {
namespace lean_trace {

// Asks the game whether anything stands between the clean eye and where the head wants
// to lean, in the shape cameraunlock::camera::LeanClamp takes its answers in. The split
// is deliberate: this half is the engine's own physics query and is different in every
// game, the half that decides what to do with the answer is arithmetic and lives in core.
//
// `context` is the APlayerController the frame's viewpoint came from, which is what the
// check needs to know whose pawn to ignore. Distances are in the engine's centimetres.
cameraunlock::camera::LeanObstruction Query(void* context,
                                            const cameraunlock::math::Vec3& start,
                                            const cameraunlock::math::Vec3& direction,
                                            float maxDistance);

// The trace mask the query runs with. Set once at install from the configuration.
void SetTraceFlags(unsigned int flags);

}  // namespace lean_trace
}  // namespace OutlastHeadTracking
