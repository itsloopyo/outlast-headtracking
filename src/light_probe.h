// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace OutlastHeadTracking {

// Reports the lights the player is carrying, once a second: where each one is, which way
// it points, and how far that is from both the direction the game is aiming and the
// direction the frame is drawn along. Those two angles are how the camcorder's light is
// known to be following the head rather than the mouse.
//
// Diagnostic only, and off in every shipped INI: it walks game objects every second and
// arms a CPU watchpoint once, and it exists for the session that has to find the light
// again on a build whose addresses have moved.
void StartLightProbe();

}  // namespace OutlastHeadTracking
