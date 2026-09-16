// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_hook.h"

#include "aim_offset.h"
#include "camera_trace.h"
#include "aim_projection.h"
#include "frame_projection.h"
#include "frame_sample.h"
#include "frame_zoom.h"
#include "frame_view.h"
#include "game_state.h"
#include "head_pose.h"
#include "log_once.h"
#include "logging.h"
#include "minhook_util.h"
#include "render_frame.h"
#include "tracking_runtime.h"
#include "ue3_types.h"

#include <windows.h>
#include <intrin.h>

namespace OutlastHeadTracking {

namespace {

// APlayerController::GetPlayerViewPoint(FVector& out_Location, FRotator& out_Rotation):
// FindFunction(NAME_GetPlayerViewPoint) then ProcessEvent, with the script's answer
// copied back into the caller's two out-parameters.
using GetPlayerViewPoint_t = void(*)(void* controller, UE3Vector* outLocation,
                                     UE3Rotator* outRotation);

GetPlayerViewPoint_t g_original = nullptr;
void*                g_sceneViewReturn = nullptr;
TrackingRuntime*     g_tracking = nullptr;

// The line that answers "is the mod actually driving the camera". Everything before it
// says the mod started, matched the build and is receiving packets, none of which proves
// the pose reaches the frame - the accessor could be called from somewhere this detour
// filters out, on a build where an offset survived the fingerprint but the call site
// moved. This prints from inside the frame's own viewpoint call, so it cannot.
void ReportEngagedOnce() {
    static bool reported = false;
    if (!ClaimOnce(reported)) {
        return;
    }
    Log::Line("Camera hook engaged: the frame is being drawn from the viewpoint this mod "
              "returns.");
}

// The one line that shows the whole chain arriving, on the first frame that actually
// moves the camera: what was applied, what the game's own rotator was, and what the
// frame was drawn with. Everything above it can be true while the composition is wrong -
// an axis mirrored, degrees taken for rotator units, a lean in metres applied as
// centimetres - and each of those has a distinct signature here that no other line
// carries. Once per session, from the frame it first happened on.
//
// The pose on it is the one that reached the camera, so on a zoomed frame it is the
// tracker's scaled by the frame's field of view rather than the tracker's raw numbers.
// The factor is on its own line (frame_zoom.h), and a line here that disagrees with the
// tracker app by exactly that factor is the correction working.
void ReportFirstAppliedPose(const FrameSample& sample, const UE3Rotator& clean,
                            const UE3Rotator& composed, const Lean& lean) {
    static bool reported = false;
    if (!ClaimOnce(reported)) {
        return;
    }
    Log::Line("Head pose reached the camera. Applied yaw/pitch/roll %.1f/%.1f/%.1f deg "
              "and lean %.2f/%.2f/%.2f m turned the game's own pitch/yaw/roll "
              "%d/%d/%d into %d/%d/%d (65536 units to the turn), and moved the eye "
              "%.1f/%.1f/%.1f world units right/up/forward.",
              sample.yaw, sample.pitch, sample.roll,
              sample.pos_x, sample.pos_y, sample.pos_z,
              clean.Pitch, clean.Yaw, clean.Roll,
              composed.Pitch, composed.Yaw, composed.Roll,
              lean.ruf[0], lean.ruf[1], lean.ruf[2]);
}

// How many times the gate may say the state changed before it stops writing them down.
// The transitions a session actually has are few - front end to level, level to load, a
// chapter change - and each of them is worth a line. A world that cannot be read flicker
// during a long load would otherwise be the one thing in this mod that makes the log
// grow with the length of the session rather than with what happened in it.
constexpr int kMaxStateChangesLogged = 20;

// Says what the gate decided, each time it decides something new. This is the line that
// separates "the mod stopped applying the pose" from "the mod stopped working", which
// otherwise look identical from the player's side of the screen.
void ReportStateChange(GameplayState state) {
    static GameplayState last = GameplayState::NoWorld;
    static bool seen = false;
    static int logged = 0;
    if (seen && state == last) {
        return;
    }
    last = state;
    seen = true;
    if (logged >= kMaxStateChangesLogged) {
        return;
    }
    ++logged;
    if (state == GameplayState::Playing) {
        Log::Line("Gameplay: the head pose is reaching the camera again.");
    } else {
        Log::Line("Gameplay: the head pose is held off this frame - %s.", Describe(state));
    }
    if (logged == kMaxStateChangesLogged) {
        Log::Line("Gameplay: that is %d state changes; the rest of this session's are "
                  "not written down.", kMaxStateChangesLogged);
    }
}

// The same rule as the gate above, for the only other line a stuck condition can
// repeat. A trace that cannot read the world fails on every frame it is asked, so the
// second-long spacing on its own still writes a line a second for as long as the player
// stays in whatever state the read is failing in.
constexpr int kMaxTraceFailuresLogged = 20;

// Rate-limited as well as capped: the spacing keeps a brief failure from costing a line
// per frame, and the cap keeps a lasting one from costing a line per second.
void ReportTraceUnavailable() {
    static ULONGLONG lastReport = 0;
    static int logged = 0;
    if (logged >= kMaxTraceFailuresLogged) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now - lastReport < 1000) {
        return;
    }
    lastReport = now;
    ++logged;
    Log::Line("WARN: reticle trace could not read the world or player pawn; hiding the "
              "reticle for this frame.");
    if (logged == kMaxTraceFailuresLogged) {
        Log::Line("WARN: that is %d frames the reticle trace could not run on; the rest "
                  "of this session's are not written down.", kMaxTraceFailuresLogged);
    }
}

bool g_aimProbe = false;
thread_local UE3Vector g_aimTarget{};
thread_local UE3Vector g_cleanEye{};
thread_local UE3Vector g_drawnEye{};
thread_local bool g_aimHit = false;
thread_local bool g_hasAim = false;
thread_local UE3Vector g_aimDirection{};
thread_local UE3Rotator g_drawnRotation{};

void PrepareCrosshair(void* controller, const FrameSample& sample,
                      const UE3Vector& cleanLocation, const UE3Vector& drawnLocation,
                      const UE3Rotator& clean, const UE3Rotator& drawn) {
    if (!sample.has_rotation && !sample.has_position) {
        return;
    }
    g_cleanEye = cleanLocation;
    g_drawnEye = drawnLocation;
    g_aimHit = false;
    const Mat3 aim = RotatorToMatrix(clean);
    g_aimDirection = {aim.m[0][0], aim.m[0][1], aim.m[0][2]};
    if (sample.has_position) {
        UE3Vector target{};
        bool hit = false;
        if (!TraceCameraAim(controller, cleanLocation, clean, target, hit)) {
            ReportTraceUnavailable();
            PublishAimOffset(CrosshairPlacement::Hidden, 0.0f, 0.0f);
            return;
        }
        g_aimTarget = target;
        g_aimHit = hit;
        if (hit) {
            g_aimDirection = {target.X - drawnLocation.X, target.Y - drawnLocation.Y,
                               target.Z - drawnLocation.Z};
        }
    }
    g_drawnRotation = drawn;
    g_hasAim = true;
}

void ReportNonFinitePoseOnce() {
    static bool reported = false;
    if (!ClaimOnce(reported)) {
        return;
    }
    Log::Line("WARN: a tracker pose arrived that is not a finite number, so this frame "
              "kept the game's own viewpoint. Check the tracker app is sending sane "
              "values; nothing else about the session is affected.");
}

void Detour(void* controller, UE3Vector* outLocation, UE3Rotator* outRotation) {
    g_original(controller, outLocation, outRotation);

    // See CameraHookTargets for why both tests are here and why neither covers the
    // other's case. Cheap enough to run on every caller: a pointer compare and a
    // thread-local read.
    if (_ReturnAddress() != g_sceneViewReturn || !IsDrawingFrame()) {
        return;
    }
    ReportEngagedOnce();
    PublishFrameController(controller);

    // Before the pipeline is advanced, not after: SampleFrame is what measures the frame
    // interval every smoothing decision is made against, so a menu the player sits in
    // for a minute must not be a minute of frames the processor has smoothed through.
    const GameplayState state = GetGameplayState();
    ReportStateChange(state);
    if (state != GameplayState::Playing) {
        PublishAimOffset(CrosshairPlacement::GamesOwn, 0.0f, 0.0f);
        // Published with the two rotators equal, which is how a consumer reads "the head
        // did not turn this frame". Skipping the publish instead would leave the last
        // GAMEPLAY frame's pair standing, and the camcorder's light would keep being
        // turned by a head movement that is no longer reaching the camera - for the whole
        // of a cutscene or a menu.
        PublishFrameView(*outLocation, *outRotation, *outRotation);
        return;
    }

    // Advanced here, once per drawn frame, because this is the only call in the process
    // that happens exactly that often and immediately before the view is built from what
    // it returns. The pose is therefore as new as the frame it is composed into.
    //
    // Scaled for the frame's field of view before anything else sees it, so the camera,
    // the crosshair's projection, the camcorder's light and the diagnostic line below
    // all describe the same pose. The factor was decided a few instructions earlier in
    // this same CalcSceneView, from the angle this frame is being drawn with, and is 1.0
    // whenever the game is not zoomed - see frame_zoom.h.
    // Clamped AFTER the scaling, not before. The processor holds the lean inside the
    // configured limits on its way out, and scaling for a frame the game draws wider than
    // its unzoomed angle multiplies it straight back out of them - and those limits are
    // the only thing keeping the eye inside the player's body. The engine-boundary
    // conversion comes first, the game's own limits last.
    // Sequenced rather than nested: as two arguments of one call the order they run in is
    // unspecified, and GetPositionLimits must not be the one that runs first - SampleFrame
    // carries the acquire that makes the configuration visible to this thread at all.
    const FrameSample raw = g_tracking->SampleFrame();
    const PositionLimits limits = g_tracking->GetPositionLimits();
    const FrameSample sample = ClampedToLimits(ScaledForZoom(raw, FrameZoomFactor()),
                                               limits);
    const UE3Vector cleanLocation = *outLocation;
    const UE3Rotator clean = *outRotation;
    Lean lean;
    if (!ApplyHeadPose(g_tracking->IsWorldSpaceYaw(), sample, outLocation, outRotation,
                       &lean)) {
        ReportNonFinitePoseOnce();
        PublishAimOffset(CrosshairPlacement::GamesOwn, 0.0f, 0.0f);
        PublishFrameView(*outLocation, *outRotation, *outRotation);
        return;
    }
    PrepareCrosshair(controller, sample, cleanLocation, *outLocation, clean, *outRotation);
    // The CLEAN eye, not the leaned one. A consumer asking where the frame was drawn from
    // wants the position the game itself put the camera at: the camcorder's light is found
    // by measuring how near the eye it sits, and against a leaned eye that distance is the
    // size of the lean, so a full forward lean pushes the light out of its own search
    // radius and the lit cone snaps back to the game's aim.
    PublishFrameView(cleanLocation, clean, *outRotation);
    // A pose that composed to the game's own rotator and no lean is a tracker sitting at
    // its centre, which is what every session starts on and is not yet evidence of
    // anything. The report waits for a frame that moved.
    if (clean.Pitch != outRotation->Pitch || clean.Yaw != outRotation->Yaw ||
        clean.Roll != outRotation->Roll || !lean.IsZero()) {
        ReportFirstAppliedPose(sample, clean, *outRotation, lean);
    }
}

}  // namespace

void BeginCameraFrame() {
    g_hasAim = false;
    PublishAimOffset(CrosshairPlacement::GamesOwn, 0.0f, 0.0f);
}

void FinishCameraFrame() {
    if (!g_hasAim) {
        return;
    }
    float tanHalfH = 0.0f;
    float tanHalfV = 0.0f;
    if (!TryGetFrameProjection(tanHalfH, tanHalfV)) {
        PublishAimOffset(CrosshairPlacement::Hidden, 0.0f, 0.0f);
        return;
    }
    float x = 0.0f;
    float y = 0.0f;
    const AimProjection result = ProjectAimDirection(g_aimDirection, g_drawnRotation,
                                                      tanHalfH, tanHalfV, &x, &y);
    PublishAimOffset(result == AimProjection::Ok ? CrosshairPlacement::Offset :
                                                   CrosshairPlacement::Hidden, x, y);
    static ULONGLONG lastProbe = 0;
    const ULONGLONG now = GetTickCount64();
    if (g_aimProbe && now - lastProbe >= 1000) {
        lastProbe = now;
        Log::Line("Aim: hit=%d clean=%.3f/%.3f/%.3f eye=%.3f/%.3f/%.3f "
                  "target=%.3f/%.3f/%.3f drawn=%d/%d/%d tan=%.5f/%.5f ndc=%.6f/%.6f %s",
                  g_aimHit, g_cleanEye.X, g_cleanEye.Y, g_cleanEye.Z,
                  g_drawnEye.X, g_drawnEye.Y, g_drawnEye.Z,
                  g_aimTarget.X, g_aimTarget.Y, g_aimTarget.Z,
                  g_drawnRotation.Pitch, g_drawnRotation.Yaw, g_drawnRotation.Roll,
                  tanHalfH, tanHalfV, x, y, Describe(result));
    }
}

bool InstallCameraHook(const CameraHookTargets& targets, TrackingRuntime& tracking, bool aimProbe) {
    g_tracking = &tracking;
    g_aimProbe = aimProbe;
    g_sceneViewReturn = reinterpret_cast<void*>(targets.sceneViewReturn);

    void* target = reinterpret_cast<void*>(targets.getPlayerViewPoint);
    const MH_STATUS st = CreateAndEnableHook(target, reinterpret_cast<void*>(&Detour),
                                             reinterpret_cast<void**>(&g_original));
    if (st != MH_OK) {
        Log::Line("ERROR: hooking APlayerController::GetPlayerViewPoint @ 0x%p failed: "
                  "%d. There is no head tracking this session; the game is otherwise "
                  "untouched.", target, st);
        g_original = nullptr;
        g_tracking = nullptr;
        return false;
    }
    Log::Line("Camera hook installed: APlayerController::GetPlayerViewPoint @ 0x%p, "
              "scene-view return address 0x%p", target, g_sceneViewReturn);
    return true;
}

}  // namespace OutlastHeadTracking
