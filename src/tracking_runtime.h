// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"
#include "frame_sample.h"

#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"

#include <atomic>
#include <thread>

namespace OutlastHeadTracking {

class TrackingRuntime {
public:
    TrackingRuntime() : m_session(m_receiver) {}

    // Deliberately not destructible. This object owns threads, and joining them would run
    // from the CRT's static teardown during DLL_PROCESS_DETACH - under the loader lock,
    // against threads that take it themselves, which is a hang on exit. The one instance is
    // heap-allocated and never deleted; deleting the destructor is what makes that a
    // compile error to get wrong rather than a std::terminate from ~std::thread.
    ~TrackingRuntime() = delete;

    // No return value: a receiver that cannot bind immediately is NOT a failure. The
    // port is usually held by another game's mod that is still shutting down, and core's
    // supervisor retries until it frees up, so the mod carries on and tracking starts
    // when the port does.
    void Start(const Config& cfg);

    // Runs the per-frame pipeline once and returns the processed pose. Called
    // from the camera hook on the render thread.
    FrameSample SampleFrame();

    void ToggleEnabled();
    void CycleTrackingMode();
    void ToggleYawMode();

    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(std::memory_order_relaxed); }

    // The configured lean limits, for the camera hook to re-apply after it has scaled
    // the pose for the frame's field of view - see ClampedToLimits.
    PositionLimits GetPositionLimits() const;

    // Seconds between this frame and the last, as the pipeline measured them. The lean
    // clamp eases its allowance open on the same clock the smoothing runs on, and one
    // frame interval measured twice is two numbers that drift apart under a stall.
    // Only valid on the thread SampleFrame runs on, which is the only one that asks.
    float LastFrameDtSec() const { return m_lastDt; }

private:
    static constexpr float kMaxFrameDtSec = 0.25f;

    // Link monitor poll period. Core's supervisor already retries the bind every
    // 500ms and logs the moment it succeeds, so this is not what makes the port
    // get reclaimed - it is what reports whether packets followed.
    static constexpr int kLinkPollMs = 250;

    // How long the port has to stay quiet before the monitor calls the link idle.
    // Deliberately well above the receiver's own 500ms liveness window: a webcam
    // tracker that loses the face for a moment stops sending for a few hundred
    // milliseconds routinely, and reporting each of those would bury the one
    // transition anybody reads the log for.
    static constexpr int kLinkSilentMs = 3000;

    // How many link state changes get a line before the monitor stops writing them.
    // A tracker that loses the face for longer than kLinkSilentMs and finds it again
    // is one idle/receiving pair, and a player who keeps looking away produces them
    // for as long as they play - which is the one thing in this mod that would make
    // the log grow with the length of the session rather than with what happened in
    // it. The first few pairs already say the link is intermittent; the rest repeat it.
    static constexpr int kMaxLinkChangesLogged = 12;

    // True while the newest packet is younger than Config::data_freshness_ms. The
    // core receiver's own IsReceiving() is fixed at 500ms, which is where the
    // shipped default comes from; this is what makes a user-chosen window mean
    // anything.
    bool IsPoseFresh() const;

    // m_held masked by the tracking mode in force right now.
    FrameSample HeldForCurrentMode() const;

    // What the link monitor last told the user.
    enum class Link { Unknown, WaitingForPort, BoundSilent, Receiving };

    // Watches the receiver for as long as the mod runs and logs each change of
    // state. Nothing in the tracking path depends on it. It never returns: this object
    // is deliberately leaked so that no join can run from the CRT's static teardown
    // under the loader lock, and the thread goes away with the process.
    void LinkMonitorThread();

    Config m_cfg{};
    cameraunlock::UdpReceiver m_receiver;
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session;
    cameraunlock::time::FrameClock m_clock{kMaxFrameDtSec};

    // The pose last injected, re-applied while the tracker is quiet so a gap holds
    // the view where it was. Touched only from SampleFrame, i.e. only on the thread
    // the camera detour runs on.
    FrameSample m_held{};

    // Touched only from SampleFrame, on the render thread, like m_held.
    float m_lastDt = 0.0f;

    std::thread m_linkMonitor;

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_worldSpaceYaw{true};
};

}  // namespace OutlastHeadTracking
