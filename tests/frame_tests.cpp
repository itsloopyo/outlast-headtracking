// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Characterization tests for the three things a frame publishes about itself: which
// CalcSceneView call is drawing it (render_frame.h), which of the matrices in the
// FSceneView is the projection (projection_scan.h) and what perspective it was drawn
// with (frame_projection.h). The first is half of what picks the frame's viewpoint out
// of the ones script asked for; the last is what anything placed in the frame divides
// by.

#include "test_support.h"

#include "frame_projection.h"
#include "projection_scan.h"
#include "render_frame.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

using namespace OutlastHeadTracking;
using olht_tests::Check;
using olht_tests::NearEqual;

void Publish(bool valid, float tanH, float tanV) {
    FrameProjection& p = GetFrameProjection();
    p.tan_half_h.store(tanH, std::memory_order_relaxed);
    p.tan_half_v.store(tanV, std::memory_order_relaxed);
    p.valid.store(valid, std::memory_order_release);
}

void NoProjectionUntilAFramePublishesOne() {
    Publish(false, 0.0f, 0.0f);
    float h = -1.0f, v = -1.0f;
    Check(!TryGetFrameProjection(h, v), "no frame has published a projection yet");
    Check(h == -1.0f && v == -1.0f, "and the caller's outputs are left alone");
}

void PublishedTangentsAreReadBack() {
    // A measured pair on steam-win64-20140429 at 16:9: 80 degrees projected
    // tanH=0.93233 tanV=0.52444.
    Publish(true, 0.93233f, 0.52444f);
    float h = 0.0f, v = 0.0f;
    Check(TryGetFrameProjection(h, v), "a published projection is readable");
    Check(NearEqual(h, 0.93233f) && NearEqual(v, 0.52444f),
          "and comes back as the pair that was published");
}

void NonPositiveTangentsAreRefused() {
    // A consumer that rolled its own validity check is a consumer that eventually
    // divides by a zero the publisher never meant to hand out.
    float h = 0.0f, v = 0.0f;
    Publish(true, 0.0f, 0.5f);
    Check(!TryGetFrameProjection(h, v), "a zero horizontal tangent is refused");
    Publish(true, 0.5f, -0.5f);
    Check(!TryGetFrameProjection(h, v), "a negative vertical tangent is refused");
    Publish(true, std::nanf(""), 0.5f);
    Check(!TryGetFrameProjection(h, v), "a NaN tangent is refused");
}

void TangentsConvertBackToTheMeasuredFrame() {
    // 0.93233 and 0.52444 are an 86.0 by 55.3 degree frame - the numbers measured in
    // game, and what the log line reports.
    Check(NearEqual(FovDegreesFromHalfTangent(0.93233f), 86.0f, 0.1f),
          "the horizontal tangent is an 86 degree frame");
    Check(NearEqual(FovDegreesFromHalfTangent(0.52444f), 55.3f, 0.1f),
          "the vertical tangent is a 55.3 degree frame");
    Check(NearEqual(FovDegreesFromHalfTangent(1.0f), 90.0f, 1e-3f),
          "a unit tangent is a right angle");
}

void DrawingFrameIsScopedToTheCall() {
    Check(!IsDrawingFrame(), "no frame is being drawn outside a scene-view call");
    {
        const DrawingFrameScope drawing(true);
        Check(IsDrawingFrame(), "the scope marks the enclosing call as the drawing one");
    }
    Check(!IsDrawingFrame(), "and the mark is gone once the call returns");

    {
        const DrawingFrameScope projecting(false);
        Check(!IsDrawingFrame(),
              "a Project or Deproject call is not marked as drawing the frame");
    }
}

// The calls nest: the script behind the frame's own GetPlayerViewPoint can reach
// ULocalPlayer::Project, which enters the scene-view detour again from inside the drawing
// one. The inner call must not be mistaken for the frame, and the outer one must still be
// the frame after it returns - the second half is what a scope that cleared the flag on
// the way out would get wrong, and the symptom is a frame the pose silently never
// reaches.
void NestedCallsRestoreTheEnclosingFrame() {
    const DrawingFrameScope drawing(true);
    Check(IsDrawingFrame(), "the outer scene-view call is drawing the frame");
    {
        const DrawingFrameScope projecting(false);
        Check(!IsDrawingFrame(), "a Project nested inside it is not");
    }
    Check(IsDrawingFrame(), "and the outer call is still the frame after it returns");
}

// The projection CalcSceneView built on steam-win64-20140429 at 16:9 for the camera's
// unzoomed 80 degrees: tanH=0.93233 tanV=0.52444, with MinZ=MaxZ=10 giving the 0.999 and
// -9.99 in the depth column. Row-vector layout, so m[8] and m[9] are row 2's free
// entries and m[11] is the 1 that divides by view depth.
ProjectionMatrix MeasuredProjection() {
    ProjectionMatrix p{};
    p.m[0]  = 1.0f / 0.93233f;
    p.m[5]  = 1.0f / 0.52444f;
    p.m[10] = 0.999f;
    p.m[11] = 1.0f;
    p.m[14] = -9.99f;
    return p;
}

void TheMeasuredProjectionIsRecognised() {
    const ProjectionMatrix p = MeasuredProjection();
    Check(LooksLikeProjection(p.m), "the matrix the game drew a frame with is recognised");

    float tanH = 0.0f, tanV = 0.0f;
    Check(ProjectionTangents(p.m, tanH, tanV), "and its tangents can be read off it");
    Check(NearEqual(tanH, 0.93233f) && NearEqual(tanV, 0.52444f),
          "which are the pair measured in game");
}

void RowTwosFreeEntriesDoNotDisqualifyIt() {
    // A projection jitter is written into row 2's first two entries, so a frame that has
    // one must still be recognised as the same matrix.
    ProjectionMatrix p = MeasuredProjection();
    p.m[8] = 0.0007f;
    p.m[9] = -0.0011f;
    Check(LooksLikeProjection(p.m), "a jittered projection is still a projection");
}

void OtherMatricesInTheObjectAreRejected() {
    // The view matrix has its 1 in the last slot rather than at m[11]. Accepting it would
    // hand every consumer two numbers off the camera's orientation instead of its field
    // of view, and nothing downstream could tell.
    ProjectionMatrix view{};
    view.m[0] = view.m[5] = view.m[10] = view.m[15] = 1.0f;
    Check(!LooksLikeProjection(view.m), "the view matrix is not mistaken for the projection");

    // A concatenated view-projection has non-zero entries where the projection's row 0
    // and row 1 are zero.
    ProjectionMatrix concatenated = MeasuredProjection();
    concatenated.m[1] = 0.31f;
    Check(!LooksLikeProjection(concatenated.m),
          "a concatenated view-projection is rejected");

    // The 1 at m[11] is the whole of what makes this the matrix that divides by view
    // depth. Without that test a matrix with the right diagonal but no perspective
    // divide - an orthographic one, or an unrelated pair of floats that happen to sit
    // in the tangent bounds - is accepted and every placement is projected with it.
    ProjectionMatrix noDepthDivide = MeasuredProjection();
    noDepthDivide.m[11] = 0.0f;
    Check(!LooksLikeProjection(noDepthDivide.m),
          "a matrix that does not divide by view depth is not the projection");

    // Zeroed memory passes the zero tests and nothing else.
    ProjectionMatrix zeroed{};
    Check(!LooksLikeProjection(zeroed.m), "a block of zeroes is not a projection");
}

void ImplausibleFieldsAreRejected() {
    // The bounds are what stop an unrelated 1.0f at m[11] from being read as a frame the
    // game could not have drawn.
    ProjectionMatrix tooNarrow = MeasuredProjection();
    tooNarrow.m[0] = 1.0f / (kMinProjectionTan * 0.5f);
    Check(!LooksLikeProjection(tooNarrow.m),
          "a horizontal field narrower than the camcorder's own zoom is refused");

    ProjectionMatrix tooWide = MeasuredProjection();
    tooWide.m[5] = 1.0f / (kMaxProjectionTan * 2.0f);
    Check(!LooksLikeProjection(tooWide.m), "and one wider than any display is too");

    ProjectionMatrix negative = MeasuredProjection();
    negative.m[0] = -1.07258f;
    Check(!LooksLikeProjection(negative.m), "a negative scale is not a field of view");

    ProjectionMatrix notANumber = MeasuredProjection();
    notANumber.m[10] = std::nanf("");
    Check(!LooksLikeProjection(notANumber.m),
          "a matrix carrying a NaN anywhere in it is refused");
}

void BadTangentsLeaveTheCallersOutputsAlone() {
    // Same contract TryGetFrameProjection offers: a refused read must not have written
    // an infinity into what the caller was going to divide by.
    ProjectionMatrix p = MeasuredProjection();
    p.m[0] = 0.0f;
    float tanH = -1.0f, tanV = -1.0f;
    Check(!ProjectionTangents(p.m, tanH, tanV), "a zero scale yields no tangents");
    Check(tanH == -1.0f && tanV == -1.0f, "and the caller's outputs are untouched");

    p.m[0] = 1.07258f;
    p.m[5] = std::numeric_limits<float>::infinity();
    Check(!ProjectionTangents(p.m, tanH, tanV), "nor does an infinite one");
    Check(tanH == -1.0f && tanV == -1.0f, "with the outputs still untouched");
}

void TheSearchWindowCanHoldAMatrix() {
    // The scan steps by the stride and reads a whole matrix at each offset, so a window
    // that cannot hold one, or a stride that does not divide the field alignment, would
    // walk past the projection without ever testing it.
    Check(kProjectionMatrixBytes == 64, "a UE3 matrix is sixteen floats");
    Check(kProjectionSearchBytes >= kProjectionMatrixBytes,
          "the search window holds at least one matrix");
    Check(kProjectionSearchBytes % kProjectionScanStride == 0,
          "and the stride divides the window, so the last offset is reachable");
}

}  // namespace

int RunFrameTests() {
    std::cout << "Frame projection and draw scoping\n";
    NoProjectionUntilAFramePublishesOne();
    PublishedTangentsAreReadBack();
    NonPositiveTangentsAreRefused();
    TangentsConvertBackToTheMeasuredFrame();
    DrawingFrameIsScopedToTheCall();
    NestedCallsRestoreTheEnclosingFrame();
    TheMeasuredProjectionIsRecognised();
    RowTwosFreeEntriesDoNotDisqualifyIt();
    OtherMatricesInTheObjectAreRejected();
    ImplausibleFieldsAreRejected();
    BadTangentsLeaveTheCallersOutputsAlone();
    TheSearchWindowCanHoldAMatrix();
    return olht_tests::TakeFailures();
}
