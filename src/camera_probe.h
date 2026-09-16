// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace OutlastHeadTracking {

// Diagnostic aid, off in every shipped INI. Finds the live UE3
// TCameraCache by scanning committed memory for the record layout and keeping the
// one whose TimeStamp advances every frame, then reports - as module RVAs, which is
// what pastes into Ghidra - the instruction that writes the POV each frame and every
// instruction that reads it.
//
// Those two sets are what a new game build needs: the writer is where the head pose
// has to go in, and the readers say who would see it. No game OBJECT is written - the
// record it arms against is only ever read - so arming against something that turns out
// not to be the camera cannot corrupt a running session.
//
// It is not, however, free of side effects on the process, and that is why it is gated on
// a matched build and off in every shipped INI: finding the writer means a hardware
// watchpoint, so the pass installs a vectored exception handler that is never removed,
// and suspends every thread in the process to write DR0 and DR7 into each of them. A
// debugger's own hardware breakpoint on DR0 does not survive that.
//
// Runs on its own thread for the life of the process; a pass takes about fifteen seconds
// and repeats, reporting only when the finding changes.
class CameraProbe {
public:
    bool Start();

private:
    static unsigned __stdcall ThreadProc(void*);
};

}  // namespace OutlastHeadTracking
