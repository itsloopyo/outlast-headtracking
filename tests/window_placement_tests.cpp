// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Where the game's window is put when the game resizes it. Pure arithmetic on a window
// rect and a monitor's work area, which is the half of the centring that can be wrong
// without anyone noticing on the machine it was written on: a taskbar on one edge, a
// monitor that does not start at the origin, and a window too large to centre at all.

#include "test_support.h"

#include "window_placement.h"

#include <iostream>

namespace {

using namespace OutlastHeadTracking;
using olht_tests::Check;

// The measured Outlast case: one 5120x1440 screen with a 48px taskbar along the bottom,
// the 1926x1109 window the game resizes to when it leaves the splash movies, and the
// 1286x749 one it played them in.
const WindowRect kWideWork{ 0, 0, 5120, 1392 };

void CentresOnTheWorkArea() {
    const Placement placement = CenterOnWorkArea(WindowRect{ 1920, 283, 3846, 1392 }, kWideWork);
    Check(placement.move, "a window smaller than the work area is moved");
    Check(placement.x == (5120 - 1926) / 2, "it is centred horizontally on the work area");
    Check(placement.y == (1392 - 1109) / 2, "and vertically, against the work area, not the screen");
}

void TheSplashSizeIsCentredToo() {
    const Placement placement = CenterOnWorkArea(WindowRect{ 1920, 360, 3206, 1109 }, kWideWork);
    Check(placement.x == (5120 - 1286) / 2, "the splash-sized window centres on the same middle");
    Check(placement.y == (1392 - 749) / 2, "and sits above the taskbar rather than under it");
}

void AMonitorAwayFromTheOriginKeepsItsOffset() {
    // A second screen to the right of a 1920-wide primary, with a taskbar down its left
    // edge: both the offset and the inset have to survive into the answer.
    const WindowRect work{ 1980, 0, 3840, 1080 };
    const Placement placement = CenterOnWorkArea(WindowRect{ 0, 0, 1280, 720 }, work);
    Check(placement.x == 1980 + (1860 - 1280) / 2, "the work area's left edge is the origin, not the desktop's");
    Check(placement.y == (1080 - 720) / 2, "and its top edge likewise");
}

void AWindowThatFillsTheScreenIsLeftAlone() {
    Check(!CenterOnWorkArea(WindowRect{ 0, 0, 5120, 1440 }, kWideWork).move,
          "a fullscreen window is taller than the work area, so it is not moved");
    Check(!CenterOnWorkArea(WindowRect{ 0, 0, 5120, 1000 }, kWideWork).move,
          "nor is one exactly as wide as the work area");
    Check(CenterOnWorkArea(WindowRect{ 0, 0, 5119, 1391 }, kWideWork).move,
          "one pixel smaller in both directions is still centred");
}

}  // namespace

int RunWindowPlacementTests() {
    std::cout << "Window placement\n";
    CentresOnTheWorkArea();
    TheSplashSizeIsCentredToo();
    AMonitorAwayFromTheOriginKeepsItsOffset();
    AWindowThatFillsTheScreenIsLeftAlone();
    return olht_tests::TakeFailures();
}
