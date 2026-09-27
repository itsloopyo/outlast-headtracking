// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "legacy_config/legacy_config.h"

#include "legacy_config/config_sanitize.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"

#include <windows.h>

#include <cmath>
#include <cstdio>

namespace OutlastHeadTracking::legacy {

namespace {

constexpr bool  kEnableOnStartup = true;
constexpr int   kPort            = 4242;
constexpr int   kMinPort         = 1024;
constexpr int   kMaxPort         = 65535;
constexpr int   kDataFreshnessMs = 500;
constexpr bool  kWorldSpaceYaw   = true;
constexpr bool  kCenterWindow    = true;
constexpr float kFovOverride     = 0.0f;
constexpr float kMinFovOverride  = 20.0f;
constexpr float kMaxFovOverride  = 170.0f;
constexpr float kLocalSmoothing  = static_cast<float>(0.0);
constexpr float kRemoteSmoothing = static_cast<float>(0.15);
constexpr int   kVkToggle        = 0x23; // VK_END
constexpr int   kVkCycleMode     = 0x21; // VK_PRIOR (Page Up)
constexpr int   kVkYawMode       = 0x22; // VK_NEXT (Page Down)
constexpr bool  kChord           = true;
constexpr bool  kCameraProbe     = false;
constexpr bool  kLightProbe      = false;
constexpr bool  kAimProbe        = false;
constexpr bool  kPositionEnabled = true;
constexpr float kPosLimitX       = 0.30f;
constexpr float kPosLimitY       = 0.20f;
constexpr float kPosLimitZ       = 0.40f;
constexpr float kPosLimitZBack   = 0.10f;
constexpr bool  kCollisionEnabled = false;
constexpr float kCollisionMargin = 15.0f;
constexpr int   kCollisionChannel = 0x20BF;
constexpr float kCollisionRelease = 0.9f;

bool FileExists(const char* path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

template <typename Sanitizer>
float ReadSanitized(const cameraunlock::IniReader& ini, const char* section,
                    const char* key, float fallback, Sanitizer clean) {
    const float raw = ini.ReadFloat(section, key, fallback);
    const float value = clean(raw);
    if (raw != value) {
        Log::Line("WARN: INI %s.%s value %.4f out of range or non-finite; using %.4f",
                  section, key, raw, value);
    }
    return value;
}

float ReadPositionLimit(const cameraunlock::IniReader& ini, const char* key,
                        float fallback) {
    return ReadSanitized(ini, "Position", key, fallback,
                         [fallback](float v) { return SanitizePositionLimit(v, fallback); });
}

void WarnRetiredSmoothingKey(const cameraunlock::IniReader& reader,
                             const char* section, const char* key) {
    if (reader.ReadString(section, key, "").empty()) return;
    Log::Line(
        "WARN: Config key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

const struct { const char* section; const char* key; } kRetiredShaping[] = {
    { "Sensitivity", "Yaw" },         { "Sensitivity", "Pitch" },
    { "Sensitivity", "Roll" },        { "Sensitivity", "InvertYaw" },
    { "Sensitivity", "InvertPitch" }, { "Sensitivity", "InvertRoll" },
    { "Smoothing",   "DeadzoneDeg" }, { "Position",    "SensitivityX" },
    { "Position",    "SensitivityY" },{ "Position",    "SensitivityZ" },
    { "Position",    "InvertX" },     { "Position",    "InvertY" },
    { "Position",    "InvertZ" },     { "Position",    "PositionScale" },
};

void WarnRetiredShapingKeys(const cameraunlock::IniReader& reader) {
    for (const auto& entry : kRetiredShaping) {
        if (reader.ReadString(entry.section, entry.key, "").empty()) {
            continue;
        }
        Log::Line("WARN: Config key [%s] %s has been retired and is IGNORED. The mod uses "
                  "the tracker's pose at 1:1 so one tracker profile behaves the same in "
                  "every game - set sensitivity, deadzones and axis inversion in "
                  "OpenTrack or your phone app instead.", entry.section, entry.key);
    }
}

void WarnRetiredRecenterKeys(const cameraunlock::IniReader& reader) {
    for (const char* key : { "Recenter", "ChordRecenter" }) {
        if (reader.ReadString("Hotkeys", key, "").empty()) {
            continue;
        }
        Log::Line("WARN: Config key [Hotkeys] %s has been retired and is IGNORED. The mod "
                  "keeps no centre of its own, so there is nothing for it to bind - "
                  "recentre in your tracker instead (opentrack's Center bind, or the "
                  "CENTER button in a phone app).", key);
    }
}

void WarnRetiredKeys(const cameraunlock::IniReader& ini) {
    WarnRetiredSmoothingKey(ini, "Smoothing", "Smoothing");
    WarnRetiredSmoothingKey(ini, "Position", "Smoothing");
    WarnRetiredShapingKeys(ini);
    WarnRetiredRecenterKeys(ini);
}

constexpr int kMaxVirtualKey = 0xFE;

int ReadVirtualKey(const cameraunlock::IniReader& ini, const char* key, int fallback) {
    const int vk = ini.ReadHex("Hotkeys", key, fallback);
    if (vk < 0 || vk > kMaxVirtualKey) {
        Log::Line("WARN: INI Hotkeys.%s value 0x%X is not a virtual-key code (0x01-0xFE, "
                  "or 0 to unbind); using 0x%02X", key, vk, fallback);
        return fallback;
    }
    return vk;
}

// Returns the refusal the old reader logged, or an empty string.
std::string ReadGeneralSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.enabled_on_startup = ini.ReadBool("General", "EnableOnStartup", kEnableOnStartup);
    const int port = ini.ReadInt("General", "Port", kPort);
    if (port < kMinPort || port > kMaxPort) {
        char reason[96];
        std::snprintf(reason, sizeof reason, "INI port %d out of range %d-%d", port, kMinPort, kMaxPort);
        Log::Line("ERROR: %s", reason);
        return reason;
    }
    cfg.udp_port = static_cast<std::uint16_t>(port);
    cfg.data_freshness_ms = ini.ReadInt("General", "DataFreshnessMs", kDataFreshnessMs);
    if (cfg.data_freshness_ms <= 0) {
        Log::Line("WARN: INI General.DataFreshnessMs %d is not a positive window; using %d",
                  cfg.data_freshness_ms, kDataFreshnessMs);
        cfg.data_freshness_ms = kDataFreshnessMs;
    }
    cfg.world_space_yaw = ini.ReadBool("General", "WorldSpaceYaw", kWorldSpaceYaw);
    cfg.center_window = ini.ReadBool("General", "CenterWindow", kCenterWindow);
    return {};
}

void ReadViewSection(const cameraunlock::IniReader& ini, Config& cfg) {
    const float requested = ini.ReadFloat("View", "FieldOfView", kFovOverride);
    if (!std::isfinite(requested) || requested < 0.0f) {
        Log::Line("WARN: INI View.FieldOfView value %.4f is not an angle; rendering the "
                  "game's own field of view", requested);
        return;
    }
    if (requested == 0.0f) {
        return;
    }
    if (requested < kMinFovOverride || requested > kMaxFovOverride) {
        Log::Line("WARN: INI View.FieldOfView %.1f is outside %.0f-%.0f degrees; "
                  "rendering the game's own field of view", requested, kMinFovOverride,
                  kMaxFovOverride);
        return;
    }
    cfg.fov_override = requested;
}

void ReadSmoothingSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.local_smoothing = ReadSanitized(
        ini, "Smoothing", "LocalSmoothing", kLocalSmoothing,
        [](float v) { return SanitizeSmoothing(v, kLocalSmoothing); });
    cfg.remote_smoothing = ReadSanitized(
        ini, "Smoothing", "RemoteSmoothing", kRemoteSmoothing,
        [](float v) { return SanitizeSmoothing(v, kRemoteSmoothing); });
}

void ReadPositionSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.position_enabled = ini.ReadBool("Position", "Enabled", kPositionEnabled);
    cfg.pos_limit_x = ReadPositionLimit(ini, "LimitX", kPosLimitX);
    cfg.pos_limit_y = ReadPositionLimit(ini, "LimitY", kPosLimitY);
    cfg.pos_limit_y_down = ReadPositionLimit(ini, "LimitYDown", cfg.pos_limit_y);
    cfg.pos_limit_z = ReadPositionLimit(ini, "LimitZ", kPosLimitZ);
    cfg.pos_limit_z_back = ReadPositionLimit(ini, "LimitZBack", kPosLimitZBack);

    cfg.collision_enabled = ini.ReadBool("Position", "CollisionEnabled", kCollisionEnabled);
    cfg.collision_margin = ReadSanitized(
        ini, "Position", "CollisionMargin", kCollisionMargin,
        [](float v) { return SanitizePositionLimit(v, kCollisionMargin); });
    cfg.collision_release_smoothing = ReadSanitized(
        ini, "Position", "CollisionReleaseSmoothing", kCollisionRelease,
        [](float v) { return SanitizeSmoothing(v, kCollisionRelease); });
    cfg.collision_channel = ini.ReadHex("Position", "CollisionChannel", kCollisionChannel);
}

void ReadHotkeysSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.vk_toggle     = ReadVirtualKey(ini, "Toggle",    kVkToggle);
    cfg.vk_cycle_mode = ReadVirtualKey(ini, "CycleMode", kVkCycleMode);
    cfg.vk_yaw_mode   = ReadVirtualKey(ini, "YawMode",   kVkYawMode);
    cfg.chord_toggle     = ini.ReadBool("Hotkeys", "ChordToggle",    kChord);
    cfg.chord_cycle_mode = ini.ReadBool("Hotkeys", "ChordCycleMode", kChord);
    cfg.chord_yaw_mode   = ini.ReadBool("Hotkeys", "ChordYawMode",   kChord);
}

void ReadDiagnosticsSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.camera_probe = ini.ReadBool("Diagnostics", "CameraProbe", kCameraProbe);
    cfg.light_probe = ini.ReadBool("Diagnostics", "LightProbe", kLightProbe);
    cfg.aim_probe = ini.ReadBool("Diagnostics", "AimProbe", kAimProbe);
}

}  // namespace

ReadResult Config::Read(const char* iniPath) {
    if (!FileExists(iniPath)) {
        return {ReadStatus::Absent, {}};
    }

    cameraunlock::IniReader ini;
    if (!ini.Open(iniPath)) {
        Log::Line("ERROR: Failed to open INI: %s", iniPath);
        return {ReadStatus::Refused, "the file could not be opened"};
    }

    const std::string refused = ReadGeneralSection(ini, *this);
    if (!refused.empty()) {
        return {ReadStatus::Refused, refused};
    }
    ReadViewSection(ini, *this);
    ReadSmoothingSection(ini, *this);
    ReadPositionSection(ini, *this);
    ReadHotkeysSection(ini, *this);
    ReadDiagnosticsSection(ini, *this);
    WarnRetiredKeys(ini);

    return {ReadStatus::Read, {}};
}

std::vector<cameraunlock::config::LegacyKey> ReadKeys() {
    std::vector<cameraunlock::config::LegacyKey> keys = {
        {"General", "EnableOnStartup"},
        {"General", "Port"},
        {"General", "DataFreshnessMs"},
        {"General", "WorldSpaceYaw"},
        {"General", "CenterWindow"},
        {"View", "FieldOfView"},
        {"Smoothing", "LocalSmoothing"},
        {"Smoothing", "RemoteSmoothing"},
        {"Position", "Enabled"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitYDown"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Position", "CollisionEnabled"},
        {"Position", "CollisionMargin"},
        {"Position", "CollisionReleaseSmoothing"},
        {"Position", "CollisionChannel"},
        {"Hotkeys", "Toggle"},
        {"Hotkeys", "CycleMode"},
        {"Hotkeys", "YawMode"},
        {"Hotkeys", "ChordToggle"},
        {"Hotkeys", "ChordCycleMode"},
        {"Hotkeys", "ChordYawMode"},
        {"Diagnostics", "CameraProbe"},
        {"Diagnostics", "LightProbe"},
        {"Diagnostics", "AimProbe"},
        {"Smoothing", "Smoothing"},
        {"Position", "Smoothing"},
    };
    for (const auto& entry : kRetiredShaping) {
        keys.push_back({entry.section, entry.key});
    }
    keys.push_back({"Hotkeys", "Recenter"});
    keys.push_back({"Hotkeys", "ChordRecenter"});
    return keys;
}

}  // namespace OutlastHeadTracking::legacy
