// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

// The config reader of the last build that read HeadTracking.ini in its pre-canonical
// layout, frozen so a player updating from any older build is converted exactly as that
// build read the file. Nothing in this folder is ever edited. Three things differ from the
// reader it was taken from: it fills this frozen copy of that build's Config and defaults
// rather than the runtime type, it never writes the file (a missing file reads as the
// defaults, which is what the old reader read from the file it created there), and it
// reports a refusal apart from an absent file. The sanitizers it called are copied beside
// it in config_sanitize.h, and the core default values it named are written out here.

#include "cameraunlock/config/legacy_import.h"

#include <cstdint>
#include <string>
#include <vector>

namespace OutlastHeadTracking::legacy {

enum class ReadStatus {
    Read,
    // No file at the path. Config holds the defaults.
    Absent,
    // The old reader returned false and the mod stayed dormant.
    Refused,
};

struct ReadResult {
    ReadStatus status = ReadStatus::Read;
    // For Refused, what the old reader logged.
    std::string reason;
};

struct Config {
    bool enabled_on_startup = true;
    std::uint16_t udp_port = 4242;

    float local_smoothing = static_cast<float>(0.0);
    float remote_smoothing = static_cast<float>(0.15);

    int data_freshness_ms = 500;

    float fov_override = 0.0f;

    bool world_space_yaw = true;

    bool center_window = true;

    bool camera_probe = false;
    bool light_probe = false;
    bool aim_probe = false;

    bool position_enabled = true;

    bool  collision_enabled = false;
    float collision_margin = 15.0f;
    int   collision_channel = 0x20BF;
    float collision_release_smoothing = 0.9f;
    float pos_limit_x = 0.30f;
    float pos_limit_y = 0.20f;
    float pos_limit_y_down = 0.20f;
    float pos_limit_z = 0.40f;
    float pos_limit_z_back = 0.10f;

    int vk_toggle     = 0x23; // VK_END
    int vk_cycle_mode = 0x21; // VK_PRIOR (Page Up)
    int vk_yaw_mode   = 0x22; // VK_NEXT (Page Down)
    bool chord_toggle = true;
    bool chord_cycle_mode = true;
    bool chord_yaw_mode = true;

    // Call on a default-constructed Config.
    ReadResult Read(const char* iniPath);
};

// Every section and key Read reads, in the order it reads them.
std::vector<cameraunlock::config::LegacyKey> ReadKeys();

}  // namespace OutlastHeadTracking::legacy
