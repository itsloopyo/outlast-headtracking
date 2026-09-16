// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace OutlastHeadTracking {

// One frame's processed head pose: rotation in degrees (YPR) and position offset
// in metres (tracker basis: x=right, y=up, z=forward). has_* report whether each
// channel produced fresh data this frame.
struct FrameSample {
    bool  has_rotation = false;
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    bool  has_position = false;
    float pos_x = 0.0f, pos_y = 0.0f, pos_z = 0.0f;
};

// The positional limits the pose is held inside, in metres and in the processor's own
// axis convention: x either side of centre, y as [-down, +up], z as [-forward, +back]
// because the processor's negative z is the forward lean.
struct PositionLimits {
    float x = 0.0f;
    float y_up = 0.0f;
    float y_down = 0.0f;
    float z_forward = 0.0f;
    float z_back = 0.0f;
};

// Puts the lean back inside the configured limits, and is the LAST thing that touches it.
//
// The processor clamps the pose on its way out, and the zoom correction (ScaledForZoom,
// frame_zoom.h) then multiplies it, so on any frame the game draws WIDER than its
// unzoomed angle the factor is above 1 and the eye leaves the box the player configured.
// Those limits are the only thing keeping the eye inside the player's body, so the clamp
// has to come after the scaling rather than before it - the engine-boundary conversion
// first, the game-specific limits after it.
inline FrameSample ClampedToLimits(const FrameSample& s, const PositionLimits& lim) {
    if (!s.has_position) {
        return s;
    }
    auto clamp = [](float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    };
    FrameSample out = s;
    out.pos_x = clamp(out.pos_x, -lim.x, lim.x);
    out.pos_y = clamp(out.pos_y, -lim.y_down, lim.y_up);
    out.pos_z = clamp(out.pos_z, -lim.z_forward, lim.z_back);
    return out;
}

}  // namespace OutlastHeadTracking
