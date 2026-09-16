// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

namespace OutlastHeadTracking {

// Detours ULocalPlayer::CalcSceneView and reads the projection matrix out of the
// FSceneView it returns, publishing the two half-field tangents the frame was really
// drawn with through GetFrameProjection().
//
// This is where the mod's field of view comes from. Not from the angle in degrees the
// accessor returns, and not from the camera cache's TPOV.FOV: the same angle produces a
// different frame depending on whether the camera constrains its aspect ratio and what
// the display's is, and only the matrix states which. It also keeps stating it once a
// field-of-view override has moved the angle, so there is no second copy of the
// conversion to keep in step.
//
// Reads only. Nothing about the frame is modified here - but the detour does publish
// which of CalcSceneView's three callers it is running for (render_frame.h), which is
// what lets the camera hook, further down the same call, tell the frame apart from a
// Project or Deproject that script asked for.
//
// `drawSceneViewReturn` is the instruction UGameViewportClient::Draw returns to after its
// own call, and is what that test compares against.
bool InstallSceneViewHook(std::uintptr_t calcSceneViewAddr,
                          std::uintptr_t drawSceneViewReturn);

}  // namespace OutlastHeadTracking
