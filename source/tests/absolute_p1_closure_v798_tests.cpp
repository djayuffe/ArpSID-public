// Copyright (C) 2024-2026 Ulf Bertilsson
// absolute_p1_closure_v798_tests.cpp
//
// fix-order #52: source/status guard for complete P1 closure. This is a
// source-level guard: it does not claim AUv2/Logic/auval, VST3 SDK, or
// notarization validation. It ensures every code-fixable P1 item recorded in the
// shipped audit matrix/status is closed/scoped-closed, that the late P1 fixes
// (P1-13/P1-20) remain wired in source, and that P1 status cannot drift back to
// pending/open/todo language.

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
    const std::string fixList = readFile("arpsid-fix-list-690.md");
    const std::string audit = readFile("AUDIT-FIXES-0.0.686.md");
    const std::string matrixJson = readFile("docs/audit/AUDIT_FINDINGS_CLOSURE_MATRIX.json");
    const std::string matrixMd = readFile("docs/audit/AUDIT_FINDINGS_CLOSURE_MATRIX.md");
    const std::string oldAudit = readFile("docs/audit/AUDIT_FIXES_0_0_685.md");
    const std::string cmake = readFile("CMakeLists.txt");
    const std::string vstBridge = readFile("source/gui/arpsid_vst_cocoa_bridge.mm");
    const std::string c64Platform = readFile("include/arpsid/core/c64_platform.h");
    const std::string c64Runtime = readFile("include/arpsid/core/c64_psid_runtime.h");
    const std::string dspKernel = readFile("source/au3/ArpSIDDSPKernel.hpp");
    const std::string viewController = readFile("source/au3/ArpSIDViewController.mm");
    const std::string diagnostic = readFile("include/arpsid/gui/diagnostic_snapshot.h");

    requireContains(version, "0.0.690-pass", "VERSION.txt carries 0.0.690 pass identity");
    requireContains(versionH, "#define ARPSID_BUILD_PASS", "version.h carries build pass define");
    requireContains(readme, "absolute P1 closure audit", "README preserves P1-closure entry");
    requireContains(audit, "Fix-order #52", "audit log records fix-order #52");
    requireContains(fixList, "fix-order #52", "fix list records fix-order #52");

    // The original code-fixable P1 matrix must remain closed or scoped-closed.
    const std::vector<std::string> matrixRows = {
        "\"id\": \"P1-01\"", "\"id\": \"P1-02\"", "\"id\": \"P1-03\"", "\"id\": \"P1-04\"",
        "\"id\": \"P1-05\"", "\"id\": \"P1-06\"", "\"id\": \"P1-07\"", "\"id\": \"P1-08\""
    };
    for (const auto& id : matrixRows) requireContains(matrixJson, id, ("missing P1 matrix id " + id).c_str());
    requireContains(matrixJson, "\"status\": \"FIXED\"", "P1 matrix has FIXED statuses");
    requireContains(matrixJson, "\"status\": \"FIXED_FOR_CURRENT_ARCHITECTURE\"", "P1 matrix has scoped architecture closure");
    requireContains(matrixJson, "\"status\": \"FIXED_FOR_FOCUSED_AUDIO_PATHS\"", "P1 matrix has scoped behavior-test closure");
    requireNotContains(matrixJson, "\"status\": \"PENDING\"", "P1 matrix must not contain pending status");
    requireNotContains(matrixJson, "\"status\": \"OPEN\"", "P1 matrix must not contain open status");
    requireNotContains(matrixJson, "\"status\": \"TODO\"", "P1 matrix must not contain TODO status");

    // Audit/status documents must still carry the known P1 closure markers.
    requireContains(audit, "## P1 — high", "P1 high section exists");
    requireContains(audit, "### P1 — output-tap", "P1 output-tap section exists");
    requireContains(audit, "P1-13", "late P1-13 POTX/POTY closure marker exists");
    requireContains(audit, "Fix-order #12 — VST Cocoa factory preset range", "P1-20 VST factory range closure exists");
    requireContains(readme, "P1-20 VST Cocoa factory preset range", "README records P1-20 closure");
    requireContains(readme, "P1-13 no-sink POTX/POTY exactness", "README records P1-13 closure");
    requireContains(oldAudit, "| P1-1 | RT safety |", "legacy P1-1 row exists");
    requireContains(oldAudit, "| P1-2 | RT safety |", "legacy P1-2 row exists");

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

    const std::string combined = lower(audit + "\n" + readme + "\n" + fixList + "\n" + matrixMd + "\n" + matrixJson);
    const std::vector<std::string> forbidden = {
        "p1 pending", "p1: pending", "p1 open", "p1: open", "todo p1", "fixme p1", "unfixed p1", "p1 not fixed"
    };
    for (const auto& needle : forbidden) {
        require(combined.find(needle) == std::string::npos, ("P1 status must not contain unresolved wording: " + needle).c_str());
    }

    // Keep external release boundaries honest.
    requireContains(readme, "auval", "README still records external auval boundary");
    requireContains(readme, "PENDING", "README still marks external validation pending until logs prove it");

    std::cout << "AbsoluteP1ClosureV798Tests PASS\n";
    return 0;
}
