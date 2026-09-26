// Copyright (C) 2024-2026 Ulf Bertilsson
// absolute_p0_closure_v797_tests.cpp
//
// fix-order #51: source-status guard for the complete P0 closure surface.  This
// is intentionally a source/status guard: it does not claim macOS auval/Logic or
// VST3 SDK validation.  It ensures every P0 issue recorded in the pass685/686/690
// audit train is closed/fixed in the shipped source package and that no P0 item is
// reintroduced as pending/open/deferred/todo in release status.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

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

int main() {
    const std::string audit = readFile("AUDIT-FIXES-0.0.686.md");
    const std::string oldAudit = readFile("docs/audit/AUDIT_FIXES_0_0_685.md");
    const std::string readme = readFile("README.md");
    const std::string fixList = readFile("arpsid-fix-list-690.md");
    const std::string cmake = readFile("CMakeLists.txt");
    const std::string au2 = readFile("source/au2/ArpSIDAUv2Component.mm");
    const std::string au3 = readFile("source/au3/ArpSIDViewController.mm");
    const std::string kernel = readFile("source/au3/ArpSIDDSPKernel.hpp");

    requireContains(readme, "absolute P0 closure audit", "README preserves P0-closure entry");
    requireContains(audit, "fix-order #51", "audit log must record fix-order #51");
    requireContains(fixList, "fix-order #51", "fix list must record fix-order #51");

    // pass685 P0 capture safety must remain recorded as fixed.
    requireContains(oldAudit, "| P0-1 | RT safety |", "legacy pass685 P0-1 row exists");
    requireContains(oldAudit, "fails closed", "legacy pass685 P0-1 fail-closed wording exists");
    requireContains(oldAudit, "| P0-2 | RT safety |", "legacy pass685 P0-2 row exists");
    requireContains(oldAudit, "guarded by `_digiWaitForRecordTapCallbacksToDrain_v185_`", "legacy pass685 P0-2 drain guard exists");

    // Top-level critical P0 section and output-tap P0 section must still exist.
    requireContains(audit, "## P0 — critical", "P0 critical section exists");
    requireContains(audit, "### P0 — output-tap", "P0 output-tap section exists");
    requireContains(audit, "IOProc acquired the in-flight lease too late", "output-tap lease P0 item exists");
    requireContains(audit, "Start reset in-flight/scratch without proving drain", "output-tap start drain P0 item exists");
    requireContains(audit, "Stop ignored drain failure", "output-tap stop drain P0 item exists");

    const std::vector<std::string> p0Markers = {
        "v0.0.690 P0-1 fix", "v0.0.690 P0-2 fix", "v0.0.690 P0-3 fix",
        "v0.0.690 P0-4 fix", "v0.0.690 P0-5 fix", "v0.0.690 P0-6 fix",
        "v0.0.690 P0-8 fix", "v0.0.690 P0-9 fix", "v0.0.690 P0-10 fix",
        "v0.0.690 P0-11 fix", "v0.0.690 P0-12 fix"
    };
    for (const auto& marker : p0Markers) {
        requireContains(audit, marker, ("missing P0 closure marker: " + marker).c_str());
    }

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

    const std::string combined = lower(audit + "\n" + readme + "\n" + fixList);
    const std::vector<std::string> forbidden = {
        "p0 pending", "p0: pending", "p0 open", "p0: open", "p0 deferred", "todo p0", "fixme p0", "unfixed p0"
    };
    for (const auto& needle : forbidden) {
        require(combined.find(needle) == std::string::npos,
                ("P0 status must not contain unresolved wording: " + needle).c_str());
    }

    requireContains(readme, "auval", "README still documents external auval status");
    requireContains(readme, "PENDING", "README must still distinguish external pending validation from source P0 closure");

    std::cout << "AbsoluteP0ClosureV797Tests PASS\n";
    return 0;
}
