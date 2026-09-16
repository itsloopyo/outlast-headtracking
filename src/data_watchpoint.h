// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "game_module.h"

#include <cstdint>
#include <vector>

namespace OutlastHeadTracking {

// A small fixed-capacity set of unique instruction pointers, filled from a
// vectored exception handler. Fixed capacity and no allocation because it is
// written on whatever game thread trapped the write.
class RipSet {
public:
    static constexpr int kCapacity = 16;

    void Clear();
    void AddUnique(uintptr_t rip);  // no-op once full, or when already present
    int Count() const;
    uintptr_t At(int index) const { return m_rips[index]; }

private:
    // Slots are claimed front to back with a compare-exchange against zero,
    // because two game threads can trap the same address at once. That leaves
    // the array a dense prefix of non-zero RIPs at all times, so the first zero
    // slot is the count and no separate counter can disagree with it.
    uintptr_t m_rips[kCapacity] = {};
};

// What a watchpoint traps. The camera cache is written once a frame by the
// camera update and read by everything that consumes the viewpoint, so the two
// modes answer two different questions: WriteOnly finds the hook site the
// injection needs, ReadOrWrite finds every consumer the injection would be
// visible to.
enum class WatchMode {
    WriteOnly,
    ReadOrWrite,
};

// Captures the engine instructions that touch a given address, using the CPU's
// DR0 data watchpoint. No code is patched and nothing is written to the game,
// which is what makes it safe to arm against a record that has not yet been
// proven to be the camera.
//
// Only one watchpoint may be armed at a time: the vectored handler is process
// wide and routes to whichever instance is currently armed. Begin() claims that
// slot atomically and returns false to the loser.
//
// The handler is installed on the first Begin() and stays installed for the
// life of the process - see the note in data_watchpoint.cpp. That makes the
// module unsafe to FreeLibrary, which an ASI plugin never is.
class DataWatchpoint {
public:
    ~DataWatchpoint();

    // Arms DR0 on `address` across every other thread in the process. False if
    // another watchpoint is already armed or the handler could not be installed.
    bool Begin(uintptr_t address, const GameModule& module, WatchMode mode);
    void End();

    // Every instruction that trapped. In WriteOnly mode that is the writers; in
    // ReadOrWrite mode x86 offers no read-only setting, so it is the union of both
    // and the writer set from a preceding WriteOnly pass is what separates them.
    const RipSet& Accesses() const { return m_accesses; }
    const RipSet& Callers() const { return m_callers; }

    // Records one trapped access. Public only because the vectored handler is a
    // C callback and cannot be a member function.
    void RecordAccess(uintptr_t rip, uintptr_t stackPointer);

private:
    RipSet     m_accesses;
    RipSet     m_callers;
    GameModule m_module;
    uintptr_t  m_address = 0;
    WatchMode  m_mode = WatchMode::WriteOnly;
    bool       m_armed = false;
    // The threads the debug register was actually written to. Disarming walks a second
    // Toolhelp snapshot taken seconds later, and threads exit and are created in between,
    // so only this set says whether everything this armed was cleared again.
    std::vector<unsigned long> m_armedThreads;
};

}  // namespace OutlastHeadTracking
