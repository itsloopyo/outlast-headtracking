// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "ue3_types.h"

#include <cmath>

namespace OutlastHeadTracking {

// UE3 FRotationMatrix layout: rows are the world-space forward/right/up axes
// (row-vector convention), composed as M(P,Y,R) = M(P,0,R) * M(0,Y,0). Yaw is
// the outermost rotation about world Z - which is why plain FRotator addition
// gives horizon-locked yaw, and why camera-local yaw needs this matrix path.
struct Mat3 {
    float m[3][3];
};

inline Mat3 RotatorToMatrix(float pitchRad, float yawRad, float rollRad) {
    const float sp = std::sin(pitchRad), cp = std::cos(pitchRad);
    const float sy = std::sin(yawRad),   cy = std::cos(yawRad);
    const float sr = std::sin(rollRad),  cr = std::cos(rollRad);
    Mat3 M;
    M.m[0][0] = cp * cy;
    M.m[0][1] = cp * sy;
    M.m[0][2] = sp;
    M.m[1][0] = sr * sp * cy - cr * sy;
    M.m[1][1] = sr * sp * sy + cr * cy;
    M.m[1][2] = -sr * cp;
    M.m[2][0] = -(cr * sp * cy + sr * sy);
    M.m[2][1] = sr * cy - cr * sp * sy;
    M.m[2][2] = cr * cp;
    return M;
}

// Folded onto its half-turn first, which is what every caller handing this a rotator the
// GAME filled in needs: those fields are plain int32 the engine accumulates turns into,
// and the conversion to radians goes through float, so a yaw of a few thousand turns
// loses the low bits that carry the angle before the sine sees it. Measured: an
// unwrapped 2e9 units is 0.55 degrees off the direction it denotes, while the folded one
// stays at float noise. Free for the in-range case - the fold is the identity there.
inline Mat3 RotatorToMatrix(const UE3Rotator& r) {
    const UE3Rotator folded = Wrapped(r);
    return RotatorToMatrix(UnitsToRad(folded.Pitch), UnitsToRad(folded.Yaw),
                           UnitsToRad(folded.Roll));
}

inline Mat3 MatMul(const Mat3& a, const Mat3& b) {
    Mat3 r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
        }
    }
    return r;
}

// UE3 FMatrix::Rotator(): pitch and yaw from the forward axis, roll from the
// right and up axes projected onto the roll-free right axis.
inline void MatrixToRotator(const Mat3& M, UE3Rotator* out) {
    const float fx = M.m[0][0], fy = M.m[0][1], fz = M.m[0][2];
    const float pitch = std::atan2(fz, std::sqrt(fx * fx + fy * fy));
    const float yaw   = std::atan2(fy, fx);
    const float syx = -std::sin(yaw), syy = std::cos(yaw);
    const float roll  = std::atan2(M.m[2][0] * syx + M.m[2][1] * syy,
                                   M.m[1][0] * syx + M.m[1][1] * syy);
    out->Pitch = RadToUnits(pitch);
    out->Yaw   = RadToUnits(yaw);
    out->Roll  = RadToUnits(roll);
}

}  // namespace OutlastHeadTracking
