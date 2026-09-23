// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "data_watchpoint.h"

#include "logging.h"

#include "cameraunlock/memory/safe_memory.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>

namespace OutlastHeadTracking {

namespace {

// Return addresses left on the stack by the frames above the writing
// instruction. 128 slots is 1 KB of stack, deep enough to reach the engine
// function that called the writer without walking into unrelated frames.
constexpr int kStackScanSlots = 128;

// Dr7 bits 16-17 are RW0 and bits 18-19 are LEN0. LEN0=11 covers 4 bytes;
// RW0=01 traps writes and RW0=11 traps reads and writes both. L0 (bit 0)
// enables the breakpoint. There is no read-only setting on x86 - a read
// watchpoint necessarily catches the writes too, which is why the writer set
// stays useful in either mode.
constexpr DWORD64 kDr7Bp0Mask       = 0x000F0003ULL;
constexpr DWORD64 kDr7Bp0Write4     = 0x000D0001ULL;
constexpr DWORD64 kDr7Bp0ReadWrite4 = 0x000F0001ULL;
constexpr DWORD64 kDr6Bp0Hit     = 0x1ULL;
constexpr DWORD   kEFlagsResume  = 0x10000;  // RF: resume without re-triggering

// Read by the vectored handler on whatever thread trapped, written by the
// thread that arms and disarms, so it cannot be a plain pointer.
std::atomic<DataWatchpoint*> g_armed{nullptr};

// Threads currently inside the handler with `g_armed` already loaded. End()
// waits for this to drain before returning, because the watchpoint object is a
// local of the discovery pass: clearing the debug registers stops NEW traps but
// says nothing about a thread already part-way through RecordAccess, and letting
// End() return there destroys the object under it.
std::atomic<int> g_inHandler{0};

LONG CALLBACK WatchpointVeh(PEXCEPTION_POINTERS ex) {
    if (ex->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    if (!(ex->ContextRecord->Dr6 & kDr6Bp0Hit)) return EXCEPTION_CONTINUE_SEARCH;

    // Sequentially consistent, on all four operations of the handshake with End(). The
    // pair here and the pair there are DIFFERENT atomics, so with acquire/release alone
    // the store-then-load on each side may be reordered - by the compiler or by x86's
    // store buffer - and End() can read a drained count before its own null store lands.
    // It then returns, the discovery pass returns, its stack is reused, and this thread
    // calls RecordAccess on a DataWatchpoint that no longer exists.
    g_inHandler.fetch_add(1, std::memory_order_seq_cst);
    DataWatchpoint* armed = g_armed.load(std::memory_order_seq_cst);
    if (armed) armed->RecordAccess(ex->ContextRecord->Rip, ex->ContextRecord->Rsp);
    g_inHandler.fetch_sub(1, std::memory_order_seq_cst);

    // Only our own bit. Dr6 is the whole status word and the other three breakpoints'
    // hit flags belong to whoever set them.
    ex->ContextRecord->Dr6 &= ~kDr6Bp0Hit;
    ex->ContextRecord->EFlags |= kEFlagsResume;
    return EXCEPTION_CONTINUE_EXECUTION;
}

// Installed once and NEVER removed. Disarming clears DR0/DR7 thread by thread
// through OpenThread/SetThreadContext, any one of which can fail; a thread left
// armed with the handler gone raises an unhandled EXCEPTION_SINGLE_STEP on its
// next write to the address and takes the game down. The handler costs one
// early-out per exception, and it only claims a trap whose DR6 says breakpoint
// 0 fired - which nothing but this file arms.
//
// A function-local static because its initialisation is the one construct that
// is already guaranteed to run exactly once across threads.
bool EnsureHandlerInstalled() {
    static const bool installed = AddVectoredExceptionHandler(1, WatchpointVeh) != nullptr;
    return installed;
}

// Returns the IDs of the threads the register was actually written to. A caller cannot
// treat "armed" as a given: the snapshot can fail, an OpenThread can be refused, and a
// SetThreadContext can fail - and a capture that ran with nothing armed reports "0 writer
// instructions", which reads as a finding about the game rather than as the probe not
// having run.
//
// IDs rather than a count, because the arm and the disarm walk two snapshots taken seconds
// apart: threads exit and are created in between, so comparing totals calls a clean disarm
// a failure and vice versa. What matters is whether every thread THIS armed was cleared.
using ThreadIdSet = std::vector<unsigned long>;

ThreadIdSet SetWatchpointOnAllThreads(uintptr_t address, bool enable, WatchMode mode) {
    ThreadIdSet result;
    const DWORD pid = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;

    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
            HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                                       FALSE, te.th32ThreadID);
            if (!thread) continue;
            bool armed = false;
            SuspendThread(thread);
            CONTEXT ctx;
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(thread, &ctx)) {
                if (enable) {
                    ctx.Dr0 = address;
                    ctx.Dr7 = (ctx.Dr7 & ~kDr7Bp0Mask) |
                              (mode == WatchMode::ReadOrWrite ? kDr7Bp0ReadWrite4
                                                              : kDr7Bp0Write4);
                } else {
                    ctx.Dr0 = 0;
                    ctx.Dr7 &= ~kDr7Bp0Mask;
                }
                ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                armed = SetThreadContext(thread, &ctx) != FALSE;
            }
            ResumeThread(thread);
            CloseHandle(thread);
            // Recorded AFTER the resume. push_back can grow the vector, which takes the
            // process heap lock, and the thread suspended a few lines above is a game
            // thread that allocates routinely. Taking that lock while its owner is frozen
            // is a deadlock with no log line and no crash: the game hangs, in the one
            // session the probe was turned on to diagnose something else.
            if (armed) {
                result.push_back(te.th32ThreadID);
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return result;
}

}  // namespace

void RipSet::Clear() {
    for (int i = 0; i < kCapacity; ++i) m_rips[i] = 0;
}

int RipSet::Count() const {
    int count = 0;
    while (count < kCapacity && m_rips[count] != 0) ++count;
    return count;
}

void RipSet::AddUnique(uintptr_t rip) {
    if (rip == 0) return;  // zero is the empty-slot marker, and no RIP is zero
    for (int i = 0; i < kCapacity; ++i) {
        const uintptr_t occupant = static_cast<uintptr_t>(InterlockedCompareExchange64(
            reinterpret_cast<volatile LONG64*>(&m_rips[i]), static_cast<LONG64>(rip), 0));
        if (occupant == 0 || occupant == rip) return;  // claimed by us, or already here
    }
}

DataWatchpoint::~DataWatchpoint() { End(); }

bool DataWatchpoint::Begin(uintptr_t address, const GameModule& module,
                           WatchMode mode) {
    if (!EnsureHandlerInstalled()) return false;

    m_accesses.Clear();
    m_callers.Clear();
    m_module  = module;
    m_address = address;

    // One compare-exchange rather than a load followed by a store: the load-then-
    // store pair lets two callers both read null and both believe they own DR0,
    // and the loser's End() would then disarm the winner's watchpoint.
    DataWatchpoint* expected = nullptr;
    if (!g_armed.compare_exchange_strong(expected, this, std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
        return false;
    }

    m_mode = mode;
    m_armedThreads = SetWatchpointOnAllThreads(m_address, true, m_mode);
    if (m_armedThreads.empty()) {
        // Nothing was armed, so nothing has to be disarmed and End() is not called: it
        // would run its own failing walk and warn that the session is now degraded, which
        // on this path would be untrue.
        Log::Line("CameraProbe: the watchpoint could not be armed on any game thread, so "
                  "this capture would report no accesses whatever the game does. Nothing "
                  "is captured.");
        g_armed.store(nullptr, std::memory_order_seq_cst);
        return false;
    }
    m_armed = true;
    return true;
}

void DataWatchpoint::End() {
    if (!m_armed) return;
    m_armed = false;
    const ThreadIdSet disarmed = SetWatchpointOnAllThreads(m_address, false, m_mode);
    int stillArmed = 0;
    for (const unsigned long id : m_armedThreads) {
        if (std::find(disarmed.begin(), disarmed.end(), id) == disarmed.end()) {
            ++stillArmed;
        }
    }
    m_armedThreads.clear();
    if (stillArmed > 0) {
        // A thread left armed keeps trapping on every write to a per-frame address for
        // the rest of the session, and the handler - with nothing armed - swallows each
        // one silently. The game gets slower and nothing says why, so this does.
        Log::Line("WARN: the watchpoint could not be cleared from %d game thread(s) it was "
                  "armed on. Those threads keep trapping on this address for the rest of "
                  "the session, which costs frame time; restart the game to clear it.",
                  stillArmed);
    }
    g_armed.store(nullptr, std::memory_order_seq_cst);
    // Drain: see g_inHandler. Nothing inside the handler blocks or allocates, so
    // this is bounded by the length of one RecordAccess. Sleep(1) rather than
    // Sleep(0), which only yields to threads at this priority or above and would
    // spin against a lower-priority game thread part-way through the handler.
    while (g_inHandler.load(std::memory_order_seq_cst) != 0) Sleep(1);
}

void DataWatchpoint::RecordAccess(uintptr_t rip, uintptr_t stackPointer) {
    m_accesses.AddUnique(rip);

    uintptr_t frame[kStackScanSlots];
    if (!cameraunlock::memory::SafeRead(stackPointer, frame)) return;
    for (int i = 0; i < kStackScanSlots; ++i) {
        if (m_module.Contains(frame[i])) m_callers.AddUnique(frame[i]);
    }
}

}  // namespace OutlastHeadTracking
