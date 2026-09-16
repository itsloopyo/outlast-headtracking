// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "ue3_rotation.h"
#include "ue3_types.h"

#include <cmath>

namespace OutlastHeadTracking {

// The parent transform as the engine hands it to a component: four rows of four floats,
// of which the rotation is the leading three of each of the first three.
struct ParentTransform {
    float m[16];
};

namespace detail {

inline float RowLength(const float* row) {
    return std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
}

// The parent's rotation with any scale divided out, so that transposing it gives its
// inverse. A row of no length is not a rotation, and the caller then leaves the light
// alone rather than composing against nonsense.
inline bool ParentRotation(const ParentTransform& parent, Mat3* out) {
    for (int row = 0; row < 3; ++row) {
        const float* source = parent.m + row * 4;
        const float length = RowLength(source);
        if (!std::isfinite(length) || length < 1e-4f) {
            return false;
        }
        for (int column = 0; column < 3; ++column) {
            out->m[row][column] = source[column] / length;
        }
    }
    return true;
}

inline Mat3 Transposed(const Mat3& source) {
    Mat3 result;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            result.m[row][column] = source.m[column][row];
        }
    }
    return result;
}

}  // namespace detail

// The rotator a light should be given so that the world direction it ends up pointing is
// the one the frame is drawn along rather than the one the game is aiming along.
//
// Row-vector convention throughout, matching FRotationMatrix: a light-local direction
// reaches the world as `v * rotation * parent`. With W the head's rotation in world terms
// (`clean * W == drawn`), the world transform wanted is `rotation * parent * W`, and the
// only field a caller may change is the light's own rotator - so what that has to become
// is `rotation * parent * W * parent^-1`.
//
// Nothing here reads the light's current direction, so it holds whatever the parent is
// doing: a pawn that has turned, a camcorder held at an angle, or a parent carrying scale.
inline bool TurnedByTheHead(const UE3Rotator& lightRotation, const ParentTransform& parent,
                            const UE3Rotator& clean, const UE3Rotator& drawn,
                            UE3Rotator* out) {
    Mat3 parentRotation;
    if (!detail::ParentRotation(parent, &parentRotation)) {
        return false;
    }
    const Mat3 head =
        MatMul(detail::Transposed(RotatorToMatrix(clean)), RotatorToMatrix(drawn));
    const Mat3 inParentFrame =
        MatMul(parentRotation, MatMul(head, detail::Transposed(parentRotation)));
    MatrixToRotator(MatMul(RotatorToMatrix(lightRotation), inParentFrame), out);
    return true;
}

}  // namespace OutlastHeadTracking
