// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "window_center.h"

#include "logging.h"
#include "window_placement.h"

#include "cameraunlock/os/game_window.h"

#include <windows.h>

namespace OutlastHeadTracking {

namespace {

// One EnumWindows and one GetWindowRect a tick, which costs nothing beside a frame, and
// is quick enough that the resize is corrected while the menu is still fading in.
constexpr DWORD kPollIntervalMs = 250;

struct Size {
    int width = 0;
    int height = 0;
};

bool operator==(const Size& a, const Size& b) {
    return a.width == b.width && a.height == b.height;
}

bool operator!=(const Size& a, const Size& b) { return !(a == b); }

WindowRect ToWindowRect(const RECT& r) {
    return WindowRect{ r.left, r.top, r.right, r.bottom };
}

void CenterOnItsMonitor(HWND hwnd, const RECT& window) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info)) {
        Log::Line("WARN: Window: the monitor the game is on could not be measured (error "
                  "%lu), so the window is left where it is.", GetLastError());
        return;
    }

    const WindowRect win = ToWindowRect(window);
    const WindowRect work = ToWindowRect(info.rcWork);
    const Placement placement = CenterOnWorkArea(win, work);
    if (!placement.move) {
        Log::Line("Window: the %dx%d window fills the %dx%d work area, so it is left where "
                  "it is.", win.Width(), win.Height(), work.Width(), work.Height());
        return;
    }

    if (!SetWindowPos(hwnd, nullptr, placement.x, placement.y, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        Log::Line("WARN: Window: the %dx%d window could not be moved (error %lu).",
                  win.Width(), win.Height(), GetLastError());
        return;
    }

    Log::Line("Window: centred the %dx%d window at (%d, %d) on the %dx%d work area.",
              win.Width(), win.Height(), placement.x, placement.y, work.Width(),
              work.Height());
}

// Outlast centres its window once, at the size the splash movies play at, and then
// resizes it for the menu without moving it - so from the menu on, the window sits off
// centre by half the difference between the two sizes. What is watched for is therefore
// the resize, not the startup: a size the window has not been placed at yet.
DWORD WINAPI WatchThread(LPVOID) {
    HWND hwnd = nullptr;
    Size settled;
    Size placed;

    for (;;) {
        Sleep(kPollIntervalMs);

        if (!IsWindow(hwnd)) {
            hwnd = cameraunlock::os::FindGameWindow();
            if (!hwnd) continue;
        }

        // A minimised window reports the size of its title-bar stub off at
        // (-32000, -32000), and SetWindowPos on one moves where it restores TO. Alt-tab
        // must not move the game's window, so a minimised game is not measured at all.
        if (IsIconic(hwnd)) continue;

        RECT rect{};
        if (!GetWindowRect(hwnd, &rect)) continue;

        const Size now{ rect.right - rect.left, rect.bottom - rect.top };
        // Two agreeing ticks before moving anything. A window caught mid-resize would
        // otherwise be centred at an intermediate size and then again at the real one,
        // which the player sees as the window jumping twice.
        if (now != settled) {
            settled = now;
            continue;
        }
        if (now == placed) continue;

        CenterOnItsMonitor(hwnd, rect);
        // Recorded whether or not the window actually moved. A size deliberately left
        // alone, or one that could not be moved, must not be retried four times a second
        // with a log line each time.
        placed = now;
    }
}

}  // namespace

void StartWindowCentering() {
    const HANDLE thread = CreateThread(nullptr, 0, &WatchThread, nullptr, 0, nullptr);
    if (!thread) {
        Log::Line("ERROR: Window: the window watcher could not start (error %lu). The "
                  "window is left wherever the game puts it.", GetLastError());
        return;
    }
    CloseHandle(thread);
}

}  // namespace OutlastHeadTracking
