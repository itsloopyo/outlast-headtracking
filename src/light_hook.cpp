// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "light_hook.h"

#include "frame_view.h"
#include "light_composition.h"
#include "log_once.h"
#include "logging.h"
#include "minhook_util.h"
#include "ue3_types.h"

#include "cameraunlock/memory/safe_memory.h"

#include <cmath>

namespace OutlastHeadTracking {

namespace {

// How near the frame's eye a light has to be to count as one the player is carrying. The
// camcorder's light measures zero from it; the hero's other lights sit tens of units out
// and are switched off anyway, and a level's own lights are nowhere near.
//
// Compared squared, because this runs for every moving light in the level on every frame
// and the square root it would otherwise take buys nothing: both sides are distances.
constexpr float kEyeRadius = 30.0f;
constexpr float kEyeRadiusSquared = kEyeRadius * kEyeRadius;

using SetParentToWorld_t = void(*)(void* light);

SetParentToWorld_t g_original = nullptr;
LightHookTargets   g_targets{};

float LengthSquaredOf(float x, float y, float z) { return x * x + y * y + z * z; }

bool IsCarriedByThePlayer(std::uintptr_t light, const UE3Vector& eye) {
    std::uint32_t flags = 0;
    if (!cameraunlock::memory::SafeRead(light + g_targets.offFlags, flags)) return false;
    // bEnabled. An unlit light is not what the player is seeing by, and the hero carries
    // several of those.
    if ((flags & 1u) == 0) return false;

    UE3Vector origin{};
    if (!cameraunlock::memory::SafeRead(light + g_targets.offOrigin, origin)) return false;
    if (!std::isfinite(origin.X) || !std::isfinite(origin.Y) || !std::isfinite(origin.Z)) {
        return false;
    }
    return LengthSquaredOf(origin.X - eye.X, origin.Y - eye.Y, origin.Z - eye.Z) <=
           kEyeRadiusSquared;
}

// The rotator is the game's own property - script reads it and a save serialises it - so
// it may only carry the head across the one call that consumes it. A restore that did not
// land leaves it holding a head-turned angle for as long as the component lives, with
// nothing else in the session to say so.
void ReportRotationRestoreFailedOnce() {
    static bool reported = false;
    if (!ClaimOnce(reported)) return;
    Log::Line("WARN: the camcorder light's own rotator could not be put back after the "
              "frame that borrowed it, so the game's copy of it is left carrying a head "
              "turn. The camera is unaffected.");
}

void ReportEngagedOnce(const UE3Rotator& before, const UE3Rotator& after) {
    static bool reported = false;
    if (!ClaimOnce(reported)) return;
    Log::Line("The camcorder's light is following the head: its rotator went from %d/%d/%d "
              "to %d/%d/%d for this frame, so the lit cone points where the view does "
              "rather than where the game is aiming.", before.Pitch, before.Yaw, before.Roll,
              after.Pitch, after.Yaw, after.Roll);
}

void Detour(void* lightComponent) {
    const auto light = reinterpret_cast<std::uintptr_t>(lightComponent);

    UE3Vector eye{};
    UE3Rotator clean{};
    UE3Rotator drawn{};
    // No frame drawn yet, or a frame the head did not turn: the game's own answer is
    // already the right one, and nothing is read or written.
    if (!TryGetFrameView(eye, clean, drawn) ||
        (clean.Pitch == drawn.Pitch && clean.Yaw == drawn.Yaw && clean.Roll == drawn.Roll)) {
        g_original(lightComponent);
        return;
    }

    if (!IsCarriedByThePlayer(light, eye)) {
        g_original(lightComponent);
        return;
    }

    UE3Rotator lightRotation{};
    ParentTransform parent{};
    if (!cameraunlock::memory::SafeRead(light + g_targets.offRotation, lightRotation) ||
        !cameraunlock::memory::SafeRead(light + g_targets.offParentToWorld, parent)) {
        g_original(lightComponent);
        return;
    }

    UE3Rotator turned{};
    if (!TurnedByTheHead(lightRotation, parent, clean, drawn, &turned)) {
        g_original(lightComponent);
        return;
    }

    // The rotator is the game's own property, and script and serialisation read it, so it
    // carries the head only across the call that consumes it. What keeps the head
    // rotation afterwards is the transform the call computed, which is the renderer's.
    if (!cameraunlock::memory::SafeWrite(light + g_targets.offRotation, turned)) {
        // Put the game's own rotator back before the call consumes it. A rotator is three
        // int32s, and SafeWrite reports a fault having already copied whatever fitted
        // before it - so a failure here can leave the field half written, which is an
        // angle neither side asked for rather than nothing having happened.
        cameraunlock::memory::SafeWrite(light + g_targets.offRotation, lightRotation);
        g_original(lightComponent);
        return;
    }
    g_original(lightComponent);
    if (!cameraunlock::memory::SafeWrite(light + g_targets.offRotation, lightRotation)) {
        ReportRotationRestoreFailedOnce();
    }
    ReportEngagedOnce(lightRotation, turned);
}

}  // namespace

bool InstallLightHook(const LightHookTargets& targets) {
    g_targets = targets;

    void* target = reinterpret_cast<void*>(targets.setParentToWorld);
    const MH_STATUS status = CreateAndEnableHook(target, reinterpret_cast<void*>(&Detour),
                                                 reinterpret_cast<void**>(&g_original));
    if (status != MH_OK) {
        Log::Line("ERROR: hooking the light transform update @ 0x%p failed: %d. The "
                  "camcorder's light keeps following the game's own aim; everything else "
                  "about the session is unchanged.", target, status);
        g_original = nullptr;
        return false;
    }
    Log::Line("Light hook installed: ULightComponent::SetParentToWorld @ 0x%p", target);
    return true;
}

}  // namespace OutlastHeadTracking
