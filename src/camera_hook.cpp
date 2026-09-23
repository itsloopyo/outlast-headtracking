// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_hook.h"

#include "aim_offset.h"
#include "camera_trace.h"
#include "aim_projection.h"
#include "config.h"
#include "frame_projection.h"
#include "lean_trace.h"
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

#include "cameraunlock/camera/lean_clamp.h"

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
    static LogBudget budget(kMaxStateChangesLogged);
    if (seen && state == last) {
        return;
    }
    last = state;
    seen = true;
    if (!budget.Take()) {
        return;
    }
    if (state == GameplayState::Playing) {
        Log::Line("Gameplay: the head pose is reaching the camera again.");
    } else {
        Log::Line("Gameplay: the head pose is held off this frame - %s.", Describe(state));
    }
    if (budget.Exhausted()) {
        Log::Line("Gameplay: that is %d state changes; the rest of this session's are "
                  "not written down.", kMaxStateChangesLogged);
    }
}

// The same rule as the gate above, for the only other line a stuck condition can
// repeat. A trace that cannot read the world fails on every frame it is asked, so the
// second-long spacing on its own still writes a line a second for as long as the player
// stays in whatever state the read is failing in.
constexpr int kMaxTraceFailuresLogged = 20;

// How close together two of those lines may fall.
constexpr ULONGLONG kTraceFailureReportIntervalMs = 1000;

// Rate-limited as well as capped: the spacing keeps a brief failure from costing a line
// per frame, and the cap keeps a lasting one from costing a line per second.
void ReportTraceUnavailable() {
    static ULONGLONG lastReport = 0;
    static LogBudget budget(kMaxTraceFailuresLogged);
    const ULONGLONG now = GetTickCount64();
    if (now - lastReport < kTraceFailureReportIntervalMs) {
        return;
    }
    lastReport = now;
    if (!budget.Take()) {
        return;
    }
    Log::Line("WARN: reticle trace could not read the world or player pawn; hiding the "
              "reticle for this frame.");
    if (budget.Exhausted()) {
        Log::Line("WARN: that is %d frames the reticle trace could not run on; the rest "
                  "of this session's are not written down.", kMaxTraceFailuresLogged);
    }
}

bool g_aimProbe = false;

// Cuts the lean to what the level leaves room for. Touched only from the detour, so it
// needs no synchronisation of its own: one camera, one render thread, one allowance.
//
// The query is null until the configuration turns it on, and null IS the feature switched
// off - the clamp passes a null query straight through. The mod ships that way, because
// the trace mask behind it has not yet been watched working in a real level, and a mask
// that is wrong either blocks on nothing or blocks on everything.
cameraunlock::camera::LeanClamp g_leanClamp;
cameraunlock::camera::LeanQueryFn g_leanQuery = nullptr;

// How close together two lines about the clamp may fall. It has three states worth
// reporting and they change as the player walks, so the transitions on their own would
// write a line every time somebody steps past a doorframe.
constexpr ULONGLONG kLeanReportIntervalMs = 5000;

// Says what the clamp is doing, on each change and then no more often than the interval
// above. The failed-query case is a state of its own rather than a silence: transitions
// alone cannot tell "the check runs and the room is open" from "the check is not running
// at all", and those need different fixes.
void ReportLeanState() {
    enum class LeanState { Clear, Contact, QueryFailed };
    const LeanState now = g_leanClamp.LastQueryFailed() ? LeanState::QueryFailed
                        : g_leanClamp.InContact()       ? LeanState::Contact
                                                        : LeanState::Clear;
    static LeanState last = LeanState::Clear;
    static bool seen = false;
    static ULONGLONG lastReport = 0;
    const ULONGLONG ms = GetTickCount64();
    if (seen && now == last && ms - lastReport < kLeanReportIntervalMs) {
        return;
    }
    last = now;
    seen = true;
    lastReport = ms;
    switch (now) {
        case LeanState::Clear:
            Log::Line("Lean: the level leaves room for the whole lean.");
            return;
        case LeanState::Contact:
            Log::Line("Lean: held short of a surface.");
            return;
        case LeanState::QueryFailed:
            Log::Line("WARN: Lean: the world check could not run, so the lean is not being "
                      "held off anything this frame.");
            return;
    }
}

// The seam head_pose.h asks its lean through: the fraction of the asked-for lean the
// level leaves room for.
float LimitLean(void* controller, const UE3Vector& cleanEye, const float world[3]) {
    const cameraunlock::math::Vec3 desired{world[0], world[1], world[2]};
    const float wanted = desired.Magnitude();
    const cameraunlock::math::Vec3 allowed =
        g_leanClamp.Apply({cleanEye.X, cleanEye.Y, cleanEye.Z}, desired,
                          g_tracking->LastFrameDtSec(), g_leanQuery, controller);
    ReportLeanState();
    // A lean too small to have a direction is one the clamp returned untouched, and
    // dividing by it would turn that into a NaN scale on a frame that needed no clamp.
    if (wanted <= 0.0f) {
        return 1.0f;
    }
    return allowed.Magnitude() / wanted;
}

// What the viewpoint call worked out about where the game is pointing, held until the
// scene-view hook has published the projection it has to be divided by - two detours
// apart, further down the same CalcSceneView.
//
// thread_local rather than global, on the same terms as render_frame.h's drawing flag:
// it describes the frame on this thread's stack, not a state of the mod.
struct PendingAim {
    // False until a frame's viewpoint call has filled the rest in. Cleared at the start
    // of every frame, so a frame that applied no pose places nothing.
    bool has = false;

    // The direction to project: toward the surface the game would hit when there is one,
    // and along the game's own forward axis when there is not.
    UE3Vector direction{};
    UE3Rotator drawnRotation{};

    // The terms `direction` was built from. Read only by the AimProbe log line, which is
    // what a reticle that lands wrong is diagnosed from.
    bool hit = false;
    UE3Vector target{};
    UE3Vector cleanEye{};
    UE3Vector drawnEye{};
};

thread_local PendingAim t_aim{};

void PrepareCrosshair(void* controller, const FrameSample& sample,
                      const UE3Vector& cleanLocation, const UE3Vector& drawnLocation,
                      const UE3Rotator& clean, const UE3Rotator& drawn) {
    if (!sample.has_rotation && !sample.has_position) {
        return;
    }
    t_aim.cleanEye = cleanLocation;
    t_aim.drawnEye = drawnLocation;
    // Cleared with the flag, not merely flagged: a frame that runs no trace would
    // otherwise leave the last traced frame's surface on the AimProbe line, beside a
    // hit=0 that says it is not this frame's.
    t_aim.hit = false;
    t_aim.target = UE3Vector{};
    const Mat3 aim = RotatorToMatrix(clean);
    t_aim.direction = {aim.m[0][0], aim.m[0][1], aim.m[0][2]};
    if (sample.has_position) {
        UE3Vector target{};
        bool hit = false;
        if (!TraceCameraAim(controller, cleanLocation, clean, target, hit)) {
            ReportTraceUnavailable();
            PublishAimOffset(CrosshairPlacement::Hidden, 0.0f, 0.0f);
            return;
        }
        t_aim.target = target;
        t_aim.hit = hit;
        if (hit) {
            t_aim.direction = {target.X - drawnLocation.X, target.Y - drawnLocation.Y,
                              target.Z - drawnLocation.Z};
        }
    }
    t_aim.drawnRotation = drawn;
    t_aim.has = true;
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

// How close together two AimProbe lines may fall.
constexpr ULONGLONG kAimProbeIntervalMs = 1000;

// One line a second while [Diagnostics] AimProbe is on, carrying every term the offset
// was built from. A reticle that lands wrong is a sign, a depth or a projection fault,
// and only having all three on one line tells them apart.
void ReportAimProbe(float tanHalfH, float tanHalfV, float ndcX, float ndcY,
                    AimProjection result) {
    static ULONGLONG lastProbe = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - lastProbe < kAimProbeIntervalMs) {
        return;
    }
    lastProbe = now;
    Log::Line("Aim: hit=%d clean=%.3f/%.3f/%.3f eye=%.3f/%.3f/%.3f "
              "target=%.3f/%.3f/%.3f drawn=%d/%d/%d tan=%.5f/%.5f ndc=%.6f/%.6f %s",
              t_aim.hit, t_aim.cleanEye.X, t_aim.cleanEye.Y, t_aim.cleanEye.Z,
              t_aim.drawnEye.X, t_aim.drawnEye.Y, t_aim.drawnEye.Z,
              t_aim.target.X, t_aim.target.Y, t_aim.target.Z,
              t_aim.drawnRotation.Pitch, t_aim.drawnRotation.Yaw, t_aim.drawnRotation.Roll,
              tanHalfH, tanHalfV, ndcX, ndcY, Describe(result));
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
        // The allowance describes the room the eye is standing in, and a frame the gate
        // holds off is the other side of a load or a chapter change. Carrying it across
        // would ration the first lean of the next level against the last room's wall.
        g_leanClamp.Reset();
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
    //
    // And asked for only on a frame that has a lean to clamp. Taking the acquire is not
    // the same as the acquire having observed anything: the detours are enabled before
    // TrackingRuntime::Start() runs, so for the first frames of a session this thread can
    // be here while Start() is still assigning m_cfg, and the limits would be read out of
    // a plain struct another thread is writing. has_position is what closes that - it can
    // only be true on a frame whose acquire observed the release Start() ends on, which is
    // what publishes m_cfg - and ClampedToLimits already passes a sample with no position
    // through untouched, so the frames this now skips are the frames it did nothing on.
    const FrameSample raw = g_tracking->SampleFrame();
    FrameSample sample = ScaledForZoom(raw, FrameZoomFactor());
    if (sample.has_position) {
        sample = ClampedToLimits(sample, g_tracking->GetPositionLimits());
    }
    const UE3Vector cleanLocation = *outLocation;
    const UE3Rotator clean = *outRotation;
    Lean lean;
    if (!ApplyHeadPose(g_tracking->IsWorldSpaceYaw(), sample, outLocation, outRotation,
                       &lean, g_leanQuery ? &LimitLean : nullptr, controller)) {
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
    t_aim.has = false;
    PublishAimOffset(CrosshairPlacement::GamesOwn, 0.0f, 0.0f);
}

void FinishCameraFrame() {
    if (!t_aim.has) {
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
    const AimProjection result = ProjectAimDirection(t_aim.direction, t_aim.drawnRotation,
                                                     tanHalfH, tanHalfV, &x, &y);
    PublishAimOffset(result == AimProjection::Ok ? CrosshairPlacement::Offset :
                                                   CrosshairPlacement::Hidden, x, y);
    if (g_aimProbe) {
        ReportAimProbe(tanHalfH, tanHalfV, x, y, result);
    }
}

bool InstallCameraHook(const CameraHookTargets& targets, TrackingRuntime& tracking,
                       const Config& cfg) {
    g_tracking = &tracking;
    g_aimProbe = cfg.aim_probe;
    g_sceneViewReturn = reinterpret_cast<void*>(targets.sceneViewReturn);

    if (cfg.collision_enabled) {
        cameraunlock::camera::LeanClampSettings settings;
        settings.skin = cfg.collision_margin;
        settings.release_smoothing = cfg.collision_release_smoothing;
        g_leanClamp.SetSettings(settings);
        lean_trace::SetTraceFlags(static_cast<unsigned int>(cfg.collision_channel));
        g_leanQuery = &lean_trace::Query;
        Log::Line("Lean clamp on: the view is held %.0f units off whatever the game's own "
                  "line check stops on (mask 0x%X), and the allowance reopens at %.2f.",
                  cfg.collision_margin, cfg.collision_channel,
                  cfg.collision_release_smoothing);
    } else {
        Log::Line("Lean clamp off ([Position] CollisionEnabled is 0): a lean is held "
                  "inside the configured limits but is not checked against the level, so "
                  "leaning hard into a wall can put the view through it.");
    }

    void* target = reinterpret_cast<void*>(targets.getPlayerViewPoint);
    const MH_STATUS st = CreateAndEnableHook(target, reinterpret_cast<void*>(&Detour),
                                             reinterpret_cast<void**>(&g_original));
    if (st != MH_OK) {
        // Deliberately does NOT say the game is otherwise untouched. By the time this
        // runs the field-of-view hook is already in, so a configured [View] FieldOfView
        // is still changing the angle every frame is drawn at - and a player reading a
        // line that said otherwise would go looking for the cause somewhere else.
        Log::Line("ERROR: hooking APlayerController::GetPlayerViewPoint @ 0x%p failed: "
                  "%d. There is no head tracking this session, and no crosshair or "
                  "camcorder-light hook either. A configured [View] FieldOfView still "
                  "applies.", target, st);
        g_original = nullptr;
        g_tracking = nullptr;
        return false;
    }
    Log::Line("Camera hook installed: APlayerController::GetPlayerViewPoint @ 0x%p, "
              "scene-view return address 0x%p", target, g_sceneViewReturn);
    return true;
}

}  // namespace OutlastHeadTracking
