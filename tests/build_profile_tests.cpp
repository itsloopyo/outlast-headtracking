// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The failsafe that decides whether the mod touches the process at all. Hooking a patched
// build against the RVAs of an older one crashes the session within seconds, so the
// fingerprint comparison has to reject a build it does not describe - including one whose
// TimeDateStamp happens to match.

#include "test_support.h"

#include "build_profile.h"

#include <iostream>
#include <string>

namespace OutlastHeadTracking {
// Defined in src/steam_offsets.cpp, which the test target compiles: the point of these
// checks is the profile that actually ships, not a copy of it made here.
extern const BuildProfile kSteamProfile_20140429;
}

namespace {

using namespace OutlastHeadTracking;
using olht_tests::Check;

// The shipped OLGame.exe this mod was derived against.
constexpr PeFingerprint kShipped{ 0x535FECFFu, 0x0222C000u, 0x020B0C50u };

void AllThreeFieldsDecideTheMatch() {
    Check(kShipped == PeFingerprint{ 0x535FECFFu, 0x0222C000u, 0x020B0C50u },
          "the fingerprint matches the build it describes");
    Check(!(kShipped == PeFingerprint{ 0x535FED00u, 0x0222C000u, 0x020B0C50u }),
          "a different TimeDateStamp is a different build");
    Check(!(kShipped == PeFingerprint{ 0x535FECFFu, 0x0222D000u, 0x020B0C50u }),
          "so is the same stamp at a different image size");
    Check(!(kShipped == PeFingerprint{ 0x535FECFFu, 0x0222C000u, 0x020B0C51u }),
          "and so is a repacked EXE that differs only in its checksum");
}

void TheShippedProfileIsTheBuildTheNotesRecord() {
    Check(kSteamProfile_20140429.fingerprint == kShipped,
          "the registry's profile carries that fingerprint");
    Check(std::string(kSteamProfile_20140429.name) == "steam-win64-20140429",
          "under the name that reaches the log");
}

// The VALUES, not merely that they are set.
//
// Every profile in the registry is a positional aggregate initialiser, so a member
// inserted or reordered anywhere but the end of OffsetTable re-routes every address after
// it while leaving the struct the same size - the static_assert in build_profile.h cannot
// see that, and a non-zero check cannot either. What it costs is the camera hook going in
// at the field-of-view accessor's address on a build the log has just said matched, and
// the game dying seconds later. So each address is pinned to what was measured.
void TheAddressesAreTheOnesMeasuredOnThatBuild() {
    const OffsetTable& o = kSteamProfile_20140429.offsets;
    Check(o.rvaCalcSceneView == 0x62A020, "ULocalPlayer::CalcSceneView is at 0x62A020");
    Check(o.rvaGetFovAngle == 0x60F4F0, "eventGetFOVAngle at 0x60F4F0");
    Check(o.rvaSceneViewFovReturn == 0x62A1C0, "its scene-view return at 0x62A1C0");
    Check(o.rvaGetPlayerViewPoint == 0x168E50, "GetPlayerViewPoint at 0x168E50");
    Check(o.rvaSceneViewViewPointReturn == 0x62A226,
          "its scene-view return at 0x62A226");
    Check(o.rvaDrawSceneViewReturn == 0x63062D, "the draw return at 0x63062D");
    Check(o.rvaGWorld == 0x200B5A8, "GWorld at 0x200B5A8");
    Check(o.rvaLevelStreamingPersistentClass == 0x200B5D0,
          "the cached ULevelStreamingPersistent class at 0x200B5D0");
    Check(o.offWorldPersistentLevel == 0x80, "UWorld::PersistentLevel at +0x80");
    Check(o.offLevelActorsData == 0x60 && o.offLevelActorsCount == 0x68,
          "ULevel::Actors is data at +0x60 and count at +0x68");
}

}  // namespace

int RunBuildProfileTests() {
    std::cout << "\nBuild profile failsafe\n";
    AllThreeFieldsDecideTheMatch();
    TheShippedProfileIsTheBuildTheNotesRecord();
    TheAddressesAreTheOnesMeasuredOnThatBuild();
    return olht_tests::TakeFailures();
}
