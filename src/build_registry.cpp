// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_registry.h"

#include "logging.h"
#include "pe_headers.h"

namespace OutlastHeadTracking {

extern const BuildProfile kSteamProfile_20140429;

namespace {

// Newest first. The top entry is the diagnostic primary: when nothing matches, it is
// what an unrecognised build is compared against to say whether it is newer or older
// than anything this mod knows about.
const BuildProfile* const kKnownProfiles[] = {
    &kSteamProfile_20140429,
};

const BuildProfile* g_active = nullptr;

// A profile whose hook target is still zero is a placeholder - the fingerprint of a
// build somebody has spotted, landed before its addresses were rederived. It matches,
// and it still leaves the mod dormant, which is what makes landing one safe.
bool IsComplete(const BuildProfile& profile) {
    return profile.offsets.rvaCalcSceneView != 0;
}

void LogFingerprint(const char* what, const PeFingerprint& fp) {
    Log::Line("  %s TimeDateStamp=0x%08X SizeOfImage=0x%08X CheckSum=0x%08X",
              what, fp.timeDateStamp, fp.sizeOfImage, fp.checkSum);
}

// Names the direction the running build differs in, because the three cases need three
// different things from the player and the log line is the only place to say so.
void ReportNoMatch(const PeFingerprint& running) {
    const BuildProfile& primary = *kKnownProfiles[0];
    Log::Line("This OLGame.exe is not a build this mod knows about, so nothing has been "
              "hooked and the game runs exactly as it does without the mod.");
    LogFingerprint("running: ", running);
    for (const BuildProfile* profile : kKnownProfiles) {
        Log::Line("  known:   %s", profile->name);
        LogFingerprint("           ", profile->fingerprint);
    }
    if (running.timeDateStamp > primary.fingerprint.timeDateStamp) {
        Log::Line("It is newer than any build here - check the mod's releases page for a "
                  "version that covers it.");
    } else if (running.timeDateStamp < primary.fingerprint.timeDateStamp) {
        Log::Line("It is older than the newest build here - let Steam finish updating the "
                  "game, then start it again.");
    } else {
        Log::Line("It has the same build timestamp as %s but a different size or "
                  "checksum, so the EXE has been repacked or patched. The mod will not "
                  "engage on a modified binary.", primary.name);
    }
}

}  // namespace

bool SelectBuildProfile(const GameModule& module) {
    g_active = nullptr;
    if (module.base == 0) {
        Log::Line("ERROR: the game module's PE headers could not be read, so its build "
                  "cannot be identified. Staying dormant.");
        return false;
    }

    PeFingerprint running{};
    if (!ReadPeFingerprint(module.base, running)) {
        Log::Line("ERROR: OLGame.exe's PE headers could not be read, so its build cannot "
                  "be identified. Staying dormant.");
        return false;
    }

    for (const BuildProfile* profile : kKnownProfiles) {
        if (!(profile->fingerprint == running)) {
            continue;
        }
        if (!IsComplete(*profile)) {
            Log::Line("Build %s is recognised but its addresses have not been derived "
                      "yet, so nothing has been hooked and the game runs unmodified.",
                      profile->name);
            return false;
        }
        g_active = profile;
        Log::Line("Build %s matched (module 0x%llX..0x%llX)", profile->name,
                  (unsigned long long)module.base, (unsigned long long)module.end);
        return true;
    }

    ReportNoMatch(running);
    return false;
}

const BuildProfile& ActiveProfile() {
    return *g_active;
}

}  // namespace OutlastHeadTracking
