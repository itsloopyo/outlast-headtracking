// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "fov_override.h"

#include "build_registry.h"
#include "frame_projection.h"
#include "frame_zoom.h"
#include "game_state.h"
#include "log_once.h"
#include "logging.h"
#include "minhook_util.h"
#include "render_frame.h"

#include "cameraunlock/memory/safe_memory.h"

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cmath>
#include <cstdint>

namespace OutlastHeadTracking {

namespace {

// APlayerController::eventGetFOVAngle(): FindFunction(NAME_GetFOVAngle) then
// ProcessEvent, returning the angle in xmm0.
using GetFovAngle_t = float(*)(void* thisptr);

GetFovAngle_t g_original = nullptr;
void*         g_sceneViewReturn = nullptr;

// The configured angle in degrees, or 0 for "leave the game's own field of view alone".
// Written once at install and read on the game thread.
std::atomic<float> g_requested{0.0f};

// Whether the angle the last rendered frame was built from was the overridden one.
std::atomic<bool> g_activeLastFrame{false};

// The unzoomed angle, latched. It is a class default the game sets once, so a frame that
// cannot read it keeps the last one that could rather than losing the compensation.
std::atomic<float> g_baseFov{0.0f};

// How many times the unzoomed angle may change before the reports stop. It is read off
// whatever pawn the controller is possessing, so a level load or a possession change can
// move it for as long as the session runs, and every line here is flushed to disk.
constexpr int kMaxBaseFovLinesLogged = 4;

// The line the whole zoom correction is audited from: it names the number every factor on
// the basis line below is divided by.
void ReportBaseFov(float baseFov) {
    static LogBudget budget(kMaxBaseFovLinesLogged);
    if (!budget.Take()) {
        return;
    }
    Log::Line("Unzoomed field of view: the game draws at %.2f degrees while the player is "
              "neither running nor filming (the hero's own DefaultFOV, which OLGame.ini "
              "authors). Every zoom the head pose is scaled for is measured against that.",
              baseFov);
    if (budget.Exhausted()) {
        Log::Line("That is %d changes of the unzoomed field of view; further changes are "
                  "not written down. They still apply.", kMaxBaseFovLinesLogged);
    }
}

// The angle the frame would be drawn with if nothing were widening or narrowing it.
// Every zoom the head pose is scaled against is measured from this, and so is any
// [View] FieldOfView override.
//
// It is the HERO's own DefaultFOV, reached through the controller's Pawn, and the reason
// that is worth a comment is that two other fields look exactly like it. CalcSceneView
// normalises its level-of-detail factor against the possessed camera's DefaultFOV, or the
// controller's when nothing is possessed, so either would read as the pair the engine
// itself divides - and both hold 80 on this build while the game draws 90 walking around.
// Measuring the zoom against one of them scaled every pose by 1.19 through the whole of
// ordinary play, which is the failure whose only symptom is head tracking feeling wrong
// everywhere rather than wrong anywhere.
//
// Measured in game instead: GetFOVAngle returns exactly 90.000 while the player stands
// still, exactly the hero's DefaultFOV, and climbs toward its RunningFOV of 100 in a run.
// Same field, same object, same measure on both sides of the ratio.
//
// Every step is a guarded read rather than a dereference. This runs on the render thread,
// on a pointer chain through objects the engine owns and frees - the Pawn is cleared and
// rebuilt across a possession change and a level load - and a fault here takes the game
// down mid-frame.
float BaseFov(const void* playerController) {
    const float latched = g_baseFov.load(std::memory_order_relaxed);
    if (!playerController) {
        return latched;
    }
    const OffsetTable& offsets = ActiveProfile().offsets;
    const auto controller = reinterpret_cast<std::uintptr_t>(playerController);

    void* pawn = nullptr;
    if (!cameraunlock::memory::SafeRead(controller + offsets.offControllerPawn, pawn) ||
        !pawn) {
        return latched;
    }
    float fov = 0.0f;
    if (!cameraunlock::memory::SafeRead(
            reinterpret_cast<std::uintptr_t>(pawn) + offsets.offHeroDefaultFov, fov)) {
        return latched;
    }
    // Checked before it is kept, not merely before it is used. The pawn is whatever the
    // controller is possessing, and on a frame that is not the hero those bytes are not
    // an angle at all - keeping one would replace a reference that was measured with one
    // that was found.
    if (!std::isfinite(fov) || fov < kMinBaseFov || fov > kMaxBaseFov) {
        return latched;
    }
    if (fov != latched) {
        g_baseFov.store(fov, std::memory_order_relaxed);
        ReportBaseFov(fov);
    }
    return fov;
}

// How many changes of the unzoomed angle get an override line before the reports stop.
// The first few already say what the override became; the rest would only make the log
// grow with the length of the session.
constexpr int kMaxAppliedLinesLogged = 8;

// Reported once per unzoomed angle rather than once per session: the reference only
// changes when the game changes it, and a line then says what the override became under
// it. Per frame this is one float comparison.
void ReportApplied(float requested, float baseFov, float gameFov, float scaled) {
    static float s_reportedBase = 0.0f;
    static LogBudget budget(kMaxAppliedLinesLogged);
    if (s_reportedBase == baseFov) {
        return;
    }
    s_reportedBase = baseFov;
    if (!budget.Take()) {
        return;
    }
    Log::Line("Field of view override: %.1f over the game's unzoomed %.1f = x%.3f, so "
              "this frame's %.1f renders as %.1f and every zoom is scaled by the same "
              "factor.", requested, baseFov, requested / baseFov, gameFov, scaled);
    if (budget.Exhausted()) {
        Log::Line("The game has changed its unzoomed field of view %d times; further "
                  "changes are not being written down and this file stops growing here. "
                  "The override still applies to every one of them.",
                  kMaxAppliedLinesLogged);
    }
}

void ReportNoBase(float baseFov) {
    static bool reported = false;
    if (!ClaimOnce(reported)) {
        return;
    }
    Log::Line("WARN: the unzoomed field of view reads as %.3f, which is not an angle, so "
              "[View] FieldOfView cannot be expressed against it and the game's own field "
              "of view is being rendered. While it stays unreadable the head pose is not "
              "scaled for the game's zooms either.",
              baseFov);
}

// Frames the zoom basis may go unreported for while it waits for a projection to name
// the aspect with. This runs inside CalcSceneView, before the frame it is part of has
// published anything, so the aspect it reads is the PREVIOUS frame's - which is the same
// number, and is there by the time the player is playing. The bound is what stops a
// build where the matrix is never found from also losing the factor.
constexpr unsigned long kFramesWaitingForAspect = 300;

// Said once when the pair cannot be read as angles for long enough that it is the build
// rather than a level load. Doctrine is that an unreadable field of view means no
// compensation and one log line, never a guessed factor - and without this line there is
// no line at all on the shipped default, because DecideFov answers Off before it ever
// looks at the base when [View] FieldOfView is 0.
void ReportNoZoomBasisOnce(float gameFov, float baseFov) {
    static bool reported = false;
    if (!ClaimOnce(reported)) {
        return;
    }
    Log::Line("WARN: this frame is drawn at %.3f degrees against an unzoomed %.3f, and "
              "one of those does not read as an angle, so the head pose is NOT being "
              "scaled for the game's zooms. Raising the camcorder will move the view "
              "further per degree of head turn than walking around does. Everything else "
              "about the session is unaffected.", gameFov, baseFov);
}

// How long the pair has to stay unreadable before that warning. A level load and a
// possession change both clear the camera for a few frames, and warning on those would
// burn the session's one line on something that fixed itself.
constexpr unsigned long kFramesBeforeNoBasis = 300;

// The two zoom lines below are each owed once and both hold for ordinary gameplay, which
// GetGameplayState answers by walking the game's world chain through a dozen guarded
// reads. One of these is built per frame and handed to both, so the pair costs one walk
// rather than two - and none at all on a frame where neither line gets far enough to ask.
class GameplayOnce {
public:
    bool Playing() {
        if (!m_asked) {
            m_asked = true;
            m_playing = GetGameplayState() == GameplayState::Playing;
        }
        return m_playing;
    }

private:
    bool m_asked = false;
    bool m_playing = false;
};

// The line the whole correction is checked against, once per session, on the first
// GAMEPLAY frame the camera updates rather than the first frame a pose arrives - so it is
// there without a tracker connected and without loading a save.
//
// Every term of the factor is on it, because a factor that is wrong by a constant reads
// exactly like a factor that is right. THE GATE IS THAT IT SAYS x1.0000 in ordinary
// gameplay: anything else means the live angle and the reference are not the same
// measurement, and the symptom in game would be head tracking feeling weak everywhere
// rather than wrong anywhere.
void ReportZoomBasisOnce(const ZoomBasis& basis, float gameFov, float baseFov,
                         GameplayOnce& gameplay) {
    static bool reported = false;
    static unsigned long framesWaited = 0;
    static unsigned long framesInvalid = 0;
    if (reported) {
        return;
    }
    if (!basis.valid) {
        if (++framesInvalid >= kFramesBeforeNoBasis) {
            ReportNoZoomBasisOnce(gameFov, baseFov);
        }
        return;
    }
    // Held until the player is actually playing. The hooks go in at process start, so the
    // first frames with a readable basis are the title screen's - and Outlast animates the
    // menu camera's angle against a fixed unzoomed reference, so the line would report a
    // factor well off 1.0 on a build where nothing is wrong. The gate this line exists to
    // be read against is that it says x1.0000 in ORDINARY GAMEPLAY, which is the only
    // frame it can be taken from.
    if (!gameplay.Playing()) {
        return;
    }
    float aspect = 0.0f;
    const bool haveFrame = TryGetLastFrameAspect(aspect);
    if (!haveFrame && ++framesWaited < kFramesWaitingForAspect) {
        return;
    }
    reported = true;
    if (haveFrame) {
        Log::Line("Zoom compensation basis: the game is drawing this frame at %.2f "
                  "degrees against its own unzoomed %.2f, both GetFOVAngle's own measure "
                  "(the horizontal field of a 16:10 frame, which the projection turns "
                  "into the drawn frame's tangents), rendered as %.2f over %.2f after "
                  "[View] FieldOfView, at display aspect %.4f, so the head pose is "
                  "scaled x%.4f.",
                  gameFov, baseFov, basis.rendered_fov, basis.rendered_base_fov,
                  aspect, basis.factor);
    } else {
        Log::Line("Zoom compensation basis: the game is drawing this frame at %.2f "
                  "degrees against its own unzoomed %.2f, both GetFOVAngle's own "
                  "measure, rendered as %.2f over %.2f after [View] FieldOfView, so the "
                  "head pose is scaled x%.4f. No projection matrix has been found in %lu "
                  "frames, so the display aspect is not on this line - the ratio above "
                  "does not depend on it.",
                  gameFov, baseFov, basis.rendered_fov, basis.rendered_base_fov,
                  basis.factor, kFramesWaitingForAspect);
    }
}

// How far from 1.0 the factor has to get before the frame counts as zoomed. Well inside
// the camcorder's own range - its narrowest zoom against a 90 degree walking view is
// about x0.13 - and well outside the fraction of a degree the angle wanders by between
// two frames at the same zoom.
constexpr float kZoomedFactorMargin = 0.01f;

// Said once, on the first frame the game actually zooms, which is the evidence that the
// correction engages rather than merely being armed.
void ReportZoomEngagedOnce(const ZoomBasis& basis, float gameFov,
                           GameplayOnce& gameplay) {
    static bool reported = false;
    if (reported || !basis.valid ||
        std::fabs(basis.factor - 1.0f) < kZoomedFactorMargin) {
        return;
    }
    // Gameplay only, on the same terms as the basis line above and for the same reason: the
    // front end animates its own field of view against a fixed reference, so without this
    // the session's one line reports the title screen and never the camcorder.
    if (!gameplay.Playing()) {
        return;
    }
    if (!ClaimOnce(reported)) {
        return;
    }
    Log::Line("Zoom compensation engaged: the game %s to %.2f degrees, so the head pose is "
              "scaled x%.4f for this frame and the view moves by as much on screen as it "
              "does walking around. Roll is left alone - it rolls the picture by the same "
              "angle at any field of view.",
              basis.factor < 1.0f ? "narrowed" : "widened", gameFov, basis.factor);
}

void ReportNotRenderable(float gameFov, float scaled) {
    static bool reported = false;
    if (!ClaimOnce(reported)) {
        return;
    }
    Log::Line("Field of view left alone on a frame the game rendered at %.1f: the "
              "configured factor takes it to %.1f, which has no projection.",
              gameFov, scaled);
}

float Detour(void* thisptr) {
    const float gameFov = g_original(thisptr);

    // The accessor has four call sites and the other three are game logic. Only the
    // scene-view caller gets a changed angle, so what the player sees changes and what
    // the game does about it does not. All three of CalcSceneView's own callers are
    // answered the same way, deliberately: ULocalPlayer::Project and Deproject convert
    // between a world point and a screen point, and they have to do it against the angle
    // the frame is actually drawn with or the game's own screen placements land wrong.
    if (_ReturnAddress() != g_sceneViewReturn) {
        return gameFov;
    }

    const float requested = g_requested.load(std::memory_order_relaxed);
    const float baseFov = BaseFov(thisptr);

    // Decided and published before the override's own report, and on every frame whether
    // or not an override is configured, because the pose scaling is not part of the
    // override - it is owed to the game's own zooms. An unreadable pair publishes 1.0,
    // which is no compensation rather than a guessed one.
    const ZoomBasis zoom = DecideZoomBasis(requested, gameFov, baseFov);
    // Published only for the frame being drawn. The same return address is reached by
    // ULocalPlayer::Project and Deproject, and one of those entered from inside the drawing
    // frame would otherwise republish its factor over the one the frame on screen is being
    // composed against.
    if (IsDrawingFrame()) {
        PublishFrameZoom(zoom.factor);
        GameplayOnce gameplay;
        ReportZoomBasisOnce(zoom, gameFov, baseFov, gameplay);
        ReportZoomEngagedOnce(zoom, gameFov, gameplay);
    }

    const FovDecision decision = DecideFov(requested, gameFov, baseFov);
    if (IsDrawingFrame()) {
        g_activeLastFrame.store(decision.status == FovOverrideStatus::Applied,
                                std::memory_order_relaxed);
    }
    switch (decision.status) {
        case FovOverrideStatus::Applied:
            ReportApplied(requested, baseFov, gameFov, decision.fov);
            break;
        case FovOverrideStatus::NoBaseAngle:
            ReportNoBase(baseFov);
            break;
        case FovOverrideStatus::NotRenderable:
            ReportNotRenderable(gameFov, decision.fov);
            break;
        case FovOverrideStatus::Off:
            break;
    }
    return decision.fov;
}

}  // namespace

bool InstallFovHook(const FovHookTargets& targets, const Config& cfg) {
    g_requested.store(cfg.fov_override, std::memory_order_relaxed);
    g_sceneViewReturn = reinterpret_cast<void*>(targets.sceneViewReturn);
    void* target = reinterpret_cast<void*>(targets.getFovAngle);
    const MH_STATUS st = CreateAndEnableHook(target, reinterpret_cast<void*>(&Detour),
                                             reinterpret_cast<void**>(&g_original));
    if (st != MH_OK) {
        Log::Line("ERROR: hooking APlayerController::GetFOVAngle @ 0x%p failed: %d. The "
                  "game's own field of view is rendered, and the head pose is not scaled "
                  "for the game's zooms - raising the camcorder will then move the view "
                  "further per degree of head turn than walking around does. Head "
                  "tracking is otherwise unaffected.", target, st);
        g_original = nullptr;
        return false;
    }
    if (cfg.fov_override > 0.0f) {
        Log::Line("Field of view hook installed: APlayerController::GetFOVAngle @ 0x%p, "
                  "scene-view return address 0x%p, %.1f degrees requested",
                  target, g_sceneViewReturn, cfg.fov_override);
    } else {
        Log::Line("Field of view hook installed: APlayerController::GetFOVAngle @ 0x%p, "
                  "scene-view return address 0x%p. [View] FieldOfView is 0, so the "
                  "game's own angle is rendered unchanged and the hook is here to read "
                  "it - the head pose is scaled by whatever the game zooms to.",
                  target, g_sceneViewReturn);
    }
    return true;
}

bool FovOverrideActive() {
    return g_activeLastFrame.load(std::memory_order_relaxed);
}

}  // namespace OutlastHeadTracking
