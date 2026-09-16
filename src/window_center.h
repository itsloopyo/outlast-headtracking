// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace OutlastHeadTracking {

// Centres the game's window on its monitor every time the game resizes it, for the life
// of the process. Owns a thread, which is never joined - see the note in dllmain.cpp
// about what running anything on the exit path would deadlock against.
void StartWindowCentering();

}  // namespace OutlastHeadTracking
