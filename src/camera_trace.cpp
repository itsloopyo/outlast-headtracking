// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_trace.h"

#include "logging.h"
#include "ue3_rotation.h"
#include "cameraunlock/memory/safe_memory.h"

#include <cstddef>

namespace OutlastHeadTracking {
namespace {

struct CheckResult {
    void* next = nullptr;
    void* actor = nullptr;
    UE3Vector location{};
    UE3Vector normal{};
    float time = 1.0f;
    std::int32_t item = -1;
    std::byte remaining[0x38]{};
};
static_assert(sizeof(CheckResult) == 0x68);
static_assert(offsetof(CheckResult, location) == 0x10);
static_assert(offsetof(CheckResult, time) == 0x28);

// How far along the aim the trace reaches, in the engine's centimetres - a kilometre,
// which is past the far side of any room in this game. A miss returns this point, and the
// reticle then projects the direction rather than a surface.
constexpr float kTraceRangeCm = 100000.0f;

// SingleLineCheck returns true on a miss and writes only Actor and Time on that path.
using SingleLineCheck = bool(*)(void*, CheckResult*, void*, const UE3Vector*,
                                const UE3Vector*, std::uint32_t, const UE3Vector*, void*);
SingleLineCheck g_trace = nullptr;
std::uintptr_t g_world = 0;
std::size_t g_pawnOffset = 0;

}  // namespace

void InitCameraTrace(std::uintptr_t base, const OffsetTable& offsets) {
    g_trace = reinterpret_cast<SingleLineCheck>(base + offsets.rvaSingleLineCheck);
    g_world = base + offsets.rvaGWorld;
    g_pawnOffset = offsets.offControllerPawn;
    Log::Line("Reticle surface trace ready: UWorld::SingleLineCheck @ 0x%p", g_trace);
}

bool TraceCameraAim(void* controller, const UE3Vector& eye, const UE3Rotator& rotation,
                    UE3Vector& target, bool& hit) {
    void* world = nullptr;
    void* pawn = nullptr;
    if (!cameraunlock::memory::SafeRead(g_world, world) || !world ||
        !cameraunlock::memory::SafeRead(reinterpret_cast<std::uintptr_t>(controller) +
                                          g_pawnOffset, pawn) || !pawn) {
        return false;
    }
    const Mat3 aim = RotatorToMatrix(rotation);
    const UE3Vector end{eye.X + aim.m[0][0] * kTraceRangeCm,
                        eye.Y + aim.m[0][1] * kTraceRangeCm,
                        eye.Z + aim.m[0][2] * kTraceRangeCm};
    const UE3Vector extent{};
    CheckResult result;
    // AActor::execTrace uses 0x20BF for a zero-extent trace including actors.
    hit = !g_trace(world, &result, pawn, &end, &eye, 0x20BFu, &extent, nullptr);
    target = hit ? result.location : end;
    return true;
}

}  // namespace OutlastHeadTracking
