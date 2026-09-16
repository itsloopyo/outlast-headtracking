// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "hotkeys.h"

#include "logging.h"

#include "cameraunlock/input/chord_hotkeys.h"

#include <cstdio>
#include <exception>

namespace OutlastHeadTracking {

namespace {

// ~60Hz, which is what a key press has to be sampled at to be caught between a player's
// press and release. It is also HotkeyPoller's own default; stated here so the rate the
// mod relies on is visible at the call site rather than in another repository.
constexpr int kPollIntervalMs = 16;

// Room for "0x" and two hex digits, or the word "unbound", and the terminator.
constexpr int kKeyNameChars = 8;

}  // namespace

bool Hotkeys::Start(const Config& cfg, Action onToggle, Action onCycleMode,
                    Action onYawMode) {
    using cameraunlock::input::ChordGuarded;
    using cameraunlock::input::NavGuarded;

    // Nav-cluster keys are suppressed while Ctrl+Shift is held so the chord
    // path is the sole trigger for Ctrl+Shift+<nav> combos - a single keypress
    // never fires an action twice.
    m_poller.SetToggleKey(cfg.vk_toggle, NavGuarded(onToggle));
    m_poller.AddHotkey(cfg.vk_cycle_mode, NavGuarded(onCycleMode));
    m_poller.AddHotkey(cfg.vk_yaw_mode, NavGuarded(onYawMode));

    // Chord alternatives (Ctrl+Shift+Y / Ctrl+Shift+G / Ctrl+Shift+H) on the same
    // poller; ChordGuarded gates each action on the modifier state.
    if (cfg.chord_toggle)     m_poller.AddHotkey('Y', ChordGuarded(std::move(onToggle)));
    if (cfg.chord_cycle_mode) m_poller.AddHotkey('G', ChordGuarded(std::move(onCycleMode)));
    if (cfg.chord_yaw_mode)   m_poller.AddHotkey('H', ChordGuarded(std::move(onYawMode)));

    // The poller rethrows rather than failing silently when the thread cannot be
    // created. This entry point is reached from a __stdcall thread procedure, where an
    // escaping exception is std::terminate: the game would vanish during startup with
    // the log ending on an unrelated line. Caught here so the reason is written down and
    // the session carries on with tracking but no hotkeys - which is all a caller could
    // do about it, so the false is the reason in the log rather than a branch.
    try {
        if (!m_poller.Start(kPollIntervalMs)) {
            Log::Line("ERROR: HotkeyPoller failed to start");
            return false;
        }
    } catch (const std::exception& e) {
        Log::Line("ERROR: HotkeyPoller failed to start: %s", e.what());
        return false;
    }

    // 0 is the poller's unbind sentinel, so a key set to 0 in the INI never fires. Printed
    // as "unbound" rather than as 0x00, which reads like a successful binding and is
    // indistinguishable from a broken poller.
    const auto keyName = [](unsigned key, char* out) -> const char* {
        if (key == 0) {
            return "unbound";
        }
        std::snprintf(out, kKeyNameChars, "0x%02X", key);
        return out;
    };
    char toggle[kKeyNameChars], cycle[kKeyNameChars], yaw[kKeyNameChars];
    Log::Line("Hotkeys: toggle=%s cyclemode=%s yawmode=%s",
              keyName(cfg.vk_toggle, toggle), keyName(cfg.vk_cycle_mode, cycle),
              keyName(cfg.vk_yaw_mode, yaw));

    return true;
}

}  // namespace OutlastHeadTracking
