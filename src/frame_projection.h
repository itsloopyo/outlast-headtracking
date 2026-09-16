// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "cameraunlock/math/angle_utils.h"

#include <atomic>
#include <cmath>

namespace OutlastHeadTracking {

// The perspective the frame on screen was drawn with, as the two half-field tangents the
// projection divides by: tan(horizontal FOV / 2) and tan(vertical FOV / 2).
//
// Tangents rather than an angle in degrees, because an angle on its own does not say
// what a projection has to know. Whether the number is the horizontal or the vertical
// field, and what ratio the other one is derived with, are conventions the matrix states
// and the angle does not - and Outlast's are not the obvious ones. Its scene view builds
// FPerspectiveMatrix(halfFOV, halfFOV, 1.6/(Width/Height), 1.6, ...), which makes the
// vertical field 2*atan(tan(FOV/2)/1.6) whatever the display is and lets the horizontal
// one grow with the display's aspect ratio: the angle is the horizontal field of a 16:10
// frame, and the game is Hor+. Measured against that on steam-win64-20140429, 16:9: the
// camera's 80 degrees projected tanH=0.93233 tanV=0.52444, an 86.0 x 55.3 degree frame,
// and 110 degrees projected tanH=1.58683 tanV=0.89259. The other branch, taken when the
// camera constrains its aspect ratio, derives the vertical field from that ratio
// instead.
//
// So reading 1/M[0][0] and 1/M[1][1] out of the matrix the renderer actually used is
// what settles it, rather than the mod carrying a second copy of that arithmetic. It
// keeps settling it when a field-of-view override moves the angle, and when a branch
// this one has not seen picks different multipliers.
//
// Written on the render thread by the scene-view hook, once per frame. Read by anything
// that has to place something in the frame - the crosshair's projection above all, which
// divides the clean aim's screen-space components by exactly these two numbers.
struct FrameProjection {
    // Release-stored after the two tangents, so a reader that observes it set observes a
    // pair that was published rather than the zeroes it started on. It does not fence
    // the steady state: once valid is true it stays true, and a reader can pick up the
    // horizontal tangent from one frame and the vertical from the next. Harmless for
    // what these are for - both move together and by fractions of a degree per frame -
    // but the flag is a publication gate, not a snapshot.
    std::atomic<bool>  valid{false};
    std::atomic<float> tan_half_h{0.0f};
    std::atomic<float> tan_half_v{0.0f};
    // The aspect of the last frame that published a projection, kept across the
    // invalidation the flag above goes through at the start of every frame. Its one
    // reader sits INSIDE CalcSceneView, which runs after this frame's pair has been
    // invalidated and before the new one is published, so it can never see anything
    // through that gate - and what it wants the number for is a log line, on a quantity
    // that does not change between two frames. 0 until a frame has published one.
    std::atomic<float> last_aspect{0.0f};
};

inline FrameProjection& GetFrameProjection() {
    static FrameProjection projection;
    return projection;
}

// The frame's two half-field tangents, or false when no frame has published a usable
// projection yet. ONE place where the validity flag, the acquire and the positivity
// check meet: a consumer that rolls its own is a consumer that eventually divides by a
// zero the publisher never meant to hand out.
inline bool TryGetFrameProjection(float& tanHalfH, float& tanHalfV) {
    const FrameProjection& projection = GetFrameProjection();
    if (!projection.valid.load(std::memory_order_acquire)) {
        return false;
    }
    const float h = projection.tan_half_h.load(std::memory_order_relaxed);
    const float v = projection.tan_half_v.load(std::memory_order_relaxed);
    if (!(h > 0.0f) || !(v > 0.0f)) {
        return false;
    }
    tanHalfH = h;
    tanHalfV = v;
    return true;
}

// The aspect the frame before this one was drawn at, for a reader that cannot wait for
// this frame's own projection. False until some frame has published one.
inline bool TryGetLastFrameAspect(float& aspect) {
    const float a = GetFrameProjection().last_aspect.load(std::memory_order_relaxed);
    if (!(a > 0.0f)) {
        return false;
    }
    aspect = a;
    return true;
}

// The full angle a half-field tangent stands for, in degrees. For log lines and for the
// diagnostic display only - nothing that places anything on screen should convert back
// to an angle, because the tangent is what the projection divides by.
inline float FovDegreesFromHalfTangent(float tanHalf) {
    return 2.0f * std::atan(tanHalf) * static_cast<float>(cameraunlock::math::kRadToDeg);
}

}  // namespace OutlastHeadTracking
