// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"

#include <cmath>
#include <cstdint>

namespace OutlastHeadTracking {

// Outlast's field of view, and how to render the game at a different one.
//
// The game has no field-of-view setting in its menus. Its own numbers live in
// OLGame.ini under [OLGame.OLHero] - DefaultFOV=90, RunningFOV=100, CamcorderMinFOV=15 -
// in a file under the player's Documents that the game rewrites. Those are the hero's
// own angles and they are what reaches the renderer: measured in game, GetFOVAngle
// returns 90.000 standing still and climbs toward 100 in a run.
//
// The angle the frame is drawn with reaches the renderer through
// APlayerController::GetFOVAngle, which ULocalPlayer::CalcSceneView calls once per frame
// while it builds the view. The other three callers are game logic, which is why this
// detour is filtered on the scene-view return address and nothing else sees a changed
// angle. It is NOT additionally filtered on the frame being the one drawing, unlike the
// camera hook: ULocalPlayer::Project and Deproject reach the same return address, and they
// convert between a world point and a screen point, which has to be done against the angle
// the frame is drawn with. What IS restricted to the drawing frame is the zoom factor this
// publishes for the camera hook to scale the pose by.
//
// One thing inside CalcSceneView does see it, and should: the level-of-detail distance
// factor it derives from the angle and caches on the controller. A wider frame gets the
// LOD scaling of a wider frame, which is what the game already does for itself every
// time it widens the view for a run. Nothing outside CalcSceneView reads that field.
//
// Nothing has to be kept in sync with the override. The mod's field of view is read back
// out of the FSceneView's own projection matrix (scene_view.h), so a frame drawn at a
// changed angle is placed against the matrix it was drawn with, on the frame it was
// drawn on, whatever this override did to the angle.

// What the override did to one frame's angle.
enum class FovOverrideStatus {
    // No override configured. The frame keeps the game's own angle. The detour is still
    // installed - it is what reads the angle the zoom compensation is decided from.
    Off,
    Applied,
    // The unzoomed angle did not read as an angle, so there is nothing to express the
    // override against. The frame keeps the game's own angle.
    NoBaseAngle,
    // Scaling the frame's angle takes it past having a perspective projection. The frame
    // keeps the game's own angle rather than the override being switched off for the
    // session.
    NotRenderable,
};

struct FovDecision {
    FovOverrideStatus status = FovOverrideStatus::Off;
    float fov = 0.0f;
};

// Outside this an angle is not a field of view, so it cannot be the unzoomed one the
// override is expressed against. It is a struct field read out of the game's own object:
// a value this far out means the offset no longer fits the build, and scaling by it
// would put a garbage angle in front of the player.
constexpr float kMinBaseFov = 10.0f;
constexpr float kMaxBaseFov = 170.0f;

// At and past 180 degrees tan(fov/2) runs away and there is no projection left.
constexpr float kMaxRenderableFov = 179.0f;

// The angle to render one frame with.
//
// The override is a RATIO against the unzoomed angle, not a value written flat over
// whatever the frame happens to hold. GetFOVAngle does not always return the player's
// own field of view: a run widens it, raising the camcorder narrows it toward the
// night-vision zoom, and a scripted shot drives it wherever it likes. Writing the
// configured number in unconditionally would flatten every one of them, so zooming the
// camcorder would visibly stop zooming. A ratio shows exactly the configured number on a
// frame at the player's own angle, scales a zoom by the same factor as everything else,
// and is continuous through the transition, so there is no frame where the override
// snaps in or out.
inline FovDecision DecideFov(float requested, float gameFov, float baseFov) {
    if (!(requested > 0.0f)) {
        return { FovOverrideStatus::Off, gameFov };
    }
    if (!std::isfinite(gameFov) || !(gameFov > 0.0f) || !std::isfinite(baseFov)
        || baseFov < kMinBaseFov || baseFov > kMaxBaseFov) {
        return { FovOverrideStatus::NoBaseAngle, gameFov };
    }
    const float scaled = gameFov * (requested / baseFov);
    if (!std::isfinite(scaled) || !(scaled > 0.0f) || scaled >= kMaxRenderableFov) {
        return { FovOverrideStatus::NotRenderable, gameFov };
    }
    return { FovOverrideStatus::Applied, scaled };
}

// The two addresses the field-of-view hook pins, resolved from the matched build
// profile. Grouped because they are the same type, and swapping them detours the wrong
// function.
struct FovHookTargets {
    // APlayerController::GetFOVAngle().
    std::uintptr_t getFovAngle;
    // The instruction ULocalPlayer::CalcSceneView returns to after its call to the
    // accessor above. Only that one caller gets a changed angle.
    std::uintptr_t sceneViewReturn;
};

// Detours APlayerController::GetFOVAngle, rescales the angle it returns to the
// scene-view builder when one is configured, and - on every frame, configured or not -
// publishes the frame's zoom factor for the head pose to be scaled by (frame_zoom.h).
//
// Installed whatever [View] FieldOfView says, because the second job is owed to the
// game's own zooms rather than to the override: Outlast narrows the view for the
// camcorder and widens it for a run, and a pose left unscaled through those moves the
// picture further per degree of head turn the moment the game zooms. Returns false when
// the detour could not be installed, which costs both jobs.
bool InstallFovHook(const FovHookTargets& targets, const Config& cfg);

// Whether the last frame's angle was the overridden one. Read by the scene-view hook,
// which reports the projection the frame was really built with once with the game's own
// field of view and once with the override in place - the log line that says the
// override reached the renderer.
bool FovOverrideActive();

}  // namespace OutlastHeadTracking
