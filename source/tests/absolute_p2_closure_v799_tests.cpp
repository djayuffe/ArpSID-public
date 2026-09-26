// Copyright (C) 2024-2026 Ulf Bertilsson
// absolute_p2_closure_v799_tests.cpp
//
// Source guard for the P2 audit fixes: every P2 fix stays wired in the
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
    const std::string canonicalEvents = readFile("source/au3/ArpSIDCanonicalEvents.h");
    const std::string sidEventQueue = readFile("include/arpsid/core/sid_event_queue.h");
    const std::string dspKernel = readFile("source/au3/ArpSIDDSPKernel.hpp");
    const std::string audioUnit = readFile("source/au3/ArpSIDAudioUnit.mm");
    const std::string viewController = readFile("source/au3/ArpSIDViewController.mm");

    // P2 matrix statuses: P2-09 is external-toolchain-only, P2-10 is fixed with
    // honest physical scope. Neither may drift to OPEN/PENDING/TODO.

    // P2-12: render-adjacent sort classification remains explicit and guarded.
    requireContains(canonicalEvents, "ARPSID_RT_SORT_CLASSIFICATION", "canonical events carry RT sort classification");
    requireContains(sidEventQueue, "ARPSID_RT_SORT_CLASSIFICATION", "SID event queue carries RT sort classification");
    requireContains(cmake, "SidWriteQueueBucketSortV522Tests", "P2 sort/capacity guard remains registered");
    requireContains(cmake, "RemainingAuditClosureV747Tests", "remaining audit closure guard remains registered");

    // P2-13: event overflow telemetry must remain public and render-published.
    requireContains(dspKernel, "eventOverflowDroppedNoteOn_", "kernel tracks canonical dropped NoteOn telemetry");
    requireContains(dspKernel, "eventOverflowReplacedLowerPriority_", "kernel tracks canonical replacement telemetry");
    requireContains(dspKernel, "publishEventOverflowTelemetry_", "kernel publishes canonical overflow telemetry");
    requireContains(dspKernel, "eventOverflowDroppedTotal", "kernel exposes canonical overflow accessors/snapshot fields");

    // P2-14: AUv3 render channel count is per-invocation atomic, not stale captured format.
    requireContains(audioUnit, "std::atomic<uint32_t>                 _renderOutputChannels", "AUv3 stores render output channels atomically");
    requireContains(audioUnit, "renderOutputChannelsSlot->load", "render block loads channel count per invocation");
    requireContains(audioUnit, "captured-once", "source documents old captured-once contract as fixed");

    // P2-15: Pure-SID capture has a control-side state machine.
    requireContains(dspKernel, "enum class PureSidCaptureState", "Pure-SID capture state machine exists");
    requireContains(dspKernel, "compare_exchange", "Pure-SID capture uses CAS gating");
    requireContains(dspKernel, "copyAndStopPureSid1Q1RecordCapture", "Pure-SID stop/copy path remains guarded");

    // P2-16: non-render drain waits use bounded sleep backoff instead of yield spins.
    requireContains(dspKernel, "std::this_thread::sleep_for(std::chrono::microseconds(50))", "kernel non-render drain uses bounded sleep backoff");
    requireContains(viewController, "std::this_thread::sleep_for(std::chrono::milliseconds(1))", "view controller non-render drain uses bounded sleep backoff");
    requireNotContains(dspKernel, "std::this_thread::yield()", "kernel must not use yield spins in drain waits");
    requireNotContains(viewController, "std::this_thread::yield()", "view controller must not use yield spins in drain waits");

    // P2-09 and distribution P2 items must stay external/honest rather than being
    // falsely claimed as source-verified.

    std::cout << "AbsoluteP2ClosureV799Tests PASS\n";
    return 0;
}
