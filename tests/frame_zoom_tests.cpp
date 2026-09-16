// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The zoom correction (frame_zoom.h): the factor decided from the frame's field of view
// against the game's unzoomed one, and what that factor does to a pose.
//
// Both halves are where a silent fault lives. A factor that is wrong by a constant reads
// exactly like a factor that is right - the whole of normal play just runs at a fixed
// fraction of the pose - so the first thing asserted below is that an unzoomed frame
// comes out at exactly 1.0. And a scaling that reached roll would flatten a head tilt
// the player is holding, which no screenshot of a zoomed frame would show.

#include "test_support.h"

#include "frame_zoom.h"

#include <cmath>
#include <iostream>

namespace {

using namespace OutlastHeadTracking;
using olht_tests::Check;
using olht_tests::NearEqual;

constexpr float kDegToHalfRad = 3.14159265358979323846f / 360.0f;

float TanHalf(float deg) { return std::tan(deg * kDegToHalfRad); }

FrameSample Pose(float yaw, float pitch, float roll, float x, float y, float z) {
    FrameSample s;
    s.has_rotation = true;
    s.yaw = yaw;
    s.pitch = pitch;
    s.roll = roll;
    s.has_position = true;
    s.pos_x = x;
    s.pos_y = y;
    s.pos_z = z;
    return s;
}

void TestUnzoomedIsExactlyOne() {
    std::cout << "\n--- Zoom basis: the gate ---\n";

    // Outlast's gameplay pair, measured on steam-win64-20140429: the asylum renders 90
    // degrees standing still and the hero's own DefaultFOV reads the same 90. Anything
    // but 1.0000 here means the live angle and the reference are not the same
    // measurement - which is exactly what shipped while the reference was read off the
    // camera, whose DefaultFOV holds 80 on that build.
    const ZoomBasis play = DecideZoomBasis(0.0f, 90.0f, 90.0f);
    Check(play.valid, "an ordinary gameplay frame decides a basis");
    Check(NearEqual(play.factor, 1.0f, 1e-6f),
          "an unzoomed frame scales the pose by exactly 1.0");
    Check(NearEqual(play.rendered_fov, 90.0f) && NearEqual(play.rendered_base_fov, 90.0f),
          "and reports the angle it was drawn with on both sides of the ratio");

    // 80 is the same story at a different angle - the reference is whatever the game's
    // own unzoomed angle is on the frame, not a constant.
    Check(NearEqual(DecideZoomBasis(0.0f, 80.0f, 80.0f).factor, 1.0f, 1e-6f),
          "so does a frame at 80 degrees against an unzoomed 80");
}

void TestZoomShrinksTheFactor() {
    std::cout << "\n--- Zoom basis: the camcorder ---\n";

    // CamcorderMinFOV=15 against the asylum's 90.
    const ZoomBasis zoomed = DecideZoomBasis(0.0f, 15.0f, 90.0f);
    Check(zoomed.valid, "a zoomed frame decides a basis");
    Check(NearEqual(zoomed.factor, TanHalf(15.0f) / TanHalf(90.0f)),
          "the factor is the ratio of the two half-field tangents");
    Check(zoomed.factor < 0.14f && zoomed.factor > 0.12f,
          "so the camcorder's narrowest zoom scales the pose to about an eighth");

    // RunningFOV=100 against 90: a widened view moves the picture less, so the pose is
    // scaled UP to keep the same screen displacement.
    const ZoomBasis widened = DecideZoomBasis(0.0f, 100.0f, 90.0f);
    Check(widened.factor > 1.0f, "a widened view scales the pose up, not down");
    Check(NearEqual(widened.factor, TanHalf(100.0f) / TanHalf(90.0f)),
          "by the same ratio of tangents");
}

void TestTheOverrideMovesTheReference() {
    std::cout << "\n--- Zoom basis: with a field-of-view override ---\n";

    // With [View] FieldOfView=110 the player walks around at 110, so THAT is what an
    // unzoomed frame is, and the correction has to be 1.0 there rather than at the
    // game's own 90. Otherwise the override would be a permanent sensitivity change.
    const ZoomBasis play = DecideZoomBasis(110.0f, 90.0f, 90.0f);
    Check(play.valid, "an overridden gameplay frame decides a basis");
    Check(NearEqual(play.rendered_fov, 110.0f) && NearEqual(play.rendered_base_fov, 110.0f),
          "both sides of the ratio are the angle after the override");
    Check(NearEqual(play.factor, 1.0f, 1e-6f),
          "so ordinary play under an override is still exactly 1.0");

    // The override is a ratio on the ANGLE and the correction a ratio on the TANGENT,
    // and tan is not linear, so the override does not simply cancel out of a zoom.
    const ZoomBasis zoomed = DecideZoomBasis(110.0f, 45.0f, 90.0f);
    Check(NearEqual(zoomed.factor, TanHalf(55.0f) / TanHalf(110.0f)),
          "a zoom under an override is the ratio of the OVERRIDDEN angles");
    Check(!NearEqual(zoomed.factor, TanHalf(45.0f) / TanHalf(90.0f), 1e-3f),
          "which is not the ratio the same zoom would have had without one");
}

void TestUnreadableAnglesCompensateNothing() {
    std::cout << "\n--- Zoom basis: refusals ---\n";

    const float nan = std::nanf("");
    Check(!DecideZoomBasis(0.0f, 90.0f, 0.0f).valid,
          "a zero unzoomed angle - no controller, or a failed read - decides nothing");
    Check(NearEqual(DecideZoomBasis(0.0f, 90.0f, 0.0f).factor, 1.0f),
          "and leaves the factor at 1.0, which is no compensation rather than a guess");
    Check(!DecideZoomBasis(0.0f, 90.0f, 400.0f).valid,
          "an unzoomed angle outside any field of view means the offset has moved");
    Check(!DecideZoomBasis(0.0f, nan, 90.0f).valid, "a live angle that is not a number");
    Check(!DecideZoomBasis(0.0f, 90.0f, nan).valid, "an unzoomed angle that is not one");
    Check(!DecideZoomBasis(0.0f, 180.0f, 90.0f).valid,
          "a live angle at 180 degrees, where the tangent runs away");

    // The live angle gets the looser gate of the two on purpose: fifteen degrees is the
    // camcorder zoomed in, not a struct offset that has stopped fitting the build.
    Check(DecideZoomBasis(0.0f, 15.0f, 90.0f).valid,
          "but a live angle below the unzoomed gate is a real zoom and is compensated");
}

void TestScalingThePose() {
    std::cout << "\n--- Zoom scaling: what moves and what does not ---\n";

    const FrameSample raw = Pose(10.0f, 6.0f, 8.0f, 0.10f, 0.05f, 0.20f);
    const float factor = 0.5f;
    const FrameSample scaled = ScaledForZoom(raw, factor);

    // The tangent round trip, not a multiply: the screen displacement of an angle goes
    // as tan(angle) / tan(fov/2), so holding that fixed means tan(out) = tan(in)*factor.
    Check(NearEqual(scaled.yaw, std::atan(std::tan(10.0f * 2.0f * kDegToHalfRad) * factor)
                                    / (2.0f * kDegToHalfRad)),
          "yaw is rescaled through the tangent round trip");
    Check(scaled.yaw < raw.yaw && scaled.yaw > 0.0f, "so a zoom shrinks it");
    Check(NearEqual(scaled.pitch, std::atan(std::tan(6.0f * 2.0f * kDegToHalfRad) * factor)
                                      / (2.0f * kDegToHalfRad)),
          "pitch the same way");

    // The one that no screenshot would catch. Roll rotates the picture about the view
    // axis, by the same angle at every field of view there is.
    Check(scaled.roll == raw.roll, "roll is left exactly alone");

    Check(NearEqual(scaled.pos_x, 0.05f) && NearEqual(scaled.pos_y, 0.025f) &&
              NearEqual(scaled.pos_z, 0.10f),
          "the lean scales linearly on all three axes");

    Check(scaled.has_rotation == raw.has_rotation &&
              scaled.has_position == raw.has_position,
          "and which channels are present is untouched");
}

void TestScalingIsIdentityWhenItShouldBe() {
    std::cout << "\n--- Zoom scaling: the pass-through ---\n";

    const FrameSample raw = Pose(10.0f, 6.0f, 8.0f, 0.10f, 0.05f, 0.20f);
    const FrameSample same = ScaledForZoom(raw, 1.0f);
    Check(same.yaw == raw.yaw && same.pitch == raw.pitch && same.roll == raw.roll,
          "a factor of 1.0 returns the pose bit for bit, with no tangent round trip");
    Check(same.pos_x == raw.pos_x && same.pos_y == raw.pos_y && same.pos_z == raw.pos_z,
          "position included");

    const FrameSample nan = ScaledForZoom(raw, std::nanf(""));
    Check(nan.yaw == raw.yaw && nan.pos_z == raw.pos_z,
          "and a factor that is not a number leaves the pose alone rather than "
          "poisoning it");
    Check(ScaledForZoom(raw, 0.0f).yaw == raw.yaw, "so does a zero factor");

    // A pose with neither channel is what a frame with no packets yet looks like.
    FrameSample empty;
    const FrameSample scaledEmpty = ScaledForZoom(empty, 0.5f);
    Check(scaledEmpty.yaw == 0.0f && scaledEmpty.pos_z == 0.0f,
          "an empty pose stays empty through a zoom");
}

void TestAnglesPastTheQuarterTurnAreNotMirrored() {
    std::cout << "\n--- Zoom scaling: past the quarter turn ---\n";

    // The tangent round trip is only defined either side of 90 degrees: past it tan
    // changes sign, so an unguarded scaling sends a 100 degree head yaw to about -55 and
    // throws the view to the opposite side of the room the moment the camcorder zooms.
    // Those angles are ordinary - shaping the pose is the tracker's job and an amplified
    // opentrack curve reaches them from a small head turn.
    const float factor = 0.25f;
    for (const float angle : { 90.0f, 95.0f, 100.0f, 179.0f, -100.0f }) {
        const FrameSample scaled =
            ScaledForZoom(Pose(angle, angle, 0.0f, 0.0f, 0.0f, 0.0f), factor);
        Check(scaled.yaw == angle, "a yaw at or past the quarter turn is left as it is");
        Check(scaled.pitch == angle, "and so is the pitch");
    }

    // Still scaled right up to the singularity, and still continuous with it: the round
    // trip tends to 90 as the angle does, so the guard adds no step.
    const FrameSample near = ScaledForZoom(Pose(89.9f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
                                           factor);
    Check(near.yaw > 89.0f && near.yaw < 90.0f,
          "just inside it the round trip still runs, and lands just inside 90");

    // The lean is a length, not an angle, so nothing here changes it.
    const FrameSample leaned = ScaledForZoom(Pose(120.0f, 0.0f, 0.0f, 0.2f, 0.0f, 0.0f),
                                             factor);
    Check(NearEqual(leaned.pos_x, 0.05f),
          "and the lean still scales while the angle is left alone");
}

void TestTheLimitsSurviveTheScaling() {
    std::cout << "\n--- Zoom scaling: the configured limits still bound the lean ---\n";

    // The processor clamps the lean on its way out and the zoom scaling then multiplies
    // it, so on a frame the game draws WIDER than its unzoomed angle the eye leaves the
    // box the player configured. Those limits are what keep the eye inside the body, so
    // the clamp has to be the last thing to touch the lean.
    const PositionLimits limits{ 0.30f, 0.20f, 0.20f, 0.40f, 0.10f };

    FrameSample s;
    s.has_position = true;
    s.pos_x = 0.30f;
    s.pos_y = 0.20f;
    s.pos_z = -0.40f;

    // 100 degrees drawn against an unzoomed 90 - the game widening the view for a run.
    const float factor = DecideZoomBasis(0.0f, 100.0f, 90.0f).factor;
    Check(factor > 1.0f, "a frame drawn wider than the unzoomed angle scales the pose up");

    const FrameSample scaled = ScaledForZoom(s, factor);
    Check(scaled.pos_z < -0.40f, "which on its own carries a full lean past its limit");

    const FrameSample bounded = ClampedToLimits(scaled, limits);
    Check(NearEqual(bounded.pos_x, 0.30f), "clamped back to the lateral limit");
    Check(NearEqual(bounded.pos_y, 0.20f), "and to the upward one");
    Check(NearEqual(bounded.pos_z, -0.40f), "and to the forward one, on the negative z");

    // The asymmetry has to survive too: the generous limit is the forward lean.
    FrameSample back;
    back.has_position = true;
    back.pos_z = 0.40f;
    Check(NearEqual(ClampedToLimits(back, limits).pos_z, 0.10f),
          "pulling back is still held to the tighter backward limit");

    // Nothing to clamp when the channel is absent.
    FrameSample rotationOnly;
    rotationOnly.has_position = false;
    rotationOnly.pos_x = 5.0f;
    Check(ClampedToLimits(rotationOnly, limits).pos_x == 5.0f,
          "a sample carrying no position is passed through untouched");

    // The camera hook asks TrackingRuntime for the limits only on a frame whose scaled
    // sample carries a lean, because that flag is also what proves the configuration
    // those limits come from has been published to the render thread. The scaling must
    // therefore never invent it - on any branch, including the pass-throughs.
    FrameSample noLean;
    noLean.has_rotation = true;
    noLean.yaw = 10.0f;
    for (const float zoom : { 1.0f, 0.25f, 4.0f, 0.0f, std::nanf("") }) {
        Check(!ScaledForZoom(noLean, zoom).has_position,
              "a sample with no lean never gains one through the zoom scaling");
    }
}

void TestTheFactorIsPublished() {
    std::cout << "\n--- Zoom factor: the frame's own ---\n";

    Check(NearEqual(FrameZoomFactor(), 1.0f),
          "before any frame is drawn the factor is 1.0, so a pose is applied unscaled");
    PublishFrameZoom(0.25f);
    Check(NearEqual(FrameZoomFactor(), 0.25f), "what the field-of-view hook publishes is "
                                               "what the camera hook reads");
    PublishFrameZoom(1.0f);
}

}  // namespace

int RunFrameZoomTests() {
    std::cout << "\n=== Zoom compensation ===\n";
    TestUnzoomedIsExactlyOne();
    TestZoomShrinksTheFactor();
    TestTheOverrideMovesTheReference();
    TestUnreadableAnglesCompensateNothing();
    TestScalingThePose();
    TestScalingIsIdentityWhenItShouldBe();
    TestAnglesPastTheQuarterTurnAreNotMirrored();
    TestTheLimitsSurviveTheScaling();
    TestTheFactorIsPublished();
    return olht_tests::TakeFailures();
}
