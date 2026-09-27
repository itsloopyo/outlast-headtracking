// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include "legacy_config/legacy_config.h"
#include "path_utils.h"

#include "cameraunlock/config/head_tracking_config_table.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace OutlastHeadTracking {

namespace {

using cameraunlock::config::DroppedValue;
using cameraunlock::config::ImportResult;
using cameraunlock::config::LegacyInput;
using cameraunlock::input::KeyModifiers;

constexpr const char* kFovExpectation = "0, or an angle from 20 to 170";

// A legacy hotkey code and its Ctrl+Shift chord switch as one key list: the code's binding,
// then the chord.
std::string KeyList(int vk, bool chord, char letter, const char* key, std::vector<DroppedValue>& dropped) {
    std::string list = cameraunlock::config::LegacyVirtualKeyToBindings(vk, "Hotkeys", key, dropped);
    if (chord) {
        const std::string chordKey =
            cameraunlock::input::FormatKeyBindings({{KeyModifiers::kCtrl | KeyModifiers::kShift, letter}});
        list += (list.empty() ? "" : ", ") + chordKey;
    }
    return list;
}

ImportResult Import(const LegacyInput& input, Config& out) {
    // The published build opened the file by the ANSI path it built itself, not the one the
    // owner derives, so the import builds it the same way.
    const std::string ansiPath = LegacyAnsiPath(input.path);
    if (ansiPath.empty()) {
        return ImportResult::Refused(
            "its folder has no ANSI or short-name spelling, so the version that wrote this file could not "
            "open it and did not start");
    }

    legacy::Config c;
    const legacy::ReadResult read = c.Read(ansiPath.c_str());
    if (read.status == legacy::ReadStatus::Refused) {
        return ImportResult::Refused(read.reason);
    }

    std::vector<DroppedValue> dropped;

    out.enable_on_startup = c.enabled_on_startup;
    out.udp_port = c.udp_port;
    out.data_freshness_ms = c.data_freshness_ms;
    out.world_space_yaw = c.world_space_yaw;

    // [Position] Enabled chose only the startup mode: the cycle key reached every mode either
    // way.
    const cameraunlock::TrackingModeChannels mode = cameraunlock::EncodeTrackingMode(
        c.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                           : cameraunlock::TrackingMode::RotationOnly);
    out.rotation_enabled = mode.rotation_enabled;
    out.position_enabled = mode.position_enabled;

    out.local_smoothing = c.local_smoothing;
    out.position.local_smoothing = c.local_smoothing;
    out.remote_smoothing = c.remote_smoothing;
    out.position.remote_smoothing = c.remote_smoothing;

    out.position.limit_x = c.pos_limit_x;
    out.position.limit_y = c.pos_limit_y;
    out.position.limit_y_down = c.pos_limit_y_down;
    out.position.limit_z = c.pos_limit_z;
    out.position.limit_z_back = c.pos_limit_z_back;

    out.collision_enabled = c.collision_enabled;
    out.lean_clamp.skin = c.collision_margin;
    out.lean_clamp.release_smoothing = c.collision_release_smoothing;
    out.collision_channel = c.collision_channel;

    out.fov_override = c.fov_override;
    out.center_window = c.center_window;
    out.camera_probe = c.camera_probe;
    out.light_probe = c.light_probe;
    out.aim_probe = c.aim_probe;

    out.toggle_key_name = KeyList(c.vk_toggle, c.chord_toggle, 'Y', "Toggle", dropped);
    out.cycle_tracking_mode_key_name = KeyList(c.vk_cycle_mode, c.chord_cycle_mode, 'G', "CycleMode", dropped);
    out.yaw_mode_key_name = KeyList(c.vk_yaw_mode, c.chord_yaw_mode, 'H', "YawMode", dropped);

    // A setting the player never changed from what the published build shipped follows
    // Defaults.ini, each hotkey its code and its chord switch together.
    using cameraunlock::config::schema::Concept;
    const legacy::Config shipped;
    cameraunlock::config::LegacyFollowsDefaultsIni follows;
    follows.Setting(Concept::UdpPort, c.udp_port, shipped.udp_port);
    follows.Setting(Concept::EnableOnStartup, c.enabled_on_startup, shipped.enabled_on_startup);
    follows.Setting(Concept::DataFreshnessMs, c.data_freshness_ms, shipped.data_freshness_ms);
    follows.Setting(Concept::WorldSpaceYaw, c.world_space_yaw, shipped.world_space_yaw);
    follows.TrackingMode(c.position_enabled, shipped.position_enabled);
    follows.Setting(Concept::LocalSmoothing, c.local_smoothing, shipped.local_smoothing);
    follows.Setting(Concept::RemoteSmoothing, c.remote_smoothing, shipped.remote_smoothing);
    follows.Setting(Concept::PositionLimitX, c.pos_limit_x, shipped.pos_limit_x);
    follows.Setting(Concept::PositionLimitY, c.pos_limit_y, shipped.pos_limit_y);
    follows.Setting(Concept::PositionLimitYDown, c.pos_limit_y_down, shipped.pos_limit_y_down);
    follows.Setting(Concept::PositionLimitZ, c.pos_limit_z, shipped.pos_limit_z);
    follows.Setting(Concept::PositionLimitZBack, c.pos_limit_z_back, shipped.pos_limit_z_back);
    follows.Setting(Concept::CollisionEnabled, c.collision_enabled, shipped.collision_enabled);
    follows.Setting(Concept::CollisionReleaseSmoothing, c.collision_release_smoothing,
                    shipped.collision_release_smoothing);
    follows.Setting(Concept::ToggleKey, c.vk_toggle == shipped.vk_toggle && c.chord_toggle == shipped.chord_toggle);
    follows.Setting(Concept::CycleTrackingModeKey,
                    c.vk_cycle_mode == shipped.vk_cycle_mode && c.chord_cycle_mode == shipped.chord_cycle_mode);
    follows.Setting(Concept::YawModeKey, c.vk_yaw_mode == shipped.vk_yaw_mode && c.chord_yaw_mode == shipped.chord_yaw_mode);

    return read.status == legacy::ReadStatus::Absent
               ? ImportResult::Absent(std::move(dropped), {}, follows.Concepts())
               : ImportResult::Imported(std::move(dropped), {}, follows.Concepts());
}

}  // namespace

cameraunlock::config::CodecParseResult<float> FovCodec::Parse(std::string_view text) const {
    cameraunlock::config::CodecParseResult<float> read = angle_.Parse(text);
    if (read.ok() && read.value != 0.0f && read.value < defaults::kMinFovOverride) {
        return {0.0f, kFovExpectation};
    }
    if (!read.ok()) read.error = kFovExpectation;
    return read;
}

std::string FovCodec::Render(float value) const {
    if (value != 0.0f && value < defaults::kMinFovOverride) {
        throw std::invalid_argument("[View] FieldOfView " + std::to_string(value) + " is neither 0 nor 20 to 170");
    }
    return angle_.Render(value);
}

cameraunlock::config::ConfigTable<Config> MakeConfigTable() {
    using cameraunlock::config::schema::Concept;
    cameraunlock::config::ConfigTable<Config> table = cameraunlock::config::HeadTrackingConfigTable<Config>(
        {Concept::UdpPort, Concept::EnableOnStartup, Concept::DataFreshnessMs, Concept::WorldSpaceYaw,
         Concept::RotationEnabled, Concept::LocalSmoothing, Concept::RemoteSmoothing, Concept::PositionEnabled,
         Concept::PositionLimitX, Concept::PositionLimitY, Concept::PositionLimitYDown, Concept::PositionLimitZ,
         Concept::PositionLimitZBack, Concept::CollisionEnabled, Concept::CollisionMargin,
         Concept::CollisionChannel, Concept::CollisionReleaseSmoothing, Concept::ToggleKey,
         Concept::CycleTrackingModeKey, Concept::YawModeKey});
    table.Select(Concept::WorldSpaceYaw).Writable()
        .Select(Concept::RotationEnabled).Writable()
        .Select(Concept::PositionEnabled).Writable();
    table.Select(Concept::CollisionMargin)
        .Comment("How far, in centimetres, the view is held off a wall when you lean into it.\n"
                 "Below about 10 the wall stops being drawn before the view stops moving.");
    table.Select(Concept::CollisionChannel)
        .Comment("The game's own trace mask the wall check uses. 8383 is 0x20BF, the mask\n"
                 "the crosshair trace uses, so the check also stops on characters.");
    table.Local("General", "CenterWindow", &Config::center_window, cameraunlock::config::BoolCodec(),
                "Keep the game window centred on its monitor. Outlast centres it once, while the\n"
                "splash movies play, then resizes it for the menu without moving it. A window\n"
                "that fills the screen is left alone.");
    table.Local("View", "FieldOfView", &Config::fov_override, FovCodec{},
                "Field of view in degrees, 0 or 20 to 170. 0 keeps the game's own. Outlast has no\n"
                "field-of-view setting; this is the mod rendering the frame at a different angle,\n"
                "as a ratio against the camera's unzoomed angle, so raising the camcorder and\n"
                "running still change the view by the same proportion.");
    table.Local("Diagnostics", "CameraProbe", &Config::camera_probe, cameraunlock::config::BoolCodec(),
                "Find the live camera record by scanning memory and report what writes and reads\n"
                "it to HeadTracking.log. A diagnostic tool that sets a CPU watchpoint in every\n"
                "game thread. Leave it false.");
    table.Local("Diagnostics", "LightProbe", &Config::light_probe, cameraunlock::config::BoolCodec(),
                "Report the lights near the player to HeadTracking.log. A diagnostic tool; leave\n"
                "it false.");
    table.Local("Diagnostics", "AimProbe", &Config::aim_probe, cameraunlock::config::BoolCodec(),
                "Log the reticle target, camera position and screen offset once a second.\n"
                "A diagnostic tool; leave it false.");
    return table;
}

cameraunlock::config::LegacyImport<Config> MakeLegacyImport() {
    return {&Import, legacy::ReadKeys()};
}

cameraunlock::config::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(const std::wstring& folder,
                                                                        cameraunlock::config::DefaultsFile defaults) {
    const auto wide = [](const char* name) { return std::wstring(name, name + std::char_traits<char>::length(name)); };
    cameraunlock::config::ConfigOwnerOptions<Config> options;
    options.path = folder + wide(kConfigFileName);
    options.legacy_path = folder + wide(kLegacyConfigFileName);
    options.table = MakeConfigTable();
    options.import = MakeLegacyImport();
    options.header.display_name = kConfigDisplayName;
    options.defaults = std::move(defaults);
    return options;
}

}  // namespace OutlastHeadTracking
