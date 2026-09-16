// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "ue3_rotation.h"
#include "ue3_types.h"

#include "cameraunlock/rendering/aim_ndc_projection.h"

namespace OutlastHeadTracking {

enum class AimProjection {
    Ok,
    // The frame's projection matrix has not been found, so there are no half-field
    // tangents to divide by. Set by the caller, which is what reads them.
    NoProjection,
    // The game is pointing more than about 84 degrees off the drawn view, which is
    // outside any frame this game renders.
    OffAxis,
    // On that axis, but past the edge of the image.
    OffScreen,
};

inline const char* Describe(AimProjection r) {
    switch (r) {
        case AimProjection::Ok:           return "ok";
        case AimProjection::NoProjection: return "the frame's projection matrix has not been found";
        case AimProjection::OffAxis:      return "the game is pointing more than 84 degrees off the drawn view";
        case AimProjection::OffScreen:    return "the game is pointing outside the drawn image";
    }
    return "unknown";
}

inline AimProjection ProjectAimDirection(const UE3Vector& direction, const UE3Rotator& drawn,
                                         float tanHalfH, float tanHalfV,
                                         float* ndcX, float* ndcY) {
    const float length = std::sqrt(direction.X * direction.X + direction.Y * direction.Y +
                                   direction.Z * direction.Z);
    if (!(length > 0.0f)) {
        return AimProjection::OffAxis;
    }
    const float aim[3] = {direction.X / length, direction.Y / length, direction.Z / length};
    const Mat3 view = RotatorToMatrix(drawn);
    if (!cameraunlock::rendering::ProjectAimToNdc(aim, view.m[0], view.m[1], view.m[2],
                                                tanHalfH, tanHalfV, *ndcX, *ndcY)) {
        return AimProjection::OffAxis;
    }
    if (*ndcX < -1.0f || *ndcX > 1.0f || *ndcY < -1.0f || *ndcY > 1.0f) {
        return AimProjection::OffScreen;
    }
    return AimProjection::Ok;
}

inline AimProjection ProjectCleanAim(const UE3Rotator& clean, const UE3Rotator& drawn,
                                     float tanHalfH, float tanHalfV,
                                     float* ndcX, float* ndcY) {
    const Mat3 aim = RotatorToMatrix(clean);
    return ProjectAimDirection({aim.m[0][0], aim.m[0][1], aim.m[0][2]}, drawn,
                                tanHalfH, tanHalfV, ndcX, ndcY);
}

inline AimProjection ProjectAimPoint(const UE3Vector& target, const UE3Vector& eye,
                                     const UE3Rotator& drawn, float tanHalfH, float tanHalfV,
                                     float* ndcX, float* ndcY) {
    return ProjectAimDirection({target.X - eye.X, target.Y - eye.Y, target.Z - eye.Z},
                                drawn, tanHalfH, tanHalfV, ndcX, ndcY);
}

}  // namespace OutlastHeadTracking
