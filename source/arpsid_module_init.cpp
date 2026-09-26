// arpsid_module_init.cpp
// ArpSID — Cross-platform module entry and exit points
//
// Windows: InitDll / ExitDll
// Linux: ModuleEntry / ModuleExit
// macOS: bundleEntry / bundleExit are in bundle_entry_mac.cpp
//
// The Steinberg SDK's moduleinit.cpp (built into libsdk.a) already provides
// these symbols on most configurations. This file is a belt-and-suspenders
// supplement for build configurations where the linker fails to pull in the
// SDK object file (e.g., when linking against a static archive without
// --whole-archive).
//
// Copyright (C) 2024-2026 Ulf Bertilsson
// SPDX-License-Identifier: MIT

#include "pluginterfaces/base/funknown.h"

// ─── Windows ──────────────────────────────────────────────────────────────────
#if defined(_WIN32) || defined(_WIN64)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// DllMain is required for proper COM DLL setup on Windows.
BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hInstance);
            break;
        case DLL_PROCESS_DETACH:
            break;
        default:
            break;
    }
    return TRUE;
}

extern "C" {
    // Steinberg SDK calls InitDll() once after loading the DLL.
    bool PLUGIN_API InitDll() { return true; }
    // Called before the DLL is unloaded.
    bool PLUGIN_API ExitDll() { return true; }
}

// ─── Linux / POSIX ────────────────────────────────────────────────────────────
#elif defined(__linux__)

#include <dlfcn.h>

extern "C" {
    // Called by the Steinberg module-loading code after dlopen().
    __attribute__((visibility("default")))
    bool ModuleEntry(void* /*sharedLibraryHandle*/) { return true; }

    // Called before dlclose().
    __attribute__((visibility("default")))
    bool ModuleExit() { return true; }
}

// ─── macOS: handled by bundle_entry_mac.cpp ───────────────────────────────────
#elif defined(__APPLE__)
    // No symbols needed here — macOS uses bundleEntry/bundleExit from
    // bundle_entry_mac.cpp and the Steinberg SDK's moduleinit.cpp.
#endif
