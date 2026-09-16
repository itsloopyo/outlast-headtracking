// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "ue3_types.h"

#include <atomic>

namespace OutlastHeadTracking {

// The viewpoint the frame being drawn was built from: where the game put the eye and
// which way it pointed before the head pose reached it, and which way it pointed after.
//
// Written by the camera hook, which is the only place that holds both rotators, and read
// by anything that has to move with the head rather than with the game's own aim. The
// difference between the two rotators IS the head rotation as the engine expresses it,
// so a consumer never re-derives it from the tracker pose and cannot disagree with the
// frame about how far the head turned.
struct FrameView {
    std::atomic<float> location_x{0.0f};
    std::atomic<float> location_y{0.0f};
    std::atomic<float> location_z{0.0f};
    std::atomic<std::int32_t> clean_pitch{0};
    std::atomic<std::int32_t> clean_yaw{0};
    std::atomic<std::int32_t> clean_roll{0};
    std::atomic<std::int32_t> drawn_pitch{0};
    std::atomic<std::int32_t> drawn_yaw{0};
    std::atomic<std::int32_t> drawn_roll{0};
    // A seqlock sequence number: 0 before any frame, odd while a publish is in flight,
    // and an even value that changes on every publish once one has been. It is what lets a
    // reader on another thread tell a consistent set of the nine fields from a torn one,
    // and a frame that has not been drawn yet from one at the world origin.
    std::atomic<std::uint32_t> frame{0};
};

// The player controller the frame's viewpoint came from. Everything the mod may want to
// reach on the player - the pawn, and what the pawn owns - hangs off it, and this is the
// one place in the process that is handed it every frame.
inline std::atomic<void*>& GetFrameController() {
    static std::atomic<void*> controller{nullptr};
    return controller;
}

inline void PublishFrameController(void* controller) {
    GetFrameController().store(controller, std::memory_order_release);
}

inline FrameView& GetFrameView() {
    // Every member is constant-initialised and the type has no destructor, so this local
    // static carries no one-time-init guard on the per-frame path.
    static FrameView view;
    return view;
}

inline void PublishFrameView(const UE3Vector& location, const UE3Rotator& clean,
                             const UE3Rotator& drawn) {
    FrameView& view = GetFrameView();
    // Odd while the nine fields are being written, even when they agree with each other.
    // Bumping the counter only AFTER the writes - which is what this did before - is not a
    // seqlock and stops nothing: a reader that samples the counter, then reads fields while
    // the next publish is part-way through, sees the same counter either side and takes the
    // torn set. Measured on this exact shape, 42% of reads were internally inconsistent.
    const std::uint32_t start = view.frame.load(std::memory_order_relaxed);
    view.frame.store(start + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    view.location_x.store(location.X, std::memory_order_relaxed);
    view.location_y.store(location.Y, std::memory_order_relaxed);
    view.location_z.store(location.Z, std::memory_order_relaxed);
    view.clean_pitch.store(clean.Pitch, std::memory_order_relaxed);
    view.clean_yaw.store(clean.Yaw, std::memory_order_relaxed);
    view.clean_roll.store(clean.Roll, std::memory_order_relaxed);
    view.drawn_pitch.store(drawn.Pitch, std::memory_order_relaxed);
    view.drawn_yaw.store(drawn.Yaw, std::memory_order_relaxed);
    view.drawn_roll.store(drawn.Roll, std::memory_order_relaxed);
    view.frame.store(start + 2, std::memory_order_release);
}

// The last published frame, or false when no frame has been drawn yet or when a publish
// kept landing in the middle of the read.
//
// A seqlock read: the counter is odd while a publish is in flight and is checked either
// side of the nine fields, so a set is only handed back when no publish overlapped it.
// The camera hook's own consumers run on the render thread and cannot race a publish, but
// the light probe reads this from its own diagnostic thread, and a torn clean/drawn pair
// there reports the frame as drawn some degrees off its own viewpoint - which is exactly
// the number that probe exists to confirm is zero.
//
// The out-parameters are written only on success, so a caller that ignores the false
// cannot act on a half-read frame.
inline bool TryGetFrameView(UE3Vector& location, UE3Rotator& clean, UE3Rotator& drawn) {
    const FrameView& view = GetFrameView();
    // Four attempts is well past what a render thread can spend inside one publish.
    for (int attempt = 0; attempt < 4; ++attempt) {
        const std::uint32_t before = view.frame.load(std::memory_order_acquire);
        if (before == 0) {
            return false;
        }
        if ((before & 1u) != 0) {
            continue;
        }
        UE3Vector loc;
        UE3Rotator cleanRead;
        UE3Rotator drawnRead;
        loc.X = view.location_x.load(std::memory_order_relaxed);
        loc.Y = view.location_y.load(std::memory_order_relaxed);
        loc.Z = view.location_z.load(std::memory_order_relaxed);
        cleanRead.Pitch = view.clean_pitch.load(std::memory_order_relaxed);
        cleanRead.Yaw = view.clean_yaw.load(std::memory_order_relaxed);
        cleanRead.Roll = view.clean_roll.load(std::memory_order_relaxed);
        drawnRead.Pitch = view.drawn_pitch.load(std::memory_order_relaxed);
        drawnRead.Yaw = view.drawn_yaw.load(std::memory_order_relaxed);
        drawnRead.Roll = view.drawn_roll.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (view.frame.load(std::memory_order_relaxed) != before) {
            continue;
        }
        location = loc;
        clean = cleanRead;
        drawn = drawnRead;
        return true;
    }
    return false;
}

}  // namespace OutlastHeadTracking
