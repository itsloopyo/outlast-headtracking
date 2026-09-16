// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace OutlastHeadTracking {

// Claims a latch the first time it is offered and refuses every time after, which is
// what the mod's diagnostic lines are gated on: each of them answers a question about
// the session ("did the pose reach the camera", "what was the frame really drawn with")
// that is answered once and would otherwise be repeated every frame.
//
// The latches are plain bools rather than atomics on purpose. Each one is offered from
// exactly one place, on the thread that place runs on, and the worst a torn race could
// do is write the same line twice - which is cheaper than an atomic on the render
// thread's per-frame path.
inline bool ClaimOnce(bool& latch) {
    if (latch) {
        return false;
    }
    latch = true;
    return true;
}

// A fixed allowance of log lines for a condition that CAN repeat without bound, which is
// the case ClaimOnce above is too strict for: a gameplay gate that flickers through a long
// load, a tracker link that drops and returns while the player sits looking away, an
// unzoomed field of view the game keeps changing. Each of those is worth writing down a
// few times and worth nothing after that, and without a bound they are the one thing in
// this mod that makes the log grow with the length of the session rather than with what
// happened in it.
//
// Take() claims one line and refuses once the allowance is spent. Exhausted() is what the
// closing "that is N, the rest are not written down" line is gated on - tested right after
// a successful Take(), it is true on exactly the call that spent the last line, so the
// reader learns the log stopped rather than the condition.
//
// Plain ints rather than atomics, on the same terms as the latch above: each budget is
// offered from one place on one thread, and the worst a torn race could do is spend a line
// twice.
class LogBudget {
public:
    explicit constexpr LogBudget(int lines) : m_lines(lines) {}

    bool Take() {
        if (m_used >= m_lines) {
            return false;
        }
        ++m_used;
        return true;
    }

    bool Exhausted() const { return m_used >= m_lines; }

private:
    int m_lines;
    int m_used = 0;
};

}  // namespace OutlastHeadTracking
