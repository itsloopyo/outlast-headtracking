// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "fov_override.h"
#include "frame_sample.h"

#include "cameraunlock/camera/zoom_compensation.h"

#include <atomic>
#include <cmath>

namespace OutlastHeadTracking {

// Keeps head tracking's effect on the picture the same size whatever Outlast does with
// its field of view.
//
// Outlast moves it constantly. Raising the camcorder narrows the view toward the
// night-vision zoom, running widens it, and scripted shots drive it wherever they like.
// A narrow field of view magnifies everything in the frame, head tracking with it: the
// head still turns ten degrees and the camera still turns ten degrees, and the picture
// simply moves further, by the ratio between the two fields. Uncorrected, the player
// reads that as the mod's sensitivity jumping the moment they raise the camcorder.
//
// The correction is one number per frame, and this is where it is decided.
//
// UNITS. The trap in this correction is pairing two angles that are not the same
// measurement - a vertical field from the accessor the projection uses against a
// horizontal one from wherever the game's settings are authored. Nothing catches it: the
// ratio is merely off by a constant, so ALL of normal play runs at a fixed fraction of
// the pose and head tracking feels weak everywhere rather than wrong anywhere.
//
// Both angles here are the same measurement, and the way that was established is by
// MEASURING rather than by reasoning about which field the engine divides. The live one
// is whatever GetFOVAngle returns for the frame. The unzoomed one is the hero's own
// DefaultFOV, and in game GetFOVAngle returns exactly that number - 90.000 against a
// DefaultFOV of 90 - while the player stands still, climbing toward the hero's
// RunningFOV of 100 in a run and down toward the camcorder's 15 at full zoom. So
// whatever convention the number carries (for this build, the horizontal field of a
// 16:10 frame, which the projection turns into the drawn frame's tangents by dividing
// by 1.6 and multiplying by the display's aspect) it is the SAME convention on both
// sides, the 1.6 and the aspect cancel out of the ratio, and an unzoomed frame gives
// exactly 1.0 - which is the gate the log line reports.
//
// Reasoning it out instead is what got this wrong once. CalcSceneView normalises its own
// level-of-detail factor against the POSSESSED CAMERA's DefaultFOV, so that field reads
// like the engine's own statement of the unzoomed angle - and it holds 80 on this build
// while the game draws 90 walking around. The first version of this correction took that
// pair and scaled every pose by 1.19 through the whole of ordinary play.
//
// This is not a sensitivity setting and there is no key for it. It is an engine-boundary
// conversion in the same family as the axis signs and the metres-to-world-units scale.

// The zoom correction for one frame.
struct ZoomBasis {
    // False when the angles could not be read as angles, which means no compensation on
    // this frame rather than a guessed one. factor is then 1.0.
    bool  valid = false;
    // The angle this frame is drawn with, and the angle an unzoomed frame would be drawn
    // with, both AFTER any [View] FieldOfView override - so with an override configured
    // the reference is the wider view the player is actually walking around in, and the
    // factor is still 1.0 in ordinary play.
    float rendered_fov = 0.0f;
    float rendered_base_fov = 0.0f;
    float factor = 1.0f;
};

// `requested` is [View] FieldOfView (0 for the game's own), `gameFov` the angle
// GetFOVAngle just returned, and `baseFov` the hero's unzoomed DefaultFOV.
//
// The override is folded in through DecideFov rather than around it, so there is one
// statement of what the override does to an angle and both sides of the ratio get it.
// It matters: the override is a ratio on the ANGLE and the compensation is a ratio on
// the TANGENT, and tan is not linear, so an override does not simply cancel.
inline ZoomBasis DecideZoomBasis(float requested, float gameFov, float baseFov) {
    ZoomBasis basis;
    if (!std::isfinite(baseFov) || baseFov < kMinBaseFov || baseFov > kMaxBaseFov) {
        return basis;
    }
    if (!std::isfinite(gameFov) || !(gameFov > 0.0f) || gameFov >= kMaxRenderableFov) {
        return basis;
    }
    // The live angle is not gated on kMinBaseFov: a frame drawn at fifteen degrees is
    // the camcorder zoomed in, which is a real zoom to compensate for and not a struct
    // offset that has moved. That tighter gate is the base's, where a number outside it
    // does mean the offset no longer fits the build.
    const FovDecision live = DecideFov(requested, gameFov, baseFov);
    const FovDecision unzoomed = DecideFov(requested, baseFov, baseFov);
    if (!(live.fov > 0.0f) || !(unzoomed.fov > 0.0f)) {
        return basis;
    }

    constexpr float kDegToHalfRad = 3.14159265358979323846f / 360.0f;
    const float tanLive = std::tan(live.fov * kDegToHalfRad);
    const float tanBase = std::tan(unzoomed.fov * kDegToHalfRad);
    if (!(tanLive > 0.0f) || !(tanBase > 0.0f) || !std::isfinite(tanLive)
        || !std::isfinite(tanBase)) {
        return basis;
    }
    const float factor = cameraunlock::camera::FovZoomFactor(tanLive, tanBase);
    if (!std::isfinite(factor) || !(factor > 0.0f)) {
        return basis;
    }

    basis.valid = true;
    basis.rendered_fov = live.fov;
    basis.rendered_base_fov = unzoomed.fov;
    basis.factor = factor;
    return basis;
}

// One frame's pose, rescaled so it displaces the picture by as much as it would have at
// the unzoomed field of view.
//
// Yaw, pitch and the lean all TRANSLATE the image across the frame, so all four scale -
// the angles through the tangent round trip that makes it exact rather than approximate,
// the lean linearly, because a head offset d seen at depth D lands at
// d / (2 * D * tan(fov/2)) of the frame.
//
// Roll does not. Roll ROTATES the image about the view axis, and ten degrees of head
// roll rolls the picture ten degrees at every field of view there is. Scaling it would
// flatten a head tilt the player is actively holding and buy nothing.

// Where the scaling stops. The tangent round trip is only defined either side of the
// quarter turn: at 90 degrees the tangent is infinite and past it it CHANGES SIGN, so a
// 100 degree head yaw comes back as -55 and the view is thrown to the opposite side the
// moment the game zooms. Those angles are reached in normal use - pose shaping belongs
// to the tracker, and an amplified opentrack curve turns thirty degrees of head into
// well past ninety - so an angle at or beyond the singularity is left as it is. That is
// continuous with the round trip, which tends to 90 as the angle does, and monotone
// either side of it. It also passes a non-finite angle straight through, for the camera
// hook's own finiteness check to refuse.
constexpr float kMaxScalableAngleDeg = 90.0f;

inline float ScaledAngle(float angleDeg, float factor) {
    if (!(std::fabs(angleDeg) < kMaxScalableAngleDeg)) {
        return angleDeg;
    }
    return cameraunlock::camera::ScaleAngleForZoom(angleDeg, factor);
}

inline FrameSample ScaledForZoom(const FrameSample& s, float factor) {
    if (!std::isfinite(factor) || !(factor > 0.0f) || factor == 1.0f) {
        return s;
    }
    FrameSample out = s;
    if (out.has_rotation) {
        out.yaw = ScaledAngle(out.yaw, factor);
        out.pitch = ScaledAngle(out.pitch, factor);
    }
    if (out.has_position) {
        out.pos_x *= factor;
        out.pos_y *= factor;
        out.pos_z *= factor;
    }
    return out;
}

// The factor the frame being built is to be scaled by.
//
// Written by the field-of-view hook and read by the camera hook, which are two detours
// inside one CalcSceneView: the engine asks for the angle at 0x62A1BB and for the
// viewpoint at 0x62A221, in that order and with no branch between them, so the camera
// hook reads the factor decided from THIS frame's angle. The one path that reaches
// neither - the frozen-camera branch, which reads a stored viewpoint and the
// controller's own angle instead of calling either accessor - applies no head pose
// either, so there is no frame that composes a pose against another frame's zoom.
inline std::atomic<float>& FrameZoom() {
    // Constant-initialised, so this local static carries no one-time-init guard for a
    // per-frame path to test.
    static std::atomic<float> factor{1.0f};
    return factor;
}

inline void PublishFrameZoom(float factor) {
    FrameZoom().store(factor, std::memory_order_relaxed);
}

inline float FrameZoomFactor() {
    return FrameZoom().load(std::memory_order_relaxed);
}

}  // namespace OutlastHeadTracking
