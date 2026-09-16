// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Characterization tests for the FRotator arithmetic in ue3_types.h and the rotation
// matrix in ue3_rotation.h. These lock the unit conversion, the modular fold and the
// pitch clamp: every one of them is a place where a "tidy-up" silently changes where
// the camera points, and no in-game playtest catches a half-degree of drift.

#include "test_support.h"

#include "ue3_rotation.h"
#include "ue3_types.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

using namespace OutlastHeadTracking;
using olht_tests::Check;
using olht_tests::NearEqual;

void DegreeConversion() {
    // 65536 units to the turn, so 30 degrees is 5461.33 units and rounds to 5461. This
    // is the number the in-game verification on steam-win64-20140429 measured: a held
    // +30 degree tracker yaw turned the game's own yaw 32767 into 38228.
    Check(DegToUnits(30.0f) == 5461, "30 degrees is 5461 rotator units");
    Check(DegToUnits(-30.0f) == -5461, "-30 degrees is -5461 rotator units");
    Check(DegToUnits(0.0f) == 0, "zero degrees is zero units");
    Check(DegToUnits(90.0f) == 16384, "90 degrees is a quarter turn");
    Check(DegToUnits(180.0f) == 32768, "180 degrees is a half turn");

    // Degrees arrive off the network, so a value no int32 could hold must fold onto the
    // rotation it denotes rather than overflow the cast.
    Check(DegToUnits(360.0f) == 0, "a whole turn folds onto zero");
    Check(DegToUnits(390.0f) == DegToUnits(30.0f), "a turn past 30 degrees is 30 degrees");
    Check(std::abs(DegToUnits(1e30f)) <= kUnitsPerRevolutionInt,
          "an angle no int32 could hold still folds into one revolution");
    Check(DegToUnits(std::numeric_limits<float>::infinity()) == 0,
          "a non-finite angle converts to zero");
    Check(DegToUnits(std::nanf("")) == 0, "NaN converts to zero");
}

void RadianConversion() {
    Check(NearEqual(UnitsToRad(16384), static_cast<float>(kPi / 2.0)),
          "a quarter turn is pi/2 radians");
    Check(NearEqual(UnitsToRad(0), 0.0f), "zero units is zero radians");
    Check(RadToUnits(static_cast<float>(kPi / 2.0)) == 16384,
          "pi/2 radians is a quarter turn");
    Check(RadToUnits(0.0f) == 0, "zero radians is zero units");
}

void SignedFold() {
    Check(WrapSigned(0) == 0, "zero is already folded");
    Check(WrapSigned(32767) == 32767, "the top of the half-open range survives");
    // 180 degrees has ONE encoding after the fold, which is what lets two rotators be
    // compared at all.
    Check(WrapSigned(32768) == -32768, "a half turn folds onto the negative end");
    Check(WrapSigned(60000) == 60000 - 65536, "60000 units means -5536");
    Check(WrapSigned(-60000) == 65536 - 60000, "-60000 units means 5536");
    Check(WrapSigned(65536) == 0, "a whole turn folds onto zero");
    Check(WrapSigned(131072 + 100) == 100, "two turns and a bit folds onto the bit");
}

void PitchClamp() {
    Check(ClampPitch(0) == 0, "a level pitch is untouched");
    Check(ClampPitch(kMaxPitchUnits) == kMaxPitchUnits, "the limit itself is kept");
    Check(ClampPitch(kMaxPitchUnits + 1) == kMaxPitchUnits, "past vertical is stopped short");
    Check(ClampPitch(-kMaxPitchUnits - 1) == -kMaxPitchUnits,
          "past vertical downwards is stopped short");
    Check(kMaxPitchUnits == 16383, "the clamp is one unit short of straight up");
    // The fold happens before the compare, so an unbounded rotator from the game is
    // measured as the angle it denotes.
    Check(ClampPitch(65536 + 100) == 100, "a whole turn past level is level");
}

void MatrixRoundTrip() {
    // RotatorToMatrix and MatrixToRotator are inverses over the range the camera-local
    // yaw branch composes in, which is what makes that branch safe to compose through.
    const UE3Rotator source{ 3000, -20000, 1500 };
    UE3Rotator back{};
    MatrixToRotator(RotatorToMatrix(source), &back);
    Check(std::abs(back.Pitch - source.Pitch) <= 1, "pitch survives the matrix round trip");
    Check(std::abs(back.Yaw - source.Yaw) <= 1, "yaw survives the matrix round trip");
    Check(std::abs(back.Roll - source.Roll) <= 1, "roll survives the matrix round trip");
}

// The engine accumulates turns into an FRotator's plain int32 fields - PlausibleRotator
// bounds pitch and roll for that reason and deliberately leaves yaw alone - and the
// conversion to radians goes through float, so an unfolded yaw of a few thousand turns
// names a direction that is measurably not the one it denotes. The rotator overload
// folds first, so the two agree.
void MatrixIgnoresAccumulatedTurns() {
    const UE3Rotator folded{ 3000, -20000, 1500 };
    const UE3Rotator spun{ folded.Pitch + 65536 * 3, folded.Yaw + 65536 * 30517,
                           folded.Roll - 65536 * 7 };
    const Mat3 a = RotatorToMatrix(folded);
    const Mat3 b = RotatorToMatrix(spun);
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            Check(NearEqual(a.m[row][column], b.m[row][column]),
                  "whole turns do not move the rotation matrix");
        }
    }
}

void MatrixRowsAreOrthonormal() {
    const Mat3 m = RotatorToMatrix(UnitsToRad(4000), UnitsToRad(9000), UnitsToRad(-2500));
    for (int row = 0; row < 3; ++row) {
        const float length = std::sqrt(m.m[row][0] * m.m[row][0] +
                                       m.m[row][1] * m.m[row][1] +
                                       m.m[row][2] * m.m[row][2]);
        Check(NearEqual(length, 1.0f, 1e-3f), "rotation matrix row is unit length");
    }
}

// The light probe reads the aim direction as row 0 of the matrix rather than building
// its own forward vector, so this is the property that lets it. Row 1 is the right axis
// and row 2 the up axis, both spelled out for the same reason.
void FirstRowIsTheForwardAxis() {
    const float pitch = UnitsToRad(4000);
    const float yaw = UnitsToRad(9000);
    const Mat3 m = RotatorToMatrix(pitch, yaw, UnitsToRad(-2500));
    Check(NearEqual(m.m[0][0], std::cos(pitch) * std::cos(yaw)),
          "row 0 x is cos(pitch) * cos(yaw)");
    Check(NearEqual(m.m[0][1], std::cos(pitch) * std::sin(yaw)),
          "row 0 y is cos(pitch) * sin(yaw)");
    Check(NearEqual(m.m[0][2], std::sin(pitch)), "row 0 z is sin(pitch)");

    // Roll turns the frame about that axis and leaves it alone, which is why a forward
    // vector built from pitch and yaw alone agrees with it.
    const Mat3 rolled = RotatorToMatrix(pitch, yaw, UnitsToRad(12000));
    for (int axis = 0; axis < 3; ++axis) {
        Check(NearEqual(rolled.m[0][axis], m.m[0][axis]),
              "the forward axis does not move with roll");
    }
}

void IdentityComposition() {
    const UE3Rotator clean{ 1234, -5678, 910 };
    const Mat3 identity = RotatorToMatrix(0.0f, 0.0f, 0.0f);
    UE3Rotator composed{};
    MatrixToRotator(MatMul(identity, RotatorToMatrix(clean)), &composed);
    Check(std::abs(composed.Pitch - clean.Pitch) <= 1, "identity leaves pitch alone");
    Check(std::abs(composed.Yaw - clean.Yaw) <= 1, "identity leaves yaw alone");
    Check(std::abs(composed.Roll - clean.Roll) <= 1, "identity leaves roll alone");
}

}  // namespace

int RunUe3MathTests() {
    std::cout << "UE3 rotator math\n";
    DegreeConversion();
    RadianConversion();
    SignedFold();
    PitchClamp();
    MatrixRoundTrip();
    MatrixIgnoresAccumulatedTurns();
    MatrixRowsAreOrthonormal();
    FirstRowIsTheForwardAxis();
    IdentityComposition();
    return olht_tests::TakeFailures();
}
