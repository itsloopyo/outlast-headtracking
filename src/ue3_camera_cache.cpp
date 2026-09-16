// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "ue3_camera_cache.h"

#include <cmath>

namespace OutlastHeadTracking {

bool PlausibleStoredFloat(float value, float magnitudeLimit) {
    if (!std::isfinite(value)) return false;
    const float magnitude = std::fabs(value);
    if (magnitude > magnitudeLimit) return false;
    return magnitude == 0.0f || magnitude > kMinStoredFloatMagnitude;
}

bool PlausibleLocation(const float location[3]) {
    for (int i = 0; i < 3; ++i) {
        if (!PlausibleStoredFloat(location[i], kMaxWorldCoordinateCm)) return false;
    }
    return std::fabs(location[0]) > kMinOriginDistanceCm ||
           std::fabs(location[1]) > kMinOriginDistanceCm ||
           std::fabs(location[2]) > kMinOriginDistanceCm;
}

bool PlausibleRotator(const int32_t rotation[3]) {
    return rotation[0] >= -kMaxPitchRollUnits && rotation[0] <= kMaxPitchRollUnits &&
           rotation[2] >= -kMaxPitchRollUnits && rotation[2] <= kMaxPitchRollUnits;
}

bool PlausibleFov(float fov) {
    return std::isfinite(fov) && fov >= kMinFovDegrees && fov <= kMaxFovDegrees;
}

bool PlausibleTimeStamp(float timeStamp) {
    return std::isfinite(timeStamp) && timeStamp > 0.0f && timeStamp < kMaxTimeStampSeconds;
}

bool PlausiblePov(const Pov& pov) {
    return PlausibleFov(pov.fov) && PlausibleRotator(pov.rotation) &&
           PlausibleLocation(pov.location);
}

bool LooksLikeCameraCache(const CameraCache& cache) {
    return PlausibleTimeStamp(cache.timeStamp) && PlausiblePov(cache.pov);
}

bool AgreesAsPreviousFrame(const CameraCache& current, const CameraCache& previous) {
    if (!LooksLikeCameraCache(previous)) return false;
    if (std::fabs(current.pov.fov - previous.pov.fov) > kMaxFrameFovDeltaDegrees) return false;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(current.pov.location[i] - previous.pov.location[i]) >
            kMaxFrameLocationDeltaCm) {
            return false;
        }
    }
    // Symmetric on purpose, and it has to be. The probe's only call site compares a
    // snapshot taken during the six-second TimeStamp watch against a neighbour read live
    // several seconds later, so the neighbour's stamp is the LATER of the two however the
    // records are laid out. An ordering test would therefore never pass, and the
    // corroboration signal it feeds would be dead rather than strict.
    return std::fabs(current.timeStamp - previous.timeStamp) < kMaxFrameTimeStampDeltaSecs;
}

}  // namespace OutlastHeadTracking
