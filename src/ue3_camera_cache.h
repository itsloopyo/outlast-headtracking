// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "ue3_types.h"

#include <cstddef>
#include <cstdint>

namespace OutlastHeadTracking {

// The UE3 camera-cache record layout the runtime scan is written against, and
// the pure predicates that decide whether a block of bytes could be one.
//
//   struct TPOV         { FVector Location; FRotator Rotation; float FOV; };  // 28
//   struct TCameraCache { float TimeStamp; TPOV POV; };                       // 32
//
// The scan verifies this layout rather than assuming it: a record that passes
// every field test AND advances its TimeStamp once per frame is the live camera
// cache, and a wrong layout yields no such record at all.
//
// Nothing here reads process memory or logs. Every function is a total function
// of its arguments, which is what makes the thresholds testable without a
// running game.

#pragma pack(push, 4)
struct Pov {
    float   location[3];
    int32_t rotation[3];  // FRotator, 65536 units per revolution
    float   fov;
};
struct CameraCache {
    float timeStamp;
    Pov   pov;
};
#pragma pack(pop)

static_assert(sizeof(Pov) == 28, "TPOV must be 28 bytes");
static_assert(sizeof(CameraCache) == 32, "TCameraCache must be 32 bytes");

constexpr float kUE3RotatorUnitsToDegrees = 360.0f / kUnitsPerRevolution;

// Field bounds. Each one is a property of a UE3 player camera rather than a
// tuning knob: loosening any of them admits arbitrary memory into the scan.
constexpr float kMinStoredFloatMagnitude = 1.0e-4f;  // below this is denormal noise
constexpr float kMaxWorldCoordinateCm    = 1.0e6f;
constexpr float kMinOriginDistanceCm     = 1.0f;     // rejects zeroed memory
constexpr int32_t kMaxPitchRollUnits     = 20000;    // ~110 degrees
constexpr float kMinFovDegrees           = 5.0f;
constexpr float kMaxFovDegrees           = 175.0f;
constexpr float kMaxTimeStampSeconds     = 1.0e7f;

// Per-frame movement bounds used to recognise LastFrameCameraCache sitting
// behind CameraCache. Deliberately not equality: a camera cut moves both at once.
constexpr float kMaxFrameFovDeltaDegrees      = 45.0f;
constexpr float kMaxFrameLocationDeltaCm      = 1000.0f;
constexpr float kMaxFrameTimeStampDeltaSecs   = 1.0f;

// A float the game stored, rather than arbitrary bytes reinterpreted as one:
// finite, bounded, and either exactly zero or well clear of the denormal range.
bool PlausibleStoredFloat(float value, float magnitudeLimit);

// World coordinates in centimetres. The all-near-zero reject matters: zeroed
// memory otherwise passes every other field test in the record.
bool PlausibleLocation(const float location[3]);

// Pitch and roll are bounded on a player camera; yaw wraps and accumulates, so
// it is never bounded here.
bool PlausibleRotator(const int32_t rotation[3]);

bool PlausibleFov(float fov);

// Seconds since level start.
bool PlausibleTimeStamp(float timeStamp);

bool PlausiblePov(const Pov& pov);

bool LooksLikeCameraCache(const CameraCache& cache);

// True when `previous` holds the same camera one frame earlier than `current`.
bool AgreesAsPreviousFrame(const CameraCache& current, const CameraCache& previous);

}  // namespace OutlastHeadTracking
