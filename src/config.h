// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"

namespace OutlastHeadTracking {

// The shipped defaults, in one place. WriteDefaultIni writes these, LoadOrCreate
// falls back to them, and Config's members are initialised from them, so a
// default-constructed Config, a freshly written INI and a read of a key that is
// missing from the file cannot disagree about what the default is.
namespace defaults {
constexpr bool  kEnableOnStartup = true;
constexpr int   kPort            = 4242;
constexpr int   kMinPort         = 1024;
constexpr int   kMaxPort         = 65535;
constexpr int   kDataFreshnessMs = 500;
constexpr bool  kWorldSpaceYaw   = true;

// Keep the game's window centred on its monitor. Outlast centres it once, while the
// splash movies play, and then resizes it for the menu without moving it.
constexpr bool  kCenterWindow    = true;

// Render the game at a different field of view. 0 is the shipped default and means the
// game's own, which is also the only value that installs no hook at all.
constexpr float kFovOverride     = 0.0f;
// The angles the mod will accept for it. Narrower than the range a projection can
// physically express, because a number outside this is a typo rather than a preference,
// and a typo that reached the renderer would be a black or unusable frame the player
// then has to guess the cause of.
constexpr float kMinFovOverride  = 20.0f;
constexpr float kMaxFovOverride  = 170.0f;
constexpr float kLocalSmoothing  = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
constexpr float kRemoteSmoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);
constexpr int   kVkToggle        = 0x23; // VK_END
constexpr int   kVkCycleMode     = 0x21; // VK_PRIOR (Page Up)
constexpr int   kVkYawMode       = 0x22; // VK_NEXT (Page Down)
constexpr bool  kChord           = true;

// The camera probe is a diagnostic tool, off in every shipped INI. It is what
// finds the camera record and the code around it on a build whose layout has moved -
// turned on for one session, then off again.
constexpr bool  kCameraProbe     = false;

// Report what the game does to its lights when the camcorder is raised and night vision
// switched on. A diagnostic on the same terms as the camera probe: off in every shipped
// INI, turned on for one session.
constexpr bool  kLightProbe      = false;

// Log where the reticle is being placed, once a second. Diagnostic, on the same terms.
constexpr bool  kAimProbe        = false;

constexpr bool  kPositionEnabled = true;
constexpr float kPosLimitX       = cameraunlock::PositionSettings{}.limit_x;
constexpr float kPosLimitY       = cameraunlock::PositionSettings{}.limit_y;
constexpr float kPosLimitYDown   = cameraunlock::PositionSettings{}.limit_y_down;
constexpr float kPosLimitZ       = cameraunlock::PositionSettings{}.limit_z;
constexpr float kPosLimitZBack   = cameraunlock::PositionSettings{}.limit_z_back;
}  // namespace defaults

struct Config {
    bool  enabled_on_startup = defaults::kEnableOnStartup;
    uint16_t udp_port = static_cast<uint16_t>(defaults::kPort);

    // Smoothing is picked per connection from the packet source address: a
    // tracker on this machine (loopback) uses local_smoothing, a remote network
    // device uses remote_smoothing. Both cover rotation and position.
    float local_smoothing = defaults::kLocalSmoothing;
    float remote_smoothing = defaults::kRemoteSmoothing;

    int  data_freshness_ms = defaults::kDataFreshnessMs;

    // The field of view to render at, in degrees, or 0 for the game's own. Applied as a
    // ratio against the game's unzoomed angle so the game's own widening and narrowing
    // survive it, and only on the frame - every raycast, interaction and script that asks
    // the game the same question keeps the game's answer.
    float fov_override = defaults::kFovOverride;

    // true = horizon-locked (world-space) yaw, false = camera-local yaw.
    bool world_space_yaw = defaults::kWorldSpaceYaw;

    // Re-centre the game's window on its monitor whenever the game resizes it.
    bool center_window = defaults::kCenterWindow;

    // Find the live camera record by memory scan and report what writes and reads it.
    // Off in every shipped INI: it is a diagnostic tool, it spends several
    // seconds a pass walking the address space, and it arms a CPU watchpoint.
    bool  camera_probe = defaults::kCameraProbe;

    // Watch the game's light and camcorder natives and report what they are handed.
    bool  light_probe = defaults::kLightProbe;
    bool  aim_probe = defaults::kAimProbe;

    // 6DOF positional tracking.
    bool  position_enabled = defaults::kPositionEnabled;
    float pos_limit_x = defaults::kPosLimitX;
    // Vertical travel is clamped as [-pos_limit_y_down, +pos_limit_y]. The two are
    // separate keys because a player sitting down has less room to duck than to
    // stretch up, and mirroring one into the other would hide that.
    float pos_limit_y = defaults::kPosLimitY;
    float pos_limit_y_down = defaults::kPosLimitYDown;
    float pos_limit_z = defaults::kPosLimitZ;
    float pos_limit_z_back = defaults::kPosLimitZBack;

    int vk_toggle     = defaults::kVkToggle;
    int vk_cycle_mode = defaults::kVkCycleMode;
    int vk_yaw_mode   = defaults::kVkYawMode;
    bool chord_toggle = defaults::kChord;
    bool chord_cycle_mode = defaults::kChord;
    bool chord_yaw_mode = defaults::kChord;

    bool LoadOrCreate(const char* iniPath);
};

}  // namespace OutlastHeadTracking
