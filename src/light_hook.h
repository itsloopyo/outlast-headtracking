// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>

namespace OutlastHeadTracking {

// Where the camcorder's light gets its direction.
//
// The camcorder lights what it is pointed at, and it is pointed where the game is aiming
// - which is the mouse, not the head. Left alone, that puts the lit cone off to one side
// of the view the moment the player looks away from where they are aiming, and in night
// vision, where the cone is the only thing lit, it is the whole picture that is wrong.
//
// The engine recomputes a moving light's transform every frame from three things: the
// component's own rotator, its translation, and the matrix its parent hands it. This
// detours that recomputation and turns the component's rotator by the same rotation the
// frame's camera was turned by, so the cone lands where the player is looking. The
// rotator is put back immediately afterwards, so the only thing that ends up carrying the
// head rotation is the transform the renderer lights from.
struct LightHookTargets {
    // ULightComponent::SetParentToWorld: builds the light's world transform from the
    // three fields below and publishes it to the renderer.
    std::uintptr_t setParentToWorld;

    // The rotator the transform is built from, and the parent matrix it is built against.
    std::size_t offRotation;
    std::size_t offParentToWorld;

    // Where the light says it is, and the bitfield whose bit 0 is bEnabled. Together they
    // are what picks the camcorder's light out of the level's: it is the one that is lit
    // and standing at the player's eye.
    std::size_t offOrigin;
    std::size_t offFlags;
};

// Detours the transform update. Returns false when the detour could not be installed, in
// which case the light keeps following the game's own aim and everything else about the
// session is unchanged.
bool InstallLightHook(const LightHookTargets& targets);

}  // namespace OutlastHeadTracking
