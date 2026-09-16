// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace OutlastHeadTracking {

// A window and a monitor as the placement arithmetic sees them, so the decision below
// is a pure function and can be checked without a window to move.
struct WindowRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
};

struct Placement {
    // false when the window is at least as large as the work area in either direction,
    // which is what fullscreen and borderless look like from here: there is no middle
    // to move them to, and moving them would put part of the frame off the screen.
    bool move = false;
    int x = 0;
    int y = 0;
};

// The work area, not the monitor bounds. A taskbar docked to the top or the left moves
// where the middle of the usable screen is, and a window centred against the full
// monitor puts its title bar under that taskbar, where it cannot be dragged back out.
inline Placement CenterOnWorkArea(const WindowRect& window, const WindowRect& work) {
    Placement placement;
    if (window.Width() >= work.Width() || window.Height() >= work.Height()) {
        return placement;
    }
    placement.move = true;
    placement.x = work.left + (work.Width() - window.Width()) / 2;
    placement.y = work.top + (work.Height() - window.Height()) / 2;
    return placement;
}

}  // namespace OutlastHeadTracking
