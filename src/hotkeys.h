// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"

#include "cameraunlock/input/hotkey_poller.h"

#include <functional>

namespace OutlastHeadTracking {

class Hotkeys {
public:
    using Action = std::function<void()>;

    // Deliberately not destructible, on the same terms as TrackingRuntime. This owns a
    // polling thread and destroying it joins that thread; from a namespace-scope object
    // the join would run in the CRT's static teardown during DLL_PROCESS_DETACH, under
    // the loader lock, against a thread sitting in a Win32 call that takes it. The
    // player's game then hangs on quit. The one instance is heap-allocated and never
    // deleted, and deleting the destructor is what keeps that a compile error rather
    // than something the next edit rediscovers in game.
    ~Hotkeys() = delete;

    bool Start(const Config& cfg, Action onToggle, Action onCycleMode, Action onYawMode);

private:
    cameraunlock::input::HotkeyPoller m_poller;
};

}  // namespace OutlastHeadTracking
