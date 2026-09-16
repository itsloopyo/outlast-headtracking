// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_probe.h"

#include "data_watchpoint.h"
#include "game_module.h"
#include "log_once.h"
#include "logging.h"
#include "safe_read_range.h"
#include "ue3_camera_cache.h"

#include "cameraunlock/memory/safe_memory.h"

#include <windows.h>
#include <process.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace OutlastHeadTracking {

namespace {

constexpr DWORD  kIdleBetweenPassesMs  = 8000;
constexpr DWORD  kTimeStampWatchMs     = 6000;
constexpr DWORD  kTimeStampPollMs      = 16;
constexpr DWORD  kCaptureMs            = 5000;
constexpr DWORD  kCapturePollMs        = 40;
constexpr size_t kMaxCandidates        = 400000;
constexpr int    kMinTimeStampAdvances = 30;
constexpr int    kMaxCandidatesLogged  = 8;

// How far above a record to look for the vtable of the object owning it.
constexpr size_t kOwningObjectBackscanBytes = 0x600;

constexpr uintptr_t kScanFirstAddress = 0x10000;
constexpr uintptr_t kScanLastAddress  = 0x00007FFFFFFFFFFFULL;
constexpr size_t    kScanChunkBytes   = 2u * 1024 * 1024;
constexpr size_t    kScanStrideBytes  = 4;  // TCameraCache fields are 4-byte aligned

// The watchpoint covers four bytes, so it is armed on one field rather than on the
// record. Yaw is the one every consumer of the viewpoint reads and the camera update
// rewrites every frame - a pitch or a roll can sit unchanged for a whole session.
constexpr size_t kYawOffsetInCache =
    offsetof(CameraCache, pov) + offsetof(Pov, rotation) + sizeof(int32_t);

struct Candidate {
    uintptr_t   addr = 0;
    CameraCache snapshot{};
    int   tsAdvances = 0;  // times the TimeStamp was seen to move forward
    int   tsRegressions = 0;
    float fovMin = 0.0f;
    float fovMax = 0.0f;
    float lastTs = 0.0f;

    // Corroborating evidence, gathered once the live candidates are known.
    bool        hasNeighbour = false;  // a matching one-frame-old record sits at +0x20
    CameraCache neighbour{};
    bool        hasObject = false;     // a module vtable sits above it, so it is a UObject field
    uintptr_t   object = 0;
    size_t      objectOffset = 0;
};

// One scan over committed RW regions. VirtualQuery walks region by region, so the x64
// user range costs the count of real allocations, not the 128 TB span.
void ScanForCaches(std::vector<Candidate>& out, size_t cap) {
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = kScanFirstAddress;
    // Each chunk is read one stride short of a whole record past its end, so a record
    // straddling the boundary is still seen while the first start offset of the next
    // chunk is not tested twice.
    const size_t kOverlap = sizeof(CameraCache) - kScanStrideBytes;
    std::vector<uint8_t> buf(kScanChunkBytes + kOverlap);

    while (addr < kScanLastAddress && out.size() < cap) {
        if (VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) == 0) break;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const size_t regionSize = mbi.RegionSize;
        const bool scannable = (mbi.State == MEM_COMMIT) &&
                               (mbi.Protect & PAGE_READWRITE) &&
                               !(mbi.Protect & PAGE_GUARD) &&
                               regionSize >= sizeof(CameraCache);
        if (scannable) {
            for (size_t off = 0; off < regionSize && out.size() < cap; off += kScanChunkBytes) {
                size_t want = regionSize - off;
                if (want > kScanChunkBytes + kOverlap) want = kScanChunkBytes + kOverlap;
                if (want < sizeof(CameraCache)) break;
                if (!SafeReadRange(base + off, buf.data(), want)) continue;
                const size_t last = want - sizeof(CameraCache);
                for (size_t i = 0; i <= last && out.size() < cap; i += kScanStrideBytes) {
                    CameraCache c;
                    memcpy(&c, buf.data() + i, sizeof(c));
                    if (!LooksLikeCameraCache(c)) continue;
                    Candidate cand{};
                    cand.addr = base + off + i;
                    cand.snapshot = c;
                    cand.fovMin = c.pov.fov;
                    cand.fovMax = c.pov.fov;
                    cand.lastTs = c.timeStamp;
                    out.push_back(cand);
                }
            }
        }
        addr = base + regionSize;
    }
}

// The live camera cache is rewritten every frame, so its TimeStamp advances
// monotonically. A stale copy or a coincidental byte pattern does not.
void WatchTimeStamps(std::vector<Candidate>& candidates) {
    const DWORD start = GetTickCount();
    while (GetTickCount() - start < kTimeStampWatchMs) {
        for (Candidate& c : candidates) {
            if (c.tsRegressions != 0) continue;
            float timeStamp = 0.0f;
            if (!cameraunlock::memory::SafeRead(c.addr + offsetof(CameraCache, timeStamp),
                                                timeStamp)) {
                continue;
            }
            if (timeStamp == c.lastTs) continue;

            CameraCache now;
            if (!cameraunlock::memory::SafeRead(c.addr, now)) continue;
            if (!PlausibleTimeStamp(now.timeStamp)) continue;
            if (now.timeStamp > c.lastTs)      c.tsAdvances++;
            else if (now.timeStamp < c.lastTs) c.tsRegressions++;
            c.lastTs = now.timeStamp;
            if (PlausibleFov(now.pov.fov)) {
                c.fovMin = (std::min)(c.fovMin, now.pov.fov);
                c.fovMax = (std::max)(c.fovMax, now.pov.fov);
            }
            c.snapshot = now;
        }
        Sleep(kTimeStampPollMs);
    }
}

std::vector<Candidate*> SelectLiveCaches(std::vector<Candidate>& candidates) {
    std::vector<Candidate*> live;
    for (Candidate& c : candidates) {
        if (c.tsAdvances >= kMinTimeStampAdvances && c.tsRegressions == 0) live.push_back(&c);
    }
    std::sort(live.begin(), live.end(),
              [](const Candidate* a, const Candidate* b) { return a->tsAdvances > b->tsAdvances; });
    return live;
}

// A UObject starts with a vtable pointer into the game module, so the nearest module
// pointer at an 8-byte slot below the record is the owning object. The distance between
// them is the CameraCache property offset a hook indexes by.
bool FindOwningObject(const GameModule& module, uintptr_t recordAddr,
                      uintptr_t& objectOut, size_t& offsetOut) {
    const uintptr_t start = recordAddr & ~static_cast<uintptr_t>(7);
    for (size_t back = 0; back <= kOwningObjectBackscanBytes; back += 8) {
        if (back > start) break;
        const uintptr_t probe = start - back;
        uintptr_t vtable = 0;
        if (!cameraunlock::memory::SafeRead(probe, vtable)) continue;
        if (!module.Contains(vtable)) continue;
        uintptr_t firstSlot = 0;
        if (!cameraunlock::memory::SafeRead(vtable, firstSlot)) continue;
        if (!module.Contains(firstSlot)) continue;
        objectOut = probe;
        offsetOut = static_cast<size_t>(recordAddr - probe);
        return true;
    }
    return false;
}

void GatherCorroboration(const GameModule& module, const std::vector<Candidate*>& live) {
    for (Candidate* c : live) {
        c->hasObject = FindOwningObject(module, c->addr, c->object, c->objectOffset);
        CameraCache neighbour;
        c->hasNeighbour =
            cameraunlock::memory::SafeRead(c->addr + sizeof(CameraCache), neighbour) &&
            AgreesAsPreviousFrame(c->snapshot, neighbour);
        if (c->hasNeighbour) c->neighbour = neighbour;
    }
}

// Prefer a candidate carrying both corroborating signs, so the watchpoints land on the
// record most likely to be the real camera.
Candidate* PickBestCandidate(const std::vector<Candidate*>& live) {
    for (Candidate* c : live) {
        if (c->hasNeighbour && c->hasObject) return c;
    }
    return live.front();
}

// A pass repeats for as long as the game runs and rediscovers the same camera each
// time, so reporting every one would write the same forty lines into the log a few
// hundred times an hour. Each pass is reduced to a signature of what a reader would act
// on, and only a pass whose signature differs from the last reported one is written out.
struct PassMemory {
    std::string signature;
    int suppressed = 0;
};

// Deliberately free of anything that moves when the game reallocates: the record
// address changes across a level load while the finding does not. The RVAs are sorted
// because the order accesses trap in is not stable between passes and would otherwise
// defeat the comparison.
void AppendRvas(const GameModule& module, const RipSet& set, std::vector<uintptr_t>& out) {
    for (int i = 0; i < set.Count(); ++i) {
        out.push_back(module.Rva(set.At(i)));
    }
}

std::string PassSignature(const GameModule& module, size_t liveCount, const Candidate& best,
                          const DataWatchpoint& writers, const DataWatchpoint& readers) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "live=%d neighbour=%d object=%d offset=0x%X",
                  static_cast<int>(liveCount), best.hasNeighbour ? 1 : 0,
                  best.hasObject ? 1 : 0, (unsigned)best.objectOffset);
    std::string signature = buf;

    std::vector<uintptr_t> rvas;
    AppendRvas(module, writers.Accesses(), rvas);
    AppendRvas(module, writers.Callers(), rvas);
    AppendRvas(module, readers.Accesses(), rvas);
    AppendRvas(module, readers.Callers(), rvas);
    std::sort(rvas.begin(), rvas.end());
    for (uintptr_t rva : rvas) {
        std::snprintf(buf, sizeof(buf), " 0x%llX", (unsigned long long)rva);
        signature += buf;
    }
    return signature;
}

// True when this pass found something the last reported pass did not, so the caller
// should write its detail block.
bool ShouldReport(PassMemory& memory, const std::string& signature) {
    if (signature == memory.signature) {
        memory.suppressed++;
        return false;
    }
    if (memory.suppressed > 0) {
        Log::Line("CameraProbe: %d further pass(es) found the same thing and were not "
                  "logged.", memory.suppressed);
    }
    memory.suppressed = 0;
    memory.signature = signature;
    return true;
}

void LogPov(const char* label, const Pov& p) {
    Log::Line("    %s loc(%.1f, %.1f, %.1f) rot(P=%.2f Y=%.2f R=%.2f) FOV=%.3f",
              label, p.location[0], p.location[1], p.location[2],
              p.rotation[0] * kUE3RotatorUnitsToDegrees,
              p.rotation[1] * kUE3RotatorUnitsToDegrees,
              p.rotation[2] * kUE3RotatorUnitsToDegrees,
              p.fov);
}

void LogCandidate(const GameModule& module, const Candidate& c) {
    Log::Line("  cache @ 0x%llX  advances=%d  FOV %.3f..%.3f",
              (unsigned long long)c.addr, c.tsAdvances, c.fovMin, c.fovMax);
    LogPov("POV", c.snapshot.pov);
    if (c.hasNeighbour) {
        Log::Line("    +0x20 holds a matching one-frame-old record, so this is "
                  "ACamera::CameraCache with LastFrameCameraCache behind it.");
        LogPov("prev POV", c.neighbour.pov);
    }
    if (c.hasObject) {
        uintptr_t vtable = 0;
        // The object can be freed between the corroboration pass and this line (a level
        // load does exactly that). Rva() of a failed read is 0 - base, which prints as a
        // plausible-looking address that points at nothing, so the read is checked rather
        // than assumed.
        if (cameraunlock::memory::SafeRead(c.object, vtable) && module.Contains(vtable)) {
            Log::Line("    owning object 0x%llX, CameraCache at object+0x%X, vtable "
                      "RVA=0x%llX", (unsigned long long)c.object,
                      (unsigned)c.objectOffset, (unsigned long long)module.Rva(vtable));
        } else {
            Log::Line("    owning object 0x%llX, CameraCache at object+0x%X, vtable no "
                      "longer readable", (unsigned long long)c.object,
                      (unsigned)c.objectOffset);
        }
    } else {
        Log::Line("    no module vtable within 0x%X below the record - not an object "
                  "field, or the object is larger than the backscan.",
                  (unsigned)kOwningObjectBackscanBytes);
    }
}

void LogCapture(const char* what, const GameModule& module, const DataWatchpoint& wp) {
    const RipSet& rips = wp.Accesses();
    const int count = rips.Count();
    Log::Line("CameraProbe: %d %s instruction(s) for POV.Rotation.Yaw.", count, what);
    for (int i = 0; i < count; ++i) {
        const uintptr_t rip = rips.At(i);
        // An RVA is only meaningful inside the module: below the base it wraps, and
        // printing that under an "RVA=" label sends the reader to an address that has
        // nothing to do with the access.
        if (module.Contains(rip)) {
            Log::Line("    %s rip RVA=0x%llX [GAME]", what, (unsigned long long)module.Rva(rip));
        } else {
            Log::Line("    %s rip=0x%llX [ext] - outside the game module, no RVA", what,
                      (unsigned long long)rip);
        }
    }
    // Count() walks to the first empty slot, so it is hoisted rather than re-evaluated
    // per iteration - the same reason the loop above holds `count`.
    const RipSet& callers = wp.Callers();
    const int callerCount = callers.Count();
    for (int i = 0; i < callerCount; ++i) {
        const uintptr_t caller = callers.At(i);
        if (!module.Contains(caller)) continue;
        Log::Line("    %s caller RVA=0x%llX [GAME]", what,
                  (unsigned long long)module.Rva(caller));
    }
}

// Arms one watchpoint and lets it collect. Returns false when DR0 could not be claimed,
// which is the one failure a caller has to stop for: the capture would otherwise report
// an empty set as though nothing touched the address.
bool Capture(DataWatchpoint& wp, uintptr_t address,
             const GameModule& module, WatchMode mode) {
    if (!wp.Begin(address, module, mode)) {
        return false;
    }
    const DWORD start = GetTickCount();
    while (GetTickCount() - start < kCaptureMs) Sleep(kCapturePollMs);
    wp.End();
    return true;
}

// Both capture attempts fail the same way and owe the reader the same line, so the
// pass reports it from one place rather than repeating the block per attempt.
void ReportWatchpointUnavailable(PassMemory& memory) {
    if (ShouldReport(memory, "watchpoint-unavailable")) {
        Log::Line("CameraProbe: could not arm the watchpoint - skipping capture.");
    }
}

void RunDiscoveryPass(const GameModule& module,
                      PassMemory& memory) {
    // The narration below exists to explain the pass's multi-second waits to someone
    // reading the log live. That is worth one telling and nothing after.
    const bool narrate = memory.signature.empty();

    std::vector<Candidate> candidates;
    if (narrate) Log::Line("CameraProbe: scanning committed memory...");
    ScanForCaches(candidates, kMaxCandidates);
    static bool cappedReported = false;
    if (candidates.size() >= kMaxCandidates && ClaimOnce(cappedReported)) {
        // Its own latch, not ShouldReport's: that holds one signature for the whole pass,
        // so sharing it would make this line and the pass's actual finding overwrite each
        // other and print on every pass forever.
        //
        // A truncated scan and a complete one produce the same shaped report, and the
        // difference matters: if the live cache sits past the cut, the pass goes on to
        // conclude "all stale copies", which is a finding about the game rather than
        // about the scan having stopped early.
        Log::Line("CameraProbe: the scan stopped at its %d candidate cap, so memory past "
                  "that point was not looked at. A conclusion below that nothing was live "
                  "may only mean the live cache is beyond the cap.",
                  static_cast<int>(kMaxCandidates));
    }
    if (candidates.empty()) {
        if (ShouldReport(memory, "no-candidates")) {
            Log::Line("CameraProbe: nothing matched the layout. Be in a level with a live "
                      "camera, not on a loading screen or the front end.");
        }
        return;
    }

    if (narrate) {
        Log::Line("CameraProbe: %d structural TCameraCache candidate(s). Watching "
                  "TimeStamps for 6s to find the live one...",
                  static_cast<int>(candidates.size()));
    }
    WatchTimeStamps(candidates);

    std::vector<Candidate*> live = SelectLiveCaches(candidates);
    if (live.empty()) {
        if (ShouldReport(memory, "no-live-cache")) {
            Log::Line("CameraProbe: %d structural matches but none had an advancing "
                      "TimeStamp - all stale copies. Retrying.",
                      static_cast<int>(candidates.size()));
        }
        return;
    }

    GatherCorroboration(module, live);
    Candidate* best = PickBestCandidate(live);
    const uintptr_t yaw = best->addr + kYawOffsetInCache;

    // Writers first, then readers, because DR0 holds one watchpoint at a time. The
    // read-or-write pass necessarily traps the writes as well - x86 has no read-only
    // setting - so its set is the union, and the writer set from the first pass is what
    // separates the two.
    DataWatchpoint writers;
    DataWatchpoint readers;
    if (narrate) {
        Log::Line("CameraProbe: capturing writers of POV.Rotation.Yaw @ 0x%llX for 5s...",
                  (unsigned long long)yaw);
    }
    if (!Capture(writers, yaw, module, WatchMode::WriteOnly)) {
        ReportWatchpointUnavailable(memory);
        return;
    }
    if (narrate) Log::Line("CameraProbe: capturing readers for 5s...");
    if (!Capture(readers, yaw, module, WatchMode::ReadOrWrite)) {
        ReportWatchpointUnavailable(memory);
        return;
    }

    if (!ShouldReport(memory, PassSignature(module, live.size(), *best, writers, readers))) {
        return;
    }

    Log::Line("CameraProbe: === %d live camera cache(s), TimeStamp advancing ===",
              static_cast<int>(live.size()));
    const size_t shown = (std::min)(live.size(), static_cast<size_t>(kMaxCandidatesLogged));
    for (size_t i = 0; i < shown; ++i) LogCandidate(module, *live[i]);
    LogCapture("writer", module, writers);
    LogCapture("reader-or-writer", module, readers);
    Log::Line("CameraProbe: === pass done, read-only === repeating quietly; only a change "
              "is logged from here.");
}

}  // namespace

bool CameraProbe::Start() {
    // The handle is closed straight away rather than kept: nothing ever waits on this
    // thread. It runs for the life of the process and goes away with it, which is what
    // keeps the exit path from joining anything under the loader lock.
    const auto thread = reinterpret_cast<HANDLE>(
        _beginthreadex(nullptr, 0, ThreadProc, nullptr, 0, nullptr));
    if (!thread) {
        return false;
    }
    CloseHandle(thread);
    return true;
}

unsigned __stdcall CameraProbe::ThreadProc(void*) {
    const GameModule module = HostGameModule();
    Log::Line("CameraProbe: module 0x%llX..0x%llX. Scanning for UE3 TCameraCache pairs. "
              "No input needed - the live camera is the one whose TimeStamp advances.",
              (unsigned long long)module.base, (unsigned long long)module.end);

    PassMemory memory;
    for (;;) {
        Sleep(kIdleBetweenPassesMs);
        RunDiscoveryPass(module, memory);
    }
}

}  // namespace OutlastHeadTracking
