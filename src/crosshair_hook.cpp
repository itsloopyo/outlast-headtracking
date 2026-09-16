// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "crosshair_hook.h"

#include "aim_offset.h"
#include "log_once.h"
#include "logging.h"
#include "minhook_util.h"

#include "cameraunlock/memory/safe_memory.h"

#include <windows.h>

namespace OutlastHeadTracking {

namespace {

// AOLHUD::DrawCrosshair(AOLHUD* this).
using DrawCrosshair_t = void(*)(void* hud);

DrawCrosshair_t      g_original = nullptr;
CrosshairHookTargets g_targets{};

// Says the mark is being moved, with the canvas it is being moved across, on the first
// frame it actually moves. Everything before it - the hook installing, the pose reaching
// the camera - can be true while the dot sits at the middle of the screen, because the
// two numbers this reads are the ones a build could have moved without the fingerprint
// noticing. A canvas that is not the display's size is the signature of that.
void ReportMovedOnce(float clipX, float clipY, float dx, float dy) {
    static bool reported = false;
    if (!ClaimOnce(reported)) {
        return;
    }
    Log::Line("Crosshair moved: the game's own dot is being placed where the game is "
              "pointing rather than at the middle of the screen. Canvas %.0fx%.0f, first "
              "offset %.0f/%.0f pixels.", clipX, clipY, dx, dy);
}

// The one place the mod writes to a game-owned object outside the camera's own
// out-parameters, so it says exactly what it writes and puts it back.
//
// The function being wrapped computes both tiles' position ONCE, as
// `ClipX * 0.5 - size * 0.5` and the same in Y, and reads the canvas for nothing else -
// the fade it advances first does not touch it. So biasing the pair by twice the offset
// wanted moves the dot by exactly that offset and moves nothing else in the frame. The
// alternative, detouring the tile draw itself, is every UI element in the game paying
// for one dot.
void ReportClipRestoreFailedOnce() {
    static bool reported = false;
    if (!ClaimOnce(reported)) {
        return;
    }
    Log::Line("WARN: the canvas the crosshair is drawn on could not be put back the way "
              "it was found, so the rest of this frame's HUD is laid out against a screen "
              "of the wrong width. The camera is unaffected.");
}

class ClipBias {
public:
    ClipBias(std::uintptr_t canvas, float clipX, float clipY, float dx, float dy)
        : m_canvas(canvas), m_clipX(clipX), m_clipY(clipY) {
        cameraunlock::memory::SafeWrite(m_canvas + g_targets.offCanvasClipX,
                                        clipX + 2.0f * dx);
        cameraunlock::memory::SafeWrite(m_canvas + g_targets.offCanvasClipY,
                                        clipY + 2.0f * dy);
    }
    // Restored on every path out, including one the wrapped call leaves by unwinding. A
    // canvas left biased is every later element on the HUD - subtitles, the battery meter,
    // the pause overlay - laid out against a screen up to a head-turn wider or narrower
    // than the one it is drawn on, for the rest of the frame.
    //
    // A failed restore is the one case worth a line: the write succeeded microseconds
    // earlier, so it failing means the canvas was freed or reprotected underneath the
    // draw, and the HUD the player is looking at is now laid out wrong with nothing else
    // to say so.
    ~ClipBias() {
        const bool restoredX =
            cameraunlock::memory::SafeWrite(m_canvas + g_targets.offCanvasClipX, m_clipX);
        const bool restoredY =
            cameraunlock::memory::SafeWrite(m_canvas + g_targets.offCanvasClipY, m_clipY);
        if (!restoredX || !restoredY) {
            ReportClipRestoreFailedOnce();
        }
    }

    ClipBias(const ClipBias&) = delete;
    ClipBias& operator=(const ClipBias&) = delete;

private:
    std::uintptr_t m_canvas;
    float m_clipX;
    float m_clipY;
};

void Detour(void* hud) {
    const AimOffset& offset = GetAimOffset();
    // Acquire, paired with the release in PublishAimOffset: the two components are only
    // guaranteed to be this frame's once the placement they were stored with has been
    // observed.
    const CrosshairPlacement placement = offset.placement.load(std::memory_order_acquire);
    if (placement == CrosshairPlacement::GamesOwn) {
        g_original(hud);
        return;
    }
    if (placement == CrosshairPlacement::Hidden) {
        // Skipping the call also skips the fade it advances first, so a head held past
        // the edge of the frame freezes the dot's opacity for as long as it is held.
        // Bounded and invisible: the frames it misses are frames nothing is drawn on,
        // and it catches up on the first one that is. Reaching here at all needs a head
        // turn of about half the field of view.
        return;
    }

    std::uintptr_t canvas = 0;
    float clipX = 0.0f;
    float clipY = 0.0f;
    if (!cameraunlock::memory::SafeRead(
            reinterpret_cast<std::uintptr_t>(hud) + g_targets.offHudCanvas, canvas) ||
        canvas == 0 ||
        !cameraunlock::memory::SafeRead(canvas + g_targets.offCanvasClipX, clipX) ||
        !cameraunlock::memory::SafeRead(canvas + g_targets.offCanvasClipY, clipY) ||
        !(clipX > 0.0f) || !(clipY > 0.0f)) {
        // No canvas to place it against. The game's own dot is drawn where the game
        // would have drawn it rather than skipped, because a missing mark reads as the
        // mod having broken the HUD.
        g_original(hud);
        return;
    }

    // Normalised device coordinates to pixels of this canvas. The vertical flip is the
    // only conversion: NDC counts up the screen, a canvas counts down it.
    const float dx = offset.ndc_x.load(std::memory_order_relaxed) * 0.5f * clipX;
    const float dy = -offset.ndc_y.load(std::memory_order_relaxed) * 0.5f * clipY;

    {
        const ClipBias bias(canvas, clipX, clipY, dx, dy);
        g_original(hud);
    }
    ReportMovedOnce(clipX, clipY, dx, dy);
}

}  // namespace

bool InstallCrosshairHook(const CrosshairHookTargets& targets) {
    g_targets = targets;

    void* target = reinterpret_cast<void*>(targets.drawCrosshair);
    const MH_STATUS st = CreateAndEnableHook(target, reinterpret_cast<void*>(&Detour),
                                             reinterpret_cast<void**>(&g_original));
    if (st != MH_OK) {
        Log::Line("ERROR: hooking AOLHUD::DrawCrosshair @ 0x%p failed: %d. Head tracking "
                  "still works; the game's crosshair stays in the middle of the screen "
                  "rather than where the game is pointing.", target, st);
        g_original = nullptr;
        return false;
    }
    Log::Line("Crosshair hook installed: AOLHUD::DrawCrosshair @ 0x%p", target);
    return true;
}

}  // namespace OutlastHeadTracking
