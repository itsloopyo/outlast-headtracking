// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Characterization tests for the UE3 TCameraCache predicates the camera probe scans
// with. Loosening any of these thresholds admits arbitrary memory into the scan and the
// probe starts reporting instructions that have nothing to do with the camera, which is
// a wasted session rather than a crash - and so is exactly the kind of regression that
// goes unnoticed without a test.

#include "test_support.h"

#include "ue3_camera_cache.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

using namespace OutlastHeadTracking;
using olht_tests::Check;
using olht_tests::NearEqual;

CameraCache LiveLookingCache() {
    CameraCache c{};
    c.timeStamp = 123.5f;
    c.pov.location[0] = 1500.0f;
    c.pov.location[1] = -2200.0f;
    c.pov.location[2] = 96.0f;
    c.pov.rotation[0] = -1200;    // pitch
    c.pov.rotation[1] = 458000;   // yaw, accumulated well past one revolution
    c.pov.rotation[2] = 0;        // roll
    c.pov.fov = 80.0f;
    return c;
}

void Layout() {
    Check(sizeof(Pov) == 28, "TPOV is 28 bytes");
    Check(sizeof(CameraCache) == 32, "TCameraCache is 32 bytes");
    Check(NearEqual(kUE3RotatorUnitsToDegrees * 65536.0f, 360.0f),
          "65536 rotator units is a full turn");
}

void StoredFloats() {
    Check(PlausibleStoredFloat(0.0f, 1000.0f), "exactly zero is a value the game stored");
    Check(PlausibleStoredFloat(-500.0f, 1000.0f), "so is an ordinary in-range float");
    Check(!PlausibleStoredFloat(5000.0f, 1000.0f), "past the bound it is not");
    Check(!PlausibleStoredFloat(std::nanf(""), 1000.0f), "NaN is reinterpreted bytes");
    Check(!PlausibleStoredFloat(std::numeric_limits<float>::infinity(), 1000.0f),
          "and so is an infinity");
    Check(!PlausibleStoredFloat(1e-20f, 1000.0f), "denormal noise is not a stored float");
}

void Locations() {
    const float ordinary[3] = { 1500.0f, -2200.0f, 96.0f };
    Check(PlausibleLocation(ordinary), "a world position in centimetres passes");

    // Zeroed memory otherwise passes every other field test in the record.
    const float zeroed[3] = { 0.0f, 0.0f, 0.0f };
    Check(!PlausibleLocation(zeroed), "zeroed memory is rejected by the origin test");

    const float nearOrigin[3] = { 0.5f, -0.25f, 0.1f };
    Check(!PlausibleLocation(nearOrigin), "and so is a position hugging the origin");

    const float huge[3] = { 1e9f, 0.0f, 0.0f };
    Check(!PlausibleLocation(huge), "a coordinate no level reaches is rejected");
}

void Rotators() {
    const int32_t level[3] = { 0, 0, 0 };
    Check(PlausibleRotator(level), "a level camera passes");

    // Yaw wraps and accumulates, so it is never bounded.
    const int32_t spunRight[3] = { 1000, 2000000, 0 };
    Check(PlausibleRotator(spunRight), "an accumulated yaw is not bounded");

    const int32_t steepPitch[3] = { 25000, 0, 0 };
    Check(!PlausibleRotator(steepPitch), "a pitch past the player camera's range is not");
    const int32_t spunRoll[3] = { 0, 0, -25000 };
    Check(!PlausibleRotator(spunRoll), "and neither is a rolled-over roll");
}

void FieldOfView() {
    Check(PlausibleFov(80.0f), "the camera's own unzoomed angle passes");
    Check(PlausibleFov(15.0f), "so does the camcorder's narrowest zoom");
    Check(!PlausibleFov(0.0f), "a zero angle is not a field of view");
    Check(!PlausibleFov(200.0f), "and neither is one past a half turn");
    Check(!PlausibleFov(std::nanf("")), "nor reinterpreted bytes");
}

void TimeStamps() {
    Check(PlausibleTimeStamp(123.5f), "seconds since level start passes");
    Check(!PlausibleTimeStamp(0.0f), "a zero timestamp does not");
    Check(!PlausibleTimeStamp(-5.0f), "nor a negative one");
    Check(!PlausibleTimeStamp(1e9f), "nor one past any session length");
}

void WholeRecord() {
    Check(LooksLikeCameraCache(LiveLookingCache()), "a plausible record is recognised");

    CameraCache badFov = LiveLookingCache();
    badFov.pov.fov = 300.0f;
    Check(!LooksLikeCameraCache(badFov), "one field out of range rejects the record");

    CameraCache zeroed{};
    Check(!LooksLikeCameraCache(zeroed), "zeroed memory is not a camera cache");
}

void PreviousFrameNeighbour() {
    const CameraCache current = LiveLookingCache();

    CameraCache oneFrameBack = current;
    oneFrameBack.timeStamp -= 0.016f;
    oneFrameBack.pov.location[0] -= 3.0f;
    Check(AgreesAsPreviousFrame(current, oneFrameBack),
          "a record one frame behind is recognised as LastFrameCameraCache");

    // Deliberately not equality: a camera cut moves both at once.
    CameraCache identical = current;
    Check(AgreesAsPreviousFrame(current, identical),
          "a stationary camera's two records are identical and still agree");

    CameraCache elsewhere = current;
    elsewhere.pov.location[0] += 5000.0f;
    Check(!AgreesAsPreviousFrame(current, elsewhere),
          "a record a level away is not the previous frame");

    CameraCache stale = current;
    stale.timeStamp -= 30.0f;
    Check(!AgreesAsPreviousFrame(current, stale), "and neither is one from half a minute ago");

    CameraCache implausible{};
    Check(!AgreesAsPreviousFrame(current, implausible),
          "a neighbour that is not itself a camera cache is rejected");
}

}  // namespace

int RunCameraCacheTests() {
    std::cout << "UE3 camera cache predicates\n";
    Layout();
    StoredFloats();
    Locations();
    Rotators();
    FieldOfView();
    TimeStamps();
    WholeRecord();
    PreviousFrameNeighbour();
    return olht_tests::TakeFailures();
}
