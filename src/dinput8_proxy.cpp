// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The shim half of the mod: OLGame.exe statically imports DINPUT8.dll, and the exe's own
// directory is searched before the system one, so a DLL of that name beside the exe is
// loaded during the game's own startup and its DllMain runs there. That is the whole
// loading mechanism - there is no ASI loader and nothing to vendor.
//
// dinput8 rather than xinput1_3, which the game also imports: the game ships its own
// xinput1_3.dll in Binaries\Win64, so proxying that one means replacing a file the
// install has to back up and the uninstall has to put back. It ships no dinput8.dll, so
// this mod is a file that is added and later deleted.
//
// The one imported function is forwarded to the genuine DLL, so keyboard and mouse keep
// working. The real one lives in the system directory; a 64-bit process resolves
// GetSystemDirectory to System32, which is the 64-bit build this proxy has to chain to.

#include "logging.h"

#include <windows.h>

#include <mutex>
#include <string>

namespace {

HMODULE g_real = nullptr;
std::once_flag g_loadOnce;

void LoadRealDInput() {
    char sysDir[MAX_PATH] = {};
    const UINT n = GetSystemDirectoryA(sysDir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        cameraunlock::logging::Line(
            "ERROR: GetSystemDirectory failed (error %lu), so the real dinput8.dll cannot "
            "be found. Input is dead until this mod is uninstalled.", GetLastError());
        return;
    }
    std::string path(sysDir, n);
    path += "\\dinput8.dll";

    g_real = LoadLibraryA(path.c_str());
    if (!g_real) {
        cameraunlock::logging::Line(
            "ERROR: could not load %s (error %lu). Input is dead until this mod is "
            "uninstalled.", path.c_str(), GetLastError());
    }
}

// Resolved on first use rather than in DllMain: this runs under the loader lock while
// the game's own imports are being bound, and LoadLibrary there is the deadlock the
// whole init-thread structure exists to avoid.
FARPROC Resolve(const char* name) {
    std::call_once(g_loadOnce, LoadRealDInput);
    return g_real ? GetProcAddress(g_real, name) : nullptr;
}

// What the forwarder returns when the real DLL is not there. Not a silent S_OK: a caller
// told "success" with an untouched out-pointer reads whatever was on its stack as a
// COM interface and jumps through it. DIERR_OUTOFMEMORY is in the documented failure
// set for this call, and DirectInput8Create's own failure path is one the game already
// has to handle.
constexpr HRESULT kOutOfMemory = 0x8007000EL;  // E_OUTOFMEMORY / DIERR_OUTOFMEMORY

}  // namespace

// The interface and GUID pointers are forwarded as void*: nothing here inspects them, so
// their layout is irrelevant to the proxy and declaring them would drag in dinput.h for
// no gain.
extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD version, const void* riid,
                                             void** out, void* outer) {
    using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, const void*, void**, void*);
    const auto fn = reinterpret_cast<Fn>(Resolve("DirectInput8Create"));
    return fn ? fn(inst, version, riid, out, outer) : kOutOfMemory;
}
