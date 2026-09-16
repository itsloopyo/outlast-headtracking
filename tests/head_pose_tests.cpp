// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Characterization tests for ApplyHeadPose - the whole of what the mod does to the
// game's viewpoint. Every axis sign, the metres-to-centimetres conversion and the order
// the lean and the rotation are applied in are locked here, because each of them is a
// coin flip that only a player wearing a tracker would otherwise catch, and each has
// shipped wrong somewhere in this fleet before.

#include "test_support.h"

#include "head_pose.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

using namespace OutlastHeadTracking;
using olht_tests::Check;
using olht_tests::NearEqual;

constexpr bool kWorldYaw = true;
constexpr bool kLocalYaw = false;

FrameSample RotationSample(float yaw, float pitch, float roll) {
    FrameSample s;
    s.has_rotation = true;
    s.yaw = yaw;
    s.pitch = pitch;
    s.roll = roll;
    return s;
}

FrameSample PositionSample(float x, float y, float z) {
    FrameSample s;
    s.has_position = true;
    s.pos_x = x;
    s.pos_y = y;
    s.pos_z = z;
    return s;
}

void CentredPoseChangesNothing() {
    UE3Vector loc{ 100.0f, 200.0f, 300.0f };
    UE3Rotator rot{ 1000, 2000, 3000 };
    FrameSample s = RotationSample(0.0f, 0.0f, 0.0f);
    s.has_position = true;
    Lean lean;

    Check(ApplyHeadPose(kWorldYaw, s, &loc, &rot, &lean), "a centred pose is accepted");
    Check(rot.Pitch == 1000 && rot.Yaw == 2000 && rot.Roll == 3000,
          "a centred pose leaves the game's rotator exactly as it was");
    Check(loc.X == 100.0f && loc.Y == 200.0f && loc.Z == 300.0f,
          "a centred pose leaves the game's view location exactly as it was");
    Check(lean.IsZero(), "a centred pose reports no lean");
}

void YawIsAddedUnmirrored() {
    // The in-game measurement on steam-win64-20140429: a held +30 degree tracker yaw
    // turned the game's own yaw 32767 into 38228, a delta of 5461 units, with pitch and
    // roll untouched.
    UE3Vector loc{};
    UE3Rotator rot{ 0, 32767, 0 };
    Lean lean;
    ApplyHeadPose(kWorldYaw, RotationSample(30.0f, 0.0f, 0.0f), &loc, &rot, &lean);
    Check(rot.Yaw == 38228, "a +30 degree tracker yaw turns the game's yaw the same way");
    Check(rot.Pitch == 0 && rot.Roll == 0, "yaw alone leaves pitch and roll untouched");
}

void PitchIsAddedUnmirrored() {
    UE3Vector loc{};
    UE3Rotator rot{ 0, 0, 0 };
    Lean lean;
    ApplyHeadPose(kWorldYaw, RotationSample(0.0f, 10.0f, 0.0f), &loc, &rot, &lean);
    Check(rot.Pitch == DegToUnits(10.0f), "a +10 degree tracker pitch is added as +10");
    Check(rot.Yaw == 0 && rot.Roll == 0, "pitch alone leaves yaw and roll untouched");
}

void RollIsMirrored() {
    UE3Vector loc{};
    UE3Rotator rot{ 0, 0, 0 };
    Lean lean;
    ApplyHeadPose(kWorldYaw, RotationSample(0.0f, 0.0f, 10.0f), &loc, &rot, &lean);
    Check(rot.Roll == DegToUnits(-10.0f), "roll is mirrored at the engine boundary");
    Check(rot.Pitch == 0 && rot.Yaw == 0, "roll alone leaves pitch and yaw untouched");
}

void UnboundedGameRotatorIsFoldedBeforeTheAdd() {
    // The engine's rotator fields are plain int32 and nothing bounds what the game put
    // there. Adding to a raw one would be signed overflow.
    UE3Vector loc{};
    UE3Rotator rot{ 0, 2147483000, 0 };
    Lean lean;
    ApplyHeadPose(kWorldYaw, RotationSample(30.0f, 0.0f, 0.0f), &loc, &rot, &lean);
    Check(rot.Yaw == WrapSigned(2147483000) + 5461,
          "a huge game yaw is folded onto one revolution before the head yaw is added");
}

void PitchIsStoppedShortOfVertical() {
    UE3Vector loc{};
    UE3Rotator rot{ 16000, 0, 0 };
    Lean lean;
    ApplyHeadPose(kWorldYaw, RotationSample(0.0f, 45.0f, 0.0f), &loc, &rot, &lean);
    Check(rot.Pitch == kMaxPitchUnits,
          "a head pitch stacked on a steep camera stops one unit short of vertical");

    UE3Rotator down{ -16000, 0, 0 };
    ApplyHeadPose(kWorldYaw, RotationSample(0.0f, -45.0f, 0.0f), &loc, &down, &lean);
    Check(down.Pitch == -kMaxPitchUnits, "the same limit applies looking down");
}

void LeanConvertsMetresToWorldUnits() {
    // Tracker y is up in both conventions, so 0.1 m up is +10 world units up and needs
    // no sign flip - which makes it the clean check on the unit conversion.
    UE3Vector loc{};
    UE3Rotator rot{};
    Lean lean;
    ApplyHeadPose(kWorldYaw, PositionSample(0.0f, 0.1f, 0.0f), &loc, &rot, &lean);
    Check(NearEqual(lean.ruf[1], 10.0f), "0.1 m of head rise is 10 UE3 world units");
    Check(NearEqual(loc.Z, 10.0f), "the rise is applied to the engine's up axis");
}

void LeanMirrorsXAndZ() {
    // The processor's negative z is the forward lean, and x is mirrored on the same
    // terms, so both are flipped once here at the engine boundary. At yaw 0 the clean
    // basis is forward=+X, right=+Y.
    UE3Vector loc{};
    UE3Rotator rot{};
    Lean lean;
    ApplyHeadPose(kWorldYaw, PositionSample(0.0f, 0.0f, 0.1f), &loc, &rot, &lean);
    Check(NearEqual(lean.ruf[2], -10.0f), "a positive tracker z is a backward lean");
    Check(NearEqual(loc.X, -10.0f), "the backward lean moves the eye along -X at yaw 0");

    UE3Vector loc2{};
    UE3Rotator rot2{};
    ApplyHeadPose(kWorldYaw, PositionSample(0.1f, 0.0f, 0.0f), &loc2, &rot2, &lean);
    Check(NearEqual(lean.ruf[0], -10.0f), "a positive tracker x is mirrored too");
    Check(NearEqual(loc2.Y, -10.0f), "the sideways lean moves the eye along -Y at yaw 0");
}

void LeanFollowsBodyFacing() {
    // At a quarter turn the clean forward is +Y, so the same forward lean must move the
    // eye along Y instead of X.
    UE3Vector loc{};
    UE3Rotator rot{ 0, 16384, 0 };
    Lean lean;
    ApplyHeadPose(kWorldYaw, PositionSample(0.0f, 0.0f, -0.1f), &loc, &rot, &lean);
    Check(NearEqual(lean.ruf[2], 10.0f), "a negative tracker z is a forward lean");
    Check(NearEqual(loc.X, 0.0f, 1e-3f) && NearEqual(loc.Y, 10.0f, 1e-3f),
          "the forward lean follows the body's facing, not the world axes");
}

void LeanIgnoresWholeTurnsOfCleanYaw() {
    // The rotator field is a plain int32 and nothing bounds what the game put there. A
    // yaw carrying whole revolutions names the same direction, but converting it to a
    // float and taking its sine loses exactly the low bits that carry the angle - so the
    // fold that ComposeRotation does before its add has to happen here too, or the same
    // camera leans in two different directions depending on how the game got to it.
    const std::int32_t quarterTurn = 16384;
    UE3Vector folded{};
    UE3Vector accumulated{};
    UE3Rotator foldedRot{ 0, quarterTurn, 0 };
    UE3Rotator accumulatedRot{ 0, quarterTurn + kUnitsPerRevolutionInt * 20000, 0 };
    Lean lean;

    ApplyHeadPose(kWorldYaw, PositionSample(0.0f, 0.0f, -0.1f), &folded, &foldedRot, &lean);
    ApplyHeadPose(kWorldYaw, PositionSample(0.0f, 0.0f, -0.1f), &accumulated,
                  &accumulatedRot, &lean);

    Check(NearEqual(accumulated.X, folded.X, 1e-3f) &&
          NearEqual(accumulated.Y, folded.Y, 1e-3f) &&
          NearEqual(accumulated.Z, folded.Z, 1e-3f),
          "a clean yaw carrying whole turns leans the same way as the folded one");
    Check(NearEqual(accumulated.Y, 10.0f, 1e-3f),
          "and that way is still the body's forward");
}

void LeanIgnoresCameraPitch() {
    // The lean basis uses a FLAT forward. A pitched forward would put part of a forward
    // lean into world Z, where the vertical limits - already applied in tracker space -
    // cannot see it.
    UE3Vector loc{};
    UE3Rotator rot{ 8000, 0, 0 };
    Lean lean;
    ApplyHeadPose(kWorldYaw, PositionSample(0.0f, 0.0f, -0.1f), &loc, &rot, &lean);
    Check(NearEqual(loc.Z, 0.0f, 1e-3f),
          "a forward lean under a pitched camera puts nothing into world Z");
    Check(NearEqual(loc.X, 10.0f, 1e-3f), "and all of it into the flat forward");
}

void LeanIsResolvedAgainstTheCleanRotation() {
    // The ordering invariant: the lean goes in while the rotator still holds the game's
    // own rotation. Composing the head yaw first would swing the lean with the head.
    UE3Vector loc{};
    UE3Rotator rot{ 0, 0, 0 };
    FrameSample s = PositionSample(0.0f, 0.0f, -0.1f);
    s.has_rotation = true;
    s.yaw = 90.0f;
    Lean lean;
    ApplyHeadPose(kWorldYaw, s, &loc, &rot, &lean);
    Check(NearEqual(loc.X, 10.0f, 1e-3f) && NearEqual(loc.Y, 0.0f, 1e-3f),
          "the lean is resolved against the clean rotation, not the head-turned one");
    Check(rot.Yaw == DegToUnits(90.0f), "and the head yaw still reaches the rotator");
}

void ChannelsAreIndependent() {
    UE3Vector loc{ 5.0f, 6.0f, 7.0f };
    UE3Rotator rot{ 10, 20, 30 };
    Lean lean;
    ApplyHeadPose(kWorldYaw, RotationSample(15.0f, 0.0f, 0.0f), &loc, &rot, &lean);
    Check(loc.X == 5.0f && loc.Y == 6.0f && loc.Z == 7.0f,
          "a rotation-only sample leaves the view location alone");
    Check(lean.IsZero(), "a rotation-only sample reports no lean");

    UE3Rotator rot2{ 10, 20, 30 };
    UE3Vector loc2{};
    ApplyHeadPose(kWorldYaw, PositionSample(0.0f, 0.1f, 0.0f), &loc2, &rot2, &lean);
    Check(rot2.Pitch == 10 && rot2.Yaw == 20 && rot2.Roll == 30,
          "a position-only sample leaves the rotator alone");
}

void NonFinitePoseIsRefused() {
    const float nan = std::nanf("");
    const float inf = std::numeric_limits<float>::infinity();

    UE3Vector loc{ 1.0f, 2.0f, 3.0f };
    UE3Rotator rot{ 4, 5, 6 };
    Lean lean;
    lean.world[0] = 99.0f;  // a previous frame's parallax correction

    Check(!ApplyHeadPose(kWorldYaw, RotationSample(nan, 0.0f, 0.0f), &loc, &rot, &lean),
          "a NaN rotation is refused");
    Check(rot.Pitch == 4 && rot.Yaw == 5 && rot.Roll == 6 && loc.X == 1.0f,
          "and the viewpoint is left untouched");
    Check(lean.IsZero(), "and the stale lean is cleared rather than carried forward");

    FrameSample infPosition = PositionSample(0.0f, inf, 0.0f);
    Check(!ApplyHeadPose(kWorldYaw, infPosition, &loc, &rot, &lean),
          "an infinite position is refused");
    Check(loc.X == 1.0f && loc.Y == 2.0f && loc.Z == 3.0f,
          "and the view location is left untouched");
}

void YawModesAgreeOnALevelCamera() {
    UE3Vector loc{};
    UE3Rotator world{ 0, 4000, 0 };
    UE3Rotator local{ 0, 4000, 0 };
    Lean lean;
    ApplyHeadPose(kWorldYaw, RotationSample(20.0f, 0.0f, 0.0f), &loc, &world, &lean);
    ApplyHeadPose(kLocalYaw, RotationSample(20.0f, 0.0f, 0.0f), &loc, &local, &lean);
    Check(std::abs(world.Yaw - local.Yaw) <= 1,
          "the two yaw modes coincide when the camera is level");
    Check(std::abs(world.Pitch - local.Pitch) <= 1, "and so do their pitches");
}

void CameraLocalYawDiffersOnAPitchedCamera() {
    // The reason the mode exists: with the camera pitched, head yaw about the tilted
    // up-axis is not the same rotation as head yaw about world up.
    UE3Vector loc{};
    UE3Rotator world{ 12000, 0, 0 };
    UE3Rotator local{ 12000, 0, 0 };
    Lean lean;
    ApplyHeadPose(kWorldYaw, RotationSample(30.0f, 0.0f, 0.0f), &loc, &world, &lean);
    ApplyHeadPose(kLocalYaw, RotationSample(30.0f, 0.0f, 0.0f), &loc, &local, &lean);
    Check(world.Yaw != local.Yaw || world.Roll != local.Roll,
          "camera-local yaw follows the tilted up-axis at a steep pitch");
}

void CameraLocalPitchIsAlsoStoppedShort() {
    // A range check alone cannot fail here, and that is the trap: composing +40 degrees
    // onto an 89.5 degree camera takes the view OVER the top, and the matrix round trip
    // reports the far side of it as a modest pitch with yaw and roll each turned a half
    // turn - the view upside down and facing backwards, comfortably inside +/-16383. So
    // the yaw and roll are what this has to assert on.
    UE3Vector loc{};
    UE3Rotator rot{ 16300, 0, 0 };
    Lean lean;
    ApplyHeadPose(kLocalYaw, RotationSample(0.0f, 40.0f, 0.0f), &loc, &rot, &lean);
    Check(rot.Pitch <= kMaxPitchUnits && rot.Pitch >= -kMaxPitchUnits,
          "the camera-local branch is bounded at the singularity too");
    Check(std::abs(WrapSigned(rot.Yaw)) < 1000,
          "and it does not go over the top, which would swing yaw a half turn");
    Check(std::abs(WrapSigned(rot.Roll)) < 1000, "nor roll");
    Check(rot.Pitch == kMaxPitchUnits,
          "the view is held one unit short of straight up, not somewhere below it");
}

void CameraLocalRefusesToGoOverThePoleOnARolledCamera() {
    // The state the hero's fall at a chapter start leaves behind while it settles: the
    // game's own camera steep AND rolled. The pitch budget cannot see a crossing coming
    // here, because the composed elevation is not the sum of the two pitches once roll is
    // in the clean rotator - three degrees of tracker pitch is enough to take the view
    // over the top, and the matrix round trip reports the far side of it as a modest pitch
    // with the yaw swung a half turn. The player cannot undo that by looking back down.
    UE3Vector loc{};
    Lean lean;
    for (float headPitch = -6.0f; headPitch <= 6.0f; headPitch += 0.5f) {
        UE3Rotator rot{ 16000, 0, 32768 };
        ApplyHeadPose(kLocalYaw, RotationSample(0.0f, headPitch, 0.0f), &loc, &rot, &lean);
        if (std::abs(WrapSigned(rot.Yaw)) > 16384) {
            Check(false, "a small head pitch on a steep rolled camera swung the yaw a "
                         "half turn, which is the view facing backwards");
            return;
        }
    }
    Check(true, "no small head pitch takes a steep rolled camera over the pole");
}

void AnAlreadySteepGameCameraIsLeftAloneByACentredTracker() {
    // The rotator the game itself fills in is not always inside the limit - the hero's
    // fall at a chapter start leaves pitch in the tens of thousands while it settles. The
    // limit belongs on the head's CONTRIBUTION: applied to the sum it rewrites a frame the
    // tracker had no part in, and the player who has not moved gets the view snapped
    // seventy degrees off what the game is drawing.
    UE3Vector loc{};
    UE3Rotator rot{ 30000, 1234, 0 };
    Lean lean;
    ApplyHeadPose(kWorldYaw, RotationSample(0.0f, 0.0f, 0.0f), &loc, &rot, &lean);
    Check(rot.Pitch == 30000, "a centred tracker leaves a past-vertical game pitch alone");

    UE3Rotator pushed{ 30000, 0, 0 };
    ApplyHeadPose(kWorldYaw, RotationSample(0.0f, 10.0f, 0.0f), &loc, &pushed, &lean);
    Check(pushed.Pitch == 30000, "and the head cannot push it further past vertical");

    UE3Rotator pulled{ 30000, 0, 0 };
    ApplyHeadPose(kWorldYaw, RotationSample(0.0f, -10.0f, 0.0f), &loc, &pulled, &lean);
    Check(pulled.Pitch < 30000, "but can still bring it back down");
}

void TheHeadPitchStopsAtVerticalFromALevelCamera() {
    UE3Vector loc{};
    UE3Rotator rot{ 0, 0, 0 };
    Lean lean;
    ApplyHeadPose(kWorldYaw, RotationSample(0.0f, 120.0f, 0.0f), &loc, &rot, &lean);
    Check(rot.Pitch == kMaxPitchUnits, "a head pitch past vertical stops one unit short");
}

}  // namespace

int RunHeadPoseTests() {
    std::cout << "Head pose composition\n";
    CentredPoseChangesNothing();
    YawIsAddedUnmirrored();
    PitchIsAddedUnmirrored();
    RollIsMirrored();
    UnboundedGameRotatorIsFoldedBeforeTheAdd();
    PitchIsStoppedShortOfVertical();
    LeanConvertsMetresToWorldUnits();
    LeanMirrorsXAndZ();
    LeanFollowsBodyFacing();
    LeanIgnoresWholeTurnsOfCleanYaw();
    LeanIgnoresCameraPitch();
    LeanIsResolvedAgainstTheCleanRotation();
    ChannelsAreIndependent();
    NonFinitePoseIsRefused();
    YawModesAgreeOnALevelCamera();
    CameraLocalYawDiffersOnAPitchedCamera();
    CameraLocalPitchIsAlsoStoppedShort();
    CameraLocalRefusesToGoOverThePoleOnARolledCamera();
    AnAlreadySteepGameCameraIsLeftAloneByACentredTracker();
    TheHeadPitchStopsAtVerticalFromALevelCamera();
    return olht_tests::TakeFailures();
}
