// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Where the camcorder's light ends up pointing once the head has turned. The mod may
// only change the light's own rotator, while the direction that matters is the one that
// rotator produces after the engine has multiplied it by the parent's transform - so the
// thing worth locking is the round trip: turn the rotator, compose it the way the engine
// does, and check the world direction is the one the frame was drawn along.

#include "test_support.h"

#include "light_composition.h"
#include "ue3_rotation.h"
#include "ue3_types.h"

#include <cmath>
#include <iostream>

namespace {

using namespace OutlastHeadTracking;
using olht_tests::Check;
using olht_tests::NearEqual;

ParentTransform Identity() {
    ParentTransform parent{};
    parent.m[0] = 1.0f;
    parent.m[5] = 1.0f;
    parent.m[10] = 1.0f;
    parent.m[15] = 1.0f;
    return parent;
}

// The parent the game was measured handing the camcorder's light: a quarter turn about
// world Z, with the player's position in the fourth row.
ParentTransform QuarterTurn() {
    ParentTransform parent = Identity();
    parent.m[0] = 0.0f;  parent.m[1] = -1.0f;
    parent.m[4] = 1.0f;  parent.m[5] = 0.0f;
    parent.m[12] = 3689.8f;
    parent.m[13] = 4291.7f;
    parent.m[14] = 659.0f;
    return parent;
}

ParentTransform Scaled(float scale) {
    ParentTransform parent = QuarterTurn();
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            parent.m[row * 4 + column] *= scale;
        }
    }
    return parent;
}

// The world direction a light with this rotator points once the engine has composed it
// with the parent - `v * rotation * parent`, the first row being the forward axis.
void WorldForward(const UE3Rotator& rotation, const ParentTransform& parent, float out[3]) {
    Mat3 parentRotation;
    Check(detail::ParentRotation(parent, &parentRotation), "the parent is a rotation");
    const Mat3 world = MatMul(RotatorToMatrix(rotation), parentRotation);
    out[0] = world.m[0][0];
    out[1] = world.m[0][1];
    out[2] = world.m[0][2];
}

float AngleBetweenDegrees(const float a[3], const float b[3]) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    if (dot > 1.0f) dot = 1.0f;
    if (dot < -1.0f) dot = -1.0f;
    return static_cast<float>(std::acos(dot) * 180.0 / kPi);
}

// The whole point, expressed once and reused: whatever the light was pointing at and
// whatever the parent is doing, after the turn it points where the frame was drawn -
// which is the direction the head is looking, not the one the game is aiming along.
void ChecksOut(const char* what, const UE3Rotator& lightRotation,
               const ParentTransform& parent, const UE3Rotator& clean,
               const UE3Rotator& drawn) {
    UE3Rotator turned{};
    Check(TurnedByTheHead(lightRotation, parent, clean, drawn, &turned), what);

    float before[3];
    float after[3];
    float cleanForward[3];
    float drawnForward[3];
    WorldForward(lightRotation, parent, before);
    WorldForward(turned, parent, after);
    const Mat3 cleanMatrix = RotatorToMatrix(clean);
    const Mat3 drawnMatrix = RotatorToMatrix(drawn);
    cleanForward[0] = cleanMatrix.m[0][0];
    cleanForward[1] = cleanMatrix.m[0][1];
    cleanForward[2] = cleanMatrix.m[0][2];
    drawnForward[0] = drawnMatrix.m[0][0];
    drawnForward[1] = drawnMatrix.m[0][1];
    drawnForward[2] = drawnMatrix.m[0][2];

    // The light starts out pointing where the game is aiming, which is what the engine
    // leaves it doing and what the mod is there to change.
    Check(AngleBetweenDegrees(before, cleanForward) < 0.05f,
          "the light starts out pointing where the game aims");
    Check(AngleBetweenDegrees(after, drawnForward) < 0.05f,
          "and after the turn it points along the drawn frame");
}

// A light whose rotator composes with the parent to the clean view direction, which is
// the state the engine leaves it in every frame.
UE3Rotator LightPointingWhereTheGameAims(const UE3Rotator& clean,
                                         const ParentTransform& parent) {
    Mat3 parentRotation;
    Check(detail::ParentRotation(parent, &parentRotation), "the parent is a rotation");
    UE3Rotator rotation{};
    MatrixToRotator(MatMul(RotatorToMatrix(clean), detail::Transposed(parentRotation)),
                    &rotation);
    return rotation;
}

void APlainYawTurn() {
    const ParentTransform parent = QuarterTurn();
    const UE3Rotator clean{ -299, -26376, 0 };
    const UE3Rotator drawn{ -299, -26376 + 4551, 0 };  // 25 degrees of head yaw
    ChecksOut("a 25 degree head yaw composes", LightPointingWhereTheGameAims(clean, parent),
              parent, clean, drawn);
}

void PitchYawAndRollTogether() {
    const ParentTransform parent = QuarterTurn();
    const UE3Rotator clean{ -13746, -30504, -715 };
    const UE3Rotator drawn{ -11015, -26863, -2535 };
    ChecksOut("a combined pose composes", LightPointingWhereTheGameAims(clean, parent),
              parent, clean, drawn);
}

void AParentThatIsNotTurned() {
    const ParentTransform parent = Identity();
    const UE3Rotator clean{ 2000, 12000, 0 };
    const UE3Rotator drawn{ 4000, 16000, 900 };
    ChecksOut("an unturned parent composes", LightPointingWhereTheGameAims(clean, parent),
              parent, clean, drawn);
}

void AParentCarryingScale() {
    // The scale is divided out rather than composed: a parent scaled by four still turns
    // the light by the same angle, and leaving the scale in would tilt it instead.
    const ParentTransform parent = Scaled(4.0f);
    const UE3Rotator clean{ 1200, -8000, 0 };
    const UE3Rotator drawn{ 1200, -8000 + 4551, 0 };
    ChecksOut("a scaled parent composes", LightPointingWhereTheGameAims(clean, parent),
              parent, clean, drawn);
}

void AHeadThatHasNotMoved() {
    const ParentTransform parent = QuarterTurn();
    const UE3Rotator clean{ 500, 9000, 0 };
    const UE3Rotator light = LightPointingWhereTheGameAims(clean, parent);
    UE3Rotator turned{};
    Check(TurnedByTheHead(light, parent, clean, clean, &turned), "a centred head composes");
    // Not bit-identical: the rotator goes through a matrix and back, so it lands within a
    // unit of where it started - 1/182 of a degree.
    Check(std::abs(turned.Pitch - light.Pitch) <= 1 && std::abs(turned.Yaw - light.Yaw) <= 1 &&
              std::abs(turned.Roll - light.Roll) <= 1,
          "a centred head leaves the light where the game put it");
}

void AParentWithNoRotationAtAll() {
    ParentTransform parent{};
    UE3Rotator turned{};
    const UE3Rotator clean{ 0, 0, 0 };
    const UE3Rotator drawn{ 0, 4551, 0 };
    Check(!TurnedByTheHead(UE3Rotator{ 0, 0, 0 }, parent, clean, drawn, &turned),
          "an all-zero parent is refused rather than composed against");
}

}  // namespace

int RunLightCompositionTests() {
    std::cout << "Camcorder light composition\n";
    APlainYawTurn();
    PitchYawAndRollTogether();
    AParentThatIsNotTurned();
    AParentCarryingScale();
    AHeadThatHasNotMoved();
    AParentWithNoRotationAtAll();
    return olht_tests::TakeFailures();
}
