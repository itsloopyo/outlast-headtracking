// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

namespace OutlastHeadTracking {

class TrackingRuntime;
struct Config;

void BeginCameraFrame();
void FinishCameraFrame();

// Where head tracking enters the game.
//
// APlayerController::GetPlayerViewPoint is the one question the engine asks about where
// the player is looking from, and everything that matters asks it: the frame, the audio
// listener, and the traces that decide what the player can reach. The mod detours it and
// answers the frame's copy - and only the frame's copy - with the head pose composed in.
// Every other caller gets the answer the game computed, so what the player reaches for,
// where sound comes from and everywhere a trace lands are identical with tracking on and
// off. That is the whole of the aim-decoupling story here; there is no restore step,
// because nothing outside the frame was ever changed.
//
// Two things pick the frame's copy out of the rest, and neither implies the other:
//
//   - the return address, which picks the call inside ULocalPlayer::CalcSceneView out of
//     the accessor's four call sites, and out of any nested call the UnrealScript behind
//     it makes, which re-enters this detour from inside the original;
//   - the drawing-frame flag (render_frame.h), which picks the CalcSceneView call that is
//     drawing out of the three that reach that same call site.
struct CameraHookTargets {
    // APlayerController::GetPlayerViewPoint(FVector& out_Location, FRotator& out_Rotation).
    std::uintptr_t getPlayerViewPoint;

    // The instruction ULocalPlayer::CalcSceneView returns to after its call to the
    // accessor above.
    std::uintptr_t sceneViewReturn;
};

// Detours the viewpoint accessor and composes `tracking`'s pose into the frame's answer.
// `tracking` must outlive the hook, which for the whole life of the process it does.
// `cfg` is read here and not kept: what the detour needs from it is the diagnostic
// switch and the lean clamp's settings, and both are fixed for the session.
bool InstallCameraHook(const CameraHookTargets& targets, TrackingRuntime& tracking,
                       const Config& cfg);

}  // namespace OutlastHeadTracking
