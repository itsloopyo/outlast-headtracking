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

// Cuts the frame's lean to what the level leaves room for, and returns the fraction of
// it to keep, in [0, 1]. `cleanEye` is where the game itself put the camera and
// `worldOffset` is the lean the tracker asked for, in world units, so the two together
// are the segment to test. A null function is the clamp switched off, which is what the
// mod ships until the trace behind it has been confirmed in a running game.
//
// A fraction rather than a vector because the policy only ever shortens the lean along
// its own direction, and returning the scale keeps the diagnostic components and the
// world vector in step without either being recomputed from the other.
using LeanLimitFn = float (*)(void* context, const UE3Vector& cleanEye,
                              const float worldOffset[3]);

namespace detail {

// Moves the render eye by the frame's positional lean, in the CLEAN orientation basis -
// `cleanRot` must therefore be the rotator the engine filled in, before any head
// rotation is composed into it, so a lean follows body facing rather than the
// head-rotated view. UE3 is left-handed with X forward, Y right, Z up.
inline void ApplyLean(const FrameSample& s, const UE3Rotator& cleanRot, UE3Vector* outLoc,
                      Lean* outLean, LeanLimitFn limit, void* limitContext) {
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

    // Asked BEFORE the eye moves, from the clean position: a query starting at an eye
    // that is already inside the wall answers a question about the wrong room. Both the
    // world vector and the components the diagnostic line reports are cut by what comes
    // back, so the log describes the lean that was applied rather than the one asked for.
    if (limit != nullptr) {
        const float keep = limit(limitContext, *outLoc, outLean->world);
        for (int i = 0; i < 3; ++i) {
            outLean->ruf[i] *= keep;
            outLean->world[i] *= keep;
        }
    }

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
    // A pose that names no rotation at all leaves the rotator exactly as the game filled
    // it in, down to the number. Composing it anyway is arithmetic that cannot move the
    // camera and can still change the field: the camera-local branch round-trips through
    // a matrix, and both branches fold. The camera hook decides the pose reached the
    // frame by comparing these three fields against the game's, so a centred tracker
    // would otherwise announce itself on the first frame the game's yaw passed a half
    // turn - and that line is the one thing in the log that says the chain is live.
    if (DegToUnits(headYaw) == 0 && DegToUnits(headPitch) == 0 && DegToUnits(headRoll) == 0) {
        return;
    }

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
        // the fold the game's side is inside +/-32768 and the head's inside +/-65536 -
        // DegToUnits folds onto a whole revolution, not a half one - so the sum is at
        // most 98303 and cannot leave int32.
        //
        // An axis the head did not turn is left ALONE rather than written back folded.
        // The fold names the same direction, but not the same number, and the game's yaw
        // accumulates turns without bound: rewriting it would mean a centred tracker
        // changed the rotator, which is what the camera hook reads as the pose having
        // moved.
        if (const std::int32_t yaw = DegToUnits(headYaw)) {
            outRot->Yaw = WrapSigned(outRot->Yaw) + yaw;
        }
        if (const std::int32_t roll = DegToUnits(headRoll)) {
            outRot->Roll = WrapSigned(outRot->Roll) + roll;
        }
        if (const std::int32_t pitch = DegToUnits(budgetedPitch)) {
            outRot->Pitch = cleanPitch + pitch;
        }
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
    // chapter start leaves behind while it settles.
    //
    // What a crossing always does is carry the camera's own up-axis across the horizon,
    // and row 2 of the matrix IS that axis, so the test is the sign of its world-Z
    // component against the sign it had. Relative, not absolute: the camera the fall
    // leaves behind is rolled past upright, its up-axis already points below the horizon,
    // and a crossing there moves it back ABOVE one. Tested on the matrix rather than on
    // the rotator, which folds a crossing into a plausible-looking pitch and a reversed
    // yaw.
    //
    // It is NOT tested by comparing which way the two forward axes face. That reverses on
    // a crossing, but it also reverses without one: camera-local yaw at a steep camera
    // swings the heading a long way for a small head movement, which is the whole point of
    // the mode. Measured, with a horizontal-reversal test in this spot: a clean pitch of 70
    // degrees and a head pitch of 15 threw the pose away from 43 degrees of head yaw, and a
    // clean pitch of 80 threw it away from 2 - the view snapping back to the game's aim and
    // sticking there while the lean carried on tracking.
    //
    // A head ROLL past a quarter turn would tip the axis across on its own and be read as a
    // crossing. A neck does not do that, and a tracker sending one has bigger problems than
    // this branch.
    //
    // The frame then keeps the game's own rotator. Holding at the limit instead would mean
    // solving for the head angle that reaches it, and this is a state the camera is passing
    // through rather than sitting in.
    if ((composed.m[2][2] < 0.0f) != (clean.m[2][2] < 0.0f)) {
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
                          UE3Rotator* outRot, Lean* outLean,
                          LeanLimitFn leanLimit = nullptr, void* leanContext = nullptr) {
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
        detail::ApplyLean(s, *outRot, outLoc, outLean, leanLimit, leanContext);
    }
    if (s.has_rotation) {
        detail::ComposeRotation(worldSpaceYaw, headYaw, headPitch, headRoll, outRot);
    }
    return true;
}

}  // namespace OutlastHeadTracking
