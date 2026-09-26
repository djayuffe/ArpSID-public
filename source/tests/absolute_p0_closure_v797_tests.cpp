// Copyright (C) 2024-2026 Ulf Bertilsson
// absolute_p0_closure_v797_tests.cpp
//
// Source guard for the P0 audit fixes: every P0 fix stays wired in the
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

int main() {
    const std::string cmake = readFile("CMakeLists.txt");
    const std::string au2 = readFile("source/au2/ArpSIDAUv2Component.mm");
    const std::string au3 = readFile("source/au3/ArpSIDViewController.mm");
    const std::string kernel = readFile("source/au3/ArpSIDDSPKernel.hpp");

    const std::vector<std::string> requiredGuards = {
        "StateRootMailboxOwnershipV748Tests",
        "TemplateBlobMailboxOwnershipV749Tests",
        "RenderStateRestoreAllocTrapV750Tests",
        "RenderDrumBridgeDeferralV751Tests",
        "IngressResetEpochBoundaryV752Tests",
        "C64RomHelperRangeSafeV753Tests",
        "Auv2CocoaViewTimeoutOrphanV754Tests",
        "Auv2AttachedViewReplacementV755Tests",
        "CVDisplayLinkCallbackLifetimeV756Tests",
        "ParamObserverNoBridgeReentrancyV757Tests"
    };
    for (const auto& guard : requiredGuards) {
        requireContains(cmake, guard, ("missing P0 guard target/test registration: " + guard).c_str());
    }

    // Structural sanity checks for the late P0/lifetime fixes.
    require(au2.find("(__bridge void*)self") == std::string::npos,
            "AUv2 view factory must not pass raw self as bridged context");
    requireContains(au2, "weakAudioUnit_v795", "AUv2 view factory weak AU token remains present");
    requireContains(au2, "removeFromSuperview", "AUv2 attached replacement clean-detach remains present");
    requireContains(au3, "ArpSIDDisplayLinkContext", "CVDisplayLink weak context remains present");
    requireContains(au3, "weakForDispatch", "CVDisplayLink main-queue weak continuation remains present");
    require(kernel.find("void realtimeEngineResetPreserveIngress") == std::string::npos &&
            kernel.find("realtimeEngineResetPreserveIngress_()") == std::string::npos,
            "misleading realtimeEngineResetPreserveIngress callable symbol must stay removed");
    requireContains(kernel, "clearEnqueuedBeforeNow", "ingress reset boundary implementation remains present");

    std::cout << "AbsoluteP0ClosureV797Tests PASS\n";
    return 0;
}
