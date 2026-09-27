// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include "legacy_config/legacy_config.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"

#include <windows.h>

namespace OutlastHeadTracking {

namespace {

// The defaults live in config.h so the writer below and Config's own member
// initialisers name the same constant.
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

}  // namespace

bool Config::LoadOrCreate(const char* iniPath) {
    if (!FileExists(iniPath) && !WriteDefaultIni(iniPath)) {
        return false;
    }

    legacy::Config read;
    if (read.Read(iniPath).status == legacy::ReadStatus::Refused) {
        return false;
    }

    enabled_on_startup = read.enabled_on_startup;
    udp_port = read.udp_port;
    local_smoothing = read.local_smoothing;
    remote_smoothing = read.remote_smoothing;
    data_freshness_ms = read.data_freshness_ms;
    fov_override = read.fov_override;
    world_space_yaw = read.world_space_yaw;
    center_window = read.center_window;
    camera_probe = read.camera_probe;
    light_probe = read.light_probe;
    aim_probe = read.aim_probe;
    position_enabled = read.position_enabled;
    collision_enabled = read.collision_enabled;
    collision_margin = read.collision_margin;
    collision_channel = read.collision_channel;
    collision_release_smoothing = read.collision_release_smoothing;
    pos_limit_x = read.pos_limit_x;
    pos_limit_y = read.pos_limit_y;
    pos_limit_y_down = read.pos_limit_y_down;
    pos_limit_z = read.pos_limit_z;
    pos_limit_z_back = read.pos_limit_z_back;
    vk_toggle = read.vk_toggle;
    vk_cycle_mode = read.vk_cycle_mode;
    vk_yaw_mode = read.vk_yaw_mode;
    chord_toggle = read.chord_toggle;
    chord_cycle_mode = read.chord_cycle_mode;
    chord_yaw_mode = read.chord_yaw_mode;
    return true;
}

}  // namespace OutlastHeadTracking
