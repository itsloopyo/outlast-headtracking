// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/config_table.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/head_tracking_config.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/value_codecs.h"

#include <string>
#include <string_view>

namespace OutlastHeadTracking {

constexpr const char* kConfigFileName = "CameraUnlock.ini";
// The file every build before the canonical format read, beside kConfigFileName. Imported once
// while kConfigFileName is absent, and never written.
constexpr const char* kLegacyConfigFileName = "HeadTracking.ini";
// The game's name as cameraunlock-core's data/games.json spells it.
constexpr const char* kConfigDisplayName = "Outlast";

namespace defaults {
// Render the game at a different field of view. 0 means the game's own angle is drawn
// unchanged. It does NOT mean the hook is absent: the same detour is what reads the live angle
// the zoom correction is measured against, so it goes in either way and at 0 it simply passes
// the game's answer straight back.
constexpr float kFovOverride    = 0.0f;
// The angles the mod will accept for it. Narrower than the range a projection can physically
// express, because a number outside this is a typo rather than a preference, and a typo that
// reached the renderer would be a black or unusable frame the player then has to guess the
// cause of.
constexpr float kMinFovOverride = 20.0f;
constexpr float kMaxFovOverride = 170.0f;

// How far off a blocking surface the eye is held, in the engine's centimetres. It has to
// EXCEED the near clip distance or the clamp buys nothing: geometry closer to the eye than the
// near plane is not drawn, so a wall held inside it is still see-through. This build's
// CalcSceneView builds its projection with MinZ 10.0, so 15 leaves a margin over it.
constexpr float kCollisionMargin = 15.0f;

// The engine's own trace mask for the check. The reticle's mask is the only one this build is
// known to use, so the clamp starts from it rather than from a narrower one nothing here has
// confirmed - which also means it currently stops on characters as well as on walls.
constexpr int   kCollisionChannel = 0x20BF;
}  // namespace defaults

// Core's config with this game's defaults and its own rows.
struct Config : cameraunlock::HeadTrackingConfig {
    // The field of view to render at, in degrees, or 0 for the game's own. Applied as a ratio
    // against the game's unzoomed angle so the game's own widening and narrowing survive it,
    // and only on the frame - every raycast, interaction and script that asks the game the same
    // question keeps the game's answer.
    float fov_override = defaults::kFovOverride;

    // Re-centre the game's window on its monitor whenever the game resizes it.
    bool center_window = true;

    // Find the live camera record by memory scan and report what writes and reads it. A
    // diagnostic tool: it spends several seconds a pass walking the address space, and it arms
    // a CPU watchpoint.
    bool camera_probe = false;

    // Watch the game's light and camcorder natives and report what they are handed.
    bool light_probe = false;

    // Log where the reticle is being placed, once a second.
    bool aim_probe = false;

    Config() {
        lean_clamp.skin = defaults::kCollisionMargin;
        collision_channel = defaults::kCollisionChannel;
    }
};

// [View] FieldOfView: 0, or an angle from kMinFovOverride to kMaxFovOverride.
class FovCodec {
public:
    using Value = float;

    cameraunlock::config::CodecParseResult<float> Parse(std::string_view text) const;
    // Throws std::invalid_argument for a value Parse would not read back.
    std::string Render(float value) const;
    bool Equal(float a, float b) const { return angle_.Equal(a, b); }

private:
    cameraunlock::config::FloatCodec angle_{0.0f, defaults::kMaxFovOverride};
};

// The rows of CameraUnlock.ini. Only the tracking mode pair and WorldSpaceYaw are Writable: the
// mode and yaw hotkeys save the player's choice, and End changes the session only.
cameraunlock::config::ConfigTable<Config> MakeConfigTable();

// HeadTracking.ini as the builds before the canonical format read it (legacy_config/), mapped
// into Config.
cameraunlock::config::LegacyImport<Config> MakeLegacyImport();

// The owner's options for the files in `folder` (with its trailing separator): the settings in
// CameraUnlock.ini, imported once from HeadTracking.ini. The mod passes DefaultsFile::PerUser()
// and a test a scratch file.
cameraunlock::config::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(const std::wstring& folder,
                                                                        cameraunlock::config::DefaultsFile defaults);

}  // namespace OutlastHeadTracking
