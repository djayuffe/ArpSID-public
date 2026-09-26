// Copyright (C) 2024-2026 Ulf Bertilsson
// absolute_p2_closure_v799_tests.cpp
//
// fix-order #53: source/status guard for complete P2 closure. This is a
// source-level guard: it does not claim AUv2/AUv3/Logic/auval, VST3 SDK, or
// notarization validation. It ensures every source-code-fixable P2 item recorded
// in the shipped audit/status matrix is closed, scoped-closed, or explicitly
// accepted-by-design, and that the external Apple-toolchain boundary remains
// honest rather than falsely source-verified.

#include <algorithm>
#include <cctype>
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

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}

static void requireContains(const std::string& haystack, const std::string& needle, const char* msg) {
    require(haystack.find(needle) != std::string::npos, msg);
}

static void requireNotContains(const std::string& haystack, const std::string& needle, const char* msg) {
    require(haystack.find(needle) == std::string::npos, msg);
}

int main() {
    const std::string version = readFile("VERSION.txt");
    const std::string versionH = readFile("include/arpsid/version.h");
    const std::string readme = readFile("README.md");
    const std::string audit = readFile("AUDIT-FIXES-0.0.686.md");
    const std::string fixList = readFile("arpsid-fix-list-690.md");
    const std::string matrixJson = readFile("docs/audit/AUDIT_FINDINGS_CLOSURE_MATRIX.json");
    const std::string matrixMd = readFile("docs/audit/AUDIT_FINDINGS_CLOSURE_MATRIX.md");
    const std::string cmake = readFile("CMakeLists.txt");
    const std::string canonicalEvents = readFile("source/au3/ArpSIDCanonicalEvents.h");
    const std::string sidEventQueue = readFile("include/arpsid/core/sid_event_queue.h");
    const std::string dspKernel = readFile("source/au3/ArpSIDDSPKernel.hpp");
    const std::string audioUnit = readFile("source/au3/ArpSIDAudioUnit.mm");
    const std::string viewController = readFile("source/au3/ArpSIDViewController.mm");

    requireContains(version, "0.0.690-pass", "VERSION.txt carries 0.0.690 pass identity");
    requireContains(versionH, "#define ARPSID_BUILD_PASS", "version.h carries build pass define");
    requireContains(readme, "absolute P2 closure audit", "README preserves P2-closure entry");
    requireContains(audit, "fix-order #53", "audit log records fix-order #53");
    requireContains(fixList, "fix-order #53", "fix list records fix-order #53");

    // P2 matrix statuses: P2-09 is external-toolchain-only, P2-10 is fixed with
    // honest physical scope. Neither may drift to OPEN/PENDING/TODO.
    requireContains(matrixJson, "\"id\": \"P2-09\"", "P2-09 matrix row exists");
    requireContains(matrixJson, "\"status\": \"NOT_PROVABLE_IN_CONTAINER\"", "P2-09 remains honest source-only boundary");
    requireContains(matrixJson, "\"id\": \"P2-10\"", "P2-10 matrix row exists");
    requireContains(matrixJson, "\"status\": \"FIXED_WITH_SCOPE\"", "P2-10 remains fixed-with-scope");
    requireContains(matrixMd, "C64CycleExactClosureV741Tests", "P2-10 matrix evidence keeps cycle-exact guard");
    requireNotContains(matrixJson, "\"status\": \"PENDING\"", "P2 matrix must not contain pending status");
    requireNotContains(matrixJson, "\"status\": \"OPEN\"", "P2 matrix must not contain open status");
    requireNotContains(matrixJson, "\"status\": \"TODO\"", "P2 matrix must not contain TODO status");

    // P2-12: render-adjacent sort classification remains explicit and guarded.
    requireContains(audit, "Render-adjacent `std::sort`", "P2-12 audit row exists");
    requireContains(canonicalEvents, "ARPSID_RT_SORT_CLASSIFICATION", "canonical events carry RT sort classification");
    requireContains(sidEventQueue, "ARPSID_RT_SORT_CLASSIFICATION", "SID event queue carries RT sort classification");
    requireContains(cmake, "SidWriteQueueBucketSortV522Tests", "P2 sort/capacity guard remains registered");
    requireContains(cmake, "RemainingAuditClosureV747Tests", "remaining audit closure guard remains registered");

    // P2-13: event overflow telemetry must remain public and render-published.
    requireContains(audit, "Event-overflow telemetry was invisible", "P2-13 audit row exists");
    requireContains(dspKernel, "eventOverflowDroppedNoteOn_", "kernel tracks canonical dropped NoteOn telemetry");
    requireContains(dspKernel, "eventOverflowReplacedLowerPriority_", "kernel tracks canonical replacement telemetry");
    requireContains(dspKernel, "publishEventOverflowTelemetry_", "kernel publishes canonical overflow telemetry");
    requireContains(dspKernel, "eventOverflowDroppedTotal", "kernel exposes canonical overflow accessors/snapshot fields");

    // P2-14: AUv3 render channel count is per-invocation atomic, not stale captured format.
    requireContains(audit, "AUv3 stale captured render format", "P2-14 audit row exists");
    requireContains(audioUnit, "std::atomic<uint32_t>                 _renderOutputChannels", "AUv3 stores render output channels atomically");
    requireContains(audioUnit, "renderOutputChannelsSlot->load", "render block loads channel count per invocation");
    requireContains(audioUnit, "captured-once", "source documents old captured-once contract as fixed");

    // P2-15: Pure-SID capture has a control-side state machine.
    requireContains(audit, "Pure-SID capture lacked a control-side state machine", "P2-15 audit row exists");
    requireContains(dspKernel, "enum class PureSidCaptureState", "Pure-SID capture state machine exists");
    requireContains(dspKernel, "compare_exchange", "Pure-SID capture uses CAS gating");
    requireContains(dspKernel, "copyAndStopPureSid1Q1RecordCapture", "Pure-SID stop/copy path remains guarded");

    // P2-16: non-render drain waits use bounded sleep backoff instead of yield spins.
    requireContains(audit, "Non-render `std::this_thread::yield()` drain spins", "P2-16 audit row exists");
    requireContains(dspKernel, "std::this_thread::sleep_for(std::chrono::microseconds(50))", "kernel non-render drain uses bounded sleep backoff");
    requireContains(viewController, "std::this_thread::sleep_for(std::chrono::milliseconds(1))", "view controller non-render drain uses bounded sleep backoff");
    requireNotContains(dspKernel, "std::this_thread::yield()", "kernel must not use yield spins in drain waits");
    requireNotContains(viewController, "std::this_thread::yield()", "view controller must not use yield spins in drain waits");

    // P2-09 and distribution P2 items must stay external/honest rather than being
    // falsely claimed as source-verified.
    requireContains(readme, "auval", "README still records auval boundary");
    requireContains(readme, "Logic runtime", "README still records Logic runtime boundary");
    requireContains(readme, "notarization", "README still records notarization boundary");
    requireContains(readme, "PENDING", "README keeps external validation pending until logs prove it");
    requireContains(audit, "NOT source bugs", "P2 distribution section stays source-boundary honest");

    const std::string combined = lower(audit + "\n" + readme + "\n" + fixList + "\n" + matrixMd + "\n" + matrixJson);
    const std::vector<std::string> forbidden = {
        "p2 pending", "p2: pending", "p2 open", "p2: open", "todo p2", "fixme p2", "unfixed p2", "p2 not fixed"
    };
    for (const auto& needle : forbidden) {
        require(combined.find(needle) == std::string::npos, ("P2 status must not contain unresolved wording: " + needle).c_str());
    }

    std::cout << "AbsoluteP2ClosureV799Tests PASS\n";
    return 0;
}
