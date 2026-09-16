// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <atomic>

namespace OutlastHeadTracking {

// What the game's own crosshair should do on the frame being drawn.
enum class CrosshairPlacement {
    // Leave it where the game puts it. This is the right answer whenever no head
    // ROTATION is being applied - tracking off, position only, a menu, no packets yet -
    // because the view and the direction the game is pointing are then the same thing
    // and the middle of the screen is already correct.
    GamesOwn,
    // Move it by the published offset.
    Offset,
    // Draw nothing. The game is pointing somewhere this frame does not show, so there is
    // no honest place to put the mark, and leaving it at the middle of the screen would
    // claim the player is pointing at whatever is there.
    Hidden,
};

// Where the direction the game is pointing lands in the frame the head is looking at,
// in normalised device coordinates - x right, y up, -1 to 1 across the image.
//
// Written by the camera hook, which is the only place that has both the rotator the
// engine filled in and the one the frame was drawn with. Read by the crosshair hook
// later in the same frame, on the same thread. Atomic anyway, because they are two
// different call stacks reached through two different detours and the handful of bytes
// between them are not worth an assumption.
struct AimOffset {
    std::atomic<CrosshairPlacement> placement{CrosshairPlacement::GamesOwn};
    std::atomic<float> ndc_x{0.0f};
    std::atomic<float> ndc_y{0.0f};
};

inline AimOffset& GetAimOffset() {
    // Every member is constant-initialised and the type has no destructor, so this local
    // static carries no one-time-init guard for its callers to test - which matters
    // because both of them are on a per-frame path.
    static AimOffset offset;
    return offset;
}

inline void PublishAimOffset(CrosshairPlacement placement, float ndcX, float ndcY) {
    AimOffset& offset = GetAimOffset();
    offset.ndc_x.store(ndcX, std::memory_order_relaxed);
    offset.ndc_y.store(ndcY, std::memory_order_relaxed);
    offset.placement.store(placement, std::memory_order_release);
}

}  // namespace OutlastHeadTracking
