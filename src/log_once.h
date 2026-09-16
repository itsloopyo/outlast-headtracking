// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace OutlastHeadTracking {

// Claims a latch the first time it is offered and refuses every time after, which is
// what the mod's diagnostic lines are gated on: each of them answers a question about
// the session ("did the pose reach the camera", "what was the frame really drawn with")
// that is answered once and would otherwise be repeated every frame.
//
// The latches are plain bools rather than atomics on purpose. Each one is offered from
// exactly one place, on the thread that place runs on, and the worst a torn race could
// do is write the same line twice - which is cheaper than an atomic on the render
// thread's per-frame path.
inline bool ClaimOnce(bool& latch) {
    if (latch) {
        return false;
    }
    latch = true;
    return true;
}

}  // namespace OutlastHeadTracking
