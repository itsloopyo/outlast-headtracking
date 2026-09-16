// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cmath>
#include <cstddef>

namespace OutlastHeadTracking {

// Recognising the frame's projection matrix inside the FSceneView the renderer returns,
// and reading the two half-field tangents out of it.
//
// Separate from the hook because it is a total function of sixteen floats: the shape
// tests and the tangent bounds decide what the mod will accept as the perspective the
// frame was drawn with, and getting them wrong means either no projection at all or a
// pair of numbers taken off some other matrix in the object. Neither is visible in game
// until something is placed in the frame with them, so they are checked here instead.

// How far into the FSceneView the search looks. UE3 puts the view and projection
// matrices near the front of the object, and a window this size costs a few hundred
// float comparisons on the one frame that runs the scan.
constexpr int kProjectionSearchBytes = 0x400;

constexpr int kProjectionMatrixFloats = 16;
constexpr int kProjectionMatrixBytes =
    kProjectionMatrixFloats * static_cast<int>(sizeof(float));

// FSceneView's fields are 4-byte aligned, so a matrix can start on any of them.
constexpr int kProjectionScanStride = 4;

// Bounds on a half-field tangent this game could have drawn a frame with. The lower one
// is below the camcorder's own limit: OLGame.ini sets CamcorderMinFOV=15, and the
// vertical tangent at that zoom is smaller again, so a bound drawn at a "reasonable"
// field of view would leave the matrix unfound on a frame the player had zoomed in on.
// The upper one covers a wide override on a very wide display, where the horizontal
// tangent grows with the display's aspect ratio.
constexpr float kMinProjectionTan = 0.05f;
constexpr float kMaxProjectionTan = 12.0f;

// One candidate matrix, as the unit the scan reads out of the game. Trivially copyable
// and exactly the width of a matrix, which is what lets every read of engine memory in
// scene_view.cpp go through a guarded copy rather than a dereference.
struct ProjectionMatrix {
    float m[kProjectionMatrixFloats];
};

namespace detail {
inline bool IsZeroEntry(float v) {
    return v == 0.0f;
}
}  // namespace detail

// A UE3 perspective projection, in row-vector form:
//
//   [ 1/tanH    0       0     0 ]
//   [   0     1/tanV    0     0 ]
//   [   j       k       c     1 ]
//   [   0       0       d     0 ]
//
// Row 2's first two entries are left free because that is where a projection jitter
// would be written. Row 2 column 3 being exactly 1 with row 3 column 3 exactly 0 is what
// makes this the matrix that divides by view depth rather than any other matrix in the
// object: the view matrix has a 1 in the last slot, and every concatenated
// view-projection has non-zero entries where this one is zero.
//
// Ordered cheapest-and-most-selective first. The tests are pure and joined by AND, so
// the order does not change which matrices are accepted - but m[11] having to be exactly
// 1.0f rejects almost every offset in one compare, and running it ahead of the
// sixteen-element finiteness sweep is what keeps the scan to a few hundred compares
// rather than a few thousand.
inline bool LooksLikeProjection(const float* m) {
    if (m[11] != 1.0f) return false;
    if (!detail::IsZeroEntry(m[1]) || !detail::IsZeroEntry(m[2]) ||
        !detail::IsZeroEntry(m[3])) {
        return false;
    }
    if (!detail::IsZeroEntry(m[4]) || !detail::IsZeroEntry(m[6]) ||
        !detail::IsZeroEntry(m[7])) {
        return false;
    }
    if (!detail::IsZeroEntry(m[12]) || !detail::IsZeroEntry(m[13]) ||
        !detail::IsZeroEntry(m[15])) {
        return false;
    }
    for (int i = 0; i < kProjectionMatrixFloats; ++i) {
        if (!std::isfinite(m[i])) {
            return false;
        }
    }
    if (m[0] <= 0.0f || m[5] <= 0.0f) return false;
    const float tanH = 1.0f / m[0];
    const float tanV = 1.0f / m[5];
    return tanH >= kMinProjectionTan && tanH <= kMaxProjectionTan &&
           tanV >= kMinProjectionTan && tanV <= kMaxProjectionTan;
}

// The two terms anything placed in the frame divides by. False when the matrix does not
// express a perspective, in which case the outputs are left alone - the same contract
// TryGetFrameProjection offers its own callers, so a bad matrix stops here rather than
// reaching one of them as an infinity.
inline bool ProjectionTangents(const float* m, float& tanHalfH, float& tanHalfV) {
    if (!std::isfinite(m[0]) || !std::isfinite(m[5]) || m[0] <= 0.0f || m[5] <= 0.0f) {
        return false;
    }
    tanHalfH = 1.0f / m[0];
    tanHalfV = 1.0f / m[5];
    return true;
}

}  // namespace OutlastHeadTracking
