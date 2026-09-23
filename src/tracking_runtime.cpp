// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "tracking_runtime.h"

#include "log_once.h"
#include "logging.h"

#include <chrono>
#include <cstdint>
#include <thread>

namespace OutlastHeadTracking {

namespace {

std::int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The receiver timestamps packets in microseconds and every window the mod compares
// them against is in milliseconds, so the conversion happens here rather than at each
// comparison.
std::int64_t ElapsedMs(std::int64_t sinceUs, std::int64_t nowUs) {
    return (nowUs - sinceUs) / 1000;
}

}  // namespace

void TrackingRuntime::Start(const Config& cfg) {
    m_cfg = cfg;

    // No sensitivity, deadzone or inversion is set: the core defaults are 1:1 with no
    // deadzone and no inversion, which is the pose the tracker sent. Shaping it is the
    // tracker app's job, so one profile behaves the same in every game.
    cameraunlock::PositionSettings pos;
    pos.limit_x = m_cfg.pos_limit_x;
    pos.limit_y = m_cfg.pos_limit_y;
    pos.limit_y_down = m_cfg.pos_limit_y_down;
    pos.limit_z = m_cfg.pos_limit_z;
    pos.limit_z_back = m_cfg.pos_limit_z_back;
    m_session.SetPositionSettings(pos);

    // One value feeds both the rotation and the position processor - there is no
    // separate position smoothing setting - picked per connection from the
    // receiver's IsRemoteConnection(), re-read on every Update().
    static_assert(decltype(m_session)::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection() or smoothing silently stays local");
    m_session.SetLocalSmoothing(m_cfg.local_smoothing);
    m_session.SetRemoteSmoothing(m_cfg.remote_smoothing);

    m_worldSpaceYaw.store(m_cfg.world_space_yaw, std::memory_order_relaxed);
    m_session.SetMode(m_cfg.position_enabled
                          ? cameraunlock::TrackingMode::RotationAndPosition
                          : cameraunlock::TrackingMode::RotationOnly);

    m_receiver.SetLog([](const std::string& msg) {
        Log::Line("UDP: %s", msg.c_str());
    });

    // With a release, and AFTER everything the render thread reads without a lock. The
    // camera detour is already installed by the time this runs, so that thread is free to
    // enter SampleFrame the instant this reads true - and the first thing it does there is
    // read m_cfg, which is a plain struct written at the top of this function. A relaxed
    // store publishes no ordering, so that read would be a data race on non-atomic memory;
    // the release pairs with the acquire in SampleFrame and makes every write ABOVE IT
    // visible before it.
    //
    // Above it is the operative half. The receiver and the link monitor start below,
    // deliberately: both are self-synchronising and neither is read off a plain field by
    // the render thread. Anything added below this line that IS must move above it, or it
    // is published by nothing.
    m_enabled.store(m_cfg.enabled_on_startup, std::memory_order_release);

    // Core's receiver keeps its own supervisor thread on the port for as long as the
    // mod is loaded: it deliberately binds without SO_REUSEADDR, so a port another
    // game's mod is still holding fails the bind rather than quietly sharing it, and
    // it re-attempts every 500ms until that holder exits. A player who starts Outlast
    // over a game they forgot to close gets tracking within about half a second of
    // closing it, without touching the INI or restarting Outlast.
    if (m_receiver.Start(m_cfg.udp_port)) {
        Log::Line("UDP receiver listening on port %u", m_cfg.udp_port);
    } else {
        // Deliberately says nothing about WHY. The line immediately above this one is
        // core's, and it carries the OS's own account of the failure - the numeric code
        // and the system's message text. A port conflict is the usual cause but not the
        // only one, and naming it here would send a player hunting for an app that is
        // not running while the real reason sits one line up.
        Log::Line("UDP port %u could not be opened; see the UDP line above for what the "
                  "OS said. Retrying every %dms - tracking starts on its own once the "
                  "port opens, with no restart needed.", m_cfg.udp_port,
                  cameraunlock::UdpReceiver::kRetryIntervalMs);
    }

    m_linkMonitor = std::thread(&TrackingRuntime::LinkMonitorThread, this);
}

// Bound-and-silent is the state the log cannot otherwise distinguish. Core announces
// the bind ("tracking is live") and the first packet, so a player who frees the port
// but has meanwhile closed their tracker app reads a success line and nothing after
// it. Reporting each change of link state - and only each change - closes that gap
// without turning the log into a per-second stream.
void TrackingRuntime::LinkMonitorThread() {
    // Seeded from what Start() has already written, so the monitor opens on a change
    // of state rather than restating the line above it. A failed initial bind is
    // already in the log; a successful one is seeded Unknown, because "bound, and
    // nothing ever arrived" still needs saying and nothing else says it.
    Link reported = m_receiver.IsRetrying() ? Link::WaitingForPort : Link::Unknown;
    bool everReceived = false;
    LogBudget changes(kMaxLinkChangesLogged);
    bool capReported = false;

    // The later of "the port became ours" and "a packet arrived". Measuring silence
    // from the bind rather than from zero is what stops the monitor calling the link
    // dead in the quarter-second before a tracker that is starting up normally has
    // sent anything.
    std::int64_t lastActivityUs = NowUs();

    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kLinkPollMs));

        const std::int64_t nowUs = NowUs();
        Link now;
        if (m_receiver.IsRetrying()) {
            now = Link::WaitingForPort;
            lastActivityUs = nowUs;
        } else {
            const std::int64_t lastPacketUs = m_receiver.GetLastReceiveTimestamp();
            if (lastPacketUs > lastActivityUs) {
                lastActivityUs = lastPacketUs;
            }
            if (lastPacketUs != 0 && ElapsedMs(lastPacketUs, nowUs) < kLinkSilentMs) {
                now = Link::Receiving;
            } else if (ElapsedMs(lastActivityUs, nowUs) >= kLinkSilentMs) {
                now = Link::BoundSilent;
            } else {
                // Bound moments ago and nothing has arrived yet. That is what the
                // start of every session looks like, so it gets the same grace a
                // mid-session gap does rather than a warning nobody needs to read.
                now = reported;
            }
        }

        if (now == reported) {
            continue;
        }
        reported = now;

        // See kMaxLinkChangesLogged for why the change log is capped at all.
        if (!changes.Take()) {
            if (ClaimOnce(capReported)) {
                Log::Line("The tracker link has changed between receiving and idle %d "
                          "times, so further changes are not being written down and this "
                          "file stops growing here. The link is intermittent: check the "
                          "tracker app can still see you, or the network if the tracker "
                          "is on another device.", kMaxLinkChangesLogged);
            }
            continue;
        }

        switch (now) {
            case Link::WaitingForPort:
                // Same rule as the bind line in Start(): core has already written what
                // the OS said, so this reports the state and not a guess at its cause.
                Log::Line("UDP port %u is no longer open to us; retrying every %dms "
                          "(the preceding UDP line has the OS's reason)", m_cfg.udp_port,
                          cameraunlock::UdpReceiver::kRetryIntervalMs);
                break;
            case Link::BoundSilent:
                Log::Line("UDP port %u is ours but no tracker packets are arriving. Check "
                          "the tracker app is running and sending to this machine on port "
                          "%u.", m_cfg.udp_port, m_cfg.udp_port);
                break;
            case Link::Receiving:
                Log::Line("Tracker packets are arriving on UDP port %u%s", m_cfg.udp_port,
                          everReceived ? " again" : "");
                everReceived = true;
                break;
            case Link::Unknown:
                break;
        }
    }
}

void TrackingRuntime::ToggleEnabled() {
    // Acquire then release, not two relaxed accesses. This runs on the hotkey thread,
    // and SampleFrame's acquire load is what makes Start()'s plain writes to m_cfg
    // visible to the render thread. A relaxed store here is not part of the release
    // sequence headed by Start()'s release store, so a render thread that read the
    // hotkey's value would have no ordering back to m_cfg at all. The acquire picks
    // Start()'s writes up and the release passes them on.
    const bool prev = m_enabled.load(std::memory_order_acquire);
    m_enabled.store(!prev, std::memory_order_release);
    Log::Line("Tracking %s", !prev ? "enabled" : "disabled");
}

PositionLimits TrackingRuntime::GetPositionLimits() const {
    // The same acquire SampleFrame takes, for the same reason: m_cfg is a plain struct
    // written by Start() on another thread, and this is the only other thing on the render
    // thread that reads it.
    (void)m_enabled.load(std::memory_order_acquire);
    return PositionLimits{ m_cfg.pos_limit_x, m_cfg.pos_limit_y, m_cfg.pos_limit_y_down,
                           m_cfg.pos_limit_z, m_cfg.pos_limit_z_back };
}

void TrackingRuntime::CycleTrackingMode() {
    switch (m_session.CycleMode()) {
        case cameraunlock::TrackingMode::RotationAndPosition:
            Log::Line("Tracking mode: rotation + position (6DOF)");
            break;
        case cameraunlock::TrackingMode::RotationOnly:
            Log::Line("Tracking mode: rotation only");
            break;
        case cameraunlock::TrackingMode::PositionOnly:
            Log::Line("Tracking mode: position only");
            break;
    }
}

void TrackingRuntime::ToggleYawMode() {
    const bool prev = m_worldSpaceYaw.load(std::memory_order_relaxed);
    m_worldSpaceYaw.store(!prev, std::memory_order_relaxed);
    Log::Line("Yaw mode: %s", !prev ? "world-space (horizon-locked)" : "camera-local");
}

bool TrackingRuntime::IsPoseFresh() const {
    const std::int64_t lastUs = m_receiver.GetLastReceiveTimestamp();
    if (lastUs == 0) {
        return false;
    }
    return ElapsedMs(lastUs, NowUs()) < m_cfg.data_freshness_ms;
}

FrameSample TrackingRuntime::SampleFrame() {
    // Switched off is not tracking lost: the player asked for the game's own camera
    // back, so the held pose is dropped rather than re-applied on the next enable.
    // Acquire, paired with the release at the end of Start(): everything Start() wrote
    // into m_cfg and the session must be visible before the first frame reads it.
    if (!m_enabled.load(std::memory_order_acquire)) {
        m_held = FrameSample{};
        return m_held;
    }

    // Ticked unconditionally, including on a held frame. Skipping it while the tracker
    // is quiet makes the first fresh frame arrive with the whole gap as its delta,
    // clamped to kMaxFrameDtSec - and at 0.25s the smoothing factor is 0.99998, so the
    // view snaps to the new pose instead of blending back to it. That is the snap the
    // hold exists to prevent, moved to the other end of the gap.
    const float dt = m_clock.Tick();
    m_lastDt = dt;

    // A tracker that has gone quiet holds its last pose instead of snapping the view
    // back to the game's camera. The face leaving the webcam for half a second, or two
    // dropped datagrams over WiFi, otherwise throws the view across the screen and back
    // again - and with smoothing running only while samples arrive, nothing blends it.
    if (!IsPoseFresh() || !m_session.Update(dt)) {
        return HeldForCurrentMode();
    }

    FrameSample out;
    out.has_rotation = m_session.GetRotation(out.yaw, out.pitch, out.roll);
    out.has_position = m_session.GetPositionOffset(out.pos_x, out.pos_y, out.pos_z);
    m_held = out;
    return out;
}

// The held pose predates whatever mode the player has since cycled to, and a held frame
// never reaches m_session.Update(), which is where the mode is normally applied. Without
// this, cycling to rotation-only while the tracker is quiet leaves the stale lean on
// screen until packets resume - the player presses the key precisely because the lean is
// in the way, and nothing happens.
FrameSample TrackingRuntime::HeldForCurrentMode() const {
    FrameSample out = m_held;
    if (!m_session.IsRotationActive()) {
        out.has_rotation = false;
        out.yaw = out.pitch = out.roll = 0.0f;
    }
    if (!m_session.IsPositionActive()) {
        out.has_position = false;
        out.pos_x = out.pos_y = out.pos_z = 0.0f;
    }
    return out;
}

}  // namespace OutlastHeadTracking
