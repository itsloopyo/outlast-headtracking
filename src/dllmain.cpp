// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_registry.h"
#include "camera_hook.h"
#include "camera_trace.h"
#include "camera_probe.h"
#include "config.h"
#include "fov_override.h"
#include "game_module.h"
#include "game_state.h"
#include "hotkeys.h"
#include "light_hook.h"
#include "light_probe.h"
#include "logging.h"
#include "path_utils.h"
#include "crosshair_hook.h"
#include "scene_view.h"
#include "tracking_runtime.h"
#include "window_center.h"

#include "MinHook.h"

#include <windows.h>

#include <string>

#ifndef HEADTRACKING_VERSION
#error "HEADTRACKING_VERSION must be defined by the build (the version in CMakeLists.txt)"
#endif

namespace OutlastHeadTracking {

namespace {

Config g_config;
CameraProbe g_cameraProbe;

// Heap-allocated and never deleted, unlike the two above. Each of these owns a thread
// and destroying it joins them; as namespace-scope objects that join would run from the
// CRT's static teardown during DLL_PROCESS_DETACH, i.e. under the loader lock, against
// threads that take it themselves. Leaking them is what keeps the exit path doing
// nothing, which is the same reasoning as the DLL_PROCESS_DETACH case below.
TrackingRuntime* g_tracking = nullptr;
Hotkeys*         g_hotkeys = nullptr;

// The two probes are gated differently, because they need different things.
//
// The camera probe needs nothing this mod has pinned: it takes the module's bounds and
// scans committed memory with VirtualQuery, so it works on a build no profile describes -
// which is the one situation it exists for, and gating it on the fingerprint would
// disable it exactly there. What it does do is arm a hardware watchpoint, which means a
// vectored exception handler and DR0/DR7 written into every thread, so it is off in every
// shipped INI and says plainly what it touches.
//
// The light probe's only root is the player controller the camera detour publishes, so
// without an installed camera hook it has nothing to look at and would spin for ten
// minutes reporting nothing at all.
void StartDiagnosticProbes(bool cameraHookInstalled) {
    if (g_config.camera_probe && !g_cameraProbe.Start()) {
        Log::Line("ERROR: [Diagnostics] CameraProbe is on but its thread could not be "
                  "started, so nothing is scanned and the probe reports nothing this "
                  "session.");
    }
    if (!g_config.light_probe) {
        return;
    }
    if (!cameraHookInstalled) {
        Log::Line("[Diagnostics] LightProbe is on, but the camera hook is not installed on "
                  "this build, so there is no viewpoint to measure a light against and the "
                  "probe is not started. CameraProbe is the one to turn on here.");
        return;
    }
    StartLightProbe();
}

// Identifies the running build and detours it. Nothing here runs until the fingerprint
// matches a profile: on any other build the mod stays dormant and the game runs exactly
// as it does without the DLL, because hooking a patched EXE against the RVAs of an older
// one crashes the session within seconds.
//
// The UDP receiver and the pose pipeline start either way, so on an unrecognised build
// the port is still claimed and the hotkeys still answer - there is simply no camera to
// put the pose on.
//
// True when the camera hook went in, which is what everything downstream of the frame's
// viewpoint depends on.
bool InstallEngineHooks() {
    const GameModule module = HostGameModule();
    if (!SelectBuildProfile(module)) {
        return false;
    }

    const MH_STATUS initialised = MH_Initialize();
    if (initialised != MH_OK) {
        Log::Line("ERROR: MinHook could not start (%d). Nothing is detoured.", initialised);
        return false;
    }

    const OffsetTable& offsets = ActiveProfile().offsets;
    const uintptr_t base = module.base;

    // The gameplay gate is armed FIRST, before any detour is enabled. Every hook here
    // publishes its own targets before MinHook enables it, which makes the enable the
    // moment they become visible to the render thread; this table is the one that is
    // shared, and the field-of-view detour below asks it a question on the first frame it
    // runs for. Armed afterwards, that read races the init thread's write - a plain
    // twelve-member struct, half of it published - and the answer it gets is a walk
    // through a partly-filled chain. The reads are guarded so it cannot fault; what it
    // produces is a wrong answer on the one frame the zoom basis is latched from.
    const GameStateTargets stateTargets = { base + offsets.rvaGWorld,
                                            base + offsets.rvaLevelStreamingPersistentClass,
                                            offsets.offWorldPersistentLevel,
                                            offsets.offLevelActorsData,
                                            offsets.offLevelActorsCount,
                                            offsets.offWorldInfoStreamingData,
                                            offsets.offWorldInfoStreamingCount,
                                            offsets.offLevelStreamingLoadedLevel,
                                            offsets.offObjectClass,
                                            offsets.offStructSuperStruct,
                                            offsets.offWorldInfoFlags,
                                            offsets.maskIsMenuLevel };
    InitGameState(stateTargets);

    // The field-of-view hook goes in before the scene-view and camera hooks. It is what
    // decides the angle the scene view is then built from, so installing it later would
    // leave the first frames reporting a projection the override had not reached yet -
    // and it is also what publishes the zoom factor the camera hook scales the pose by,
    // from the same CalcSceneView a few instructions earlier.
    const FovHookTargets fovTargets = { base + offsets.rvaGetFovAngle,
                                        base + offsets.rvaSceneViewFovReturn };
    InstallFovHook(fovTargets, g_config);

    // Before the camera hook, and not only for the field of view: this is what marks the
    // CalcSceneView call that is drawing, which is half of what picks the frame's
    // viewpoint out of the ones script asked for.
    //
    // Fatal, because it is the only thing that ever sets that mark. Without it the camera
    // detour filters out every frame there is, so carrying on would install three more
    // hooks, start the light probe on the strength of a camera hook that can never fire,
    // and end with a log that says four hooks went in and a game with no head tracking.
    if (!InstallSceneViewHook(base + offsets.rvaCalcSceneView,
                              base + offsets.rvaDrawSceneViewReturn)) {
        return false;
    }

    InitCameraTrace(base, offsets);
    const CameraHookTargets cameraTargets = { base + offsets.rvaGetPlayerViewPoint,
                                              base + offsets.rvaSceneViewViewPointReturn };
    if (!InstallCameraHook(cameraTargets, *g_tracking, g_config)) {
        return false;
    }
    // Fed by the same frame the camera hook publishes, so it goes in after it: the light
    // is turned by the difference between the rotator the game filled in and the one the
    // frame was drawn with, and before the first frame there is no such difference.
    const LightHookTargets lightTargets = { base + offsets.rvaLightSetParentToWorld,
                                            offsets.offLightRotation,
                                            offsets.offLightParentToWorld,
                                            offsets.offLightOrigin,
                                            offsets.offLightFlags };
    InstallLightHook(lightTargets);

    // Last, and only with a camera hook to feed it: the crosshair is moved by the offset
    // that hook publishes, so without it there is nothing to move it by.
    const CrosshairHookTargets crosshairTargets = { base + offsets.rvaDrawCrosshair,
                                                    offsets.offHudCanvas,
                                                    offsets.offCanvasClipX,
                                                    offsets.offCanvasClipY };
    InstallCrosshairHook(crosshairTargets);
    return true;
}

// The settings that decide how the pose behaves and that no other line writes down. The
// port leaves with the receiver's line, the field of view with the hook's and the keys
// with the hotkeys', so none of those is repeated here. What is left is what a report of
// "it feels sluggish" or "leaning does nothing" has to be read against, and the INI it
// came from does not travel with the log.
void ReportSettings(const Config& cfg) {
    Log::Line("Settings: tracking starts %s, yaw is %s, smoothing is %.2f for a tracker on "
              "this machine and %.2f for one reaching it over the network, and a pose "
              "counts as current for %d ms.",
              cfg.enabled_on_startup ? "enabled" : "disabled",
              cfg.world_space_yaw ? "horizon-locked (world-space)" : "camera-local",
              cfg.local_smoothing, cfg.remote_smoothing, cfg.data_freshness_ms);
    if (cfg.position_enabled) {
        Log::Line("Settings: leaning is on, limited to %.2f m either side, %.2f m up and "
                  "%.2f m down, %.2f m forward and %.2f m back.",
                  cfg.pos_limit_x, cfg.pos_limit_y, cfg.pos_limit_y_down,
                  cfg.pos_limit_z, cfg.pos_limit_z_back);
    } else {
        Log::Line("Settings: leaning is off ([Position] Enabled is false), so only head "
                  "rotation reaches the camera.");
    }
}

// Everything that touches another module runs here rather than in DllMain: resolving
// OLGame.exe, detouring it and starting threads all need the loader lock this DLL is
// holding while its entry point runs.
DWORD WINAPI InitThread(LPVOID) {
    Log::Line("Outlast Head Tracking " HEADTRACKING_VERSION " starting");

    const std::string iniPath = GetModulePath("HeadTracking.ini");
    if (iniPath.empty() || !g_config.LoadOrCreate(iniPath.c_str())) {
        Log::Line("ERROR: configuration could not be loaded. Staying dormant.");
        return 0;
    }
    ReportSettings(g_config);

    if (g_config.center_window) {
        StartWindowCentering();
    }

    // Constructed before the hooks and started after them. The camera hook takes a
    // reference to it, so it has to exist first; and Start() is what publishes the
    // configuration the render thread reads, so it goes last, when there is a detour to
    // read it from.
    g_tracking = new TrackingRuntime();

    const bool cameraHookInstalled = InstallEngineHooks();

    // After the hooks, because one of the two needs them - see StartDiagnosticProbes.
    StartDiagnosticProbes(cameraHookInstalled);

    // Claims the tracker port and keeps claiming it. Started regardless of
    // EnableOnStartup, which decides whether a pose reaches the camera, not whether
    // the socket exists - a player who launches with tracking off still wants the
    // port waiting for them when they switch it on.
    g_tracking->Start(g_config);

    g_hotkeys = new Hotkeys();
    g_hotkeys->Start(g_config,
                     [] { g_tracking->ToggleEnabled(); },
                     [] { g_tracking->CycleTrackingMode(); },
                     [] { g_tracking->ToggleYawMode(); });
    return 0;
}

}  // namespace

}  // namespace OutlastHeadTracking

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    using namespace OutlastHeadTracking;

    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(module);

            // Beside this DLL, which the installer puts next to OLGame.exe in
            // Binaries\Win64. Opened before anything else runs: core's Open() rotates the
            // last run to HeadTracking.prev.log and truncates this one, so the file is
            // this launch and only this launch, and every line below has somewhere to go.
            const std::wstring logPath = GetModulePathW("HeadTracking.log");
            if (logPath.empty()) {
                // Nowhere to write the log and nowhere to read the INI from, so there is
                // nothing to say and no channel left to say it on but the debugger.
                OutputDebugStringW(L"Outlast Head Tracking: could not resolve its own "
                                   L"directory. Staying dormant.\n");
                return TRUE;
            }
            Log::Open(logPath);

            const HANDLE init = CreateThread(nullptr, 0, &InitThread, nullptr, 0, nullptr);
            if (!init) {
                Log::Line("ERROR: could not start the init thread (error %lu). Staying "
                          "dormant.", GetLastError());
                return TRUE;
            }
            CloseHandle(init);
            return TRUE;
        }

        case DLL_PROCESS_DETACH:
            // Process exit, always. OLGame.exe statically imports DINPUT8.dll, so the
            // loader holds a reference on this module that nothing can release: an
            // explicit FreeLibrary drops only a reference its caller added and cannot
            // unmap us. That is what makes doing nothing here correct rather than lazy.
            //
            // Nothing is joined, unhooked or uninitialised, because all three would run
            // under the loader lock this entry point is holding. MH_Uninitialize suspends
            // every thread in the process and rewrites their instruction pointers, which
            // deadlocks against a thread waiting on that same lock; joining the tracker
            // or hotkey threads deadlocks against whichever of them is inside a Win32
            // call that takes it. At exit every other thread is already gone anyway, so
            // there is nothing to join and an address space about to be discarded.
            //
            // The log is closed on the exit path only, and its lines are already on disk.
            if (reserved != nullptr) {
                // The one line this path adds to its own do-nothing rule. A log that ends
                // on it is a session that ended; one that stops mid-frame without it is a
                // crash or a kill, and telling those apart is the first thing a report of
                // "it stopped working" has to answer.
                //
                // EmergencyLine rather than Line, and no Close() after it. ExitProcess
                // terminates every other thread in the process before this runs, at
                // whatever instruction each had reached - and this mod's threads write to
                // the log: the link monitor, the hotkey actions, both probes. A thread
                // killed inside Line() still owns the log's mutex and can never release
                // it, and taking that mutex here - which Line() and Close() both do -
                // would then block forever, under the loader lock, with the game already
                // exiting. The player's game hangs on quit and has to be killed. That is
                // what EmergencyLine exists for: no mutex, one WriteFile, then a flush.
                // Nothing is lost by not closing either - every line is written straight
                // through with no buffering, and the handle belongs to a process the
                // kernel is about to tear down.
                Log::EmergencyLine("Outlast Head Tracking shutting down with the game.");
            }
            return TRUE;
    }
    return TRUE;
}
