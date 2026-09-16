// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "frame_sample.h"
#include "ue3_rotation.h"
#include "ue3_types.h"

#include <cmath>

namespace OutlastHeadTracking {

// The positional lean applied to the render eye this frame: along the clean camera's
// right, up and forward axes, and as the world vector those add up to. Both are kept -
// the world vector is what moves the eye, the components are what the diagnostic line
// reports, and a lean that looks wrong in game is settled by reading the two against
// each other rather than by argument.
struct Lean {
    float ruf[3] = { 0.0f, 0.0f, 0.0f };
    float world[3] = { 0.0f, 0.0f, 0.0f };

    bool IsZero() const {
        return world[0] == 0.0f && world[1] == 0.0f && world[2] == 0.0f;
    }
};

namespace detail {

// Moves the render eye by the frame's positional lean, in the CLEAN orientation basis -
// `cleanRot` must therefore be the rotator the engine filled in, before any head
// rotation is composed into it, so a lean follows body facing rather than the
// head-rotated view. UE3 is left-handed with X forward, Y right, Z up.
inline void ApplyLean(const FrameSample& s, const UE3Rotator& cleanRot, UE3Vector* outLoc,
                      Lean* outLean) {
    // Horizon-locked: forward is FLAT, so the three axes are orthogonal and a lean
    // moves the eye by the amount asked for along the axis asked for. Using the
    // pitched forward instead puts part of a forward lean into world Z, where the
    // vertical limits - already applied, in tracker space - cannot see it.
    //
    // Folded onto its half-turn first, on the same terms as ComposeRotation: the field
    // is a plain int32 the game may have accumulated turns into, and both the int-to-
    // float conversion and the sine of a large argument lose the low bits that carry
    // the angle. The fold subtracts whole revolutions, so it names the same direction.
    const float yawRad = UnitsToRad(WrapSigned(cleanRot.Yaw));
    const float cy = std::cos(yawRad), sy = std::sin(yawRad);

    const float fwd[3]   = { cy,   sy,   0.0f };
    const float right[3] = { -sy,  cy,   0.0f };
    const float up[3]    = { 0.0f, 0.0f, 1.0f };

    // The processor's own axes are mirrored against the engine's on x and z: its
    // negative z is the forward lean, which is what puts the generous LimitZ on
    // leaning in and the restricted LimitZBack on pulling away. Converted here,
    // after the clamp rather than before it, because doing it with the processor's
    // inversion instead hands the 0.40 m budget to the backward lean.
    const float oR = -s.pos_x * kWorldUnitsPerMetre;
    const float oU =  s.pos_y * kWorldUnitsPerMetre;
    const float oF = -s.pos_z * kWorldUnitsPerMetre;
    outLean->ruf[0] = oR;
    outLean->ruf[1] = oU;
    outLean->ruf[2] = oF;

    // Kept in world units as well as in the clean basis: the eye moves by the sum,
    // and the components on their own cannot be added to a world position.
    outLean->world[0] = right[0] * oR + up[0] * oU + fwd[0] * oF;
    outLean->world[1] = right[1] * oR + up[1] * oU + fwd[1] * oF;
    outLean->world[2] = right[2] * oR + up[2] * oU + fwd[2] * oF;
    outLoc->X += outLean->world[0];
    outLoc->Y += outLean->world[1];
    outLoc->Z += outLean->world[2];
}

// The head's share of the pitch, cut to whatever is left before vertical.
//
// The limit belongs on the head's CONTRIBUTION, never on the sum. Applied to the sum it
// rewrites a rotator the game itself put past vertical - which it does, briefly, while
// the hero's fall at a chapter start settles - and it does so even with the tracker
// sitting dead centre, so a player who has not moved gets the frame drawn seventy degrees
// away from where the game is pointing, and the mod's own "head pose reached the camera"
// line fires on a pose of 0/0/0. Budgeting the contribution instead leaves that frame
// exactly as the game left it, and still refuses to let the head push past the
// singularity, which is the whole of what the limit is for.
inline float BudgetedPitch(std::int32_t cleanPitch, float headPitchDeg) {
    constexpr float kUnitsToDeg = 360.0f / kUnitsPerRevolution;
    const std::int32_t up = cleanPitch < kMaxPitchUnits ? kMaxPitchUnits - cleanPitch : 0;
    const std::int32_t down = cleanPitch > -kMaxPitchUnits ? -kMaxPitchUnits - cleanPitch : 0;
    const float upDeg = static_cast<float>(up) * kUnitsToDeg;
    const float downDeg = static_cast<float>(down) * kUnitsToDeg;
    if (headPitchDeg > upDeg) {
        return upDeg;
    }
    if (headPitchDeg < downDeg) {
        return downDeg;
    }
    return headPitchDeg;
}

// Composes the head rotation onto the rotator the engine filled in. The angles are in
// degrees and already in the engine's convention - see the sign flip in ApplyHeadPose.
inline void ComposeRotation(bool worldSpaceYaw, float headYaw, float headPitch,
                            float headRoll, UE3Rotator* outRot) {
    // The budget is exact for the horizon-locked branch below, where the composed pitch IS
    // the sum of the two. It is only an approximation for the camera-local branch, which
    // composes matrices: there the composed elevation is not that sum once the clean camera
    // carries roll, and head yaw moves elevation directly. That branch therefore carries a
    // second guard of its own rather than trusting this one.
    const std::int32_t cleanPitch = WrapSigned(outRot->Pitch);
    const float budgetedPitch = BudgetedPitch(cleanPitch, headPitch);

    if (worldSpaceYaw) {
        // Horizon-locked yaw (default): FRotator yaw is the outermost rotation about
        // world Z, so per-axis addition keeps head yaw on the world up-axis no matter
        // how the camera is pitched. Pitch and roll stay camera-relative.
        //
        // Each axis is folded onto its half-turn BEFORE the add. The engine's rotator
        // fields are plain int32 and nothing bounds what the game put there, so adding
        // up to a revolution to a raw one is signed overflow, which is undefined. After
        // the fold both operands are inside +/-32768 and the sum cannot leave int32.
        outRot->Yaw   = WrapSigned(outRot->Yaw) + DegToUnits(headYaw);
        outRot->Roll  = WrapSigned(outRot->Roll) + DegToUnits(headRoll);
        outRot->Pitch = cleanPitch + DegToUnits(budgetedPitch);
        return;
    }

    // Camera-local yaw: compose the head rotation in the camera frame
    // (M_head * M_clean, row-vector convention) so yaw follows the tilted up-axis at
    // extreme pitches. Coincides with the horizon-locked branch when the clean
    // camera is level.
    const Mat3 clean = RotatorToMatrix(*outRot);
    const Mat3 head = RotatorToMatrix(budgetedPitch * kDegToRad, headYaw * kDegToRad,
                                      headRoll * kDegToRad);
    const Mat3 composed = MatMul(head, clean);

    // Passing over the zenith is what the pitch limit exists to stop, and here the budget
    // cannot see it coming - three degrees of head pitch is enough to cross it when the
    // game's own camera is steep and rolled, which is the state the hero's fall at a
    // chapter start leaves behind while it settles. What a crossing always does is reverse
    // the horizontal direction the view faces, so that is what is tested, on the matrix
    // itself rather than on the rotator the round trip folds it into. A head yaw past a
    // quarter turn reverses it legitimately, so the test stops there.
    //
    // The frame then keeps the game's own rotator. Holding at the limit instead would mean
    // solving for the head angle that reaches it, and this is a state the camera is passing
    // through rather than sitting in.
    const float alignment = clean.m[0][0] * composed.m[0][0] +
                            clean.m[0][1] * composed.m[0][1];
    if (alignment < 0.0f && std::fabs(headYaw) < 90.0f) {
        return;
    }

    MatrixToRotator(composed, outRot);
    // atan2 bounds this branch at exactly vertical rather than past it, but vertical is the
    // singularity itself: the composition flips roll there. Stopped one unit short, on the
    // same terms as the branch above.
    outRot->Pitch = ClampPitch(outRot->Pitch);
}

}  // namespace detail

// Adds one frame's head pose to the viewpoint the engine just filled in, and reports the
// positional part of it back through outLean.
//
// Pure arithmetic on the two out-parameters, with nothing logged and nothing global read,
// so the axis signs, the unit conversion and the pitch clamp can be exercised without the
// game. Returns false with the viewpoint untouched and the lean zero when the pose is not
// a finite number; saying so is the caller's job.
inline bool ApplyHeadPose(bool worldSpaceYaw, const FrameSample& s, UE3Vector* outLoc,
                          UE3Rotator* outRot, Lean* outLean) {
    // Written through the out-parameter rather than copied out at each return: with three
    // exits, the copy is a line one of them eventually forgets, and a missing lean reaches
    // the reticle as the previous frame's parallax correction.
    *outLean = Lean{};

    // The pose comes off the network and everything below writes it into the engine's
    // own out-parameters, so this is the boundary worth validating at: a NaN in the
    // rotator or in the camera's world position renders a black frame, and the
    // camera-local branch would carry it into lround, which is undefined for one.
    if (!std::isfinite(s.yaw) || !std::isfinite(s.pitch) || !std::isfinite(s.roll) ||
        !std::isfinite(s.pos_x) || !std::isfinite(s.pos_y) || !std::isfinite(s.pos_z)) {
        return false;
    }

    // The tracker declares no convention of its own, so the mirrored axes are flipped
    // here, once, where the tracker meets the engine. Yaw and pitch match UE3 directly
    // and roll is mirrored - the same three signs the other two UE3 mods in this fleet
    // arrived at and verified in game (spec-ops-the-line, dishonored, bioshock-infinite).
    const float headYaw   =  s.yaw;
    const float headPitch =  s.pitch;
    const float headRoll  = -s.roll;

    // The lean goes in FIRST, while outRot still holds the clean rotation it has to be
    // resolved against.
    if (s.has_position) {
        detail::ApplyLean(s, *outRot, outLoc, outLean);
    }
    if (s.has_rotation) {
        detail::ComposeRotation(worldSpaceYaw, headYaw, headPitch, headRoll, outRot);
    }
    return true;
}

}  // namespace OutlastHeadTracking
