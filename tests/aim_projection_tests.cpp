// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Where the mod's mark goes. The projection is fed the two rotators the camera hook
// hands the engine - the game's own and the one the frame is drawn with - so every case
// below is a pose the player can hold, and the expected answer is the one the reticle
// litmus tests ask for by name.

#include "test_support.h"

#include "aim_projection.h"

#include <cmath>
#include <iostream>

namespace {

using namespace OutlastHeadTracking;
using olht_tests::Check;
using olht_tests::NearEqual;

// The frame measured in game on steam-win64-20140429 at the camera's own 80 degrees,
// 16:9: 86.0 degrees across by 55.3 down.
constexpr float kTanH = 0.93233f;
constexpr float kTanV = 0.52444f;

UE3Rotator Rot(float pitchDeg, float yawDeg, float rollDeg) {
    return { DegToUnits(pitchDeg), DegToUnits(yawDeg), DegToUnits(rollDeg) };
}

// The frame's rotator for a head pose of `dPitch/dYaw/dRoll` on top of `clean`, on the
// horizon-locked path the mod runs by default: per-axis addition.
UE3Rotator Composed(const UE3Rotator& clean, float dPitch, float dYaw, float dRoll) {
    return { clean.Pitch + DegToUnits(dPitch), clean.Yaw + DegToUnits(dYaw),
             clean.Roll + DegToUnits(dRoll) };
}

float Tan(float deg) {
    return std::tan(deg * kDegToRad);
}

void CentredWhenNothingIsApplied() {
    const UE3Rotator clean = Rot(-12.0f, 137.0f, 0.0f);
    float x = 9.0f, y = 9.0f;
    Check(ProjectCleanAim(clean, clean, kTanH, kTanV, &x, &y) == AimProjection::Ok,
          "an unmodified frame places the mark");
    Check(NearEqual(x, 0.0f) && NearEqual(y, 0.0f),
          "with no head pose applied the mark is the middle of the screen");
}

void HeadYawMovesTheMarkTheOtherWay() {
    const UE3Rotator clean = Rot(0.0f, 0.0f, 0.0f);
    float x = 0.0f, y = 0.0f;
    Check(ProjectCleanAim(clean, Composed(clean, 0.0f, 20.0f, 0.0f), kTanH, kTanV, &x, &y)
              == AimProjection::Ok,
          "a 20 degree head yaw places the mark");
    Check(x < 0.0f, "turning the head right leaves the game pointing to the left of it");
    Check(NearEqual(x, -Tan(20.0f) / kTanH, 1e-3f),
          "and by exactly the tangent of the yaw over the horizontal half-field");
    Check(NearEqual(y, 0.0f, 1e-3f), "a pure yaw does not move the mark vertically");
}

void HeadPitchMovesTheMarkVertically() {
    const UE3Rotator clean = Rot(0.0f, 0.0f, 0.0f);
    float x = 0.0f, y = 0.0f;
    Check(ProjectCleanAim(clean, Composed(clean, 15.0f, 0.0f, 0.0f), kTanH, kTanV, &x, &y)
              == AimProjection::Ok,
          "a 15 degree head pitch places the mark");
    Check(y < 0.0f, "looking up leaves the game pointing below the middle of the screen");
    Check(NearEqual(y, -Tan(15.0f) / kTanV, 1e-3f),
          "and by exactly the tangent of the pitch over the vertical half-field");
    Check(NearEqual(x, 0.0f, 1e-3f), "a pure pitch does not move the mark horizontally");
}

// Litmus test 1 of the reticle checklist: pure roll with the pitch centred leaves the
// mark at the middle of the screen. Rolling the head spins the picture about the view
// axis, and the direction the game is pointing is on that axis.
void PureRollLeavesTheMarkCentred() {
    const UE3Rotator clean = Rot(0.0f, 40.0f, 0.0f);
    float x = 9.0f, y = 9.0f;
    Check(ProjectCleanAim(clean, Composed(clean, 0.0f, 0.0f, 25.0f), kTanH, kTanV, &x, &y)
              == AimProjection::Ok,
          "a 25 degree head roll places the mark");
    Check(NearEqual(x, 0.0f, 1e-3f) && NearEqual(y, 0.0f, 1e-3f),
          "a pure roll leaves the mark in the middle of the screen");
}

// Litmus test 3: pitch and roll together must not make the mark wander. This camera
// writes roll into the frame's own rotator, so the offset ROTATES with roll rather than
// staying vertical - what must hold is that its distance from the centre is the one the
// pitch alone put there.
void PitchWithRollRotatesRatherThanWanders() {
    const UE3Rotator clean = Rot(0.0f, 0.0f, 0.0f);
    float x0 = 0.0f, y0 = 0.0f;
    ProjectCleanAim(clean, Composed(clean, 12.0f, 0.0f, 0.0f), kTanH, kTanV, &x0, &y0);
    const float radius = std::sqrt(x0 * x0 * kTanH * kTanH + y0 * y0 * kTanV * kTanV);

    for (float roll = -30.0f; roll <= 30.0f; roll += 15.0f) {
        float x = 0.0f, y = 0.0f;
        ProjectCleanAim(clean, Composed(clean, 12.0f, 0.0f, roll), kTanH, kTanV, &x, &y);
        const float r = std::sqrt(x * x * kTanH * kTanH + y * y * kTanV * kTanV);
        Check(NearEqual(r, radius, 1e-3f),
              "rolling the head with a held pitch does not move the mark off its circle");
    }
}

// The radius check above holds just as well for a projection that IGNORES roll, so on its
// own it does not pin litmus test 3 at all - stripping roll out of the drawn basis leaves
// the whole suite green. What separates the two is the DIRECTION: this camera writes roll
// into the frame's rotator, so a held pitch with roll applied has to move off the vertical
// axis, and has to move to the opposite side when the roll reverses. A roll-blind
// projection keeps the mark on x = 0 at every roll.
void PitchWithRollLeavesTheVerticalAxis() {
    const UE3Rotator clean = Rot(0.0f, 0.0f, 0.0f);

    float xUp = 0.0f, yUp = 0.0f;
    ProjectCleanAim(clean, Composed(clean, 12.0f, 0.0f, 0.0f), kTanH, kTanV, &xUp, &yUp);
    Check(NearEqual(xUp, 0.0f, 1e-3f), "a pitch with no roll keeps the mark on the axis");

    float xRight = 0.0f, yRight = 0.0f;
    ProjectCleanAim(clean, Composed(clean, 12.0f, 0.0f, 30.0f), kTanH, kTanV, &xRight,
                    &yRight);
    float xLeft = 0.0f, yLeft = 0.0f;
    ProjectCleanAim(clean, Composed(clean, 12.0f, 0.0f, -30.0f), kTanH, kTanV, &xLeft,
                    &yLeft);

    Check(std::fabs(xRight) > 0.05f,
          "rolling the head with a held pitch takes the mark off the vertical axis");
    Check(xRight * xLeft < 0.0f, "and reversing the roll takes it to the other side");
    Check(NearEqual(xRight, -xLeft, 1e-3f), "by the same distance either way");
}

// Litmus test 4. In the horizon-locked mode the mod ships, head yaw is added to a rotator
// axis that IS world up, so its effect on screen depends on where the camera points: at
// the horizon it sweeps the mark across, and looking straight down it is very nearly a
// spin about the view axis and the mark must stay put. A projection that treated head yaw
// as camera-local would sweep it the same distance at every pitch.
void WorldYawStopsMovingTheMarkWhenLookingDown() {
    float xLevel = 0.0f, yLevel = 0.0f;
    const UE3Rotator level = Rot(0.0f, 0.0f, 0.0f);
    ProjectCleanAim(level, Composed(level, 0.0f, 30.0f, 0.0f), kTanH, kTanV, &xLevel,
                    &yLevel);

    float xDown = 0.0f, yDown = 0.0f;
    const UE3Rotator down = Rot(-88.0f, 0.0f, 0.0f);
    ProjectCleanAim(down, Composed(down, 0.0f, 30.0f, 0.0f), kTanH, kTanV, &xDown, &yDown);

    const float atLevel = std::sqrt(xLevel * xLevel + yLevel * yLevel);
    const float atFloor = std::sqrt(xDown * xDown + yDown * yDown);
    Check(atLevel > 0.3f, "at the horizon a 30 degree head yaw sweeps the mark across");
    Check(atFloor < atLevel * 0.2f,
          "looking almost straight down the same yaw barely moves it - the world spins "
          "around the mark instead");
}

void OffTheAxisAndOffTheScreenAreReported() {
    const UE3Rotator clean = Rot(0.0f, 0.0f, 0.0f);
    float x = 0.0f, y = 0.0f;
    Check(ProjectCleanAim(clean, Composed(clean, 0.0f, 90.0f, 0.0f), kTanH, kTanV, &x, &y)
              == AimProjection::OffAxis,
          "a quarter turn of head yaw is off the drawn view entirely");
    Check(ProjectCleanAim(clean, Composed(clean, 0.0f, 50.0f, 0.0f), kTanH, kTanV, &x, &y)
              == AimProjection::OffScreen,
          "50 degrees is on the axis but past the edge of an 86 degree frame");
    Check(ProjectCleanAim(clean, Composed(clean, 0.0f, 40.0f, 0.0f), kTanH, kTanV, &x, &y)
              == AimProjection::Ok,
          "40 degrees is still inside it");
}

// The engine's rotator fields are plain int32 and a session accumulates turns into the
// yaw. The projection folds both rotators onto their half-turn first, so a camera that
// has spun five times reads exactly like one that has not.
void AccumulatedTurnsDoNotMoveTheMark() {
    const UE3Rotator near = Rot(0.0f, 30.0f, 0.0f);
    UE3Rotator far = near;
    far.Yaw += 5 * kUnitsPerRevolutionInt;

    float xNear = 0.0f, yNear = 0.0f, xFar = 0.0f, yFar = 0.0f;
    ProjectCleanAim(near, Composed(near, 8.0f, 14.0f, 6.0f), kTanH, kTanV, &xNear, &yNear);
    ProjectCleanAim(far, Composed(far, 8.0f, 14.0f, 6.0f), kTanH, kTanV, &xFar, &yFar);
    Check(NearEqual(xNear, xFar, 1e-4f) && NearEqual(yNear, yFar, 1e-4f),
          "five accumulated turns of yaw leave the mark where it was");
}

void LeanStaysOnTheSameSurfacePoint() {
    const UE3Rotator drawn{};
    for (float distance : {100.0f, 1000.0f}) {
        const UE3Vector target{distance, 0.0f, 0.0f};
        for (float lateral : {-30.0f, 30.0f}) {
            for (float vertical : {-20.0f, 20.0f}) {
                for (float forward : {-20.0f, 40.0f}) {
                    float x = 0.0f, y = 0.0f;
                    Check(ProjectAimPoint(target, {forward, lateral, vertical}, drawn,
                                           kTanH, kTanV, &x, &y) == AimProjection::Ok,
                          "combined lean projects a nearby or distant surface");
                    Check(NearEqual(lateral + x * kTanH * (distance - forward), 0.0f, 1e-4f) &&
                          NearEqual(vertical + y * kTanV * (distance - forward), 0.0f, 1e-4f),
                          "opposite leans intersect the same original aim point");
                }
            }
        }
    }
}

void LeanWithHeadRotationUsesTheDrawnBasis() {
    const UE3Vector target{200.0f, 40.0f, 30.0f};
    const UE3Vector eye{20.0f, -15.0f, 10.0f};
    for (float roll : {-25.0f, 0.0f, 25.0f}) {
        const UE3Rotator drawn = Rot(10.0f, 15.0f, roll);
        const Mat3 view = RotatorToMatrix(drawn);
        float x = 0.0f, y = 0.0f;
        Check(ProjectAimPoint(target, eye, drawn, kTanH, kTanV, &x, &y) == AimProjection::Ok,
              "lean with yaw pitch and roll projects the target");
        const float dx = view.m[0][0] + x * kTanH * view.m[1][0] + y * kTanV * view.m[2][0];
        const float dy = view.m[0][1] + x * kTanH * view.m[1][1] + y * kTanV * view.m[2][1];
        const float dz = view.m[0][2] + x * kTanH * view.m[1][2] + y * kTanV * view.m[2][2];
        const float t = (target.X - eye.X) / dx;
        Check(NearEqual(eye.Y + t * dy, target.Y, 1e-3f) &&
              NearEqual(eye.Z + t * dz, target.Z, 1e-3f),
              "the reticle pixel ray reaches the target with combined head rotation");
    }
}

void ATargetAtOrBehindTheEyeIsHidden() {
    float x = 0.0f, y = 0.0f;
    Check(ProjectAimPoint({0, 0, 0}, {0, 0, 0}, {}, kTanH, kTanV, &x, &y) ==
              AimProjection::OffAxis, "a target at the eye has no screen projection");
    Check(ProjectAimPoint({100, 0, 0}, {120, 0, 0}, {}, kTanH, kTanV, &x, &y) ==
              AimProjection::OffAxis, "leaning beyond the target hides it");
}

}  // namespace

int RunAimProjectionTests() {
    std::cout << "\nAim projection\n";
    LeanStaysOnTheSameSurfacePoint();
    LeanWithHeadRotationUsesTheDrawnBasis();
    ATargetAtOrBehindTheEyeIsHidden();
    CentredWhenNothingIsApplied();
    HeadYawMovesTheMarkTheOtherWay();
    HeadPitchMovesTheMarkVertically();
    PureRollLeavesTheMarkCentred();
    PitchWithRollRotatesRatherThanWanders();
    PitchWithRollLeavesTheVerticalAxis();
    WorldYawStopsMovingTheMarkWhenLookingDown();
    OffTheAxisAndOffTheScreenAreReported();
    AccumulatedTurnsDoNotMoveTheMark();
    return olht_tests::TakeFailures();
}
