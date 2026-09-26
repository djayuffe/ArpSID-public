// Copyright (C) 2024-2026 Ulf Bertilsson
// release_cleanup_closure_v818_tests.cpp
//
// V818 final cleanup/source-release guard. Closes remaining P2 release-hygiene
// items without rewriting historical archive entries.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static std::string readText(const char* rel) {
    std::ifstream stream(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!stream) {
        std::cerr << "FAIL: missing " << rel << "\n";
        std::exit(1);
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

static void requireContains(const std::string& haystack, const char* needle, const char* msg) {
    require(haystack.find(needle) != std::string::npos, msg);
}

static void requireNotContains(const std::string& haystack, const char* needle, const char* msg) {
    require(haystack.find(needle) == std::string::npos, msg);
}

int main() {
    const std::string version = readText("VERSION.txt");
    const std::string readme = readText("README.md");
    const std::string finalDoc = readText("RELEASE_FINAL_SOURCE_CLOSURE.md");
    const std::string todo = readText("TODO.md");
    const std::string patching = readText("PATCHING.md");
    const std::string exactness = readText("C64_EXACTNESS_BOUNDARIES.md");
    const std::string ownership = readText("OWNERSHIP_MAP.md");
    const std::string rollback = readText("REALTIME_ROLLBACK_JOURNAL.md");
    const std::string verifyScript = readText("scripts/verify_source_tree.py");
    const std::string auditScript = readText("scripts/check_audit_closure.py");
    const std::string cmake = readText("CMakeLists.txt");

    requireContains(version, "0.0.690-pass380", "package remains pass380");
    require(readme.rfind("# ArpSID 0.0.690 pass380", 0) == 0,
            "README starts with current pass380 entry");

    requireContains(finalDoc, "# ArpSID 0.0.690 pass380", "final source closure doc is current pass380");
    requireContains(finalDoc, "source-level P0/P1/P2 closure: PASS", "source closure is explicitly PASS");
    requireContains(finalDoc, "auval: PENDING until Mac log proves success", "auval remains external pending");
    requireContains(finalDoc, "Logic runtime: PENDING until Mac runtime test proves success", "Logic remains external pending");
    requireContains(finalDoc, "VST3 SDK/toolchain validation: PENDING until built with the SDK", "VST3 remains external pending");
    requireContains(finalDoc, "notarization: PENDING until Apple notarization log proves success", "notarization remains external pending");
    requireNotContains(finalDoc, "AU VALIDATION SUCCEEDED", "final source closure must not claim AU validation success");
    requireNotContains(finalDoc, "Logic runtime: PASS", "final source closure must not claim Logic PASS");

    requireNotContains(verifyScript, "pass48-compatible", "verify_source_tree.py no longer prints stale pass48-compatible status");
    requireContains(verifyScript, "current package", "verify_source_tree.py prints current-package status");
    requireNotContains(auditScript, "pass56 audit closure matrix", "check_audit_closure.py no longer prints stale pass56 label");
    requireContains(auditScript, "current package", "check_audit_closure.py prints current-package status");
    requireNotContains(cmake, "Verify ArpSID pass49 source tree guard", "CMake source-tree guard comment no longer carries stale pass49 label");
    requireContains(cmake, "Verify ArpSID current source tree guard", "CMake source-tree guard comment is current-package scoped");

    requireContains(exactness, "Strict RSID", "C64 exactness boundary doc names Strict RSID");
    requireContains(exactness, "PSID", "C64 exactness boundary doc names PSID");
    requireContains(exactness, "Mos6510", "C64 exactness boundary doc separates Mos6510 compatibility path");
    requireContains(ownership, "Render / audio thread", "ownership map documents Render / audio thread ownership");
    requireContains(rollback, "bounded mutation journal", "rollback doc documents bounded mutation journal");
    requireContains(rollback, "no heap allocation", "rollback doc documents no heap allocation");

    requireContains(todo, "V818", "TODO records V818 final cleanup closure");
    requireContains(patching, "V818", "PATCHING records V818 final cleanup closure");

    std::cout << "ReleaseCleanupClosureV818Tests PASS\n";
    return 0;
}
