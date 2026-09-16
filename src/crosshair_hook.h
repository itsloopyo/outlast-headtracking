// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>

namespace OutlastHeadTracking {

struct CrosshairHookTargets {
    // AOLHUD::DrawCrosshair, reached through AOLHUD::Draw's vtable slot. Its whole job is
    // the crosshair: it advances the fade, then draws two tiles - a soft outer one and
    // the dot - both at the middle of the canvas.
    std::uintptr_t drawCrosshair;
    // AHUD::Canvas, and UCanvas::ClipX / ClipY, which is the pair the function halves to
    // find that middle.
    std::size_t offHudCanvas;
    std::size_t offCanvasClipX;
    std::size_t offCanvasClipY;
};

// Moves the game's OWN crosshair to where the game is actually pointing.
//
// Outlast draws a small dot in the middle of the screen, switched on by [OLGame.OLHUD]
// bShowCrosshair. That dot marks where the player is pointing only while the rendered
// view IS the aim; once the head turns the view away, the two are different directions
// and the dot is a lie. So the mod moves it rather than drawing a second one - one mark
// on screen, the game's own, in the right place.
bool InstallCrosshairHook(const CrosshairHookTargets& targets);

}  // namespace OutlastHeadTracking
