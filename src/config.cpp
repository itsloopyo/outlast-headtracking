// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include "config_sanitize.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"

#include <windows.h>

#include <cmath>

namespace OutlastHeadTracking {

namespace {

// The defaults live in config.h so the writer below, the reader's fallbacks and
// Config's own member initialisers all name the same constant.
using namespace defaults;

bool FileExists(const char* path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

void WriteGeneralSection(cameraunlock::IniWriter& w) {
    w.WriteSection("General");
    w.WriteBool("EnableOnStartup", kEnableOnStartup);
    w.WriteInt("Port", kPort);
    w.WriteInt("DataFreshnessMs", kDataFreshnessMs);
    w.WriteComment(" Yaw mode: true = horizon-locked yaw (default), false = camera-local.");
    w.WriteBool("WorldSpaceYaw", kWorldSpaceYaw);
    w.WriteComment(" Keep the game window centred on its monitor. Outlast centres it once,");
    w.WriteComment(" while the splash movies play, and then resizes it for the menu without");
    w.WriteComment(" moving it, which leaves it off centre for the rest of the session. A");
    w.WriteComment(" window that fills the screen is left alone.");
    w.WriteBool("CenterWindow", kCenterWindow);
    w.WriteBlankLine();
}

void WriteViewSection(cameraunlock::IniWriter& w) {
    w.WriteSection("View");
    w.WriteComment(" Field of view in degrees, or 0 for the game's own. Outlast has no");
    w.WriteComment(" field-of-view setting; this is the mod rendering the frame at a");
    w.WriteComment(" different angle. It is applied as a ratio against the camera's");
    w.WriteComment(" unzoomed angle, so raising the camcorder and running still change the");
    w.WriteComment(" view by the same proportion they always did, and it changes the frame");
    w.WriteComment(" only - what the game reaches for, and every script that asks it the");
    w.WriteComment(" same question, keep the game's own answer.");
    w.WriteDouble("FieldOfView", kFovOverride);
    w.WriteBlankLine();
}

void WriteSmoothingSection(cameraunlock::IniWriter& w) {
    w.WriteSection("Smoothing");
    w.WriteComment(" Smoothing 0.0 (responsive) - 1.0 (heavy). Covers rotation and position.");
    w.WriteComment(" The value is picked per connection from the packet source address:");
    w.WriteComment(" LocalSmoothing for a tracker sending to 127.0.0.1 on this PC,");
    w.WriteComment(" RemoteSmoothing for a phone or other device on the network.");
    w.WriteDouble("LocalSmoothing", kLocalSmoothing);
    w.WriteDouble("RemoteSmoothing", kRemoteSmoothing);
    w.WriteBlankLine();
}

void WritePositionSection(cameraunlock::IniWriter& w) {
    w.WriteSection("Position");
    w.WriteComment(" 6DOF positional tracking. The pose is used at 1:1 - shape it in your tracker,");
    w.WriteComment(" not here. The limits below are metres of head travel, not a sensitivity.");
    w.WriteBool("Enabled", kPositionEnabled);
    w.WriteDouble("LimitX", kPosLimitX);
    w.WriteComment(" Vertical travel is clamped to [-LimitYDown, +LimitY]: how far the view");
    w.WriteComment(" may rise and how far it may drop, as separate metre budgets.");
    w.WriteDouble("LimitY", kPosLimitY);
    w.WriteDouble("LimitYDown", kPosLimitYDown);
    w.WriteDouble("LimitZ", kPosLimitZ);
    w.WriteDouble("LimitZBack", kPosLimitZBack);
    w.WriteComment(" Hold the leaned view out of walls. The limits above keep the eye");
    w.WriteComment(" inside your body; this keeps it inside the room. It asks the game's");
    w.WriteComment(" own line check where the wall is on every frame your head is off");
    w.WriteComment(" centre, and it is off until that has been watched working in a real");
    w.WriteComment(" level - turn it on and HeadTracking.log reports what it found.");
    w.WriteBool("CollisionEnabled", kCollisionEnabled);
    w.WriteComment(" How far off a surface the view is held, in the game's units (100 to");
    w.WriteComment(" the metre). Below about 10 the surface stops being drawn before the");
    w.WriteComment(" view stops moving, and you see through it anyway.");
    w.WriteDouble("CollisionMargin", kCollisionMargin);
    w.WriteComment(" Which things the check stops on, as the game's own trace mask. The");
    w.WriteComment(" default is the mask the crosshair trace uses, so it currently stops");
    w.WriteComment(" on characters as well as on walls.");
    w.WriteHex("CollisionChannel", kCollisionChannel);
    w.WriteComment(" How smoothly the lean opens back up once you step clear, 0 (instant)");
    w.WriteComment(" to 1 (slow). Closing it is always instant.");
    w.WriteDouble("CollisionReleaseSmoothing", kCollisionRelease);
    w.WriteBlankLine();
}

void WriteHotkeysSection(cameraunlock::IniWriter& w) {
    w.WriteSection("Hotkeys");
    w.WriteComment(" Virtual-key codes. Defaults: End (toggle), Page Up (cycle tracking mode), Page Down (yaw mode).");
    w.WriteComment(" Set one to 0 to leave that action unbound; its chord below still works.");
    w.WriteHex("Toggle", kVkToggle);
    w.WriteHex("CycleMode", kVkCycleMode);
    w.WriteHex("YawMode", kVkYawMode);
    w.WriteComment(" Chord alternatives: Ctrl+Shift+Y (toggle), Ctrl+Shift+G (cycle tracking mode), Ctrl+Shift+H (yaw mode).");
    w.WriteBool("ChordToggle", kChord);
    w.WriteBool("ChordCycleMode", kChord);
    w.WriteBool("ChordYawMode", kChord);
    w.WriteBlankLine();
}

void WriteDiagnosticsSection(cameraunlock::IniWriter& w) {
    w.WriteSection("Diagnostics");
    w.WriteComment(" Find the live camera record by scanning memory, and report what writes");
    w.WriteComment(" and reads it. This is a diagnostic tool: a pass walks the whole");
    w.WriteComment(" address space and sets a CPU watchpoint across every thread in the");
    w.WriteComment(" game, and the findings go to HeadTracking.log. It is the one thing here");
    w.WriteComment(" that still runs on a game build this mod does not recognise, because");
    w.WriteComment(" that is what it is for. Leave it 0 otherwise.");
    w.WriteBool("CameraProbe", kCameraProbe);
    w.WriteComment(" Reports the lights near the player, and how far each one points from");
    w.WriteComment(" where the game aims and from the view you are looking along. A");
    w.WriteComment(" diagnostic tool; leave it 0.");
    w.WriteBool("LightProbe", kLightProbe);
    w.WriteComment(" Log the reticle target, camera position and screen offset once a second.");
    w.WriteBool("AimProbe", kAimProbe);
}

// Returns false when the file could not be created, so the caller reports the real
// reason. Failing silently here surfaces one step later as "Failed to open INI",
// which reads as a corrupt file rather than a directory the game cannot write to.
bool WriteDefaultIni(const char* path) {
    cameraunlock::IniWriter w;
    if (!w.Open(path)) {
        Log::Line("ERROR: could not create %s (error %lu). The mod cannot store its "
                  "settings; check that the game directory is writable.",
                  path, GetLastError());
        return false;
    }
    w.WriteComment(" Outlast - Head Tracking configuration");
    w.WriteComment(" Lives next to OLGame.exe in Binaries/Win64/.");
    w.WriteBlankLine();
    WriteGeneralSection(w);
    WriteViewSection(w);
    WriteSmoothingSection(w);
    WritePositionSection(w);
    WriteHotkeysSection(w);
    WriteDiagnosticsSection(w);
    w.Close();
    return true;
}

// Reads a float and runs it through the boundary check for its key, warning when the
// file held something the mod will not use. One place where the read, the check and the
// report meet, so a key cannot end up reported under another key's name or written into
// the struct without having been checked at all.
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

// The four [Position] limits share one boundary check, whose NaN fallback is the key's
// own shipped default, so each of them is one line at the call site.
float ReadPositionLimit(const cameraunlock::IniReader& ini, const char* key,
                        float fallback) {
    return ReadSanitized(ini, "Position", key, fallback,
                         [fallback](float v) { return SanitizePositionLimit(v, fallback); });
}

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. The single
// smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
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

// The pose-shaping keys, retired together. The tracker owns pose shaping: sensitivity,
// deadzone and axis inversion are configured once in OpenTrack or the phone app, where
// one profile then behaves the same in every game, instead of per-game here. Silently
// ignoring a key an existing INI still sets would leave the user adjusting a number that
// does nothing, so EVERY key still present is named - not just the first one found.
//
// Protocol-to-engine sign conversion is NOT one of these and stays in the camera hook,
// where it belongs: that is a fact about UE3, not a preference. Nor is the job of
// turning metres into the engine's centimetres, which is a constant (ue3_types.h)
// rather than a key.
void WarnRetiredShapingKeys(const cameraunlock::IniReader& reader) {
    static const struct { const char* section; const char* key; } kRetired[] = {
        { "Sensitivity", "Yaw" },         { "Sensitivity", "Pitch" },
        { "Sensitivity", "Roll" },        { "Sensitivity", "InvertYaw" },
        { "Sensitivity", "InvertPitch" }, { "Sensitivity", "InvertRoll" },
        { "Smoothing",   "DeadzoneDeg" }, { "Position",    "SensitivityX" },
        { "Position",    "SensitivityY" },{ "Position",    "SensitivityZ" },
        { "Position",    "InvertX" },     { "Position",    "InvertY" },
        { "Position",    "InvertZ" },     { "Position",    "PositionScale" },
    };
    for (const auto& entry : kRetired) {
        if (reader.ReadString(entry.section, entry.key, "").empty()) {
            continue;
        }
        Log::Line("WARN: Config key [%s] %s has been retired and is IGNORED. The mod uses "
                  "the tracker's pose at 1:1 so one tracker profile behaves the same in "
                  "every game - set sensitivity, deadzones and axis inversion in "
                  "OpenTrack or your phone app instead.", entry.section, entry.key);
    }
}

// The recentre binding, retired earlier. The mod keeps no centre of its own: it applies
// whatever pose the tracker sends, the way a TrackIR driver's game support does. Two
// centres in series drift apart, because each side recentres at moments the other cannot
// see, and the player then needs two presses for one recentre. An INI that still binds a
// key here would silently bind nothing.
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

// Every key an existing INI may still set that the mod no longer reads, named once each
// so the user is not left adjusting a number that does nothing.
void WarnRetiredKeys(const cameraunlock::IniReader& ini) {
    WarnRetiredSmoothingKey(ini, "Smoothing", "Smoothing");
    WarnRetiredSmoothingKey(ini, "Position", "Smoothing");
    WarnRetiredShapingKeys(ini);
    WarnRetiredRecenterKeys(ini);
}

// GetAsyncKeyState, which the poller polls these with, is defined for virtual-key codes
// 0x01-0xFE; 0 is the poller's own "this hotkey is unbound" sentinel. Anything else is a
// typo (an extra digit, a scan code pasted in place of a VK) that reaches the poller,
// polls nothing, and leaves the user with a key that silently never fires. Report it and
// keep the shipped binding rather than shipping a dead one.
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

// The INI is read section by section, in the order the file is written, so a key added to
// one has exactly one place to be read from and one place to be written.
bool ReadGeneralSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.enabled_on_startup = ini.ReadBool("General", "EnableOnStartup", kEnableOnStartup);
    const int port = ini.ReadInt("General", "Port", kPort);
    if (port < kMinPort || port > kMaxPort) {
        Log::Line("ERROR: INI port %d out of range %d-%d", port, kMinPort, kMaxPort);
        return false;
    }
    cfg.udp_port = static_cast<uint16_t>(port);
    cfg.data_freshness_ms = ini.ReadInt("General", "DataFreshnessMs", kDataFreshnessMs);
    if (cfg.data_freshness_ms <= 0) {
        Log::Line("WARN: INI General.DataFreshnessMs %d is not a positive window; using %d",
                  cfg.data_freshness_ms, kDataFreshnessMs);
        cfg.data_freshness_ms = kDataFreshnessMs;
    }
    cfg.world_space_yaw = ini.ReadBool("General", "WorldSpaceYaw", kWorldSpaceYaw);
    cfg.center_window = ini.ReadBool("General", "CenterWindow", kCenterWindow);
    return true;
}

// Out of range is refused rather than clamped, and the game's own field of view is
// rendered. Clamping would silently hand back an angle the user did not ask for, and
// they would be left comparing a frame against a number the mod never used.
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
    // Checked on the same terms as the smoothing values above: these feed the position
    // processor and are then added straight to the view location, so one "LimitZ=nan"
    // puts a NaN in the camera's world position and the frame renders black.
    cfg.pos_limit_x = ReadPositionLimit(ini, "LimitX", kPosLimitX);
    cfg.pos_limit_y = ReadPositionLimit(ini, "LimitY", kPosLimitY);
    // Falls back to whatever LimitY resolved to, not to kPosLimitYDown: a config that
    // sets only LimitY would otherwise keep 0.20 m of downward travel while the upward
    // budget moved, with nothing in the log saying the key was half-effective.
    cfg.pos_limit_y_down = ReadPositionLimit(ini, "LimitYDown", cfg.pos_limit_y);
    cfg.pos_limit_z = ReadPositionLimit(ini, "LimitZ", kPosLimitZ);
    cfg.pos_limit_z_back = ReadPositionLimit(ini, "LimitZBack", kPosLimitZBack);

    cfg.collision_enabled = ini.ReadBool("Position", "CollisionEnabled", kCollisionEnabled);
    // Checked on the same terms as the limits: the margin is subtracted from a distance
    // and a NaN or a negative one would let the eye through the surface it is meant to
    // stop short of, which is the one thing the clamp exists to prevent.
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

bool Config::LoadOrCreate(const char* iniPath) {
    if (!FileExists(iniPath) && !WriteDefaultIni(iniPath)) {
        return false;
    }

    cameraunlock::IniReader ini;
    if (!ini.Open(iniPath)) {
        Log::Line("ERROR: Failed to open INI: %s", iniPath);
        return false;
    }

    if (!ReadGeneralSection(ini, *this)) {
        return false;
    }
    ReadViewSection(ini, *this);
    ReadSmoothingSection(ini, *this);
    ReadPositionSection(ini, *this);
    ReadHotkeysSection(ini, *this);
    ReadDiagnosticsSection(ini, *this);
    WarnRetiredKeys(ini);

    return true;
}

}  // namespace OutlastHeadTracking
