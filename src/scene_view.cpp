// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "scene_view.h"

#include "fov_override.h"
#include "camera_hook.h"
#include "frame_projection.h"
#include "log_once.h"
#include "logging.h"
#include "minhook_util.h"
#include "projection_scan.h"
#include "render_frame.h"

#include "cameraunlock/memory/safe_memory.h"

#include <windows.h>
#include <intrin.h>

#include <cstdint>

namespace OutlastHeadTracking {

namespace {

// ULocalPlayer::CalcSceneView(FSceneViewFamily*, FVector& out_ViewLocation,
// FRotator& out_ViewRotation, FViewport*, FViewElementDrawer*), returning the FSceneView
// the frame is rendered from.
using CalcSceneView_t = void*(*)(void* thisptr, void* family, void* outLocation,
                                 void* outRotation, void* viewport, void* viewDrawer);

CalcSceneView_t g_original = nullptr;
void*           g_drawReturn = nullptr;

// Byte offset of the projection matrix inside the returned FSceneView, found once by
// shape and reused. -1 until then.
int g_projectionOffset = -1;

// Every read of the FSceneView goes through this rather than through a dereference. The
// object is the engine's, its size is not something the mod knows, and the scan walks a
// kilobyte from its start: an FSceneView shorter than that and ending on the last
// committed page faults the RENDER thread, which is the game gone rather than a
// diagnostic. A guarded copy turns the same case into an offset the scan skips.
bool ReadProjectionAt(const void* view, int offset, ProjectionMatrix& out) {
    return cameraunlock::memory::SafeRead(
        reinterpret_cast<std::uintptr_t>(view) + static_cast<std::uintptr_t>(offset), out);
}

// Reported with the two terms anything placed in the frame divides by, so a placement
// argument can be settled against the numbers the renderer used instead of against an
// assumed field-of-view convention. Read back through TryGetFrameProjection rather than
// off the matrix, so the line says what a consumer will actually get rather than what
// was meant for it.
//
// Twice per session at most: once for a frame the game chose the angle for, and once for
// a frame [View] FieldOfView chose it for. The second line is how the override is known
// to have reached the renderer - this is the same projection everything else is placed
// with, so an angle that moved here moved for them too.
void ReportProjection(int offset) {
    static bool reportedGame = false;
    static bool reportedOverridden = false;
    const bool overridden = FovOverrideActive();
    bool& latch = overridden ? reportedOverridden : reportedGame;
    if (latch) {
        return;
    }
    float tanH = 0.0f;
    float tanV = 0.0f;
    if (!TryGetFrameProjection(tanH, tanV)) {
        return;
    }
    latch = true;
    Log::Line("Scene view projection at FSceneView+0x%X (%s field of view): tanH=%.5f "
              "tanV=%.5f, so the frame is %.1f degrees across by %.1f down, at aspect "
              "%.4f", offset, overridden ? "overridden" : "the game's", tanH, tanV,
              FovDegreesFromHalfTangent(tanH), FovDegreesFromHalfTangent(tanV),
              tanH / tanV);
}

// Frames the scan may come up empty on before it says so. The first frames of a level
// are drawn before the view is fully built, so one miss means nothing; a second of them
// means the matrix is not where this looks or is not the shape this recognises, and the
// reason is owed to whoever reads the log.
constexpr unsigned long kScanFailuresBeforeReport = 120;
unsigned long g_scanFailures = 0;

void ReportScanFailureOnce() {
    static bool reported = false;
    if (!ClaimOnce(reported)) {
        return;
    }
    Log::Line("WARN: no projection matrix found in the first 0x%X bytes of FSceneView "
              "after %lu frames (looking for a perspective row-vector matrix whose "
              "half-field tangents both fall in %.3f..%.1f). Nothing that has to be "
              "placed in the frame can be - head tracking itself is unaffected.",
              kProjectionSearchBytes, kScanFailuresBeforeReport, kMinProjectionTan,
              kMaxProjectionTan);
}

void ScanForProjection(const void* view) {
    for (int offset = 0; offset + kProjectionMatrixBytes <= kProjectionSearchBytes;
         offset += kProjectionScanStride) {
        ProjectionMatrix candidate;
        if (!ReadProjectionAt(view, offset, candidate)) {
            continue;
        }
        if (!LooksLikeProjection(candidate.m)) {
            continue;
        }
        g_projectionOffset = offset;
        return;
    }
    if (++g_scanFailures >= kScanFailuresBeforeReport) {
        ReportScanFailureOnce();
    }
}

void PublishProjection(const ProjectionMatrix& matrix) {
    FrameProjection& projection = GetFrameProjection();
    // The tangents are stored rather than the matrix entries, because that is what a
    // projection divides by and because a zero entry would reach a consumer as an
    // infinity if it were inverted there instead.
    float tanH = 0.0f;
    float tanV = 0.0f;
    if (!ProjectionTangents(matrix.m, tanH, tanV)) {
        projection.valid.store(false, std::memory_order_relaxed);
        return;
    }
    projection.tan_half_h.store(tanH, std::memory_order_relaxed);
    projection.tan_half_v.store(tanV, std::memory_order_relaxed);
    projection.last_aspect.store(tanH / tanV, std::memory_order_relaxed);
    projection.valid.store(true, std::memory_order_release);
}

void* Detour(void* thisptr, void* family, void* outLocation, void* outRotation,
             void* viewport, void* viewDrawer) {
    // Held across the original call rather than tested inside it, because what the camera
    // hook needs to know is which CalcSceneView it is running under, and only this frame
    // can see that.
    const bool drawingFrame = _ReturnAddress() == g_drawReturn;
    if (drawingFrame) {
        BeginCameraFrame();
        GetFrameProjection().valid.store(false, std::memory_order_relaxed);
    }
    void* view = nullptr;
    {
        const DrawingFrameScope drawing(drawingFrame);
        view = g_original(thisptr, family, outLocation, outRotation, viewport, viewDrawer);
    }
    if (!view || !drawingFrame) {
        return view;
    }

    // Only on the frames that draw. The scan is 241 guarded 64-byte reads, and running it
    // from ULocalPlayer::Project and Deproject as well pays that for views the mod never
    // places anything in - and counts their failures against the report threshold, so a
    // burst of script projections during a load could report a failure that the drawing
    // frames were not having. It is NOT abandoned after a failure: every frame builds a new
    // FSceneView, so a build where the matrix only appears once gameplay starts still finds
    // it. What it costs until then is one scan per drawn frame.
    if (g_projectionOffset < 0) {
        if (!drawingFrame) {
            return view;
        }
        ScanForProjection(view);
        if (g_projectionOffset < 0) {
            return view;
        }
    }

    // The offset was proven on one FSceneView and every frame builds a new one, so the
    // read is guarded here too rather than only during the scan.
    ProjectionMatrix matrix;
    if (!ReadProjectionAt(view, g_projectionOffset, matrix)) {
        GetFrameProjection().valid.store(false, std::memory_order_relaxed);
        return view;
    }
    PublishProjection(matrix);
    ReportProjection(g_projectionOffset);
    FinishCameraFrame();
    return view;
}

}  // namespace

bool InstallSceneViewHook(std::uintptr_t calcSceneViewAddr,
                          std::uintptr_t drawSceneViewReturn) {
    g_drawReturn = reinterpret_cast<void*>(drawSceneViewReturn);
    void* target = reinterpret_cast<void*>(calcSceneViewAddr);
    const MH_STATUS st = CreateAndEnableHook(target, reinterpret_cast<void*>(&Detour),
                                             reinterpret_cast<void**>(&g_original));
    if (st != MH_OK) {
        Log::Line("ERROR: hooking ULocalPlayer::CalcSceneView @ 0x%p failed: %d. The "
                  "frame's field of view is unknown to the mod, and - because this is "
                  "also what tells a frame apart from a Project or Deproject - there is "
                  "no head tracking this session either.", target, st);
        g_original = nullptr;
        return false;
    }
    Log::Line("Scene view hook installed: ULocalPlayer::CalcSceneView @ 0x%p, drawing "
              "frames return to 0x%p", target, g_drawReturn);
    return true;
}

}  // namespace OutlastHeadTracking
