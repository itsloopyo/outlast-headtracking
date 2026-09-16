// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "light_probe.h"

#include "data_watchpoint.h"
#include "frame_view.h"
#include "game_module.h"
#include "logging.h"
#include "safe_read_range.h"
#include "ue3_rotation.h"
#include "ue3_types.h"

#include <windows.h>
#include <process.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace OutlastHeadTracking {

namespace {

// ULightComponent, read off the accessors script asks a light with: execGetOrigin
// (0x1DACC0) answers with the vector at +0x100, execGetDirection (0x1DAD40) with the
// first element of each of the rows at +0x98, +0xA8 and +0xB8, and execSetEnabled
// (0x1DAA40) flips bit 0 of the dword at +0x140.
constexpr std::size_t kDirectionRow = 0x98;
constexpr std::size_t kRowStride = 0x10;
constexpr std::size_t kOrigin = 0x100;
constexpr std::size_t kFlags = 0x140;

// How far the search reaches: the controller owns the pawn, the pawn owns the camcorder,
// and the camcorder owns its light. Slot offsets are not pinned, because the objects
// behind them are rebuilt on every level load - what is walked is the graph.
constexpr int kSearchDepth = 3;
constexpr std::size_t kObjectWindow = 0x400;
constexpr std::size_t kMaxObjectsPerLevel = 256;
constexpr float kSearchRadius = 300.0f;
constexpr float kEyeRadius = 20.0f;
constexpr DWORD kWatchpointMs = 6000;
constexpr DWORD kSampleIntervalMs = 1000;
constexpr int kSamples = 600;

// How many lights one sample reports. The hero carries four, so this is headroom rather
// than a bound the walk is expected to reach.
constexpr std::size_t kMaxLightsPerSample = 8;

// Samples to let the log fill before the watchpoint is armed. Its findings are read
// against the samples above it, so arming on the first one leaves nothing to read them
// against - and the camcorder is rarely up that early.
constexpr int kSamplesBeforeWatchpoint = 5;

// A UObject's first field sits past its vtable and the header slots before it, and every
// object pointer is 8-byte aligned, so the walk starts here and steps by a pointer.
constexpr std::size_t kFirstObjectSlot = 0x10;
constexpr std::size_t kObjectSlotStride = sizeof(std::uintptr_t);

struct FoundLight {
    std::uintptr_t object = 0;
    std::uintptr_t vtable = 0;
    float origin[3] = {};
    float direction[3] = {};
    std::uint32_t flags = 0;
    float distance = 0.0f;

    bool IsLit() const { return (flags & 1u) != 0; }
};

float Dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

float DistanceTo(const float* point, const UE3Vector& eye) {
    const float dx = point[0] - eye.X;
    const float dy = point[1] - eye.Y;
    const float dz = point[2] - eye.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float AngleBetweenDegrees(const float a[3], const float b[3]) {
    float dot = Dot(a, b);
    if (dot > 1.0f) dot = 1.0f;
    if (dot < -1.0f) dot = -1.0f;
    return static_cast<float>(std::acos(dot) * 180.0 / kPi);
}

// A light, by the two things the engine's own accessors answer with: a position and a
// unit direction. Near the player and pointing somewhere is specific enough to find the
// lights they carry on a build where nothing is named.
bool ReadLight(std::uintptr_t object, const UE3Vector& eye, FoundLight& out) {
    std::uint8_t window[kFlags + sizeof(std::uint32_t)];
    if (!SafeReadRange(object, window, sizeof(window))) return false;
    std::memcpy(out.origin, window + kOrigin, sizeof(out.origin));
    if (!std::isfinite(out.origin[0]) || !std::isfinite(out.origin[1]) ||
        !std::isfinite(out.origin[2])) {
        return false;
    }
    out.distance = DistanceTo(out.origin, eye);
    if (out.distance > kSearchRadius) return false;
    for (int axis = 0; axis < 3; ++axis) {
        std::memcpy(&out.direction[axis],
                    window + kDirectionRow + kRowStride * static_cast<std::size_t>(axis),
                    sizeof(float));
    }
    const float length = std::sqrt(Dot(out.direction, out.direction));
    if (!std::isfinite(length) || length < 0.99f || length > 1.01f) return false;
    std::memcpy(&out.flags, window + kFlags, sizeof(out.flags));
    std::memcpy(&out.vtable, window, sizeof(out.vtable));
    out.object = object;
    return true;
}

// Breadth-first from the player controller. Every object reached is offered to the test
// above, so the search does not need to know which slot holds what.
std::size_t FindLightsOnThePlayer(std::uintptr_t controller, const UE3Vector& eye,
                                  FoundLight* out, std::size_t capacity) {
    const GameModule module = HostGameModule();
    std::vector<std::uintptr_t> level{ controller };
    std::vector<std::uintptr_t> next;
    std::size_t found = 0;
    for (int depth = 0; depth < kSearchDepth && found < capacity && !level.empty(); ++depth) {
        next.clear();
        for (std::uintptr_t object : level) {
            std::uint8_t window[kObjectWindow];
            if (!SafeReadRange(object, window, sizeof(window))) continue;
            for (std::size_t slot = kFirstObjectSlot;
                 slot + sizeof(std::uintptr_t) <= sizeof(window) && found < capacity;
                 slot += kObjectSlotStride) {
                std::uintptr_t target = 0;
                std::memcpy(&target, window + slot, sizeof(target));
                if (target < 0x10000 || (target & 7) != 0) continue;
                std::uintptr_t vtable = 0;
                if (!SafeReadRange(target, &vtable, sizeof(vtable)) ||
                    !module.Contains(vtable)) {
                    continue;
                }
                FoundLight light;
                if (ReadLight(target, eye, light)) {
                    out[found++] = light;
                    continue;
                }
                if (next.size() < kMaxObjectsPerLevel) next.push_back(target);
            }
        }
        level.swap(next);
    }
    return found;
}

// Which engine instructions write the lit light's direction, and who calls them. On a
// build whose addresses have moved, this is what finds the transform update the light
// hook has to detour: the function that writes that direction every frame it moves.
void ReportWritersOfDirection(std::uintptr_t light) {
    const GameModule module = HostGameModule();
    DataWatchpoint watchpoint;
    if (!watchpoint.Begin(light + kDirectionRow, module, WatchMode::WriteOnly)) {
        Log::Line("LightProbe: the watchpoint could not be armed on the light's direction.");
        return;
    }
    Sleep(kWatchpointMs);
    watchpoint.End();

    // Both counts are taken once: RipSet::Count() walks to the first empty slot, so a
    // loop condition that calls it re-walks the set on every iteration.
    const RipSet& writers = watchpoint.Accesses();
    const int writerCount = writers.Count();
    Log::Line("LightProbe: %d instruction(s) wrote the direction of the light at %p.",
              writerCount, reinterpret_cast<void*>(light));
    for (int i = 0; i < writerCount; ++i) {
        const std::uintptr_t rip = writers.At(i);
        if (!module.Contains(rip)) continue;
        Log::Line("LightProbe:   writer RVA %llx",
                  static_cast<unsigned long long>(module.Rva(rip)));
    }
    const RipSet& callers = watchpoint.Callers();
    const int callerCount = callers.Count();
    for (int i = 0; i < callerCount; ++i) {
        const std::uintptr_t caller = callers.At(i);
        if (!module.Contains(caller)) continue;
        Log::Line("LightProbe:   caller RVA %llx",
                  static_cast<unsigned long long>(module.Rva(caller)));
    }
}

// One sample's worth of lines: where each light the player carries is, which way it
// points, and how far that is from both the direction the game is aiming and the
// direction the frame is drawn along.
void ReportLights(const GameModule& module, const FoundLight* lights, std::size_t count,
                  const UE3Rotator& clean, const UE3Rotator& drawn) {
    // Row 0 of an FRotationMatrix is the world-space forward axis (ue3_rotation.h), so
    // the aim direction comes from the same composition the camera hook and the crosshair
    // projection use rather than from a second copy of it here.
    const Mat3 cleanBasis = RotatorToMatrix(clean);
    const Mat3 drawnBasis = RotatorToMatrix(drawn);

    Log::Line("LightProbe: %zu light(s) on the player. The game points %d/%d/%d, the "
              "frame is drawn %d/%d/%d.", count, clean.Pitch, clean.Yaw, clean.Roll,
              drawn.Pitch, drawn.Yaw, drawn.Roll);
    for (std::size_t i = 0; i < count; ++i) {
        const FoundLight& light = lights[i];
        // The pair of angles is the whole measurement. The camcorder's light should
        // read near zero against the drawn frame and the size of the head pose against
        // where the game is aiming; the other way round is the light following the
        // mouse, which is what the light hook exists to stop.
        // The vtable is validated when the light is found and read again from a later
        // snapshot to print, and a level load between the two frees and reuses the object.
        // An RVA taken off a pointer that is no longer in the module is a plausible-looking
        // number that the next session pastes into Ghidra, so it is only printed when the
        // pointer still lands inside OLGame.exe.
        const bool inModule = module.Contains(light.vtable);
        Log::Line("LightProbe:   %p vtable %s%llx %s, %.0f from the eye, facing "
                  "%.3f/%.3f/%.3f - %.1f deg off where the game points, %.1f deg off "
                  "the drawn frame", reinterpret_cast<void*>(light.object),
                  inModule ? "RVA " : "outside the module, raw ",
                  static_cast<unsigned long long>(
                      inModule ? module.Rva(light.vtable) : light.vtable),
                  light.IsLit() ? "lit" : "unlit", light.distance, light.direction[0],
                  light.direction[1], light.direction[2],
                  AngleBetweenDegrees(light.direction, cleanBasis.m[0]),
                  AngleBetweenDegrees(light.direction, drawnBasis.m[0]));
    }
}

// The camcorder's own light: the one that is lit and standing at the eye. Null when this
// sample found no such light, which is every sample before the camcorder is raised.
const FoundLight* CamcorderLight(const FoundLight* lights, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (lights[i].IsLit() && lights[i].distance <= kEyeRadius) return &lights[i];
    }
    return nullptr;
}

unsigned __stdcall ProbeThread(void*) {
    const GameModule module = HostGameModule();
    UE3Vector eye{};
    UE3Rotator clean{};
    UE3Rotator drawn{};
    bool watched = false;

    for (int sample = 0; sample < kSamples; ++sample) {
        Sleep(kSampleIntervalMs);
        void* controller = GetFrameController().load(std::memory_order_acquire);
        if (controller == nullptr || !TryGetFrameView(eye, clean, drawn)) continue;

        FoundLight lights[kMaxLightsPerSample];
        const std::size_t count = FindLightsOnThePlayer(
            reinterpret_cast<std::uintptr_t>(controller), eye, lights, kMaxLightsPerSample);
        ReportLights(module, lights, count, clean, drawn);

        // Once, and only after the log has samples to read the findings against.
        if (watched || sample < kSamplesBeforeWatchpoint) continue;
        if (const FoundLight* camcorder = CamcorderLight(lights, count)) {
            watched = true;
            ReportWritersOfDirection(camcorder->object);
        }
    }
    Log::Line("LightProbe: finished after %d samples.", kSamples);
    return 0;
}

}  // namespace

void StartLightProbe() {
    const HANDLE thread = reinterpret_cast<HANDLE>(
        _beginthreadex(nullptr, 0, &ProbeThread, nullptr, 0, nullptr));
    if (!thread) {
        Log::Line("ERROR: LightProbe could not start (error %lu).", GetLastError());
        return;
    }
    CloseHandle(thread);
    Log::Line("LightProbe: reporting the lights the player carries, one sample a second.");
}

}  // namespace OutlastHeadTracking
