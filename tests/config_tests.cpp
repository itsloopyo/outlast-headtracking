// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Characterization tests for the boundary checks a user-editable INI passes through
// (config_sanitize.h) and for the field-of-view decision (fov_override.h). Both are
// pure functions of their arguments, and both are the last thing between a typo in a
// text file and a black or unusable frame.

#include "test_support.h"

#include "config.h"
#include "config_sanitize.h"
#include "fov_override.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

using namespace OutlastHeadTracking;
using olht_tests::Check;
using olht_tests::NearEqual;

const float kNan = std::nanf("");
const float kInf = std::numeric_limits<float>::infinity();

void FiniteGuard() {
    Check(SanitizeFinite(0.5f, 9.0f) == 0.5f, "a finite value passes through untouched");
    Check(SanitizeFinite(kNan, 9.0f) == 9.0f, "NaN falls back to the key's default");
    Check(SanitizeFinite(kInf, 9.0f) == 9.0f, "infinity falls back to the key's default");
    Check(SanitizeFinite(-kInf, 9.0f) == 9.0f, "negative infinity does too");
}

void SmoothingIsValidatedNotFloored() {
    // This is validation, not a floor: LocalSmoothing defaults to 0.0 and any value the
    // user sets inside [0,1] must reach the processor untouched.
    Check(SanitizeSmoothing(0.0f, 0.15f) == 0.0f, "zero smoothing is honoured, not floored");
    Check(SanitizeSmoothing(0.37f, 0.15f) == 0.37f, "an in-range value is untouched");
    Check(SanitizeSmoothing(1.0f, 0.15f) == 1.0f, "the top of the range is honoured");
    // Above 1 the speed lerp goes negative and the view extrapolates instead of settling.
    Check(SanitizeSmoothing(2.5f, 0.15f) == 1.0f, "above 1 is clamped to 1");
    Check(SanitizeSmoothing(-1.0f, 0.15f) == 0.0f, "below 0 is clamped to 0");
    Check(SanitizeSmoothing(kNan, 0.15f) == 0.15f, "NaN falls back to the shipped default");
}

void PositionLimitsKeepTheirSign() {
    // PositionProcessor::ClampToLimits calls Clamp(v, -limit, +limit); a negative limit
    // hands it lo > hi, which pins the offset at a constant instead of bounding it.
    Check(SanitizePositionLimit(0.30f, 0.30f) == 0.30f, "an in-range limit is untouched");
    Check(SanitizePositionLimit(-0.10f, 0.30f) == 0.0f, "a negative limit becomes zero");
    Check(SanitizePositionLimit(kNan, 0.30f) == 0.30f, "NaN falls back to the default");
    // Magnitude is deliberately not capped - a wider range than the shipped default is a
    // legitimate choice and the README quotes no ceiling.
    Check(SanitizePositionLimit(2.0f, 0.30f) == 2.0f, "a wide limit is left alone");
}

void RangeClamp() {
    Check(ClampRange(5.0f, 0.0f, 10.0f) == 5.0f, "a value inside the range is untouched");
    Check(ClampRange(-1.0f, 0.0f, 10.0f) == 0.0f, "a value below is pulled up");
    Check(ClampRange(11.0f, 0.0f, 10.0f) == 10.0f, "a value above is pulled down");
}

void FovOverrideOffByDefault() {
    Check(defaults::kFovOverride == 0.0f, "the shipped default renders the game's own angle");
    const FovDecision d = DecideFov(0.0f, 80.0f, 80.0f);
    Check(d.status == FovOverrideStatus::Off, "a zero request installs no override");
    Check(d.fov == 80.0f, "and the frame keeps the game's own angle");
}

void FovOverrideIsARatio() {
    // The override is a RATIO against the unzoomed angle, so the game's own widening for
    // a run and narrowing for the camcorder survive it.
    const FovDecision unzoomed = DecideFov(110.0f, 80.0f, 80.0f);
    Check(unzoomed.status == FovOverrideStatus::Applied, "an in-range request is applied");
    Check(NearEqual(unzoomed.fov, 110.0f),
          "a frame at the unzoomed angle renders at exactly the configured number");

    const FovDecision zoomed = DecideFov(110.0f, 40.0f, 80.0f);
    Check(NearEqual(zoomed.fov, 55.0f), "a zoom is scaled by the same factor");

    const FovDecision widened = DecideFov(110.0f, 100.0f, 80.0f);
    Check(NearEqual(widened.fov, 137.5f), "and so is the game's own widening");
}

void FovOverrideNeedsABaseAngle() {
    // The base is a struct field read out of the game's own object. A value this far out
    // means the offset no longer fits the build.
    for (float base : { 0.0f, 5.0f, 200.0f }) {
        const FovDecision d = DecideFov(110.0f, 80.0f, base);
        Check(d.status == FovOverrideStatus::NoBaseAngle,
              "an implausible unzoomed angle refuses the override");
        Check(d.fov == 80.0f, "and the frame keeps the game's own angle");
    }
    const FovDecision nanBase = DecideFov(110.0f, 80.0f, kNan);
    Check(nanBase.status == FovOverrideStatus::NoBaseAngle, "a NaN base refuses it too");

    const FovDecision nanGame = DecideFov(110.0f, kNan, 80.0f);
    Check(nanGame.status == FovOverrideStatus::NoBaseAngle,
          "so does a frame angle that is not a number");
}

void FovOverrideStopsShortOfNoProjection() {
    // At and past 180 degrees tan(fov/2) runs away and there is no projection left.
    const FovDecision d = DecideFov(170.0f, 100.0f, 80.0f);
    Check(d.status == FovOverrideStatus::NotRenderable,
          "a scaled angle past the renderable limit is refused for that frame");
    Check(d.fov == 100.0f, "and that frame keeps the game's own angle");

    // Refused per frame, not switched off for the session: the next frame at a normal
    // angle is still overridden.
    const FovDecision next = DecideFov(170.0f, 80.0f, 80.0f);
    Check(next.status == FovOverrideStatus::Applied, "the following frame is unaffected");
}

void ConfiguredFovRangeIsNarrowerThanTheRenderableOne() {
    Check(defaults::kMinFovOverride > 0.0f && defaults::kMaxFovOverride < kMaxRenderableFov,
          "the accepted config range sits inside what a projection can express");
}

void ShippedDefaults() {
    // A default-constructed Config, a freshly written INI and a read of a missing key all
    // have to agree, which they do by naming the same constants.
    const Config cfg;
    Check(cfg.udp_port == defaults::kPort, "the shipped port is the OpenTrack standard");
    Check(cfg.udp_port == 4242, "which is 4242");
    // Against the literals from the doctrine table, not against the constants the Config
    // is initialised FROM - `cfg.local_smoothing == defaults::kLocalSmoothing` is x == x
    // and passes whatever core's defaults drift to. A remote default that had moved to
    // 0.9 would give every phone tracker a ten-second time constant, silently.
    Check(cfg.local_smoothing == 0.0f, "local smoothing defaults to 0.0, with no floor");
    Check(cfg.remote_smoothing == 0.15f, "and remote smoothing to 0.15");
    Check(cfg.local_smoothing < cfg.remote_smoothing,
          "a same-machine tracker is smoothed less than one over the network");
    Check(cfg.world_space_yaw, "yaw is horizon-locked by default");
    Check(cfg.position_enabled, "6DOF position is on by default");
    Check(!cfg.camera_probe, "the camera probe is off in every shipped INI");
    Check(cfg.fov_override == 0.0f, "and the frame is drawn at the game's own angle");
    Check(cfg.vk_toggle == 0x23 && cfg.vk_cycle_mode == 0x21 && cfg.vk_yaw_mode == 0x22,
          "the nav-cluster bindings are End, Page Up and Page Down");
    Check(cfg.pos_limit_z > cfg.pos_limit_z_back,
          "there is more room to lean in than to pull back");
    // Values, not just their ordering: an ordering check passes with LimitZ at 0.12 m.
    Check(cfg.pos_limit_x == 0.30f, "the lateral lean limit is 0.30 m either side");
    Check(cfg.pos_limit_y == 0.20f, "the upward lean limit is 0.20 m");
    Check(cfg.pos_limit_y_down == 0.20f, "the downward one is its own field at 0.20 m");
    Check(cfg.pos_limit_z == 0.40f, "leaning in reaches 0.40 m");
    Check(cfg.pos_limit_z_back == 0.10f, "and pulling back is held to 0.10 m");
}

}  // namespace

int RunConfigTests() {
    std::cout << "Config boundary checks and field of view\n";
    FiniteGuard();
    SmoothingIsValidatedNotFloored();
    PositionLimitsKeepTheirSign();
    RangeClamp();
    FovOverrideOffByDefault();
    FovOverrideIsARatio();
    FovOverrideNeedsABaseAngle();
    FovOverrideStopsShortOfNoProjection();
    ConfiguredFovRangeIsNarrowerThanTheRenderableOne();
    ShippedDefaults();
    return olht_tests::TakeFailures();
}
