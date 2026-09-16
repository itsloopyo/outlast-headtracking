// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"
#include "game_module.h"

namespace OutlastHeadTracking {

// Fingerprints the running OLGame.exe and picks the profile that describes it. Runs
// before a single hook is installed, and nothing that touches game memory may run until
// it has returned true.
//
// No match leaves the mod dormant: no hooks, no scans, no writes, and the game runs
// exactly as it does without the DLL. That is not caution for its own sake - hooking a
// patched build against the RVAs of an older one crashes the session within seconds,
// which the player reads as the mod having broken their game.
//
// Logs the running fingerprint, every profile it was compared against, and which way an
// unmatched build differs, so a bug report needs no follow-up round trip.
bool SelectBuildProfile(const GameModule& module);

// The matched profile. Only valid once SelectBuildProfile has returned true - which
// every caller satisfies by construction, because each of them runs from a detour that
// is only installed after it has.
const BuildProfile& ActiveProfile();

}  // namespace OutlastHeadTracking
