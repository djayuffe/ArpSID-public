// Copyright (C) 2024-2026 Ulf Bertilsson
// absolute_p1_closure_v798_tests.cpp
//
// Source guard for the P1 audit fixes: every P1 fix stays wired in the
// shipped source and its dedicated regression guard stays registered in CMake.
// It does not claim macOS auval/Logic, VST3 SDK or notarization validation.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static std::string readFile(const char* rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    require(static_cast<bool>(f), rel);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

static void requireContains(const std::string& haystack, const std::string& needle, const char* msg) {
    require(haystack.find(needle) != std::string::npos, msg);
}

static void requireNotContains(const std::string& haystack, const std::string& needle, const char* msg) {
    require(haystack.find(needle) == std::string::npos, msg);
}

int main() {
    const std::string cmake = readFile("CMakeLists.txt");
    const std::string vstBridge = readFile("source/gui/arpsid_vst_cocoa_bridge.mm");
    const std::string c64Platform = readFile("include/arpsid/core/c64_platform.h");
    const std::string c64Runtime = readFile("include/arpsid/core/c64_psid_runtime.h");
    const std::string dspKernel = readFile("source/au3/ArpSIDDSPKernel.hpp");
    const std::string viewController = readFile("source/au3/ArpSIDViewController.mm");
    const std::string diagnostic = readFile("include/arpsid/gui/diagnostic_snapshot.h");

    // Required guard registrations for P1-specific closure must stay present.
    const std::vector<std::string> requiredGuards = {
        "KitSid808FactoryVoicePrecedenceV637Tests",
        "DrSidFactorySlotAudioShapeV640Tests",
        "DigiNonzeroSlotRuntimeV641Tests",
        "Sid808ProjectionSyncContractV642Tests",
        "KitTargetAuthorityV643Tests",
        "DrumBridgeRuntimeAuthorityV644Tests",
        "DrSidKitVoiceDeepOverrideV647Tests",
        "BridgeDrsidAccessorRemovedV649Tests",
        "C64NoSinkPotxyBlockerV758Tests",
        "VstCocoaFactoryPresetRangeV759Tests"
    };
    for (const auto& guard : requiredGuards) requireContains(cmake, guard, ("missing P1 guard registration: " + guard).c_str());

    // P1-20: VST Cocoa factory preset range must use the canonical 180-slot count,
    // not the old hard-coded 128 loop.
    requireContains(vstBridge, "kCanonicalFactoryPatchSlotCount", "VST Cocoa uses canonical factory patch count");
    requireContains(vstBridge, "factoryPatchNameForSlot", "VST Cocoa uses canonical slot names");
    requireNotContains(vstBridge, "i < 128", "VST Cocoa must not use legacy 128 preset loop");
    requireNotContains(vstBridge, "arrayWithCapacity:128", "VST Cocoa must not allocate legacy 128 preset list");

    // P1-13: no-sink POTX/POTY reads must have dedicated PotXY telemetry/blocker wiring.
    requireContains(c64Platform, "sidNoSinkPotxyReadCount_", "platform tracks no-sink POTX/POTY reads");
    requireContains(c64Platform, "sidNoSinkPotxyReadCount()", "platform exposes no-sink POTX/POTY read count");
    requireContains(c64Runtime, "sidNoSinkPotxyReadCount()", "runtime PotXYApprox considers no-sink POTX/POTY reads");
    requireContains(c64Runtime, "PotXYApprox", "runtime exposes PotXYApprox blocker");

    // P1.11 physical exactness wording must remain split from strict-PHI2 claims.
    requireContains(c64Runtime, "physicalExactnessBlockerMask", "runtime publishes physical exactness blockers");
    requireContains(diagnostic, "rsidPhysicalBlockerMask", "diagnostic snapshot carries physical blocker mask");
    requireContains(dspKernel, "c64RsidPhysicalBlockerMask_", "kernel render-publishes physical blocker mask");
    requireContains(viewController, "PHYSICAL EXACT", "GUI presents physical exactness as a distinct badge/fact");

    std::cout << "AbsoluteP1ClosureV798Tests PASS\n";
    return 0;
}
